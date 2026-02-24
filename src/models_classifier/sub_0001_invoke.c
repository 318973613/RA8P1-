#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if APP_USE_CLASSIFIER
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <rtthread.h>
#include "rt-thread/components/legacy/dfs/dfs_posix.h"
#include <fcntl.h>
#include "common_data.h"

#include "sub_0001_tensors.h"
#include "sub_0001_command_stream.h"
#include "sub_0001_model_data.h"

#include "sub_0001_invoke.h"

// For DCache maintenance
#include "hal_data.h"
// Include Ethos-U driver headers (Assumed to be available)
#include "ethosu_driver.h"

// Define arenas with allocation and 16-byte alignment.
// Place arena in external OSPI RAM (HyperRAM) to reduce internal SRAM pressure.
__attribute__((aligned(16), section(".ospi1_cs0_noinit"))) uint8_t sub_0001_arena[602112];
// Copy model data into HyperRAM to avoid NPU reading from external NOR directly.
__attribute__((aligned(16), section(".ospi1_cs0_noinit"))) static uint8_t sub_0001_model_data_ram[1127904];
static bool sub_0001_model_data_ready = false;
// Fast scratch arena not used for Ethos-U55
//  We will not create it for now and reuse the address of the other arena
// __attribute__((aligned(16))) static uint8_t sub_0001_fast_scratch[602112];
uint8_t* sub_0001_fast_scratch = sub_0001_arena;

#ifndef EMB_MODEL_FILE_PATH
#define EMB_MODEL_FILE_PATH "/emb/mobilefacenet_u55.bin"
#endif

#ifndef EMB_NPU_ERR_LOG_EVERY
#define EMB_NPU_ERR_LOG_EVERY 200U
#endif

static void sub_0001_force_idle(void)
{
    g_ethosu0.job.state  = ETHOSU_JOB_IDLE;
    g_ethosu0.job.result = ETHOSU_JOB_RESULT_ERROR;
}

static void sub_0001_recover_timeout(void)
{
    (void) ethosu_soft_reset(&g_ethosu0);
    (void) ethosu_wait(&g_ethosu0, false);
    sub_0001_force_idle();
}

static void dcache_clean_align(void *buf, size_t len)
{
#if (BSP_CFG_DCACHE_ENABLED)
    if (!buf || len == 0U)
    {
        return;
    }
    uintptr_t start = (uintptr_t) buf;
    uintptr_t end   = start + (uintptr_t) len;
    start &= ~(uintptr_t) 31U;
    end    = (end + 31U) & ~(uintptr_t) 31U;
    SCB_CleanDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) buf;
    (void) len;
#endif
}

static bool sub_0001_prepare_model_data(void)
{
    static uint32_t load_fail_cnt = 0;

    if (sub_0001_model_data_ready)
    {
        return true;
    }

    int fd = open(EMB_MODEL_FILE_PATH, O_RDONLY, 0);
    if (fd < 0)
    {
        load_fail_cnt++;
        if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
        {
            rt_kprintf("[EMB] model file open failed: %s\n", EMB_MODEL_FILE_PATH);
        }
        return false;
    }

    size_t total = 0U;
    while (total < sub_0001_model_data_size)
    {
        int rd = read(fd, sub_0001_model_data_ram + total, (int) (sub_0001_model_data_size - total));
        if (rd <= 0)
        {
            close(fd);
            load_fail_cnt++;
            if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
            {
                rt_kprintf("[EMB] model file read failed: %s (%lu/%lu)\n",
                           EMB_MODEL_FILE_PATH,
                           (unsigned long) total,
                           (unsigned long) sub_0001_model_data_size);
            }
            return false;
        }
        total += (size_t) rd;
    }
    close(fd);

    bool all_ff = true;
    bool all_00 = true;
    for (size_t i = 0; i < sub_0001_model_data_size; i++)
    {
        uint8_t b = sub_0001_model_data_ram[i];
        if (b != 0xFFU) all_ff = false;
        if (b != 0x00U) all_00 = false;
        if (!all_ff && !all_00)
        {
            break;
        }
    }
    if (all_ff || all_00)
    {
        load_fail_cnt++;
        if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
        {
            rt_kprintf("[EMB] model file invalid (blank): %s\n", EMB_MODEL_FILE_PATH);
        }
        return false;
    }

    dcache_clean_align(sub_0001_model_data_ram, sub_0001_model_data_size);
    sub_0001_model_data_ready = true;
    load_fail_cnt = 0;
    rt_kprintf("[EMB] model loaded from fs: %s (%lu bytes)\n",
               EMB_MODEL_FILE_PATH,
               (unsigned long) sub_0001_model_data_size);
    return true;
}


