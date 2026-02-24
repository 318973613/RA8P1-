/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-08-19     RTT          the first version
 */
#ifndef SRC_YOLO_RTTHREAD_H_
#define SRC_YOLO_RTTHREAD_H_

#include <rtthread.h>
#include <math.h>

// 妯″瀷鐩稿叧鍙傛暟
#define EPSILON                1e-7f
#define MAX_BOXES              16        /* 鏈�澶ф娴嬫鏁伴噺锛岀敤浜嶯MS */
#define CLASS_NUM              2
#define CONF_THRESH            0.35f   // NPU model conf is often ~0.25-0.30; keep threshold lower
#define NMS_THRESH             0.45f
#define IOU_MODE_DIou          1        /* 1: IoU, 2: DIoU */
#define INPUT_W               192
#define INPUT_H               192
#define INPUT_SIZE INPUT_W*INPUT_H
#define GRID_SIZE_1 12
#define GRID_SIZE_2 6
#define ANCHORS 3 // 姣忎釜缃戞牸浣跨敤3涓猘nchor

// anchor
#if 0
static const int anchors[2][6] = {
    {37, 94, 83, 83, 60, 137},  // 绗竴缁刌OLO棰勬祴鐨刟nchors (mask 3,4,5)
    {15, 22, 24, 51, 60, 44}    // 绗簩缁刌OLO棰勬祴鐨刟nchors (mask 0,1,2)
};

#endif

static const int anchors[2][6] = {
    {12, 18, 37, 49, 52, 132},    // 12x12 (P4/16)
    {115, 73, 119, 199, 242, 238} // 6x6 (P5/32)
};

#ifndef D2_FIX
#define D2_FIX(px) ((d2_point)((px) << 4))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef CLAMP
#define CLAMP(v,lo,hi) ( ((v)<(lo))?(lo):(((v)>(hi))?(hi):(v)) )
#endif

// 閲忓寲鍙傛暟
static const float scale_in = 0.007843137718737125;       /* 妯″瀷杈撳叆閲忓寲鐨剆cale */
static const int16_t zero_point_in = -1;                   /* 妯″瀷杈撳叆閲忓寲鐨剒ero point */
static const float scale_out2 = 0.16396920382976532;
static const int16_t zero_point_out2 = 34;
static const float scale_out1 = 0.14357048273086548;
static const int16_t zero_point_out1 = 26;

// 杈撳嚭闀垮害
static const int16_t output1_len = GRID_SIZE_1 * GRID_SIZE_1 * (3 * (5 + CLASS_NUM));
static const int16_t output2_len = GRID_SIZE_2 * GRID_SIZE_2 * (3 * (5 + CLASS_NUM));

typedef struct
{
    int16_t x1, y1, x2, y2;
    float score;
    uint8_t cls;
} det_box_t;

static inline float sigmoidf_fast(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

/**
 * 瑙ｇ爜缃戠粶杈撳嚭
 * @param out_f         杈撳嚭娴偣鏁版嵁锛岄暱搴� = grid*grid*3*(5+CLASS_NUM)
 * @param grid          缃戞牸澶у皬锛�6 鎴� 12
 * @param anchor_group  褰撳墠浣跨敤鐨� anchor 缁勶紝鍙栧�� 0 鎴� 1锛屾瘡缁� 3 涓� anchor
 * @param img_w,img_h   鍘熷鍥剧墖瀹介珮
 * @param conf_thresh   缃俊搴﹂槇鍊硷紝涓�鑸� >= 0.5
 * @param out_boxes     瑙ｇ爜鍚庣殑妫�娴嬫鏁扮粍
 * @param max_out       out_boxes 鐨勬渶澶ф暟閲�
 * @return              瀹為檯瑙ｇ爜鍑虹殑妗嗘暟閲�
 */
int16_t decode_output_layer(const float *out_f,
                               int16_t grid,
                               int16_t anchor_group,
                               int16_t img_w, int16_t img_h,
                               float conf_thresh,
                               det_box_t *out_boxes,
                               int16_t max_out);

void dequantize_int8(const int8_t *in, float *out,
                            size_t len, float scale, int32_t zp);

int8_t* image_quantization_task(uint8_t *in_frame);

void rgb565_to_gray_resize_192_and_quantization(const uint16_t *src, int16_t src_w, int16_t src_h, int8_t *i8_buf);

/**
 * NMS锛屾敮鎸両oU 鎴� DIoU
 * @param boxes        寰呯瓫閫�/妫�娴嬬殑妗�
 * @param n            妗嗘暟閲�
 * @param iou_thresh   闃堝��
 * @return             绛涢�夊悗鐨勬鏁伴噺锛屼笉瓒呰繃 MAX_BOXES
 */
int16_t nms_filter(det_box_t *boxes, int16_t n, float iou_thresh);

void sort_boxes_by_score(det_box_t *boxes, int16_t n);

float iou_rect(const det_box_t *a, const det_box_t *b);

float diou_rect(const det_box_t *a, const det_box_t *b);

void sort_boxes_by_score(det_box_t *boxes, int16_t n);

#endif /* SRC_YOLO_RTTHREAD_H_ */
