#include <math.h>
#include <string.h>

#include "model_select.h"
#include "gesture_detect.h"

#if APP_USE_FACE_PIPELINE
#include "models_gesture/model.h"

#ifndef GESTURE_MAX_BOXES
#define GESTURE_MAX_BOXES 16
#endif

#define GESTURE_CLASS_NUM   2
#define GESTURE_CONF_THRESH_DEFAULT 0.55f
#define GESTURE_NMS_THRESH  0.45f
#define GESTURE_INPUT_W     192
#define GESTURE_INPUT_H     192
#define GESTURE_GRID_1      12
#define GESTURE_GRID_2      6
#define GESTURE_ANCHORS     3
#define GESTURE_EPSILON     1e-7f

static float g_gesture_conf_thresh = GESTURE_CONF_THRESH_DEFAULT;

static const int g_anchors[2][6] = {
    {12, 18, 37, 49, 52, 132},
    {115, 73, 119, 199, 242, 238},
};

static inline float sigmoidf_fast(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static inline int round_to_int(float v)
{
    return (int) ((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void rgb565_to_rgb888_resize_192_float_nchw_01(const uint16_t *src, int16_t src_w, int16_t src_h, float *dst)
{
    const int32_t plane_size = GESTURE_INPUT_W * GESTURE_INPUT_H;
    const float inv255 = 1.0f / 255.0f;
    for (int16_t y = 0; y < GESTURE_INPUT_H; y++)
    {
        int16_t sy = (int16_t) ((y * src_h) / GESTURE_INPUT_H);
        const uint16_t *row = src + (sy * src_w);
        for (int16_t x = 0; x < GESTURE_INPUT_W; x++)
        {
            int16_t sx = (int16_t) ((x * src_w) / GESTURE_INPUT_W);
            uint16_t p = row[sx];

            uint8_t r5 = (uint8_t) ((p >> 11) & 0x1F);
            uint8_t g6 = (uint8_t) ((p >> 5) & 0x3F);
            uint8_t b5 = (uint8_t) (p & 0x1F);

            uint8_t r8 = (uint8_t) ((r5 << 3) | (r5 >> 2));
            uint8_t g8 = (uint8_t) ((g6 << 2) | (g6 >> 4));
            uint8_t b8 = (uint8_t) ((b5 << 3) | (b5 >> 2));

            int32_t pixel_idx = (int32_t) y * GESTURE_INPUT_W + x;
            dst[0 * plane_size + pixel_idx] = (float) r8 * inv255;
            dst[1 * plane_size + pixel_idx] = (float) g8 * inv255;
            dst[2 * plane_size + pixel_idx] = (float) b8 * inv255;
        }
    }
}

static int decode_output_layer(const float *out_f,
                               int16_t grid,
                               int16_t anchor_group,
                               int16_t img_w,
                               int16_t img_h,
                               float conf_thresh,
                               gesture_box_t *out_boxes,
                               int16_t max_out,
                               float *best_conf_any,
                               int *best_cls_any)
{
    const int16_t feat_stride = (5 + GESTURE_CLASS_NUM);
    const int16_t cells = grid * grid;
    int16_t count = 0;

    const int *anchor = g_anchors[anchor_group];
    const float stride_pix = (float) GESTURE_INPUT_W / (float) grid;
    const float scale_x = (float) img_w / (float) GESTURE_INPUT_W;
    const float scale_y = (float) img_h / (float) GESTURE_INPUT_H;

    for (int16_t k = 0; k < GESTURE_ANCHORS; ++k)
    {
        for (int16_t i = 0; i < grid; ++i)
        {
            for (int16_t j = 0; j < grid; ++j)
            {
                int32_t base = (int32_t) k * cells * feat_stride +
                               (int32_t) i * grid * feat_stride +
                               (int32_t) j * feat_stride;

                float tx = out_f[base + 0];
                float ty = out_f[base + 1];
                float tw = out_f[base + 2];
                float th = out_f[base + 3];
                float obj_raw = out_f[base + 4];

                float obj = sigmoidf_fast(obj_raw);

                float max_class_prob = 0.0f;
                int best_cls = 0;
                for (int c = 0; c < GESTURE_CLASS_NUM; c++)
                {
                    float cls_prob = sigmoidf_fast(out_f[base + 5 + c]);
                    if (cls_prob > max_class_prob)
                    {
                        max_class_prob = cls_prob;
                        best_cls = c;
                    }
                }

                float conf = obj * max_class_prob;
                if (best_conf_any && (conf > *best_conf_any))
                {
                    *best_conf_any = conf;
                    if (best_cls_any) *best_cls_any = best_cls;
                }
                if (conf < conf_thresh)
                {
                    continue;
                }

                float cx = (sigmoidf_fast(tx) * 2.0f - 0.5f + (float) j) * stride_pix;
                float cy = (sigmoidf_fast(ty) * 2.0f - 0.5f + (float) i) * stride_pix;

                float ww = sigmoidf_fast(tw) * 2.0f;
                float hh = sigmoidf_fast(th) * 2.0f;
                ww = ww * ww * (float) anchor[k * 2];
                hh = hh * hh * (float) anchor[k * 2 + 1];

                int x1 = round_to_int((cx - ww * 0.5f) * scale_x);
                int y1 = round_to_int((cy - hh * 0.5f) * scale_y);
                int x2 = round_to_int((cx + ww * 0.5f) * scale_x);
                int y2 = round_to_int((cy + hh * 0.5f) * scale_y);

                x1 = clamp_int(x1, 0, img_w - 1);
                y1 = clamp_int(y1, 0, img_h - 1);
                x2 = clamp_int(x2, 0, img_w - 1);
                y2 = clamp_int(y2, 0, img_h - 1);

                if ((x2 <= x1) || (y2 <= y1))
                {
                    continue;
                }

                if (count < max_out)
                {
                    out_boxes[count].x1 = (int16_t) x1;
                    out_boxes[count].y1 = (int16_t) y1;
                    out_boxes[count].x2 = (int16_t) x2;
                    out_boxes[count].y2 = (int16_t) y2;
                    out_boxes[count].score = conf;
                    out_boxes[count].cls = (uint8_t) best_cls;
                    count++;
                }
            }
        }
    }

    return count;
}

static float iou_rect(const gesture_box_t *a, const gesture_box_t *b)
{
    int16_t xx1 = (a->x1 > b->x1) ? a->x1 : b->x1;
    int16_t yy1 = (a->y1 > b->y1) ? a->y1 : b->y1;
    int16_t xx2 = (a->x2 < b->x2) ? a->x2 : b->x2;
    int16_t yy2 = (a->y2 < b->y2) ? a->y2 : b->y2;

    int16_t w = xx2 - xx1;
    int16_t h = yy2 - yy1;
    if ((w <= 0) || (h <= 0))
    {
        return 0.0f;
    }

    float inter = (float) (w * h);
    float area_a = (float) ((a->x2 - a->x1) * (a->y2 - a->y1));
    float area_b = (float) ((b->x2 - b->x1) * (b->y2 - b->y1));
    float uni = area_a + area_b - inter + GESTURE_EPSILON;
    return inter / uni;
}

static void sort_boxes_by_score(gesture_box_t *boxes, int16_t n)
{
    for (int16_t i = 0; i < n - 1; ++i)
    {
        int16_t best = i;
        for (int16_t j = i + 1; j < n; ++j)
        {
            if (boxes[j].score > boxes[best].score)
            {
                best = j;
            }
        }
        if (best != i)
        {
            gesture_box_t tmp = boxes[i];
            boxes[i] = boxes[best];
            boxes[best] = tmp;
        }
    }
}

static int16_t nms_filter(gesture_box_t *boxes, int16_t n, float iou_thresh)
{
    if (n <= 0)
    {
        return 0;
    }

    sort_boxes_by_score(boxes, n);

    uint8_t removed[600];
    int16_t flags_n = (n < (int16_t) sizeof(removed)) ? n : (int16_t) sizeof(removed);
    memset(removed, 0, (size_t) flags_n);

    int16_t keep = 0;
    for (int16_t i = 0; i < n && keep < GESTURE_MAX_BOXES; ++i)
    {
        if (removed[i])
        {
            continue;
        }

        for (int16_t j = i + 1; j < n; ++j)
        {
            if (removed[j])
            {
                continue;
            }
            if (iou_rect(&boxes[i], &boxes[j]) > iou_thresh)
            {
                removed[j] = 1;
            }
        }

        if (keep != i)
        {
            boxes[keep] = boxes[i];
        }
        keep++;
    }

    return keep;
}

int gesture_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                       gesture_box_t *out_boxes, int16_t max_out,
                       int32_t *best_ok_milli, int32_t *best_palm_milli)
{
    if (!rgb565 || !out_boxes || (max_out <= 0))
    {
        return 0;
    }

    float *in_f = GetGestureModelInputPtr_images();
    if (!in_f)
    {
        return 0;
    }

    rgb565_to_rgb888_resize_192_float_nchw_01(rgb565, w, h, in_f);
    RunGestureModel(false);

    float *out1 = GetGestureModelOutputPtr_p5_6x6_70437();
    float *out2 = GetGestureModelOutputPtr_p4_12x12_70454();
    if (!out1 || !out2)
    {
        return 0;
    }

    gesture_box_t pool[540];
    int16_t total = 0;

    float best_conf_any = 0.0f;
    int best_cls_any = -1;

    total += decode_output_layer(out2, GESTURE_GRID_1, 0, w, h, g_gesture_conf_thresh,
                                 pool + total, (int16_t) (sizeof(pool) / sizeof(pool[0])) - total,
                                 &best_conf_any, &best_cls_any);
    total += decode_output_layer(out1, GESTURE_GRID_2, 1, w, h, g_gesture_conf_thresh,
                                 pool + total, (int16_t) (sizeof(pool) / sizeof(pool[0])) - total,
                                 &best_conf_any, &best_cls_any);

    int16_t kept = nms_filter(pool, total, GESTURE_NMS_THRESH);
    int16_t out_n = (kept < max_out) ? kept : max_out;

    float best_ok = 0.0f;
    float best_palm = 0.0f;
    for (int16_t i = 0; i < kept; i++)
    {
        if (pool[i].cls == GESTURE_CLASS_OK && pool[i].score > best_ok)
        {
            best_ok = pool[i].score;
        }
        else if (pool[i].cls == GESTURE_CLASS_PALM && pool[i].score > best_palm)
        {
            best_palm = pool[i].score;
        }
    }

    if (best_ok_milli)
    {
        *best_ok_milli = (int32_t) (best_ok * 1000.0f);
    }
    if (best_palm_milli)
    {
        *best_palm_milli = (int32_t) (best_palm * 1000.0f);
    }

    static uint32_t dbg_cnt = 0;
    if ((dbg_cnt++ % 30U) == 0U)
    {
        rt_kprintf("[GEST DBG] boxes=%d best_any=%d cls=%d thr=%d\n",
                   (int) kept,
                   (int) (best_conf_any * 1000.0f),
                   best_cls_any,
                   (int) (g_gesture_conf_thresh * 1000.0f));
    }

    for (int16_t i = 0; i < out_n; i++)
    {
        out_boxes[i] = pool[i];
    }

    return out_n;
}

int gesture_set_conf_thresh_milli(int32_t milli)
{
    if (milli < 50) milli = 50;
    if (milli > 950) milli = 950;
    g_gesture_conf_thresh = (float) milli / 1000.0f;
    return milli;
}

int32_t gesture_get_conf_thresh_milli(void)
{
    return (int32_t) (g_gesture_conf_thresh * 1000.0f);
}

#else
int gesture_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                       gesture_box_t *out_boxes, int16_t max_out,
                       int32_t *best_ok_milli, int32_t *best_palm_milli)
{
    (void) rgb565;
    (void) w;
    (void) h;
    (void) out_boxes;
    (void) max_out;
    if (best_ok_milli) *best_ok_milli = 0;
    if (best_palm_milli) *best_palm_milli = 0;
    return 0;
}
#endif

















