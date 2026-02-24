#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if 1
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
 */

#include <stdint.h>

#include "gesture_compute_sub_0002.h"

#include "arm_nn_types.h"
#include "arm_nnfunctions.h"
#include "kernel_library_utils.h"

#include "kernel_library_int.h" 

 

void gesture_compute_sub_0002(
  // buffer for intermediate results
  uint8_t* main_storage, // should provide at least 3797 bytes of storage

  // inputs
  
  const int8_t _m_model_30_conv_Conv_output_0_70435_70603_11144[756], // 1,21,6,6
  
  const int8_t _m_model_38_conv_Conv_output_0_70452_70604_11148[3024], // 1,21,12,12
  

  // outputs
  
  float p4_12x12_70454[3024] , // 1,3,12,12,7
  
  float p5_6x6_70437[756]  // 1,3,6,6,7
  
) {
  // Buffers allocated on the main storage (note: depends on the execution order)
    
  
  int8_t* p4_12x12_70454_11156 = (int8_t *) &main_storage[0]; // 1,3,12,12,7 == 3024
  
  int8_t* p5_6x6_70437_11160 = (int8_t *) &main_storage[3024]; // 1,3,6,6,7 == 756
  
  

  // Parameters
  







//
// Identity - bypassing _m_model_39_Reshape_output_0_70453_10895 operation
//
// Input _m_model_38_conv_Conv_output_0_70452_70604_11148: int8_t - 1,21,12,12
// Output _m_model_39_Reshape_output_0_70453_10895: int8_t - 1,3,7,12,12


const int8_t* _m_model_39_Reshape_output_0_70453_10895 = _m_model_38_conv_Conv_output_0_70452_70604_11148;





//
// Transpose
//
// Input _m_model_39_Reshape_output_0_70453_10895: int8_t - 1,3,7,12,12
// Output p4_12x12_70454_11156: int8_t - 1,3,12,12,7
// Perm: ( 0,  1,  3,  4,  2, )

int32_t strides_p4_12x12_70454_11156[5] = { 3024, 1008, 12, 1, 144,  };

int32_t next_dim_sizes_p4_12x12_70454_11156[5] = { 3024, 3024, 1008, 84, 7,  };

int32_t dim_sizes_p4_12x12_70454_11156[5] = { 3024, 1008, 84, 7, 1,  };


Transpose(
      _m_model_39_Reshape_output_0_70453_10895
    , p4_12x12_70454_11156
    , 3024
    , 5
    , strides_p4_12x12_70454_11156
    , next_dim_sizes_p4_12x12_70454_11156
    , dim_sizes_p4_12x12_70454_11156
);

//
// Dequantize
//
// Input  p4_12x12_70454_11156: int8_t - 1,3,12,12,7
// Output p4_12x12_70454: float - 1,3,12,12,7
AffineDequantizeInt8ToFloat(p4_12x12_70454_11156, p4_12x12_70454, 3024, -1, 0.1678222119808197);


//
// Identity - bypassing _m_model_39_Reshape_1_output_0_70436_10891 operation
//
// Input _m_model_30_conv_Conv_output_0_70435_70603_11144: int8_t - 1,21,6,6
// Output _m_model_39_Reshape_1_output_0_70436_10891: int8_t - 1,3,7,6,6


const int8_t* _m_model_39_Reshape_1_output_0_70436_10891 = _m_model_30_conv_Conv_output_0_70435_70603_11144;





//
// Transpose
//
// Input _m_model_39_Reshape_1_output_0_70436_10891: int8_t - 1,3,7,6,6
// Output p5_6x6_70437_11160: int8_t - 1,3,6,6,7
// Perm: ( 0,  1,  3,  4,  2, )

int32_t strides_p5_6x6_70437_11160[5] = { 756, 252, 6, 1, 36,  };

int32_t next_dim_sizes_p5_6x6_70437_11160[5] = { 756, 756, 252, 42, 7,  };

int32_t dim_sizes_p5_6x6_70437_11160[5] = { 756, 252, 42, 7, 1,  };


Transpose(
      _m_model_39_Reshape_1_output_0_70436_10891
    , p5_6x6_70437_11160
    , 756
    , 5
    , strides_p5_6x6_70437_11160
    , next_dim_sizes_p5_6x6_70437_11160
    , dim_sizes_p5_6x6_70437_11160
);

//
// Dequantize
//
// Input  p5_6x6_70437_11160: int8_t - 1,3,6,6,7
// Output p5_6x6_70437: float - 1,3,6,6,7
AffineDequantizeInt8ToFloat(p5_6x6_70437_11160, p5_6x6_70437, 756, 0, 0.49514177441596985);


}
#endif /* !APP_USE_CLASSIFIER */


