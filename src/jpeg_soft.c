#include <stdint.h>
#include <string.h>
#include <math.h>
#include "jpeg_soft.h"

// Tables for JPEG encoding
static const uint8_t std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t std_ac_luminance_values[] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,
    0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,
    0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,
    0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,
    0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,
    0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,
    0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static const uint8_t std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t std_ac_chrominance_values[] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,
    0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,
    0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,
    0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,
    0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,
    0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,
    0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static const uint8_t ZigZag[] = { 
 0, 1, 5, 6,14,15,27,28,
 2, 4, 7,13,16,26,29,42,
 3, 8,12,17,25,30,41,43,
 9,11,18,24,31,40,44,53,
10,19,23,32,39,45,52,54,
20,22,33,38,46,51,55,60,
21,34,37,47,50,56,59,61,
35,36,48,49,57,58,62,63 
};

typedef struct {
    uint8_t *buf;
    int size;
    int pos;
    uint32_t bit_buf;
    int bit_cnt;
} JpegWriter;

static void write_byte(JpegWriter *jw, uint8_t b) {
    if (jw->pos < jw->size) {
        jw->buf[jw->pos++] = b;
    }
}

static void write_bits(JpegWriter *jw, uint32_t bits, int num) {
    jw->bit_buf |= (bits << (32 - jw->bit_cnt - num));
    jw->bit_cnt += num;
    while (jw->bit_cnt >= 8) {
        uint8_t b = (uint8_t)(jw->bit_buf >> 24);
        write_byte(jw, b);
        if (b == 0xFF) write_byte(jw, 0x00);
        jw->bit_buf <<= 8;
        jw->bit_cnt -= 8;
    }
}

typedef struct {
    uint16_t code[256];
    uint8_t len[256];
} HufTable;

static void gen_huffman_table(const uint8_t *nrcodes, const uint8_t *std_table, HufTable *ht) {
    int k = 0;
    uint16_t code = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < nrcodes[i]; j++) {
            ht->len[std_table[k]] = (uint8_t)i;
            ht->code[std_table[k]] = code;
            code++;
            k++;
        }
        code <<= 1;
    }
}

static HufTable htable_luma_dc, htable_luma_ac, htable_chroma_dc, htable_chroma_ac;
static float aasf[64];
static int YTable[64], CbCrTable[64];
static int init_done = 0;

static void init_tables(int quality) {
    if (init_done) return;
    if (quality <= 0) quality = 1;
    if (quality > 100) quality = 100;
    int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    // Standard quantization tables
    const uint8_t std_luma_qt[64] = {
        16, 11, 10, 16, 24, 40, 51, 61,
        12, 12, 14, 19, 26, 58, 60, 55,
        14, 13, 16, 24, 40, 57, 69, 56,
        14, 17, 22, 29, 51, 87, 80, 62,
        18, 22, 37, 56, 68,109,103, 77,
        24, 35, 55, 64, 81,104,113, 92,
        49, 64, 78, 87,103,121,120,101,
        72, 92, 95, 98,112,100,103, 99
    };
    const uint8_t std_chroma_qt[64] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
    };

    for(int i=0; i<64; i++) {
        int temp = ((int)std_luma_qt[i] * scale + 50) / 100;
        if(temp < 1) temp=1; if(temp > 255) temp=255;
        YTable[ZigZag[i]] = temp;

        temp = ((int)std_chroma_qt[i] * scale + 50) / 100;
        if(temp < 1) temp=1; if(temp > 255) temp=255;
        CbCrTable[ZigZag[i]] = temp;
    }

    gen_huffman_table(std_dc_luminance_nrcodes, std_dc_luminance_values, &htable_luma_dc);
    gen_huffman_table(std_ac_luminance_nrcodes, std_ac_luminance_values, &htable_luma_ac);
    gen_huffman_table(std_dc_chrominance_nrcodes, std_dc_chrominance_values, &htable_chroma_dc);
    gen_huffman_table(std_ac_chrominance_nrcodes, std_ac_chrominance_values, &htable_chroma_ac);

    for(int i=0; i<8; i++) {
        for(int j=0; j<8; j++) {
            aasf[i*8+j] = 1.0f / (8.0f * sqrt( (i==0?1.0:2.0) * (j==0?1.0:2.0)));
        }
    }
    init_done = 1;
}

