#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if !APP_USE_CLASSIFIER
#include "sub_0001_tensors.h"

const TensorInfo sub_0001_tensors[] = {
  { "_split_1_command_stream", 2, 11392, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 3, 436688, "MODEL", 0xffffffff },
  { "_split_1_scratch", 4, 442368, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 5, 442368, "FAST_SCRATCH", 0x0 },
  { "images_70602_11152_70256", 6, 110592, "INPUT_TENSOR", 0x0 },
  { "_m_model_38_conv_Conv_output_0_70452_70604_11148", 1, 3024, "OUTPUT_TENSOR", 0x1950 },
  { "_m_model_30_conv_Conv_output_0_70435_70603_11144", 0, 756, "OUTPUT_TENSOR", 0x0 },
};

const size_t sub_0001_tensors_count = sizeof(sub_0001_tensors) / sizeof(sub_0001_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0001_address_images_70602_11152_70256 = 0x0;
const uint32_t sub_0001_address__m_model_38_conv_Conv_output_0_70452_70604_11148 = 0x1950;
const uint32_t sub_0001_address__m_model_30_conv_Conv_output_0_70435_70603_11144 = 0x0;
#endif /* !APP_USE_CLASSIFIER */

