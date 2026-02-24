#include "../model_select.h"
/* MODEL_SELECT_GUARD */
#if 1
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

#include "gesture_sub_0001_tensors.h"
#include "gesture_sub_0001_command_stream.h"
#include "gesture_sub_0001_invoke.h"

#include "hal_data.h"
#include "ethosu_driver.h"

__attribute__((aligned(16), section(".ospi1_cs0_noinit"))) uint8_t gesture_sub_0001_arena[442368];
__attribute__((aligned(16), section(".ospi1_cs0_noinit"))) static uint8_t gesture_sub_0001_model_data_ram[436688];
uint8_t *gesture_sub_0001_fast_scratch = gesture_sub_0001_arena;

#ifndef GESTURE_MODEL_FILE_PATH
#define GESTURE_MODEL_FILE_PATH "/gesture/gesture_u55.bin"
#endif

#ifndef GESTURE_MODEL_SIZE
#define GESTURE_MODEL_SIZE (436688U)
#endif

#ifndef GESTURE_NPU_ERR_LOG_EVERY
#define GESTURE_NPU_ERR_LOG_EVERY 200U
#endif

static bool g_gesture_model_ready = false;

static void gesture_force_idle(void)
{
    g_ethosu0.job.state  = ETHOSU_JOB_IDLE;
    g_ethosu0.job.result = ETHOSU_JOB_RESULT_ERROR;
}

static void gesture_recover_timeout(void)
{
    (void) ethosu_soft_reset(&g_ethosu0);
    (void) ethosu_wait(&g_ethosu0, false);
    gesture_force_idle();
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

static bool gesture_prepare_model_data(void)
{
    static uint32_t load_fail_cnt = 0;

    if (g_gesture_model_ready)
    {
        return true;
    }

    int fd = open(GESTURE_MODEL_FILE_PATH, O_RDONLY, 0);
    if (fd < 0)
    {
        load_fail_cnt++;
        if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
        {
            rt_kprintf("[GEST] model file open failed: %s\n", GESTURE_MODEL_FILE_PATH);
        }
        return false;
    }

    size_t total = 0U;
    while (total < GESTURE_MODEL_SIZE)
    {
        int rd = read(fd, gesture_sub_0001_model_data_ram + total, (int) (GESTURE_MODEL_SIZE - total));
        if (rd <= 0)
        {
            close(fd);
            load_fail_cnt++;
            if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
            {
                rt_kprintf("[GEST] model file read failed: %s (%lu/%lu)\n",
                           GESTURE_MODEL_FILE_PATH,
                           (unsigned long) total,
                           (unsigned long) GESTURE_MODEL_SIZE);
            }
            return false;
        }
        total += (size_t) rd;
    }
    close(fd);

    bool all_ff = true;
    bool all_00 = true;
    for (size_t i = 0; i < GESTURE_MODEL_SIZE; i++)
    {
        uint8_t b = gesture_sub_0001_model_data_ram[i];
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
        if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((load_fail_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
        {
            rt_kprintf("[GEST] model file invalid (blank): %s\n", GESTURE_MODEL_FILE_PATH);
        }
        return false;
    }

    dcache_clean_align(gesture_sub_0001_model_data_ram, GESTURE_MODEL_SIZE);
    g_gesture_model_ready = true;
    load_fail_cnt = 0;
    rt_kprintf("[GEST] model loaded from fs: %s (%lu bytes)\n",
               GESTURE_MODEL_FILE_PATH,
               (unsigned long) GESTURE_MODEL_SIZE);
    return true;
}

int gesture_sub_0001_invoke(bool clean_outputs)
{
    if (!gesture_prepare_model_data())
    {
        return -1;
    }

    uint64_t base_addrs[6] = {0};
    size_t base_addrs_size[6] = {0};
    int num_base_addrs = 6;

    uint8_t *cms_data = (uint8_t *) gesture_sub_0001_command_stream;
    int cms_size = (int) gesture_sub_0001_command_stream_size;

    base_addrs[0] = (uint64_t) (uintptr_t) gesture_sub_0001_model_data_ram;
    base_addrs_size[0] = GESTURE_MODEL_SIZE;

    base_addrs[1] = (uint64_t) (uintptr_t) (gesture_sub_0001_arena + 0);
    base_addrs_size[1] = 442368;

    base_addrs[2] = (uint64_t) (uintptr_t) (gesture_sub_0001_arena + 0);
    base_addrs_size[2] = 442368;

    base_addrs[3] = (uint64_t) (uintptr_t) (gesture_sub_0001_arena + 0);
    base_addrs_size[3] = 110592;

    if (clean_outputs)
    {
        memset(gesture_sub_0001_arena + 6480, 0, 3024);
        memset(gesture_sub_0001_arena + 0, 0, 756);
    }

    base_addrs[4] = (uint64_t) (uintptr_t) (gesture_sub_0001_arena + 6480);
    base_addrs_size[4] = 3024;

    base_addrs[5] = (uint64_t) (uintptr_t) (gesture_sub_0001_arena + 0);
    base_addrs_size[5] = 756;

    if (num_base_addrs > 8)
    {
        num_base_addrs = 8;
    }

    if (ethosu_invoke_async(&g_ethosu0, cms_data, cms_size, base_addrs, base_addrs_size, num_base_addrs, NULL) < 0)
    {
        static uint32_t invoke_fail_cnt = 0;
        invoke_fail_cnt++;
        if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((invoke_fail_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
        {
            rt_kprintf("[GEST] ethosu_invoke_async failed (cnt=%lu)\n", (unsigned long) invoke_fail_cnt);
        }
        gesture_recover_timeout();
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
            if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((wait_fail_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
            {
                rt_kprintf("[GEST] ethosu_wait failed (cnt=%lu)\n", (unsigned long) wait_fail_cnt);
            }
            gesture_recover_timeout();
            return -1;
        }
        if ((rt_tick_get() - start_tick) > (RT_TICK_PER_SECOND * 2U))
        {
            static uint32_t wait_timeout_cnt = 0;
            wait_timeout_cnt++;
            if ((GESTURE_NPU_ERR_LOG_EVERY > 0U) && ((wait_timeout_cnt % GESTURE_NPU_ERR_LOG_EVERY) == 1U))
            {
                rt_kprintf("[GEST] ethosu_wait timeout (cnt=%lu)\n", (unsigned long) wait_timeout_cnt);
            }
            gesture_recover_timeout();
            return -1;
        }
        rt_thread_mdelay(1);
    }

    return 0;
}
#endif /* 1 */
