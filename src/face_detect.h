#ifndef FACE_DETECT_H
#define FACE_DETECT_H

#include <stdint.h>

typedef struct
{
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    float score;
    uint8_t cls;
} face_box_t;

int face_detect_run(const uint16_t *rgb565, int16_t w, int16_t h,
                    face_box_t *out_boxes, int16_t max_out);

#endif
