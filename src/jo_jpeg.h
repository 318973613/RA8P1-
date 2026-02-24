/* jo_jpeg.h - public domain JPEG encoder
 * by Jon Olick (https://www.jonolick.com)
 * 
 * Modified for embedded systems (no FILE*, uses buffer output)
 * 
 * Usage:
 *   int size = jo_write_jpg_to_buffer(buffer, buf_size, width, height, comp, data, quality);
 *   
 *   buffer   - output buffer
 *   buf_size - size of output buffer
 *   width    - image width
 *   height   - image height  
 *   comp     - number of components (1=grayscale, 3=RGB)
 *   data     - pointer to RGB or grayscale data
 *   quality  - 1-100 (higher = better quality, larger file)
 *   
 *   Returns: size of JPEG data written, or 0 on failure
 */

#ifndef JO_JPEG_H
#define JO_JPEG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode RGB888 data to JPEG */
int jo_write_jpg_to_buffer(uint8_t *buffer, int buf_size, int width, int height, int comp, const uint8_t *data, int quality);

/* Encode RGB565 data to JPEG (convenience function) */
int jo_write_jpg_rgb565_to_buffer(uint8_t *buffer, int buf_size, int width, int height, const uint8_t *rgb565_data, int quality);

#ifdef __cplusplus
}
#endif

#endif /* JO_JPEG_H */


#ifdef JO_JPEG_IMPLEMENTATION

#include <math.h>
#include <string.h>

typedef struct {
    uint8_t *buf;
    int capacity;
    int pos;
    int bitBuf;
    int bitCnt;
} jo_jpeg_buf_t;

static void jo_writeByte(jo_jpeg_buf_t *ctx, uint8_t byte) {
    if (ctx->pos < ctx->capacity) {
        volatile uint8_t *p = &ctx->buf[ctx->pos];
        *p = byte;
        ctx->pos++;
    }
}

static void jo_writeBits(jo_jpeg_buf_t *ctx, int bits, int cnt) {
    ctx->bitBuf |= bits << (24 - ctx->bitCnt - cnt);
    ctx->bitCnt += cnt;
    while (ctx->bitCnt >= 8) {
        uint8_t byte = (ctx->bitBuf >> 16) & 0xFF;
        jo_writeByte(ctx, byte);
        if (byte == 0xFF) {
            jo_writeByte(ctx, 0);
        }
        ctx->bitBuf <<= 8;
        ctx->bitCnt -= 8;
    }
}

static void jo_DCT(float *d0, float *d1, float *d2, float *d3, float *d4, float *d5, float *d6, float *d7) {
    float tmp0 = *d0 + *d7;
    float tmp7 = *d0 - *d7;
    float tmp1 = *d1 + *d6;
    float tmp6 = *d1 - *d6;
    float tmp2 = *d2 + *d5;
    float tmp5 = *d2 - *d5;
    float tmp3 = *d3 + *d4;
    float tmp4 = *d3 - *d4;

    float tmp10 = tmp0 + tmp3;
    float tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2;
    float tmp12 = tmp1 - tmp2;

    *d0 = tmp10 + tmp11;
    *d4 = tmp10 - tmp11;

    float z1 = (tmp12 + tmp13) * 0.707106781f;
    *d2 = tmp13 + z1;
    *d6 = tmp13 - z1;

    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    float z5 = (tmp10 - tmp12) * 0.382683433f;
    float z2 = tmp10 * 0.541196100f + z5;
    float z4 = tmp12 * 1.306562965f + z5;
    float z3 = tmp11 * 0.707106781f;
    float z11 = tmp7 + z3;
    float z13 = tmp7 - z3;

    *d5 = z13 + z2;
    *d3 = z13 - z2;
    *d1 = z11 + z4;
    *d7 = z11 - z4;
}

