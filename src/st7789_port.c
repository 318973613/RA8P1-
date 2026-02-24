/* src/st7789_port.c - 閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷?*/
#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>
#include "hal_data.h"
#include "st7789_port.h"

/* 閿熸枻鎷烽敓鑴氳鎷烽敓鏂ゆ嫹 - RA8 娴ｈ法鏁?BSP_IO_PORT 閺嶇厧绱?*/
#define LCD_DC_PIN    BSP_IO_PORT_07_PIN_12   // P712
#define LCD_RES_PIN   BSP_IO_PORT_07_PIN_14   // P714
#define LCD_CS_PIN    BSP_IO_PORT_07_PIN_13   // 閿熸枻鎷烽敓鏂ゆ嫹 CS (閿熸枻鎷烽敓閾拌鎷烽敓鏂ゆ嫹涔熼敓鏂ゆ嫹瑕侀敓鏂ゆ嫹涓€閿熸枻鎷?

#define SPI_BUS_NAME  "spi0"
#define SPI_DEV_NAME  "spi00"

/* 閿熸枻鎷烽敓鏂ゆ嫹 SPI 閿熷€熷閿熸枻鎷烽敓鏂ゆ嫹 (閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷锋數顭掓嫹閿熺粸纰夋嫹婢诡剨鎷烽敓鏂ゆ嫹閿熻浼欐嫹閿熻闈╂嫹閿燂拷) */
static struct rt_spi_device spi_dev_lcd;

