#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if !APP_USE_CLASSIFIER
/*
 * This file is developed by EdgeCortix Inc. to be used with certain Renesas Electronics Hardware only.
 *
 * Copyright 漏 2025 EdgeCortix Inc. Licensed to Renesas Electronics Corporation with the
 * right to sublicense under the Apache License, Version 2.0.
 *
 * This file also includes source code originally developed by the Renesas Electronics Corporation.
 * The Renesas disclaimer below applies to any Renesas-originated portions for usage of the code.
 *
 * The Renesas Electronics Corporation
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED 'AS IS' AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Changed from original python code to C source code.
 * Copyright (C) 2017 Renesas Electronics Corporation. All rights reserved.
 *
 * This file also includes source codes originally developed by the TensorFlow Authors which were distributed under the following conditions.
 *
 * The TensorFlow Authors
 * Copyright 2023 The Apache Software Foundation
 *
 * This product includes software developed at
 * The Apache Software Foundation (http://www.apache.org/).
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "model.h"
#include "hal_data.h"
#include <rtthread.h>

// CPU compute declarations
#include "compute_sub_0000.h"
#include "sub_0001_invoke.h"
#include "compute_sub_0002.h"

// Buffers for CPU units
float buf_images[110592];
float buf_p4_12x12_70454[3024];
float buf_p5_6x6_70437[756];

// Arenas for CPU units
uint8_t compute_arena_sub_0000[kBufferSize_sub_0000];
uint8_t compute_arena_sub_0002[kBufferSize_sub_0002];

  // Model input pointers
float* GetModelInputPtr_images() {
  return buf_images;
}


  // Model output pointers
float* GetModelOutputPtr_p4_12x12_70454() {
  return buf_p4_12x12_70454;
}

float* GetModelOutputPtr_p5_6x6_70437() {
  return buf_p5_6x6_70437;
}


void RunModel(bool clean_outputs) {
  static uint32_t run_cnt = 0;
  rt_tick_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;

  // Buffers for NPU units
  int8_t* buf_images_70602_11152_70256 = (int8_t*) (sub_0001_arena + sub_0001_address_images_70602_11152_70256);
  int8_t* buf__m_model_30_conv_Conv_output_0_70435_70603_11144 = (int8_t*) (sub_0001_arena + sub_0001_address__m_model_30_conv_Conv_output_0_70435_70603_11144);
  int8_t* buf__m_model_38_conv_Conv_output_0_70452_70604_11148 = (int8_t*) (sub_0001_arena + sub_0001_address__m_model_38_conv_Conv_output_0_70452_70604_11148);

// CPU Unit
  t0 = rt_tick_get();
  compute_sub_0000(compute_arena_sub_0000, buf_images, buf_images_70602_11152_70256  );
  t1 = rt_tick_get();

  // Simple checksum of NPU input (first 256 bytes) to verify it changes with camera frames.
  uint32_t in_hash = 0;
  int8_t in_min = 127, in_max = -128;
  for (int i = 0; i < 256; i++) {
    int8_t v = buf_images_70602_11152_70256[i];
    if (v < in_min) in_min = v;
    if (v > in_max) in_max = v;
    in_hash = (in_hash * 131u) + (uint8_t)v;
  }

#if (BSP_CFG_DCACHE_ENABLED)
  // Ensure NPU sees latest input in cacheable memory
  uintptr_t in_addr = (uintptr_t)buf_images_70602_11152_70256;
  uintptr_t in_start = in_addr & ~(uintptr_t)31;
  uintptr_t in_end = (in_addr + (uintptr_t)110592 + (uintptr_t)31) & ~(uintptr_t)31;
  SCB_CleanDCache_by_Addr((uint32_t *)in_start, (int32_t)(in_end - in_start));
#endif

// NPU Unit
  t2 = rt_tick_get();
  if (sub_0001_invoke(clean_outputs) != 0) {
    memset(buf_p4_12x12_70454, 0, sizeof(buf_p4_12x12_70454));
    memset(buf_p5_6x6_70437, 0, sizeof(buf_p5_6x6_70437));
    return;
  }
  t3 = rt_tick_get();

#if (BSP_CFG_DCACHE_ENABLED)
  // Invalidate NPU outputs before CPU consumes them
  uintptr_t o1_addr = (uintptr_t)buf__m_model_30_conv_Conv_output_0_70435_70603_11144;
  uintptr_t o1_start = o1_addr & ~(uintptr_t)31;
  uintptr_t o1_end = (o1_addr + (uintptr_t)756 + (uintptr_t)31) & ~(uintptr_t)31;
  SCB_InvalidateDCache_by_Addr((uint32_t *)o1_start, (int32_t)(o1_end - o1_start));

  uintptr_t o2_addr = (uintptr_t)buf__m_model_38_conv_Conv_output_0_70452_70604_11148;
  uintptr_t o2_start = o2_addr & ~(uintptr_t)31;
  uintptr_t o2_end = (o2_addr + (uintptr_t)3024 + (uintptr_t)31) & ~(uintptr_t)31;
  SCB_InvalidateDCache_by_Addr((uint32_t *)o2_start, (int32_t)(o2_end - o2_start));
#endif

// CPU Unit
  compute_sub_0002(compute_arena_sub_0002, buf__m_model_30_conv_Conv_output_0_70435_70603_11144, buf__m_model_38_conv_Conv_output_0_70452_70604_11148, buf_p4_12x12_70454, buf_p5_6x6_70437  );

  // Debug timings + hashes every 30 frames (avoid log spam).
  if ((run_cnt % 30u) == 0u) {
    uint32_t ms_qtz = (uint32_t)((t1 - t0) * 1000u / RT_TICK_PER_SECOND);
    uint32_t ms_npu = (uint32_t)((t3 - t2) * 1000u / RT_TICK_PER_SECOND);

    uint32_t out0_hash = 0;
    int8_t out0_min = 127, out0_max = -128;
    for (int i = 0; i < 256; i++) {
      int8_t v = buf__m_model_30_conv_Conv_output_0_70435_70603_11144[i % 756];
      if (v < out0_min) out0_min = v;
      if (v > out0_max) out0_max = v;
      out0_hash = (out0_hash * 131u) + (uint8_t)v;
    }

    uint32_t out1_hash = 0;
    int8_t out1_min = 127, out1_max = -128;
    for (int i = 0; i < 256; i++) {
      int8_t v = buf__m_model_38_conv_Conv_output_0_70452_70604_11148[i % 3024];
      if (v < out1_min) out1_min = v;
      if (v > out1_max) out1_max = v;
      out1_hash = (out1_hash * 131u) + (uint8_t)v;
    }

    rt_kprintf("[NPU DBG] qtz=%lums npu=%lums | in hash=%lu min=%d max=%d | out0 hash=%lu min=%d max=%d | out1 hash=%lu min=%d max=%d\n",
               (unsigned long)ms_qtz, (unsigned long)ms_npu,
               (unsigned long)in_hash, (int)in_min, (int)in_max,
               (unsigned long)out0_hash, (int)out0_min, (int)out0_max,
               (unsigned long)out1_hash, (int)out1_min, (int)out1_max);
  }

  run_cnt++;

}
#endif /* !APP_USE_CLASSIFIER */