static const uint8_t jo_ZigZag[] = { 
    0,1,5,6,14,15,27,28,
    2,4,7,13,16,26,29,42,
    3,8,12,17,25,30,41,43,
    9,11,18,24,31,40,44,53,
    10,19,23,32,39,45,52,54,
    20,22,33,38,46,51,55,60,
    21,34,37,47,50,56,59,61,
    35,36,48,49,57,58,62,63 
};

static const uint8_t std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t std_ac_luminance_values[] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};
static const uint8_t std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t std_ac_chrominance_values[] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static uint8_t YTable[64], UVTable[64];
static float fdtbl_Y[64], fdtbl_UV[64];
static uint16_t YDC_HT[12][2], UVDC_HT[12][2];
static uint16_t YAC_HT[256][2], UVAC_HT[256][2];
static int tables_initialized = 0;

static void jo_computeHuffmanTbl(uint16_t tbl[][2], const uint8_t *nrcodes, const uint8_t *vals) {
    int k = 0;
    uint16_t code = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < nrcodes[i]; j++) {
            tbl[vals[k]][0] = code;
            tbl[vals[k]][1] = i;
            k++;
            code++;
        }
        code <<= 1;
    }
}

static void jo_initTables(int quality) {
    if (tables_initialized) return;
    
    quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    int q = quality < 50 ? 5000 / quality : 200 - quality * 2;
    
    static const uint8_t std_luminance_qt[64] = {
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68,109,103, 77,
        24, 35, 55, 64, 81,104,113, 92,
        49, 64, 78, 87,103,121,120,101,
        72, 92, 95, 98,112,100,103, 99
    };
    static const uint8_t std_chrominance_qt[64] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
    };
    
    static const float aasf[] = { 1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f, 1.175875602f * 2.828427125f, 
                                  1.0f * 2.828427125f, 0.785694958f * 2.828427125f, 0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f };
    
    for (int i = 0; i < 64; i++) {
        int yti = (std_luminance_qt[i] * q + 50) / 100;
        YTable[jo_ZigZag[i]] = yti < 1 ? 1 : yti > 255 ? 255 : yti;
        int uvti = (std_chrominance_qt[i] * q + 50) / 100;
        UVTable[jo_ZigZag[i]] = uvti < 1 ? 1 : uvti > 255 ? 255 : uvti;
        
        int row = i / 8, col = i % 8;
        fdtbl_Y[jo_ZigZag[i]] = 1.0f / (YTable[jo_ZigZag[i]] * aasf[row] * aasf[col]);
        fdtbl_UV[jo_ZigZag[i]] = 1.0f / (UVTable[jo_ZigZag[i]] * aasf[row] * aasf[col]);
    }
    
    jo_computeHuffmanTbl(YDC_HT, std_dc_luminance_nrcodes, std_dc_luminance_values);
    jo_computeHuffmanTbl(UVDC_HT, std_dc_chrominance_nrcodes, std_dc_chrominance_values);
    jo_computeHuffmanTbl(YAC_HT, std_ac_luminance_nrcodes, std_ac_luminance_values);
    jo_computeHuffmanTbl(UVAC_HT, std_ac_chrominance_nrcodes, std_ac_chrominance_values);
    
    tables_initialized = 1;
}

static int jo_calcBits(int val, uint16_t bits[2]) {
    int tmp1 = val < 0 ? -val : val;
    val = val < 0 ? val - 1 : val;
    bits[1] = 1;
    while (tmp1 >>= 1) bits[1]++;
    bits[0] = val & ((1 << bits[1]) - 1);
    return 0;
}

