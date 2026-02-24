#ifndef SRC_GESTURE_DETECT_H_
#define SRC_GESTURE_DETECT_H_

#include <rtthread.h>

typedef struct
{
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    float score;
    uint8_t cls;
} gesture_box_t;

#define GESTURE_CLASS_OK   0
#define GESTURE_CLASS_PALM 1

int gesture_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                       gesture_box_t *out_boxes, int16_t max_out,
                       int32_t *best_ok_milli, int32_t *best_palm_milli);

int gesture_set_conf_thresh_milli(int32_t milli);
int32_t gesture_get_conf_thresh_milli(void);

#endif /* SRC_GESTURE_DETECT_H_ */