static void process_du(JpegWriter *jw, float *component, int *DC, int *QT, HufTable *htDC, HufTable *htAC) {
    float DU[64];
    // FDCT
    for (int i=0; i<8; ++i) {
        float d0 = component[i*8+0]; float d1 = component[i*8+1]; float d2 = component[i*8+2]; float d3 = component[i*8+3];
        float d4 = component[i*8+4]; float d5 = component[i*8+5]; float d6 = component[i*8+6]; float d7 = component[i*8+7];
        
        float tmp0 = d0 + d7; float tmp7 = d0 - d7; float tmp1 = d1 + d6; float tmp6 = d1 - d6;
        float tmp2 = d2 + d5; float tmp5 = d2 - d5; float tmp3 = d3 + d4; float tmp4 = d3 - d4;
        float tmp10 = tmp0 + tmp3; float tmp13 = tmp0 - tmp3; float tmp11 = tmp1 + tmp2; float tmp12 = tmp1 - tmp2;
        
        float z1 = (tmp12 + tmp13) * 0.707106781f;
        component[i*8+0] = tmp10 + tmp11; component[i*8+4] = tmp10 - tmp11;
        component[i*8+2] = tmp13 + z1;    component[i*8+6] = tmp13 - z1;
        
        tmp10 = tmp4 + tmp5; tmp11 = tmp5 + tmp6; tmp12 = tmp6 + tmp7;
        float z5 = (tmp10 - tmp12) * 0.382683433f;
        float z2 = 0.5411961f * tmp10 + z5; float z4 = 1.306562965f * tmp12 + z5;
        float z3 = tmp11 * 0.707106781f;
        
        float z11 = tmp7 + z3; float z13 = tmp7 - z3;
        component[i*8+5] = z13 + z2; component[i*8+3] = z13 - z2;
        component[i*8+1] = z11 + z4; component[i*8+7] = z11 - z4;
    }
    
    for (int j=0; j<8; ++j) {
        float d0 = component[0*8+j]; float d1 = component[1*8+j]; float d2 = component[2*8+j]; float d3 = component[3*8+j];
        float d4 = component[4*8+j]; float d5 = component[5*8+j]; float d6 = component[6*8+j]; float d7 = component[7*8+j];
        
        float tmp0 = d0 + d7; float tmp7 = d0 - d7; float tmp1 = d1 + d6; float tmp6 = d1 - d6;
        float tmp2 = d2 + d5; float tmp5 = d2 - d5; float tmp3 = d3 + d4; float tmp4 = d3 - d4;
        float tmp10 = tmp0 + tmp3; float tmp13 = tmp0 - tmp3; float tmp11 = tmp1 + tmp2; float tmp12 = tmp1 - tmp2;
        
        float z1 = (tmp12 + tmp13) * 0.707106781f;
        component[0*8+j] = tmp10 + tmp11; component[4*8+j] = tmp10 - tmp11;
        component[2*8+j] = tmp13 + z1;    component[6*8+j] = tmp13 - z1;
        // ... (simplified structure for brevity, standard AAN FDCT here)
        tmp10 = tmp4 + tmp5; tmp11 = tmp5 + tmp6; tmp12 = tmp6 + tmp7;
        float z5 = (tmp10 - tmp12) * 0.382683433f;
        float z2 = 0.5411961f * tmp10 + z5; float z4 = 1.306562965f * tmp12 + z5;
        float z3 = tmp11 * 0.707106781f;
        
        float z11 = tmp7 + z3; float z13 = tmp7 - z3;
        component[5*8+j] = z13 + z2; component[3*8+j] = z13 - z2;
        component[1*8+j] = z11 + z4; component[7*8+j] = z11 - z4;
    }

    // Quantize
    int du_i[64];
    for (int i=0; i<64; ++i) {
        float v = component[i] * aasf[i];
        du_i[ZigZag[i]] = (int)(v / QT[i]);
    }

    // Entropy Encode (DC)
    int diff = du_i[0] - *DC;
    *DC = du_i[0];
    if (diff == 0) write_bits(jw, htDC->code[0], htDC->len[0]);
    else {
        int bits = 0; int v = diff;
        if (diff < 0) { v = -diff; bits = ~diff; } else bits = diff;
        int len = 0; while(v > 0) { v>>=1; len++; }
        write_bits(jw, htDC->code[len], htDC->len[len]);
        write_bits(jw, bits & ((1<<len)-1), len);
    }

    // Entropy Encode (AC)
    int end0pos = 63;
    while((end0pos > 0) && (du_i[end0pos] == 0)) end0pos--;
    
    for (int i=1; i<=end0pos; ++i) {
        int run = 0;
        while((du_i[i] == 0) && (i <= end0pos)) { run++; i++; }
        while (run >= 16) { write_bits(jw, htAC->code[0xF0], htAC->len[0xF0]); run -= 16; }
        
        int val = du_i[i];
        int ads = (val < 0) ? -val : val;
        int len = 0; while(ads > 0) { ads>>=1; len++; }
        
        int sym = (run << 4) | len;
        write_bits(jw, htAC->code[sym], htAC->len[sym]);
        
        int bits = (val < 0) ? (~val) : val;
        write_bits(jw, bits & ((1<<len)-1), len);
    }
    if (end0pos != 63) write_bits(jw, htAC->code[0], htAC->len[0]);
}