static int jo_processDU(jo_jpeg_buf_t *ctx, float *CDU, int du_stride, float *fdtbl, int DC, uint16_t HTDC[][2], uint16_t HTAC[][2]) {
    int DU[64];
    
    /* DCT rows */
    for (int i = 0; i < 8; i++) {
        float *p = CDU + i * du_stride;
        jo_DCT(&p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &p[6], &p[7]);
    }
    
    /* DCT columns */
    for (int i = 0; i < 8; i++) {
        float p0 = CDU[0*du_stride+i], p1 = CDU[1*du_stride+i], p2 = CDU[2*du_stride+i], p3 = CDU[3*du_stride+i];
        float p4 = CDU[4*du_stride+i], p5 = CDU[5*du_stride+i], p6 = CDU[6*du_stride+i], p7 = CDU[7*du_stride+i];
        jo_DCT(&p0, &p1, &p2, &p3, &p4, &p5, &p6, &p7);
        CDU[0*du_stride+i] = p0; CDU[1*du_stride+i] = p1; CDU[2*du_stride+i] = p2; CDU[3*du_stride+i] = p3;
        CDU[4*du_stride+i] = p4; CDU[5*du_stride+i] = p5; CDU[6*du_stride+i] = p6; CDU[7*du_stride+i] = p7;
    }
    
    /* Quantize and zigzag reorder */
    for (int i = 0; i < 64; i++) {
        int row = jo_ZigZag[i] / 8, col = jo_ZigZag[i] % 8;
        float v = CDU[row * du_stride + col] * fdtbl[i];
        DU[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    
    /* Encode DC */
    int diff = DU[0] - DC;
    if (diff == 0) {
        jo_writeBits(ctx, HTDC[0][0], HTDC[0][1]);
    } else {
        uint16_t bits[2];
        jo_calcBits(diff, bits);
        jo_writeBits(ctx, HTDC[bits[1]][0], HTDC[bits[1]][1]);
        jo_writeBits(ctx, bits[0], bits[1]);
    }
    
    /* Encode AC */
    int end0pos = 63;
    while (end0pos > 0 && DU[end0pos] == 0) end0pos--;
    
    /* AC coefficients start from index 1 */
    if (end0pos == 0) {
        /* All AC coefficients are zero, write EOB */
        jo_writeBits(ctx, HTAC[0x00][0], HTAC[0x00][1]);
        return DU[0];
    }
    
    for (int i = 1; i <= end0pos; i++) {
        int startpos = i;
        while (DU[i] == 0 && i <= end0pos) i++;
        int nrzeroes = i - startpos;
        
        while (nrzeroes >= 16) {
            jo_writeBits(ctx, HTAC[0xF0][0], HTAC[0xF0][1]);
            nrzeroes -= 16;
        }
        
        uint16_t bits[2];
        jo_calcBits(DU[i], bits);
        jo_writeBits(ctx, HTAC[(nrzeroes << 4) + bits[1]][0], HTAC[(nrzeroes << 4) + bits[1]][1]);
        jo_writeBits(ctx, bits[0], bits[1]);
    }
    
    if (end0pos != 63) {
        jo_writeBits(ctx, HTAC[0x00][0], HTAC[0x00][1]);
    }
    
    return DU[0];
}

int jo_write_jpg_to_buffer(uint8_t *buffer, int buf_size, int width, int height, int comp, const uint8_t *data, int quality) {
    if (!data || width <= 0 || height <= 0 || comp < 1 || comp > 4) return 0;
    
    jo_initTables(quality);
    
    jo_jpeg_buf_t ctx;
    ctx.buf = buffer;
    ctx.capacity = buf_size;
    ctx.pos = 0;
    ctx.bitBuf = 0;
    ctx.bitCnt = 0;
    
    /* SOI */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xD8);
    
    /* APP0 */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xE0);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x10);
    jo_writeByte(&ctx, 'J');
    jo_writeByte(&ctx, 'F');
    jo_writeByte(&ctx, 'I');
    jo_writeByte(&ctx, 'F');
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x00);
    
    /* DQT Luminance */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDB);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x43);
    jo_writeByte(&ctx, 0x00);
    for (int i = 0; i < 64; i++) jo_writeByte(&ctx, YTable[i]);
    
    /* DQT Chrominance */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDB);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x43);
    jo_writeByte(&ctx, 0x01);
    for (int i = 0; i < 64; i++) jo_writeByte(&ctx, UVTable[i]);
    
    /* SOF0 */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC0);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x08);
    jo_writeByte(&ctx, height >> 8);
    jo_writeByte(&ctx, height & 0xFF);
    jo_writeByte(&ctx, width >> 8);
    jo_writeByte(&ctx, width & 0xFF);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x02);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x01);
    
    /* DHT Luminance DC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x1F);
    jo_writeByte(&ctx, 0x00);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_dc_luminance_nrcodes[i]);
    for (int i = 0; i < 12; i++) jo_writeByte(&ctx, std_dc_luminance_values[i]);
    
    /* DHT Luminance AC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0xB5);
    jo_writeByte(&ctx, 0x10);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_ac_luminance_nrcodes[i]);
    for (int i = 0; i < 162; i++) jo_writeByte(&ctx, std_ac_luminance_values[i]);
    
    /* DHT Chrominance DC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x1F);
    jo_writeByte(&ctx, 0x01);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_dc_chrominance_nrcodes[i]);
    for (int i = 0; i < 12; i++) jo_writeByte(&ctx, std_dc_chrominance_values[i]);
    
    /* DHT Chrominance AC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0xB5);
    jo_writeByte(&ctx, 0x11);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_ac_chrominance_nrcodes[i]);
    for (int i = 0; i < 162; i++) jo_writeByte(&ctx, std_ac_chrominance_values[i]);
    
    /* SOS */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDA);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x0C);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x02);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x3F);
    jo_writeByte(&ctx, 0x00);
    
    /* Encode image data */
    float Y[64], U[64], V[64];
    int DCY = 0, DCU = 0, DCV = 0;
    
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    int px = x + col;
                    int py = y + row;
                    if (px >= width) px = width - 1;
                    if (py >= height) py = height - 1;
                    
                    int p = (py * width + px) * comp;
                    float r, g, b;
                    
                    if (comp == 1) {
                        r = g = b = data[p];
                    } else {
                        r = data[p];
                        g = data[p + 1];
                        b = data[p + 2];
                    }
                    
                    Y[row * 8 + col] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    U[row * 8 + col] = -0.16874f * r - 0.33126f * g + 0.5f * b;
                    V[row * 8 + col] = 0.5f * r - 0.41869f * g - 0.08131f * b;
                }
            }
            
            DCY = jo_processDU(&ctx, Y, 8, fdtbl_Y, DCY, YDC_HT, YAC_HT);
            DCU = jo_processDU(&ctx, U, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
            DCV = jo_processDU(&ctx, V, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
        }
    }
    
    /* Flush remaining bits */
    if (ctx.bitCnt > 0) {
        jo_writeBits(&ctx, 0x7F, 7);
    }
    
    /* EOI */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xD9);
    
    return ctx.pos;
}

