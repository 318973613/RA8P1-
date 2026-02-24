#include <rtthread.h>
#include <string.h>

#include "model_select.h"
#include "face_detect.h"

#if APP_USE_FACE_PIPELINE
#include "models_face/model.h"
#include "yolo_face/yolo_rtthread.h"
#include "hal_data.h"

#ifndef FACE_DETECT_DEBUG
#define FACE_DETECT_DEBUG 0
#endif

static int8_t s_in_i8[INPUT_SIZE];
#ifndef OUTPUT1_LEN
#define OUTPUT1_LEN (GRID_SIZE_1 * GRID_SIZE_1 * (3 * (5 + CLASS_NUM)))
#endif
#ifndef OUTPUT2_LEN
#define OUTPUT2_LEN (GRID_SIZE_2 * GRID_SIZE_2 * (3 * (5 + CLASS_NUM)))
#endif

static float s_out_f1[OUTPUT1_LEN];
static float s_out_f2[OUTPUT2_LEN];

static void dcache_clean_align(void *buf, size_t len)
{
#if (BSP_CFG_DCACHE_ENABLED)
    if (!buf || len == 0U)
    {
        return;
    }
    uintptr_t start = (uintptr_t) buf;
    uintptr_t end   = start + (uintptr_t) len;
    start &= ~(uintptr_t) 31U;
    end    = (end + 31U) & ~(uintptr_t) 31U;
    SCB_CleanDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) buf;
    (void) len;
#endif
}

static void dcache_invalidate_align(void *buf, size_t len)
{
#if (BSP_CFG_DCACHE_ENABLED)
    if (!buf || len == 0U)
    {
        return;
    }
    uintptr_t start = (uintptr_t) buf;
    uintptr_t end   = start + (uintptr_t) len;
    start &= ~(uintptr_t) 31U;
    end    = (end + 31U) & ~(uintptr_t) 31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) buf;
    (void) len;
#endif
}

int face_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                    face_box_t *out_boxes, int16_t max_out)
{
    if (!rgb565 || !out_boxes || max_out <= 0)
    {
        return 0;
    }

    rgb565_to_gray_resize_192_and_quantization(rgb565, w, h, s_in_i8);

#if FACE_DETECT_DEBUG
    static uint32_t in_dbg_cnt = 0;
    if ((in_dbg_cnt++ % 20U) == 0U)
    {
        int8_t min_i = s_in_i8[0], max_i = s_in_i8[0];
        for (int i = 1; i < INPUT_SIZE; i++)
        {
            if (s_in_i8[i] < min_i) min_i = s_in_i8[i];
            if (s_in_i8[i] > max_i) max_i = s_in_i8[i];
        }
        rt_kprintf("[FACE DBG] in[min=%d max=%d]\n", (int)min_i, (int)max_i);
    }
#endif

    int8_t *in_ptr = GetFaceDetectInputPtr_serving_default_image_input_0();
    memcpy(in_ptr, s_in_i8, INPUT_SIZE);

    if (RunFaceDetectModel(true) != 0)
    {
        return 0;
    }

    int8_t *out1 = GetFaceDetectOutputPtr_StatefulPartitionedCall_0_70273();
    int8_t *out2 = GetFaceDetectOutputPtr_StatefulPartitionedCall_1_70283();
    /* Input/output are in sub_0000_arena (internal RAM), no extra cache ops needed here. */

    dequantize_int8(out1, s_out_f1, OUTPUT1_LEN, scale_out1, zero_point_out1);
    dequantize_int8(out2, s_out_f2, OUTPUT2_LEN, scale_out2, zero_point_out2);

#if FACE_DETECT_DEBUG
    static uint32_t dbg_cnt = 0;
    if ((dbg_cnt++ % 20U) == 0U)
    {
        float max1 = s_out_f1[0], max2 = s_out_f2[0];
        float min1 = s_out_f1[0], min2 = s_out_f2[0];
        for (int i = 1; i < OUTPUT1_LEN; i++)
        {
            if (s_out_f1[i] > max1) max1 = s_out_f1[i];
            if (s_out_f1[i] < min1) min1 = s_out_f1[i];
        }
        for (int i = 1; i < OUTPUT2_LEN; i++)
        {
            if (s_out_f2[i] > max2) max2 = s_out_f2[i];
            if (s_out_f2[i] < min2) min2 = s_out_f2[i];
        }
        int32_t min1_m = (int32_t)(min1 * 1000.0f);
        int32_t max1_m = (int32_t)(max1 * 1000.0f);
        int32_t min2_m = (int32_t)(min2 * 1000.0f);
        int32_t max2_m = (int32_t)(max2 * 1000.0f);
        rt_kprintf("[FACE DBG] out1[min_m=%d max_m=%d] out2[min_m=%d max_m=%d]\n",
                   min1_m, max1_m, min2_m, max2_m);
    }
#endif

    det_box_t pool[540];
    int16_t total = 0;

    total += decode_output_layer(s_out_f1, GRID_SIZE_1, 0, w, h, CONF_THRESH,
                                 pool + total, (int16_t)(sizeof(pool) / sizeof(pool[0])) - total);
    total += decode_output_layer(s_out_f2, GRID_SIZE_2, 1, w, h, CONF_THRESH,
                                 pool + total, (int16_t)(sizeof(pool) / sizeof(pool[0])) - total);

    int16_t kept = nms_filter(pool, total, NMS_THRESH);
    int16_t out_n = (kept < max_out) ? kept : max_out;

    for (int16_t i = 0; i < out_n; i++)
    {
        out_boxes[i].x1 = pool[i].x1;
        out_boxes[i].y1 = pool[i].y1;
        out_boxes[i].x2 = pool[i].x2;
        out_boxes[i].y2 = pool[i].y2;
        out_boxes[i].score = pool[i].score;
        out_boxes[i].cls = pool[i].cls;
    }

    return out_n;
}
#else
int face_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                    face_box_t *out_boxes, int16_t max_out)
{
    (void) rgb565;
    (void) w;
    (void) h;
    (void) out_boxes;
    (void) max_out;
    return 0;
}
#endif