int jpeg_soft_encode_rgb565(const uint8_t *p_rgb565, int width, int height, int quality, uint8_t *p_jpg_buf, int buf_size) {
    init_tables(quality);
    
    JpegWriter jw;
    jw.buf = p_jpg_buf;
    jw.size = buf_size;
    jw.pos = 0;
    jw.bit_buf = 0;
    jw.bit_cnt = 0;

    // SOI
    write_byte(&jw, 0xFF); write_byte(&jw, 0xD8);
    // SOF0
    write_byte(&jw, 0xFF); write_byte(&jw, 0xC0); write_byte(&jw, 0x00); write_byte(&jw, 0x11);
    write_byte(&jw, 0x08); 
    write_byte(&jw, (height >> 8) & 0xFF); write_byte(&jw, height & 0xFF);
    write_byte(&jw, (width >> 8) & 0xFF); write_byte(&jw, width & 0xFF);
    write_byte(&jw, 0x03); 
    write_byte(&jw, 1); write_byte(&jw, 0x11); write_byte(&jw, 0); // Y
    write_byte(&jw, 2); write_byte(&jw, 0x11); write_byte(&jw, 1); // Cb
    write_byte(&jw, 3); write_byte(&jw, 0x11); write_byte(&jw, 1); // Cr
    
    // DQT
    write_byte(&jw, 0xFF); write_byte(&jw, 0xDB); write_byte(&jw, 0x00); write_byte(&jw, 0x84);
    write_byte(&jw, 0); for(int i=0; i<64; i++) write_byte(&jw, YTable[i]);
    write_byte(&jw, 1); for(int i=0; i<64; i++) write_byte(&jw, CbCrTable[i]);
    
    // DHT
    // (Skipped full DHT payload generation for brevity, assuming standard tables written directly)
    // To properly work, we need to write the standard tables here.
    // Luma DC
    write_byte(&jw, 0xFF); write_byte(&jw, 0xC4); write_byte(&jw, 0x00); write_byte(&jw, 0x1F);
    write_byte(&jw, 0); for(int i=0; i<16; i++) write_byte(&jw, std_dc_luminance_nrcodes[i+1]);
    for(int i=0; i<12; i++) write_byte(&jw, std_dc_luminance_values[i]);
    // Luma AC
    write_byte(&jw, 0xFF); write_byte(&jw, 0xC4); write_byte(&jw, 0x00); write_byte(&jw, 0xB5);
    write_byte(&jw, 0x10); for(int i=0; i<16; i++) write_byte(&jw, std_ac_luminance_nrcodes[i+1]);
    for(int i=0; i<162; i++) write_byte(&jw, std_ac_luminance_values[i]);
    // Chroma DC
    write_byte(&jw, 0xFF); write_byte(&jw, 0xC4); write_byte(&jw, 0x00); write_byte(&jw, 0x1F);
    write_byte(&jw, 0x01); for(int i=0; i<16; i++) write_byte(&jw, std_dc_chrominance_nrcodes[i+1]);
    for(int i=0; i<12; i++) write_byte(&jw, std_dc_chrominance_values[i]);
    // Chroma AC
    write_byte(&jw, 0xFF); write_byte(&jw, 0xC4); write_byte(&jw, 0x00); write_byte(&jw, 0xB5);
    write_byte(&jw, 0x11); for(int i=0; i<16; i++) write_byte(&jw, std_ac_chrominance_nrcodes[i+1]);
    for(int i=0; i<162; i++) write_byte(&jw, std_ac_chrominance_values[i]);

    // SOS (Start of Scan)
    write_byte(&jw, 0xFF); write_byte(&jw, 0xDA); write_byte(&jw, 0x00); write_byte(&jw, 0x0C);
    write_byte(&jw, 0x03); 
    write_byte(&jw, 1); write_byte(&jw, 0x00);
    write_byte(&jw, 2); write_byte(&jw, 0x11);
    write_byte(&jw, 3); write_byte(&jw, 0x11);
    write_byte(&jw, 0); write_byte(&jw, 63); write_byte(&jw, 0);

    const int stride = width * 2;
    int DCY = 0, DCCb = 0, DCCr = 0;
    
    // Process MCUs
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            float Y[64], Cb[64], Cr[64];
            
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    int srcX = x + c;
                    int srcY = y + r;
                    if (srcX >= width) srcX = width - 1;
                    if (srcY >= height) srcY = height - 1;
                    
                    int pIdx = srcY * stride + srcX * 2;
                    uint16_t pix = p_rgb565[pIdx] | (p_rgb565[pIdx+1] << 8);
                    
                    // RGB565 to RGB888
                    int R = (pix >> 11) & 0x1F; R = (R << 3) | (R >> 2);
                    int G = (pix >> 5) & 0x3F;  G = (G << 2) | (G >> 4);
                    int B = (pix) & 0x1F;       B = (B << 3) | (B >> 2);
                    
                    // RGB to YCbCr
                    Y[r*8+c]  = (float)( 0.299f*R + 0.587f*G + 0.114f*B - 128);
                    Cb[r*8+c] = (float)(-0.1687f*R - 0.3313f*G + 0.500f*B);
                    Cr[r*8+c] = (float)( 0.500f*R - 0.4187f*G - 0.0813f*B);
                }
            }
            process_du(&jw, Y, &DCY, YTable, &htable_luma_dc, &htable_luma_ac);
            process_du(&jw, Cb, &DCCb, CbCrTable, &htable_chroma_dc, &htable_chroma_ac);
            process_du(&jw, Cr, &DCCr, CbCrTable, &htable_chroma_dc, &htable_chroma_ac);
        }
    }

    // EOI
    if (jw.bit_cnt > 0) {
        uint8_t b = (uint8_t)(jw.bit_buf >> 24);
        write_byte(&jw, b);
        if (b == 0xFF) write_byte(&jw, 0x00);
    }
    write_byte(&jw, 0xFF); write_byte(&jw, 0xD9);
    
    return jw.pos;
}

