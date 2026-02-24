#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include "common_data.h"

#include "sub_0000_tensors.h"
#include "sub_0000_command_stream.h"
#include "sub_0000_model_data.h"

#include "sub_0000_invoke.h"

// Include Ethos-U driver headers (Assumed to be available)
#include "ethosu_driver.h"

#ifndef FACE_NPU_DIAG_ONCE
#define FACE_NPU_DIAG_ONCE 0
#endif

#ifndef FACE_NPU_ERR_LOG_EVERY
#define FACE_NPU_ERR_LOG_EVERY 200U
#endif

// Define arenas with allocation and 16-byte alignment
__attribute__((aligned(16), section(".ram_noinit_nocache"))) uint8_t sub_0000_arena[442368];
// Fast scratch arena not used for Ethos-U55
//  We will not create it for now and reuse the address of the other arena
// __attribute__((aligned(16))) static uint8_t sub_0000_fast_scratch[442368];
uint8_t* sub_0000_fast_scratch = sub_0000_arena;

static void sub_0000_force_idle(void)
{
  g_ethosu0.job.state  = ETHOSU_JOB_IDLE;
  g_ethosu0.job.result = ETHOSU_JOB_RESULT_ERROR;
}

static void sub_0000_recover_timeout(void)
{
  (void) ethosu_soft_reset(&g_ethosu0);
  (void) ethosu_wait(&g_ethosu0, false);
  sub_0000_force_idle();
}

int sub_0000_invoke(bool clean_outputs) {
  // Initialize base addresses and sizes
  uint64_t base_addrs[6] = {0};
  size_t base_addrs_size[6] = {0};
  int num_base_addrs = 6;

  // Variables for command stream
  uint8_t* cms_data = NULL;
  int cms_size = 0;

  // Prepare base_addrs and base_addrs_size arrays
  // Buffer sub_0000_model with size 440048
  base_addrs[0] = (uint64_t)(uintptr_t)sub_0000_model_data;
  base_addrs_size[0] = sub_0000_model_data_size;
  // Buffer sub_0000_arena with size 442368 and address: 0
  base_addrs[1] = (uint64_t)(uintptr_t) (sub_0000_arena+0);
  base_addrs_size[1] = 442368;

  // Buffer sub_0000_fast_scratch with size 442368 and address: 0
  base_addrs[2] = (uint64_t)(uintptr_t) (sub_0000_arena+0);
  base_addrs_size[2] = 442368;

  // Buffer input_tensor_0 with size 36864 and address: 147456
  base_addrs[3] = (uint64_t)(uintptr_t) (sub_0000_arena+147456);
  base_addrs_size[3] = 36864;

  // Buffer output_tensor_0 with size 2592 and address: 13824
  if (clean_outputs) {
    memset(sub_0000_arena + 13824, 0, 2592);
  }
  base_addrs[4] = (uint64_t)(uintptr_t) (sub_0000_arena+13824);
  base_addrs_size[4] = 2592;

  // Buffer output_tensor_1 with size 648 and address: 4608
  if (clean_outputs) {
    memset(sub_0000_arena + 4608, 0, 648);
  }
  base_addrs[5] = (uint64_t)(uintptr_t) (sub_0000_arena+4608);
  base_addrs_size[5] = 648;

  // Command stream data
  cms_data = (uint8_t*)sub_0000_command_stream;
  cms_size = (int) sub_0000_command_stream_size;

  // Invoke the Ethos-U driver
  if (num_base_addrs > 8) {
    num_base_addrs = 8;
  }
#if FACE_NPU_DIAG_ONCE
  static bool dbg_once = false;
  if (!dbg_once) {
    dbg_once = true;
    uint32_t first_word = ((const uint32_t *)cms_data)[0];
    rt_kprintf("[FACE] cms_size=%d first_word=0x%08x\n", cms_size, (unsigned)first_word);
    rt_kprintf("[FACE] base0=%p size0=%u\n", (void *)(uintptr_t)base_addrs[0], (unsigned)base_addrs_size[0]);
    rt_kprintf("[FACE] base1=%p size1=%u\n", (void *)(uintptr_t)base_addrs[1], (unsigned)base_addrs_size[1]);
  }
#endif
  if (ethosu_invoke_async(&g_ethosu0, cms_data, cms_size, base_addrs, base_addrs_size, num_base_addrs, NULL) < 0)
  {
    static uint32_t invoke_fail_cnt = 0;
    invoke_fail_cnt++;
    if ((FACE_NPU_ERR_LOG_EVERY > 0U) && ((invoke_fail_cnt % FACE_NPU_ERR_LOG_EVERY) == 1U))
    {
      rt_kprintf("[FACE] ethosu_invoke_async failed (cnt=%lu)\n", (unsigned long)invoke_fail_cnt);
    }
    sub_0000_recover_timeout();
    return -1;
  }

  rt_tick_t start_tick = rt_tick_get();
  while (1)
  {
    int wait_ret = ethosu_wait(&g_ethosu0, false);
    if (wait_ret == 0)
    {
      break;
    }
    if (wait_ret < 0)
    {
      static uint32_t wait_fail_cnt = 0;
      wait_fail_cnt++;
      if ((FACE_NPU_ERR_LOG_EVERY > 0U) && ((wait_fail_cnt % FACE_NPU_ERR_LOG_EVERY) == 1U))
      {
        rt_kprintf("[FACE] ethosu_wait failed (cnt=%lu)\n", (unsigned long)wait_fail_cnt);
      }
      sub_0000_recover_timeout();
      return -1;
    }
    if ((rt_tick_get() - start_tick) > (RT_TICK_PER_SECOND * 2U))
    {
      static uint32_t wait_timeout_cnt = 0;
      wait_timeout_cnt++;
      if ((FACE_NPU_ERR_LOG_EVERY > 0U) && ((wait_timeout_cnt % FACE_NPU_ERR_LOG_EVERY) == 1U))
      {
        rt_kprintf("[FACE] ethosu_wait timeout (cnt=%lu)\n", (unsigned long)wait_timeout_cnt);
      }
      sub_0000_recover_timeout();
      return -1;
    }
    rt_thread_mdelay(1);
  }

  return 0;
}