/* 5x7 glyphs, rows encoded as 5-bit values (bit4 = leftmost). */
static const uint8_t glyph_h[7] = { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11 };
static const uint8_t glyph_a[7] = { 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x11, 0x0F };
static const uint8_t glyph_l[7] = { 0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C };
static const uint8_t glyph_o[7] = { 0x00, 0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E };
static const uint8_t glyph_O[7] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
static const uint8_t glyph_K[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
static const uint8_t glyph_P[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
static const uint8_t glyph_A[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
static const uint8_t glyph_L[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
static const uint8_t glyph_M[7] = { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 };
static const uint8_t glyph_colon[7] = { 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00 };
static const uint8_t glyph_dot[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04 };
static const uint8_t glyph_0[7] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
static const uint8_t glyph_1[7] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
static const uint8_t glyph_2[7] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
static const uint8_t glyph_3[7] = { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
static const uint8_t glyph_4[7] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
static const uint8_t glyph_5[7] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
static const uint8_t glyph_6[7] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
static const uint8_t glyph_7[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
static const uint8_t glyph_8[7] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
static const uint8_t glyph_9[7] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
static const uint8_t glyph_space[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const uint8_t * glyph_get(char c)
{
    switch (c)
    {
    case 'h': return glyph_h;
    case 'a': return glyph_a;
    case 'l': return glyph_l;
    case 'o': return glyph_o;
    case 'O': return glyph_O;
    case 'K': return glyph_K;
    case 'P': return glyph_P;
    case 'A': return glyph_A;
    case 'L': return glyph_L;
    case 'M': return glyph_M;
    case ':': return glyph_colon;
    case '.': return glyph_dot;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case ' ': return glyph_space;
    default:  return glyph_space;
    }
}


void lcd_write_cmd(uint8_t cmd)
{
    rt_pin_write(LCD_DC_PIN, PIN_LOW);
    rt_spi_send(&spi_dev_lcd, &cmd, 1); // 娉ㄩ敓鏂ゆ嫹閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷?&spi_dev_lcd
}

void lcd_write_data(uint8_t data)
{
    rt_pin_write(LCD_DC_PIN, PIN_HIGH);
    rt_spi_send(&spi_dev_lcd, &data, 1);
}

void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2A);
    lcd_write_data((uint8_t)(x0 >> 8));
    lcd_write_data((uint8_t)(x0 & 0xFF));
    lcd_write_data((uint8_t)(x1 >> 8));
    lcd_write_data((uint8_t)(x1 & 0xFF));

    lcd_write_cmd(0x2B);
    lcd_write_data((uint8_t)(y0 >> 8));
    lcd_write_data((uint8_t)(y0 & 0xFF));
    lcd_write_data((uint8_t)(y1 >> 8));
    lcd_write_data((uint8_t)(y1 & 0xFF));

    lcd_write_cmd(0x2C);
}

int st7789_write_pixels(const void *data, uint32_t length)
{
    if ((data == RT_NULL) || (length == 0))
    {
        return -RT_ERROR;
    }
    rt_pin_write(LCD_DC_PIN, PIN_HIGH);
    return rt_spi_send(&spi_dev_lcd, data, length);
}

int st7789_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg, int scale)
{
    if ((text == RT_NULL) || (scale <= 0))
    {
        return -RT_ERROR;
    }

    if (scale > 6)
    {
        scale = 6;
    }

    const int glyph_w = 5;
    const int glyph_h = 7;
    const int char_w = glyph_w * scale;
    const int char_h = glyph_h * scale;
    const int spacing = scale;

    static uint8_t buf[5 * 7 * 6 * 6 * 2];

    int cursor_x = x;
    const char *p = text;
    while (*p)
    {
        const uint8_t *glyph = glyph_get(*p++);
        uint32_t idx = 0;

        for (int row = 0; row < glyph_h; row++)
        {
            uint8_t bits = glyph[row];
            for (int sy = 0; sy < scale; sy++)
            {
                for (int col = 0; col < glyph_w; col++)
                {
                    uint16_t px = (bits & (1U << (4 - col))) ? color : bg;
                    for (int sx = 0; sx < scale; sx++)
                    {
                        buf[idx++] = (uint8_t)(px >> 8);
                        buf[idx++] = (uint8_t)(px & 0xFF);
                    }
                }
            }
        }

        st7789_set_window((uint16_t)cursor_x, (uint16_t)y,
                          (uint16_t)(cursor_x + char_w - 1),
                          (uint16_t)(y + char_h - 1));
        st7789_write_pixels(buf, idx);

        cursor_x += char_w + spacing;
    }

    return RT_EOK;
}

void lcd_fill(uint16_t color)
{
    lcd_write_cmd(0x2A);
    lcd_write_data(0); lcd_write_data(0); lcd_write_data(0); lcd_write_data(240-1);
    lcd_write_cmd(0x2B);
    lcd_write_data(0); lcd_write_data(0); lcd_write_data(0); lcd_write_data(240-1);
    lcd_write_cmd(0x2C);

    rt_pin_write(LCD_DC_PIN, PIN_HIGH);

    // 閿熸触鍗曠鎷烽敓鍙紮鎷烽敓鏂ゆ嫹
    uint8_t line_buff[240 * 2];
    for(int i = 0; i < 240; i++)
    {
        line_buff[i*2] = color >> 8;
        line_buff[i*2+1] = color;
    }

    for(int i = 0; i < 240; i++)
    {
        rt_spi_send(&spi_dev_lcd, line_buff, 240 * 2);
    }
}

/* src/st7789_port.c 涓殑 st7789_init 鍑芥暟 */
static int lcd_initialized = 0;

int st7789_init(void)
{
    // 1. 閰嶇疆 GPIO
    rt_pin_mode(LCD_DC_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_RES_PIN, PIN_MODE_OUTPUT);

    // 2. 硬件复位（与 STM32 驱动一致）
    rt_thread_mdelay(10);
    rt_pin_write(LCD_RES_PIN, PIN_LOW);
    rt_thread_mdelay(10);
    rt_pin_write(LCD_RES_PIN, PIN_HIGH);
    rt_thread_mdelay(20);

    // 3. 鎸傝浇 SPI 璁惧 (鍙寕杞戒竴娆?
    if (!lcd_initialized) {
        if(rt_spi_bus_attach_device(&spi_dev_lcd, SPI_DEV_NAME, SPI_BUS_NAME, (void *)LCD_CS_PIN) != RT_EOK)
        {
            rt_kprintf("[LCD] SPI Attach Failed!\n");
            return -1;
        }
        lcd_initialized = 1;
    }

    // 4. 閰嶇疆 SPI 鍙傛暟
    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_3 | RT_SPI_MSB | RT_SPI_NO_CS;
    cfg.max_hz = 20 * 1000 * 1000;
    rt_spi_configure(&spi_dev_lcd, &cfg);

    // ST7789 init sequence aligned with STM32 proven driver
    lcd_write_cmd(0x36); lcd_write_data(0x00);  // MADCTL
    lcd_write_cmd(0x3A); lcd_write_data(0x05);  // COLMOD RGB565

    lcd_write_cmd(0xB2);
    lcd_write_data(0x0C); lcd_write_data(0x0C); lcd_write_data(0x00); lcd_write_data(0x33); lcd_write_data(0x33);

    lcd_write_cmd(0xB7); lcd_write_data(0x35);
    lcd_write_cmd(0xBB); lcd_write_data(0x19);
    lcd_write_cmd(0xC0); lcd_write_data(0x2C);
    lcd_write_cmd(0xC2); lcd_write_data(0x01);
    lcd_write_cmd(0xC3); lcd_write_data(0x12);
    lcd_write_cmd(0xC4); lcd_write_data(0x20);
    lcd_write_cmd(0xC6); lcd_write_data(0x0F);
    lcd_write_cmd(0xD0); lcd_write_data(0xA4); lcd_write_data(0xA1);

    lcd_write_cmd(0xE0);
    lcd_write_data(0xD0); lcd_write_data(0x04); lcd_write_data(0x0D); lcd_write_data(0x11);
    lcd_write_data(0x13); lcd_write_data(0x2B); lcd_write_data(0x3F); lcd_write_data(0x54);
    lcd_write_data(0x4C); lcd_write_data(0x18); lcd_write_data(0x0D); lcd_write_data(0x0B);
    lcd_write_data(0x1F); lcd_write_data(0x23);

    lcd_write_cmd(0xE1);
    lcd_write_data(0xD0); lcd_write_data(0x04); lcd_write_data(0x0C); lcd_write_data(0x11);
    lcd_write_data(0x13); lcd_write_data(0x2C); lcd_write_data(0x3F); lcd_write_data(0x44);
    lcd_write_data(0x51); lcd_write_data(0x2F); lcd_write_data(0x1F); lcd_write_data(0x1F);
    lcd_write_data(0x20); lcd_write_data(0x23);

    lcd_write_cmd(0x21);  // INVON
    lcd_write_cmd(0x11);  // SLPOUT
    lcd_write_cmd(0x13);  // NORON
    lcd_write_cmd(0x29);  // DISPON
    rt_thread_mdelay(50);

    rt_kprintf("[LCD] Init Success.\n");
    lcd_fill(0xF800); // 绾㈣壊娴嬭瘯

    return 0;
}
MSH_CMD_EXPORT(st7789_init, Init ST7789);

int st7789_hello(void)
{
    if (st7789_init() != 0)
    {
        return -RT_ERROR;
    }

    lcd_fill(0x0000);

#ifndef ST7789_LCD_WIDTH
#define ST7789_LCD_WIDTH 240
#endif
#ifndef ST7789_LCD_HEIGHT
#define ST7789_LCD_HEIGHT 240
#endif
    const char *msg = "hallo";
    int scale = 4;
    int char_w = 5 * scale;
    int char_h = 7 * scale;
    int spacing = scale;
    int text_w = (int)rt_strlen(msg) * char_w + ((int)rt_strlen(msg) - 1) * spacing;
    int x = (ST7789_LCD_WIDTH - text_w) / 2;
    int y = (ST7789_LCD_HEIGHT - char_h) / 2;

    st7789_draw_text(x, y, msg, 0xFFFF, 0x0000, scale);
    return RT_EOK;
}
MSH_CMD_EXPORT(st7789_hello, Show 'hallo' on ST7789);



