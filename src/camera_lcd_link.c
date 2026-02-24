/*
 * Connect CEU camera capture to ST7789 SPI LCD.
 * User-level glue code (safe to edit).
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "hal_data.h"

#include "sensor.h"
#include "st7789_port.h"

/* Adjust these if your LCD size differs. */
#ifndef ST7789_LCD_WIDTH
#define ST7789_LCD_WIDTH   240
#endif
#ifndef ST7789_LCD_HEIGHT
#define ST7789_LCD_HEIGHT  240
#endif

/* CEU in this BSP is configured for QVGA (320x240). */
#ifndef CAM_FRAME_WIDTH
#define CAM_FRAME_WIDTH    320
#endif
#ifndef CAM_FRAME_HEIGHT
#define CAM_FRAME_HEIGHT   240
#endif

#define CAM_PIXEL_BYTES    2
#define CAM_FRAME_BYTES    (CAM_FRAME_WIDTH * CAM_FRAME_HEIGHT * CAM_PIXEL_BYTES)

static rt_thread_t s_cam_lcd_thread = RT_NULL;
static uint16_t *s_cam_buf = RT_NULL;
static uint8_t  *s_line_buf = RT_NULL;
static volatile rt_uint32_t s_frame_ready = 0;
static volatile rt_uint32_t s_frame_count = 0;

static void cam_frame_cb(void)
{
    s_frame_count++;
    s_frame_ready = 1;
}

static void st7789_blit_rgb565_center(const uint16_t *src, int src_w, int src_h)
{
    int dst_w = ST7789_LCD_WIDTH;
    int dst_h = ST7789_LCD_HEIGHT;
    int copy_w = (src_w < dst_w) ? src_w : dst_w;
    int copy_h = (src_h < dst_h) ? src_h : dst_h;
    int x_off = (src_w - copy_w) / 2;
    int y_off = (src_h - copy_h) / 2;

    st7789_set_window(0, 0, (uint16_t)(copy_w - 1), (uint16_t)(copy_h - 1));

    for (int y = 0; y < copy_h; y++)
    {
        const uint16_t *src_line = src + (y + y_off) * src_w + x_off;
        for (int x = 0; x < copy_w; x++)
        {
            uint16_t p = src_line[x];
            s_line_buf[x * 2]     = (uint8_t)(p >> 8);
            s_line_buf[x * 2 + 1] = (uint8_t)(p & 0xFF);
        }
        st7789_write_pixels(s_line_buf, (uint32_t)(copy_w * 2));
    }
}

static void cam_lcd_entry(void *parameter)
{
    if (st7789_init() != 0)
    {
        rt_kprintf("[cam_lcd] st7789_init failed\n");
        return;
    }

    if (sensor_init() != 0)
    {
        rt_kprintf("[cam_lcd] sensor_init failed\n");
        return;
    }

    if (sensor_set_pixformat(PIXFORMAT_RGB565) != 0)
    {
        rt_kprintf("[cam_lcd] sensor_set_pixformat failed\n");
        return;
    }

    if (sensor_set_framesize(FRAMESIZE_QVGA) != 0)
    {
        rt_kprintf("[cam_lcd] sensor_set_framesize failed\n");
        return;
    }

    lcd_fill(0x0000);

    sensor_set_frame_callback(cam_frame_cb);

    if (sensor_snapshot(&sensor, (uint8_t *)s_cam_buf, 0) != 0)
    {
        rt_kprintf("[cam_lcd] snapshot start failed\n");
        return;
    }

    while (1)
    {
        if (s_frame_ready)
        {
            s_frame_ready = 0;
#if (BSP_CFG_DCACHE_ENABLED)
            SCB_InvalidateDCache_by_Addr((uint32_t *) s_cam_buf, CAM_FRAME_BYTES);
#endif
            st7789_blit_rgb565_center(s_cam_buf, CAM_FRAME_WIDTH, CAM_FRAME_HEIGHT);

            /* trigger next frame after one is consumed */
            if (sensor_snapshot(&sensor, (uint8_t *)s_cam_buf, 0) != 0)
            {
                rt_kprintf("[cam_lcd] snapshot restart failed\n");
                rt_thread_mdelay(50);
            }
        }

        static rt_uint32_t last_tick = 0;
        rt_uint32_t now = rt_tick_get();
        if (now - last_tick >= RT_TICK_PER_SECOND)
        {
            last_tick = now;
            rt_kprintf("[cam_lcd] frames=%u first_px=0x%04X\n",
                       (unsigned)s_frame_count,
                       (unsigned)(s_cam_buf ? s_cam_buf[0] : 0));
        }
        rt_thread_mdelay(5);
    }
}

static int cam_lcd_start(int argc, char **argv)
{
    if (s_cam_lcd_thread != RT_NULL)
    {
        rt_kprintf("[cam_lcd] already running\n");
        return -RT_EBUSY;
    }

    s_cam_buf = (uint16_t *)rt_malloc_align(CAM_FRAME_BYTES, 32);
    if (s_cam_buf == RT_NULL)
    {
        rt_kprintf("[cam_lcd] alloc cam buffer failed (%d bytes)\n", CAM_FRAME_BYTES);
        return -RT_ENOMEM;
    }

    s_line_buf = (uint8_t *)rt_malloc_align(ST7789_LCD_WIDTH * 2, 32);
    if (s_line_buf == RT_NULL)
    {
        rt_kprintf("[cam_lcd] alloc line buffer failed\n");
        rt_free_align(s_cam_buf);
        s_cam_buf = RT_NULL;
        return -RT_ENOMEM;
    }

    s_cam_lcd_thread = rt_thread_create("cam_lcd",
                                        cam_lcd_entry,
                                        RT_NULL,
                                        4096,
                                        15,
                                        10);
    if (s_cam_lcd_thread == RT_NULL)
    {
        rt_kprintf("[cam_lcd] thread create failed\n");
        rt_free_align(s_line_buf);
        rt_free_align(s_cam_buf);
        s_line_buf = RT_NULL;
        s_cam_buf = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(s_cam_lcd_thread);
    return RT_EOK;
}
MSH_CMD_EXPORT(cam_lcd_start, Start CEU camera preview on ST7789);
