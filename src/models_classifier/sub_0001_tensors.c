#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if APP_USE_CLASSIFIER
#include "sub_0001_tensors.h"

const TensorInfo sub_0001_tensors[] = {
  { "_split_1_command_stream", 0, 7064, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 1, 1127904, "MODEL", 0xffffffff },
  { "_split_1_scratch", 2, 602112, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 3, 602112, "FAST_SCRATCH", 0x0 },
  { "input0_70350_10684_70152", 4, 37632, "INPUT_TENSOR", 0x0 },
  { "output0_70264_10526", 5, 128, "OUTPUT_TENSOR", 0x0 },
};

const size_t sub_0001_tensors_count = sizeof(sub_0001_tensors) / sizeof(sub_0001_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0001_address_input0_70350_10684_70152 = 0x0;
const uint32_t sub_0001_address_output0_70264_10526 = 0x0;
#endif /* APP_USE_CLASSIFIER */