int sub_0001_invoke(bool clean_outputs) {
  if (!sub_0001_prepare_model_data()) {
    return -1;
  }
  // Initialize base addresses and sizes
  uint64_t base_addrs[5] = {0};
  size_t base_addrs_size[5] = {0};
  int num_base_addrs = 5;

  // Variables for command stream
  uint8_t* cms_data = NULL;
  int cms_size = 0;

  // Prepare base_addrs and base_addrs_size arrays
  // Buffer sub_0001_model with size 1127904
  base_addrs[0] = (uint64_t)(uintptr_t)sub_0001_model_data_ram;
  base_addrs_size[0] = sub_0001_model_data_size;
  // Buffer sub_0001_arena with size 602112 and address: 0
  base_addrs[1] = (uint64_t)(uintptr_t) (sub_0001_arena+0);
  base_addrs_size[1] = 602112;

  // Buffer sub_0001_fast_scratch with size 602112 and address: 0
  base_addrs[2] = (uint64_t)(uintptr_t) (sub_0001_arena+0);
  base_addrs_size[2] = 602112;

  // Buffer input_tensor_0 with size 37632 and address: 0
  base_addrs[3] = (uint64_t)(uintptr_t) (sub_0001_arena+0);
  base_addrs_size[3] = 37632;

  // Buffer output_tensor_0 with size 128 and address: 0
  if (clean_outputs) {
    memset(sub_0001_arena + 0, 0, 128);
  }
  base_addrs[4] = (uint64_t)(uintptr_t) (sub_0001_arena+0);
  base_addrs_size[4] = 128;

  // Command stream data
  cms_data = (uint8_t*)sub_0001_command_stream;
  cms_size = (int) sub_0001_command_stream_size;

  // Invoke the Ethos-U driver
  if (num_base_addrs > 8) {
    num_base_addrs = 8;
  }
  if (ethosu_invoke_async(&g_ethosu0, cms_data, cms_size, base_addrs, base_addrs_size, num_base_addrs, NULL) < 0)
  {
    static uint32_t invoke_fail_cnt = 0;
    invoke_fail_cnt++;
    if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((invoke_fail_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
    {
      rt_kprintf("[EMB] ethosu_invoke_async failed (cnt=%lu)\n", (unsigned long) invoke_fail_cnt);
    }
    sub_0001_recover_timeout();
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
      if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((wait_fail_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
      {
        rt_kprintf("[EMB] ethosu_wait failed (cnt=%lu)\n", (unsigned long) wait_fail_cnt);
      }
      sub_0001_recover_timeout();
      return -1;
    }

    if ((rt_tick_get() - start_tick) > (RT_TICK_PER_SECOND * 4U))
    {
      static uint32_t wait_timeout_cnt = 0;
      wait_timeout_cnt++;
      if ((EMB_NPU_ERR_LOG_EVERY > 0U) && ((wait_timeout_cnt % EMB_NPU_ERR_LOG_EVERY) == 1U))
      {
        rt_kprintf("[EMB] ethosu_wait timeout (cnt=%lu)\n", (unsigned long) wait_timeout_cnt);
      }
      sub_0001_recover_timeout();
      return -1;
    }
    rt_thread_mdelay(1);
  }

  return 0;
}
#endif /* APP_USE_CLASSIFIER */

