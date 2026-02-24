#ifndef JPEG_SOFT_H
#define JPEG_SOFT_H

#include <stdint.h>

/**
 * @brief Simple Software JPEG Encoder for RGB565 format
 * 
 * @param p_rgb565   Input buffer (RGB565)
 * @param width      Image width
 * @param height     Image height
 * @param quality    JPEG Quality (1-100)
 * @param p_jpg_buf  Output buffer for JPEG data
 * @param buf_size   Size of output buffer
 * @return int       Size of encoded JPEG data, or 0 if failed/overflow
 */
int jpeg_soft_encode_rgb565(const uint8_t *p_rgb565, int width, int height, int quality, uint8_t *p_jpg_buf, int buf_size);

#endif