int jo_write_jpg_rgb565_to_buffer(uint8_t *buffer, int buf_size, int width, int height, const uint8_t *rgb565_data, int quality) {
    if (!rgb565_data || width <= 0 || height <= 0 || !buffer || buf_size < 1024) return 0;
    
    jo_initTables(quality);
    
    jo_jpeg_buf_t ctx;
    ctx.buf = buffer;
    ctx.capacity = buf_size;
    ctx.pos = 0;
    ctx.bitBuf = 0;
    ctx.bitCnt = 0;
    
    /* Write fixed headers as block copy for reliability */
    static const uint8_t jpeg_header[] = {
        /* SOI */
        0xFF, 0xD8,
        /* APP0 (JFIF) */
        0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
    };
    memcpy(buffer, jpeg_header, sizeof(jpeg_header));
    ctx.pos = sizeof(jpeg_header);
    
    /* DQT Luminance */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDB);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x43);
    jo_writeByte(&ctx, 0x00);
    for (int i = 0; i < 64; i++) jo_writeByte(&ctx, YTable[i]);
    
    /* DQT Chrominance */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDB);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x43);
    jo_writeByte(&ctx, 0x01);
    for (int i = 0; i < 64; i++) jo_writeByte(&ctx, UVTable[i]);
    
    /* SOF0 */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC0);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x08);
    jo_writeByte(&ctx, height >> 8);
    jo_writeByte(&ctx, height & 0xFF);
    jo_writeByte(&ctx, width >> 8);
    jo_writeByte(&ctx, width & 0xFF);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x02);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x01);
    
    /* DHT Luminance DC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x1F);
    jo_writeByte(&ctx, 0x00);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_dc_luminance_nrcodes[i]);
    for (int i = 0; i < 12; i++) jo_writeByte(&ctx, std_dc_luminance_values[i]);
    
    /* DHT Luminance AC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0xB5);
    jo_writeByte(&ctx, 0x10);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_ac_luminance_nrcodes[i]);
    for (int i = 0; i < 162; i++) jo_writeByte(&ctx, std_ac_luminance_values[i]);
    
    /* DHT Chrominance DC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x1F);
    jo_writeByte(&ctx, 0x01);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_dc_chrominance_nrcodes[i]);
    for (int i = 0; i < 12; i++) jo_writeByte(&ctx, std_dc_chrominance_values[i]);
    
    /* DHT Chrominance AC */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xC4);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0xB5);
    jo_writeByte(&ctx, 0x11);
    for (int i = 1; i <= 16; i++) jo_writeByte(&ctx, std_ac_chrominance_nrcodes[i]);
    for (int i = 0; i < 162; i++) jo_writeByte(&ctx, std_ac_chrominance_values[i]);
    
    /* SOS */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xDA);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x0C);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x01);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x02);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x03);
    jo_writeByte(&ctx, 0x11);
    jo_writeByte(&ctx, 0x00);
    jo_writeByte(&ctx, 0x3F);
    jo_writeByte(&ctx, 0x00);
    
    /* Encode image data */
    float Y[64], U[64], V[64];
    int DCY = 0, DCU = 0, DCV = 0;
    const int stride = width * 2;
    
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    int px = x + col;
                    int py = y + row;
                    if (px >= width) px = width - 1;
                    if (py >= height) py = height - 1;
                    
                    int p = py * stride + px * 2;
                    uint16_t pixel = rgb565_data[p] | (rgb565_data[p + 1] << 8);
                    
                    /* RGB565 to RGB888 */
                    int r5 = (pixel >> 11) & 0x1F;
                    int g6 = (pixel >> 5) & 0x3F;
                    int b5 = pixel & 0x1F;
                    
                    float r = (r5 << 3) | (r5 >> 2);
                    float g = (g6 << 2) | (g6 >> 4);
                    float b = (b5 << 3) | (b5 >> 2);
                    
                    Y[row * 8 + col] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    U[row * 8 + col] = -0.16874f * r - 0.33126f * g + 0.5f * b;
                    V[row * 8 + col] = 0.5f * r - 0.41869f * g - 0.08131f * b;
                }
            }
            
            DCY = jo_processDU(&ctx, Y, 8, fdtbl_Y, DCY, YDC_HT, YAC_HT);
            DCU = jo_processDU(&ctx, U, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
            DCV = jo_processDU(&ctx, V, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
        }
    }
    
    /* Flush remaining bits */
    if (ctx.bitCnt > 0) {
        jo_writeBits(&ctx, 0x7F, 7);
    }
    
    /* EOI */
    jo_writeByte(&ctx, 0xFF);
    jo_writeByte(&ctx, 0xD9);
    
    /* Verify JPEG header is not corrupted */
    if (buffer[0] != 0xFF || buffer[1] != 0xD8 || buffer[2] != 0xFF || buffer[3] != 0xE0) {
        /* Header corrupted - return 0 to indicate failure */
        return 0;
    }
    
    return ctx.pos;
}

#endif /* JO_JPEG_IMPLEMENTATION */
