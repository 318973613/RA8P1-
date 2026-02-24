#include "../model_select.h"
#include "yolo_rtthread.h"

#if !APP_USE_FACE_PIPELINE

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Debug: print a few raw outputs periodically
static int g_decode_debug_cnt = 0;

static inline int round_to_int(float v)
{
    // Avoid systematic bias from truncation (which tends to shift boxes to top-left).
    return (int)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

int16_t decode_output_layer(const float *out_f,
                            int16_t grid,
                            int16_t anchor_group,
                            int16_t img_w, int16_t img_h,
                            float conf_thresh,
                            det_box_t *out_boxes,
                            int16_t max_out)
{
    const int16_t feat_stride = (5 + CLASS_NUM);  // 7 features per cell
    const int16_t cells = grid * grid;
    int16_t count = 0;

    const int *anchor = anchors[anchor_group];
    const float stride_pix = (float)INPUT_W / (float)grid;  // 192/12=16, 192/6=32
    const float scale_x = (float)img_w / (float)INPUT_W;
    const float scale_y = (float)img_h / (float)INPUT_H;

    int do_debug = (g_decode_debug_cnt % 60 == 0) && (anchor_group == 0);
    if (do_debug)
    {
        rt_kprintf("[YOLO DBG] grid=%d anchor_group=%d\n", grid, anchor_group);
        for (int sample = 0; sample < 3; sample++)
        {
            int32_t base = sample * feat_stride;
            rt_kprintf("  cell[%d]: tx=%d ty=%d tw=%d th=%d obj=%d c0=%d c1=%d\n",
                       sample,
                       (int)(out_f[base + 0] * 1000),
                       (int)(out_f[base + 1] * 1000),
                       (int)(out_f[base + 2] * 1000),
                       (int)(out_f[base + 3] * 1000),
                       (int)(out_f[base + 4] * 1000),
                       (int)(out_f[base + 5] * 1000),
                       (int)(out_f[base + 6] * 1000));
        }
    }
    g_decode_debug_cnt++;

    // Output layout: [1, 3, grid, grid, 7] = [batch, anchors, y, x, features]
    // Row-major: anchor -> y -> x -> features
    for (int16_t k = 0; k < ANCHORS; ++k)
    {
        for (int16_t i = 0; i < grid; ++i)  // y
        {
            for (int16_t j = 0; j < grid; ++j)  // x
            {
                int32_t base = (int32_t)k * cells * feat_stride + (int32_t)i * grid * feat_stride + (int32_t)j * feat_stride;

                float tx = out_f[base + 0];
                float ty = out_f[base + 1];
                float tw = out_f[base + 2];
                float th = out_f[base + 3];
                float obj_raw = out_f[base + 4];

                float obj = sigmoidf_fast(obj_raw);

                float max_class_prob = 0.0f;
                int best_cls = 0;
                for (int c = 0; c < CLASS_NUM; c++)
                {
                    float cls_prob = sigmoidf_fast(out_f[base + 5 + c]);
                    if (cls_prob > max_class_prob)
                    {
                        max_class_prob = cls_prob;
                        best_cls = c;
                    }
                }

                float conf = obj * max_class_prob;
                if (conf < conf_thresh)
                    continue;

                // YOLOv5 Detect_infer decode (matches Ultralytics YOLOv5 export)
                // xy = (sigmoid(xy) * 2 - 0.5 + grid) * stride
                // wh = (sigmoid(wh) * 2) ** 2 * anchor
                float cx = (sigmoidf_fast(tx) * 2.0f - 0.5f + (float)j) * stride_pix;  // input pixels
                float cy = (sigmoidf_fast(ty) * 2.0f - 0.5f + (float)i) * stride_pix;

                float ww = sigmoidf_fast(tw) * 2.0f;
                float hh = sigmoidf_fast(th) * 2.0f;
                ww = ww * ww * (float)anchor[k * 2];        // input pixels
                hh = hh * hh * (float)anchor[k * 2 + 1];

                float x1f = (cx - ww * 0.5f) * scale_x;
                float y1f = (cy - hh * 0.5f) * scale_y;
                float x2f = (cx + ww * 0.5f) * scale_x;
                float y2f = (cy + hh * 0.5f) * scale_y;

                int x1 = round_to_int(x1f);
                int y1 = round_to_int(y1f);
                int x2 = round_to_int(x2f);
                int y2 = round_to_int(y2f);

                x1 = CLAMP(x1, 0, img_w - 1);
                y1 = CLAMP(y1, 0, img_h - 1);
                x2 = CLAMP(x2, 0, img_w - 1);
                y2 = CLAMP(y2, 0, img_h - 1);

                if (x2 <= x1 || y2 <= y1)
                    continue;

                if (count < max_out)
                {
                    out_boxes[count].x1 = x1;
                    out_boxes[count].y1 = y1;
                    out_boxes[count].x2 = x2;
                    out_boxes[count].y2 = y2;
                    out_boxes[count].score = conf;
                    out_boxes[count].cls = (uint8_t)best_cls;
                    count++;
                }
            }
        }
    }

    RT_UNUSED(cells);
    return count;
}

void rgb565_to_gray_resize_192_and_quantization(const uint16_t *src, int16_t src_w, int16_t src_h, int8_t *i8_buf)
{
    const int16_t dst_w = INPUT_W;
    const int16_t dst_h = INPUT_H;

    for (int16_t y = 0; y < dst_h; y++)
    {
        int16_t sy = (y * src_h) / dst_h;
        const uint16_t *row = src + sy * src_w;

        for (int16_t x = 0; x < dst_w; x++)
        {
            int16_t sx = (x * src_w) / dst_w;
            uint16_t pix = row[sx];

            uint8_t r = (pix >> 11) & 0x1F;
            uint8_t g = (pix >> 5) & 0x3F;
            uint8_t b = (pix) & 0x1F;

            r = (uint8_t)((r << 3) | (r >> 2));
            g = (uint8_t)((g << 2) | (g >> 4));
            b = (uint8_t)((b << 3) | (b >> 2));

            float gray = (float)((r * 299 + g * 587 + b * 114) / 1000);

            float xn = (gray - 127.5f) / 127.5f;
            int8_t qi = (int8_t)(xn / scale_in + (float)zero_point_in);
            if (qi < -128)
                qi = -128;
            else if (qi > 127)
                qi = 127;

            i8_buf[y * dst_w + x] = qi;
        }
    }
}

int8_t *image_quantization_task(uint8_t *in_frame)
{
    if (!in_frame)
        return RT_NULL;

    int32_t num_pixels = INPUT_W * INPUT_H;

    int8_t *i8_buf = (int8_t *)rt_malloc(num_pixels * sizeof(int8_t));
    if (!i8_buf)
    {
        rt_kprintf("[ERR] malloc i8_buf\n");
        return RT_NULL;
    }

    for (int32_t i = 0; i < num_pixels; i++)
    {
        float xn = ((float)in_frame[i] - 127.5f) / 127.5f;
        int8_t qi = (int8_t)(xn / scale_in + (float)zero_point_in);
        if (qi < -128)
            qi = -128;
        else if (qi > 127)
            qi = 127;
        i8_buf[i] = qi;
    }
    return i8_buf;
}

void dequantize_int8(const int8_t *in, float *out, size_t len, float scale, int32_t zp)
{
    if (!in || !out || len == 0)
        return;

    if (scale == 0.0f)
    {
        for (size_t i = 0; i < len; i++)
            out[i] = (float)in[i];
        return;
    }

    for (size_t i = 0; i < len; i++)
        out[i] = (float)(in[i] - zp) * scale;
}

float iou_rect(const det_box_t *a, const det_box_t *b)
{
    int16_t xx1 = MAX(a->x1, b->x1);
    int16_t yy1 = MAX(a->y1, b->y1);
    int16_t xx2 = MIN(a->x2, b->x2);
    int16_t yy2 = MIN(a->y2, b->y2);

    int16_t w = xx2 - xx1;
    int16_t h = yy2 - yy1;
    if (w <= 0 || h <= 0)
        return 0.0f;

    float inter = (float)(w * h);
    float areaA = (float)((a->x2 - a->x1) * (a->y2 - a->y1));
    float areaB = (float)((b->x2 - b->x1) * (b->y2 - b->y1));
    float uni = areaA + areaB - inter + EPSILON;

    return inter / uni;
}

float diou_rect(const det_box_t *a, const det_box_t *b)
{
    float iou = iou_rect(a, b);

    float ax = 0.5f * (a->x1 + a->x2);
    float ay = 0.5f * (a->y1 + a->y2);
    float bx = 0.5f * (b->x1 + b->x2);
    float by = 0.5f * (b->y1 + b->y2);
    float center_dist2 = (ax - bx) * (ax - bx) + (ay - by) * (ay - by);

    int16_t x1 = MIN(a->x1, b->x1);
    int16_t y1 = MIN(a->y1, b->y1);
    int16_t x2 = MAX(a->x2, b->x2);
    int16_t y2 = MAX(a->y2, b->y2);
    float c2 = (float)((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)) + EPSILON;

    return iou - center_dist2 / c2;
}

void sort_boxes_by_score(det_box_t *boxes, int16_t n)
{
    for (int16_t i = 0; i < n - 1; ++i)
    {
        int16_t best = i;
        for (int16_t j = i + 1; j < n; ++j)
        {
            if (boxes[j].score > boxes[best].score)
                best = j;
        }
        if (best != i)
        {
            det_box_t tmp = boxes[i];
            boxes[i] = boxes[best];
            boxes[best] = tmp;
        }
    }
}

int16_t nms_filter(det_box_t *boxes, int16_t n, float iou_thresh)
{
    if (n <= 0)
        return 0;

    sort_boxes_by_score(boxes, n);

    // n is bounded by the pool size in hal_entry.c (540), keep a small fixed buffer.
    static uint8_t removed_flags[600];
    int16_t flags_n = MIN((int16_t)sizeof(removed_flags), n);
    memset(removed_flags, 0, (size_t)flags_n);

    int16_t keep = 0;
    for (int16_t i = 0; i < n && keep < MAX_BOXES; ++i)
    {
        if (removed_flags[i])
            continue;

        for (int16_t j = i + 1; j < n; ++j)
        {
            if (removed_flags[j])
                continue;

            float over;
#if IOU_MODE_DIou == 2
            over = diou_rect(&boxes[i], &boxes[j]);
            if (over > iou_thresh)
                removed_flags[j] = 1;
#else
            over = iou_rect(&boxes[i], &boxes[j]);
            if (over > iou_thresh)
                removed_flags[j] = 1;
#endif
        }

        if (keep != i)
            boxes[keep] = boxes[i];
        keep++;
    }

    return keep;
}

#endif /* !APP_USE_FACE_PIPELINE */
