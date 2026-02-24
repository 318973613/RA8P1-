/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* ======================================================
 * 主要功能：WiFi + 摄像头 + NPU 人脸/手势识别门禁系统
 * 平台：RT-Thread + RA8 MCU + Ethos-U55 NPU
 * ====================================================== */

/* ---- RT-Thread 及 BSP 头文件 ---- */
#include <rtthread.h>
#include <board.h>
#include "hal_data.h"
#include <rtdevice.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>


/* ---- 文件系统 / WiFi 驱动 ---- */
#include <fal.h>
#include <dfs_fs.h>
#include <wlan_mgnt.h>


/* ---- 摄像头 / LCD / 模型选择 ---- */
#include "sensor.h"
#include "st7789_port.h"
#include "model_select.h"

#if APP_USE_FACE_PIPELINE
#include "models_classifier/model.h"
#include "models_face/model.h"
#include "yolo_face/yolo_rtthread.h"
#include "face_detect.h"
#include "models_gesture/model.h"
#include "gesture_detect.h"
#else
#include "models/model.h"
#include "yolo/yolo_rtthread.h"
#endif

#include "pmu_ethosu.h"
#if APP_USE_FACE_PIPELINE
#include "ethosu_driver.h"
#endif

#define JO_JPEG_IMPLEMENTATION
#include "jo_jpeg.h"

#define LOG_TAG             "main"
#include <drv_log.h>

/* ---- WiFi 连接配置 ---- */
#define WIFI_SSID           "Xiaomi_C70E"           /* 路由器 SSID */
#define WIFI_PASSWORD       "13618027302"           /* 路由器密码 */


/* ---- IO 引脚定义 ---- */
#define LED_PIN_0           BSP_IO_PORT_00_PIN_12   /* 心跳指示 LED */
#define DOOR_LED_PIN        BSP_IO_PORT_06_PIN_13   /* 门锁绿色 LED（P613）*/
#define ALARM_BUZZER_PIN    BSP_IO_PORT_10_PIN_07   /* 报警蜂鸣器（PA07）*/
#define FS_PARTITION_NAME   "filesystem"            /* FAL 文件系统分区名 */


/* ---- 摄像头分辨率（QVGA）---- */
#define CAM_WIDTH   320
#define CAM_HEIGHT  240
#define DEBUG_BYPASS_CAMERA 0  /* 调试开关：1=跳过摄像头，直接用固定数据 */
#define DEBUG_BYPASS_NPU    0  /* 调试开关：1=跳过 NPU 推理 */

#ifndef ST7789_LCD_WIDTH
#define ST7789_LCD_WIDTH   240
#endif
#ifndef ST7789_LCD_HEIGHT
#define ST7789_LCD_HEIGHT  240
#endif
#ifndef ST7789_STATUS_BAR_H
#define ST7789_STATUS_BAR_H 20
#endif


/* ---- 人脸数据库与识别参数 ---- */
#define FACE_DB_MAX_USERS 3             /* 最多注册用户数 */
#define FACE_EMB_DIM 128                /* MobileFaceNet 嵌入向量维度 */
#define FACE_MATCH_THR2_MILLI 880       /* 余弦²相似度阈值（×1000），超过则视为匹配 */
#define FACE_MATCH_STABLE_FRAMES 5U     /* 连续匹配帧数达到此值才认为稳定匹配 */
#define FACE_MATCH_GRACE_MS 1200U       /* 匹配丢失后的宽限时间（ms），防止短暂遮挡 */
#define FACE_DET_MIN_SCORE_MILLI 850    /* 人脸检测最低置信度（×1000）*/
#define FACE_MULTI_MATCH_WINDOW_MS 10000U /* 多次匹配计数窗口（ms）*/
#define FACE_MULTI_MATCH_NEED 3U        /* 窗口内需匹配次数，达到后开启手势窗口 */
#define FACE_ENROLL_SAMPLES 4U          /* 注册时采集的帧数（用于平均特征）*/
#define FACE_SCORE_LOG_EVERY 30U        /* 每隔多少帧打印一次得分日志（0=关闭）*/
#define FACE_ENROLL_KEY_DEBOUNCE_MS 250U    /* 注册按键防抖时间（ms）*/
#define FACE_ENROLL_KEY_LONGPRESS_MS 3000U  /* 长按识别时间（ms），长按清空数据库 */
#define FACE_DB_FILE_PATH "/face_db.bin"    /* 人脸数据库文件路径 */
#define FACE_DB_MAGIC 0x46444231U       /* 数据库文件魔数 "FDB1" */
#define FACE_DB_VERSION 1U              /* 数据库文件版本号 */


/* ---- 手势识别参数 ---- */
#define GESTURE_OK_THR_MILLI 500        /* OK 手势置信度阈值（×1000）*/
#define GESTURE_OK_STABLE_FRAMES 1U     /* OK 手势稳定帧数 */
#define GESTURE_RUN_INTERVAL_FRAMES 1U  /* 每隔多少帧运行一次手势检测 */
#define GESTURE_MAX_BOXES 16            /* 手势检测最大输出框数 */
#define GESTURE_WINDOW_MS 30000U        /* 人脸确认后，手势窗口保持时间（ms）*/

/* ---- 门锁与报警参数 ---- */
#define DOOR_UNLOCK_HOLD_MS 3000U       /* 开门后 LED 保持亮起的时间（ms）*/
#define ALARM_BUZZER_ON_MS 120U         /* 报警蜂鸣器响的时长（ms）*/
#define ALARM_BUZZER_OFF_MS 880U        /* 报警蜂鸣器静默时长（ms）*/
#define ALARM_BUZZER_PWM_DEV "pwm1"
#define ALARM_BUZZER_PWM_CH 0
#define ALARM_BUZZER_PWM_FREQ_HZ 3800U  /* 蜂鸣器频率（Hz）*/
#define ALARM_BUZZER_PWM_DUTY_PERCENT 80U /* 蜂鸣器占空比（%）*/
/* ---- LCD 初始化重试参数 ---- */
#define LCD_INIT_RETRY_COUNT     5U     /* ST7789 最大初始化重试次数 */
#define LCD_INIT_RETRY_DELAY_MS  120U   /* 每次重试前等待时间（ms）*/
#define APP_LCD_ONLY_DEBUG       0
#define APP_GESTURE_ONLY_BOOT    0

#ifndef FACE_ENROLL_KEY_PIN
#define FACE_ENROLL_KEY_PIN BSP_IO_PORT_01_PIN_10
#endif

#ifndef FACE_ENROLL_KEY_ACTIVE
#define FACE_ENROLL_KEY_ACTIVE PIN_LOW
#endif


/* ---- 全局运行时状态变量 ---- */
static volatile bool led_status = false;               /* 心跳 LED 当前状态 */
static volatile rt_uint8_t g_face_embed_once_req = 0;  /* 单次特征提取请求标志 */
static volatile rt_uint8_t g_face_embed_auto = 1;      /* 自动特征提取开关（1=开启）*/
static volatile rt_uint8_t g_door_unlocked = 0;        /* 门锁已开（1=已开）*/
static volatile rt_tick_t g_door_unlock_until = 0;     /* 门锁自动关闭的超时 tick */
static rt_uint32_t g_buzzer_freq_hz = ALARM_BUZZER_PWM_FREQ_HZ;
static rt_bool_t g_buzzer_init_tried = RT_FALSE;
static rt_bool_t g_buzzer_hw_ready = RT_FALSE;
static volatile int g_web_led_mode = -1;               /* Web 控制 LED 模式：-1=自动, 0=关, 1=开 */
static volatile rt_uint8_t g_web_alarm_enable = 1;     /* Web 控制报警使能 */
static volatile rt_uint8_t g_web_buzzer_enable = 0;    /* Web 控制蜂鸣器使能 */
static volatile int32_t g_runtime_last_palm_m = -1;    /* 最近一次手掌置信度（×1000）*/
static volatile rt_uint8_t g_runtime_alarm_active = 0; /* 当前报警激活标志 */
#if APP_USE_FACE_PIPELINE
static volatile rt_uint8_t g_face_enroll_req = 0;
static volatile int g_face_stable_match_id = -1;
static volatile int32_t g_face_last_score2_m = -1;
static volatile rt_uint8_t g_gesture_ok_cnt = 0;
static volatile rt_uint8_t g_gesture_window_active = 0;
static volatile rt_tick_t g_gesture_window_until = 0;
static volatile int g_gesture_window_id = -1;
static volatile rt_uint8_t g_gesture_test_mode = 0;
static float g_face_db[FACE_DB_MAX_USERS][FACE_EMB_DIM];
static rt_uint8_t g_face_db_used[FACE_DB_MAX_USERS];
static rt_uint8_t g_face_db_next_slot = 0;
static float g_face_enroll_accum[FACE_EMB_DIM];
static rt_uint8_t g_face_enroll_count = 0;
static char g_face_toast_text[32];
static rt_tick_t g_face_toast_until = 0;
static int32_t g_face_match_thr2_milli = FACE_MATCH_THR2_MILLI;
static rt_uint8_t g_face_match_stable_frames = FACE_MATCH_STABLE_FRAMES;
static int32_t g_face_det_min_score_milli = FACE_DET_MIN_SCORE_MILLI;
#endif


/* 控制门锁 LED，低电平点亮（共阳接法）*/
static inline void door_led_write(rt_bool_t on)
{
    rt_pin_write(DOOR_LED_PIN, on ? PIN_LOW : PIN_HIGH);
}


/* 尝试初始化蜂鸣器的 GPT（PWM）外设，只执行一次 */
static void buzzer_pwm_try_init(void)
{
    if (g_buzzer_init_tried)
    {
        return;
    }
    g_buzzer_init_tried = RT_TRUE;

    fsp_err_t err = R_GPT_Open(&g_timer_ctrl, &g_timer_cfg);
    if ((err != FSP_SUCCESS) && (err != FSP_ERR_ALREADY_OPEN))
    {
        rt_kprintf("[BUZZ] gpt open failed: %d\n", (int) err);
        return;
    }

    (void) R_GPT_Stop(&g_timer_ctrl);
    (void) R_GPT_OutputEnable(&g_timer_ctrl, GPT_IO_PIN_GTIOCA);
    (void) R_GPT_OutputEnable(&g_timer_ctrl, GPT_IO_PIN_GTIOCB);
    g_buzzer_hw_ready = RT_TRUE;
}


/* 设置蜂鸣器音调：freq_hz=频率，on=开/关；优先用 PWM，失败则用 GPIO 模拟 */
static inline void buzzer_tone_set(uint32_t freq_hz, rt_bool_t on)
{
    buzzer_pwm_try_init();
    if (g_buzzer_hw_ready)
    {
        if (on && (freq_hz > 0U))
        {
            uint32_t pclk_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKD) >> g_timer_cfg.source_div;
            if (pclk_hz == 0U)
            {
                pclk_hz = 1U;
            }

            uint32_t period_counts = pclk_hz / freq_hz;
            if (period_counts < 2U)
            {
                period_counts = 2U;
            }

            uint32_t pulse_counts = (period_counts * ALARM_BUZZER_PWM_DUTY_PERCENT) / 100U;
            if (pulse_counts >= period_counts)
            {
                pulse_counts = period_counts - 1U;
            }

            (void) R_GPT_Stop(&g_timer_ctrl);
            (void) R_GPT_PeriodSet(&g_timer_ctrl, period_counts);
            (void) R_GPT_DutyCycleSet(&g_timer_ctrl, pulse_counts, GPT_IO_PIN_GTIOCA);
            (void) R_GPT_DutyCycleSet(&g_timer_ctrl, pulse_counts, GPT_IO_PIN_GTIOCB);
            (void) R_GPT_Start(&g_timer_ctrl);
        }
        else
        {
            (void) R_GPT_Stop(&g_timer_ctrl);
        }
        return;
    }

    rt_pin_write(ALARM_BUZZER_PIN, on ? PIN_HIGH : PIN_LOW);
}

/* 以当前全局频率控制蜂鸣器开/关 */
static inline void buzzer_write(rt_bool_t on)
{
    buzzer_tone_set(g_buzzer_freq_hz, on);
}


/* 纯 GPIO 模拟方波播放音调（无 PWM 外设时使用），hz=频率，ms=持续时间 */
static void buzzer_gpio_tone_play(uint32_t hz, uint32_t ms)
{
    if (hz < 100U) hz = 100U;
    if (hz > 10000U) hz = 10000U;
    if (ms < 1U) ms = 1U;

    uint32_t half_us = 500000U / hz;
    if (half_us < 1U) half_us = 1U;
    uint32_t total_us = ms * 1000U;

    (void) R_IOPORT_PinCfg(&g_ioport_ctrl,
                           ALARM_BUZZER_PIN,
                           (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);

    for (uint32_t elapsed = 0; elapsed < total_us; elapsed += (half_us * 2U))
    {
        rt_pin_write(ALARM_BUZZER_PIN, PIN_HIGH);
        rt_hw_us_delay(half_us);
        rt_pin_write(ALARM_BUZZER_PIN, PIN_LOW);
        rt_hw_us_delay(half_us);
    }

    rt_pin_write(ALARM_BUZZER_PIN, PIN_LOW);
}


/* 开门成功音效：两声上升音 */
static void buzzer_sfx_unlock(void)
{
    buzzer_gpio_tone_play(2400U, 120U);
    rt_thread_mdelay(40);
    buzzer_gpio_tone_play(3200U, 140U);
}

/* 陌生人/识别失败音效：两声下降警告音 */
static void buzzer_sfx_face_fail(void)
{
    buzzer_gpio_tone_play(1500U, 180U);
    rt_thread_mdelay(60);
    buzzer_gpio_tone_play(1100U, 220U);
}


/* 报警蜂鸣器周期性鸣叫驱动，每帧调用一次（非阻塞）；
 * enable=RT_FALSE 时立即停止，enable=RT_TRUE 时按 ON/OFF 时间交替鸣叫 */
static void alarm_buzzer_update(rt_bool_t enable)
{
    static rt_tick_t next_toggle_tick = 0;
    static rt_bool_t phase_on = RT_FALSE;

    if (!enable)
    {
        phase_on = RT_FALSE;
        next_toggle_tick = 0;
        buzzer_write(RT_FALSE);
        return;
    }

    rt_tick_t now = rt_tick_get();
    if (next_toggle_tick == 0)
    {
        phase_on = RT_TRUE;
        buzzer_write(RT_TRUE);
        next_toggle_tick = now + rt_tick_from_millisecond(ALARM_BUZZER_ON_MS);
        return;
    }

    if ((rt_int32_t) (now - next_toggle_tick) >= 0)
    {
        phase_on = !phase_on;
        buzzer_write(phase_on);
        next_toggle_tick = now + rt_tick_from_millisecond(phase_on ? ALARM_BUZZER_ON_MS : ALARM_BUZZER_OFF_MS);
    }
}

extern sensor_t sensor;
uint8_t g_image_rgb565_buffer[CAM_WIDTH * CAM_HEIGHT * 2] BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_noinit_nocache");

#if APP_USE_FACE_PIPELINE
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t users;
    uint32_t dim;
    uint8_t used[FACE_DB_MAX_USERS];
    float vec[FACE_DB_MAX_USERS][FACE_EMB_DIM];
} face_db_blob_t;

enum
{
    FACE_NPU_OWNER_NONE = 0U,
    FACE_NPU_OWNER_DETECT = 1U,
    FACE_NPU_OWNER_EMBED = 2U,
    FACE_NPU_OWNER_GESTURE = 3U,
};

static rt_uint8_t g_face_npu_owner = FACE_NPU_OWNER_NONE;

static void face_npu_force_idle(void)
{
    g_ethosu0.job.state = ETHOSU_JOB_IDLE;
    g_ethosu0.job.result = ETHOSU_JOB_RESULT_ERROR;
}

static void face_npu_switch(rt_uint8_t next_owner)
{
    if (g_face_npu_owner == next_owner)
    {
        return;
    }
    (void) ethosu_wait(&g_ethosu0, false);
    (void) ethosu_soft_reset(&g_ethosu0);
    (void) ethosu_wait(&g_ethosu0, false);
    face_npu_force_idle();
    g_face_npu_owner = next_owner;
}


/* 返回人脸数据库中已注册的用户数 */
static int face_db_count_used(void)
{
    int count = 0;
    for (int i = 0; i < FACE_DB_MAX_USERS; i++)
    {
        if (g_face_db_used[i]) count++;
    }
    return count;
}


/* 设置 LCD 状态栏短暂提示文字，持续 ms 毫秒后自动清除 */
static void face_set_toast(const char *text, rt_uint32_t ms)
{
    if (!text)
    {
        return;
    }
    strncpy(g_face_toast_text, text, sizeof(g_face_toast_text) - 1);
    g_face_toast_text[sizeof(g_face_toast_text) - 1] = '\0';
    g_face_toast_until = rt_tick_get() + (rt_tick_t)((ms * RT_TICK_PER_SECOND + 999U) / 1000U);
}


/* 重新计算人脸数据库中第一个空闲槽位，写入 g_face_db_next_slot */
static void face_db_recompute_next_slot(void)
{
    g_face_db_next_slot = 0;
    for (int i = 0; i < FACE_DB_MAX_USERS; i++)
    {
        if (!g_face_db_used[i])
        {
            g_face_db_next_slot = (rt_uint8_t) i;
            return;
        }
    }
}


/* 将内存中的人脸数据库序列化并写入文件系统（/face_db.bin）
 * 成功返回 0，失败返回负数错误码 */
static int face_db_save_fs(void)
{
    face_db_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = FACE_DB_MAGIC;
    blob.version = FACE_DB_VERSION;
    blob.users = FACE_DB_MAX_USERS;
    blob.dim = FACE_EMB_DIM;
    memcpy(blob.used, (const void *) g_face_db_used, sizeof(blob.used));
    memcpy(blob.vec, (const void *) g_face_db, sizeof(blob.vec));

    int fd = open(FACE_DB_FILE_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0);
    if (fd < 0)
    {
        rt_kprintf("[FACE] db save open failed: %d\n", rt_get_errno());
        return -1;
    }

    size_t total = 0;
    const uint8_t *p = (const uint8_t *) &blob;
    while (total < sizeof(blob))
    {
        int wr = write(fd, p + total, (int) (sizeof(blob) - total));
        if (wr <= 0)
        {
            close(fd);
            rt_kprintf("[FACE] db save write failed: %d\n", rt_get_errno());
            return -2;
        }
        total += (size_t) wr;
    }
    close(fd);
    rt_kprintf("[FACE] db saved (%d/%d)\n", face_db_count_used(), FACE_DB_MAX_USERS);
    return 0;
}


/* 从文件系统读取人脸数据库到内存；
 * 返回 0=成功，-1=文件不存在，-2=读取错误，-3=格式校验失败 */
static int face_db_load_fs(void)
{
    int fd = open(FACE_DB_FILE_PATH, O_RDONLY, 0);
    if (fd < 0)
    {
        return -1;
    }

    face_db_blob_t blob;
    size_t total = 0;
    uint8_t *p = (uint8_t *) &blob;
    while (total < sizeof(blob))
    {
        int rd = read(fd, p + total, (int) (sizeof(blob) - total));
        if (rd <= 0)
        {
            close(fd);
            return -2;
        }
        total += (size_t) rd;
    }
    close(fd);

    if ((blob.magic != FACE_DB_MAGIC) ||
        (blob.version != FACE_DB_VERSION) ||
        (blob.users != FACE_DB_MAX_USERS) ||
        (blob.dim != FACE_EMB_DIM))
    {
        return -3;
    }

    memcpy((void *) g_face_db_used, blob.used, sizeof(blob.used));
    memcpy((void *) g_face_db, blob.vec, sizeof(blob.vec));
    face_db_recompute_next_slot();
    rt_kprintf("[FACE] db loaded (%d/%d)\n", face_db_count_used(), FACE_DB_MAX_USERS);
    return 0;
}
#endif

int face_emb_once(void)
{
    g_face_embed_once_req = 1;
    rt_kprintf("[FACE] embedding one-shot requested\n");
    return 0;
}
MSH_CMD_EXPORT(face_emb_once, run face embedding once when a face is detected);

int face_emb_cancel(void)
{
    g_face_embed_once_req = 0;
    rt_kprintf("[FACE] embedding one-shot canceled\n");
    return 0;
}
MSH_CMD_EXPORT(face_emb_cancel, cancel pending face embedding one-shot);

int face_emb_auto_on(void)
{
    g_face_embed_auto = 1;
    rt_kprintf("[FACE] embedding auto mode ON\n");
    return 0;
}
MSH_CMD_EXPORT(face_emb_auto_on, enable automatic embedding when face is detected);

int face_emb_auto_off(void)
{
    g_face_embed_auto = 0;
    rt_kprintf("[FACE] embedding auto mode OFF\n");
    return 0;
}
MSH_CMD_EXPORT(face_emb_auto_off, disable automatic embedding mode);

int face_emb_status(void)
{
#if APP_USE_FACE_PIPELINE
    int db_count = face_db_count_used();
    rt_kprintf("[FACE] auto=%d one_shot_pending=%d enroll_pending=%d enroll=%d/%d db=%d/%d gtest=%d\n",
               (int) g_face_embed_auto,
               (int) g_face_embed_once_req,
               (int) g_face_enroll_req,
               (int) g_face_enroll_count,
               (int) FACE_ENROLL_SAMPLES,
               db_count,
               FACE_DB_MAX_USERS,
               (int) g_gesture_test_mode);
#else
    rt_kprintf("[FACE] auto=%d one_shot_pending=%d\n",
               (int) g_face_embed_auto,
               (int) g_face_embed_once_req);
#endif
    return 0;
}
MSH_CMD_EXPORT(face_emb_status, show embedding run mode status);

#if APP_USE_FACE_PIPELINE
int face_thr(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("[FACE] thr2=%ld stable=%d det=%ld\n",
                   (long) g_face_match_thr2_milli,
                   (int) g_face_match_stable_frames,
                   (long) g_face_det_min_score_milli);
        return 0;
    }

    int v = atoi(argv[1]);
    if (v < 600) v = 600;
    if (v > 980) v = 980;
    g_face_match_thr2_milli = v;
    rt_kprintf("[FACE] thr2=%ld\n", (long) g_face_match_thr2_milli);
    return 0;
}
MSH_CMD_EXPORT(face_thr, set or show face match threshold milli);

int face_det_thr(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("[FACE] det_thr=%ld\n", (long) g_face_det_min_score_milli);
        return 0;
    }

    int v = atoi(argv[1]);
    if (v < 500) v = 500;
    if (v > 980) v = 980;
    g_face_det_min_score_milli = v;
    rt_kprintf("[FACE] det_thr=%ld\n", (long) g_face_det_min_score_milli);
    return 0;
}
MSH_CMD_EXPORT(face_det_thr, set or show face detect min score milli);

int face_stable(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("[FACE] stable_frames=%d\n", (int) g_face_match_stable_frames);
        return 0;
    }

    int v = atoi(argv[1]);
    if (v < 1) v = 1;
    if (v > 10) v = 10;
    g_face_match_stable_frames = (rt_uint8_t) v;
    rt_kprintf("[FACE] stable_frames=%d\n", (int) g_face_match_stable_frames);
    return 0;
}
MSH_CMD_EXPORT(face_stable, set or show face stable frames);

#endif

#if APP_USE_FACE_PIPELINE
int gesture_test_on(void)
{
    g_gesture_test_mode = 1;
    g_gesture_window_active = 0;
    g_gesture_window_until = 0;
    g_gesture_window_id = -1;
    g_gesture_ok_cnt = 0;
    g_face_enroll_req = 0;
    g_face_embed_auto = 0;
    g_face_embed_once_req = 0;
    g_door_unlocked = 0;
    g_door_unlock_until = 0;
    face_set_toast("GEST TEST ON", 1000U);
    rt_kprintf("[GEST] test mode ON (bypass face)\n");
    return 0;
}
MSH_CMD_EXPORT(gesture_test_on, bypass face and run gesture-only test);

int gesture_test_off(void)
{
    g_gesture_test_mode = 0;
    g_gesture_ok_cnt = 0;
    g_gesture_window_active = 0;
    g_gesture_window_until = 0;
    g_gesture_window_id = -1;
    g_door_unlocked = 0;
    g_door_unlock_until = 0;
    face_set_toast("GEST TEST OFF", 1000U);
    rt_kprintf("[GEST] test mode OFF\n");
    return 0;
}
MSH_CMD_EXPORT(gesture_test_off, exit gesture-only test mode);

int gesture_test_status(void)
{
    rt_kprintf("[GEST] test=%d window=%d id=%d ok_cnt=%d\n",
               (int) g_gesture_test_mode,
               (int) g_gesture_window_active,
               (int) g_gesture_window_id,
               (int) g_gesture_ok_cnt);
    return 0;
}
MSH_CMD_EXPORT(gesture_test_status, show gesture-only test status);

int gesture_thr(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("[GEST] conf_thresh_milli=%d\n", (int) gesture_get_conf_thresh_milli());
        return 0;
    }

    int v = atoi(argv[1]);
    int applied = gesture_set_conf_thresh_milli((int32_t) v);
    rt_kprintf("[GEST] conf_thresh_milli=%d\n", applied);
    return 0;
}
MSH_CMD_EXPORT(gesture_thr, set or show gesture conf threshold milli);

int buzzer_on(void)
{
    buzzer_write(RT_TRUE);
    rt_kprintf("[BUZZ] on\n");
    return 0;
}
MSH_CMD_EXPORT(buzzer_on, force buzzer on);

int buzzer_off(void)
{
    buzzer_write(RT_FALSE);
    rt_kprintf("[BUZZ] off\n");
    return 0;
}
MSH_CMD_EXPORT(buzzer_off, force buzzer off);

int buzzer_beep(int argc, char **argv)
{
    int ms = 200;
    if (argc > 1)
    {
        ms = atoi(argv[1]);
        if (ms < 20) ms = 20;
        if (ms > 2000) ms = 2000;
    }

    buzzer_gpio_tone_play((uint32_t) g_buzzer_freq_hz, (uint32_t) ms);
    rt_kprintf("[BUZZ] beep %d ms\n", ms);
    return 0;
}
MSH_CMD_EXPORT(buzzer_beep, beep once by ms);

typedef struct
{
    uint16_t freq_hz;
    uint16_t duration_ms;
} buzzer_note_t;

int buzzer_song(void)
{
    static const buzzer_note_t song[] =
    {
        {262,400}, {294,400}, {330,400}, {262,400},
        {262,400}, {294,400}, {330,400}, {262,400},
        {330,400}, {349,400}, {392,800},
        {330,400}, {349,400}, {392,800},
        {392,200}, {440,200}, {392,200}, {349,200}, {330,400}, {262,400},
        {392,200}, {440,200}, {392,200}, {349,200}, {330,400}, {262,400},
        {262,400}, {196,400}, {262,400}, {0,400},
        {262,400}, {196,400}, {262,400}, {0,400},
    };

    for (size_t i = 0; i < sizeof(song) / sizeof(song[0]); i++)
    {
        if (song[i].freq_hz == 0U)
        {
            rt_thread_mdelay(song[i].duration_ms);
        }
        else
        {
            buzzer_gpio_tone_play((uint32_t) song[i].freq_hz, (uint32_t) song[i].duration_ms);
        }
    }

    buzzer_tone_set(0U, RT_FALSE);
    rt_kprintf("[BUZZ] song done\n");
    return 0;
}
MSH_CMD_EXPORT(buzzer_song, play test song on passive buzzer);

int buzzer_freq(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("[BUZZ] freq=%u Hz\n", (unsigned) g_buzzer_freq_hz);
        return 0;
    }

    int hz = atoi(argv[1]);
    if (hz < 500) hz = 500;
    if (hz > 6000) hz = 6000;
    g_buzzer_freq_hz = (rt_uint32_t) hz;
    rt_kprintf("[BUZZ] freq=%u Hz\n", (unsigned) g_buzzer_freq_hz);
    return 0;
}
MSH_CMD_EXPORT(buzzer_freq, set or show buzzer freq in Hz);

int buzzer_gpio_tone(int argc, char **argv)
{
    int hz = 2000;
    int ms = 500;
    if (argc > 1)
    {
        hz = atoi(argv[1]);
    }
    if (argc > 2)
    {
        ms = atoi(argv[2]);
    }
    if (hz < 100) hz = 100;
    if (hz > 10000) hz = 10000;
    if (ms < 20) ms = 20;
    if (ms > 5000) ms = 5000;

    uint32_t half_us = (uint32_t) (500000 / hz);
    if (half_us < 1U) half_us = 1U;
    uint32_t total_us = (uint32_t) ms * 1000U;

    (void) R_IOPORT_PinCfg(&g_ioport_ctrl,
                           ALARM_BUZZER_PIN,
                           (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);

    for (uint32_t elapsed = 0; elapsed < total_us; elapsed += (half_us * 2U))
    {
        rt_pin_write(ALARM_BUZZER_PIN, PIN_HIGH);
        rt_hw_us_delay(half_us);
        rt_pin_write(ALARM_BUZZER_PIN, PIN_LOW);
        rt_hw_us_delay(half_us);
    }

    rt_pin_write(ALARM_BUZZER_PIN, PIN_LOW);
    rt_kprintf("[BUZZ] gpio_tone %d Hz %d ms\n", hz, ms);
    return 0;
}
MSH_CMD_EXPORT(buzzer_gpio_tone, force GPIO square wave test: buzzer_gpio_tone 2000 500);



int buzzer_raw(int argc, char **argv)
{
    if (argc <= 1)
    {
        rt_kprintf("usage: buzzer_raw <0|1>\n");
        return 0;
    }

    int lv = atoi(argv[1]) ? PIN_HIGH : PIN_LOW;
    rt_pin_write(ALARM_BUZZER_PIN, lv);
    rt_kprintf("[BUZZ] raw=%d\n", lv == PIN_HIGH ? 1 : 0);
    return 0;
}
MSH_CMD_EXPORT(buzzer_raw, direct pin level test 0 or 1);

int face_enroll_once(void)
{
    memset((void *) g_face_enroll_accum, 0, sizeof(g_face_enroll_accum));
    g_face_enroll_count = 0;
    g_face_enroll_req = 1;
    face_set_toast("ENROLL REQ", 1200U);
    rt_kprintf("[FACE] enroll requested (%d samples)\n", (int) FACE_ENROLL_SAMPLES);
    return 0;
}
MSH_CMD_EXPORT(face_enroll_once, enroll next detected face into DB);

int face_db_clear(void)
{
    memset((void *) g_face_db_used, 0, sizeof(g_face_db_used));
    memset((void *) g_face_db, 0, sizeof(g_face_db));
    memset((void *) g_face_enroll_accum, 0, sizeof(g_face_enroll_accum));
    g_face_enroll_count = 0;
    g_face_db_next_slot = 0;
    g_face_enroll_req = 0;
    g_face_stable_match_id = -1;
    g_face_last_score2_m = -1;
    g_gesture_ok_cnt = 0;
    g_gesture_window_active = 0;
    g_gesture_window_until = 0;
    g_gesture_window_id = -1;
    g_door_unlocked = 0;
    g_door_unlock_until = 0;
    face_set_toast("DB CLEARED", 1500U);
    (void) face_db_save_fs();
    rt_kprintf("[FACE] db cleared\n");
    return 0;
}
MSH_CMD_EXPORT(face_db_clear, clear enrolled face DB);

int face_db_list(void)
{
    rt_kprintf("[FACE] db slots=%d next=%d\n", FACE_DB_MAX_USERS, (int) g_face_db_next_slot);
    for (int i = 0; i < FACE_DB_MAX_USERS; i++)
    {
        if (g_face_db_used[i])
        {
            rt_kprintf("  id=%d used vec4_m=%d,%d,%d,%d\n",
                       i,
                       (int) (g_face_db[i][0] * 1000.0f),
                       (int) (g_face_db[i][1] * 1000.0f),
                       (int) (g_face_db[i][2] * 1000.0f),
                       (int) (g_face_db[i][3] * 1000.0f));
        }
        else
        {
            rt_kprintf("  id=%d empty\n", i);
        }
    }
    return 0;
}
MSH_CMD_EXPORT(face_db_list, list enrolled face DB slots);

int face_db_save(void)
{
    return face_db_save_fs();
}
MSH_CMD_EXPORT(face_db_save, save face DB to filesystem);

int face_db_load(void)
{
    int rc = face_db_load_fs();
    if (rc == -1)
    {
        rt_kprintf("[FACE] db file not found\n");
    }
    else if (rc < 0)
    {
        rt_kprintf("[FACE] db load failed: %d\n", rc);
    }
    return rc;
}
MSH_CMD_EXPORT(face_db_load, load face DB from filesystem);

int face_key_status(void)
{
    int lv = rt_pin_read(FACE_ENROLL_KEY_PIN);
    rt_kprintf("[FACE] key pin=%d level=%d active=%d\n",
               (int) FACE_ENROLL_KEY_PIN,
               lv,
               (int) FACE_ENROLL_KEY_ACTIVE);
    return 0;
}
MSH_CMD_EXPORT(face_key_status, show enroll key pin level);


/* 轮询注册按键，短按=触发人脸注册，长按(3秒)=清空数据库；
 * 内置防抖与长按去重，需每帧调用一次 */
static void face_key_poll_and_trigger_enroll(void)
{
    static rt_uint8_t prev_pressed = 0;
    static rt_uint8_t longpress_handled = 0;
    static rt_tick_t  last_evt_tick = 0;
    static rt_tick_t  press_start_tick = 0;
    const rt_tick_t debounce_ticks = (rt_tick_t) ((FACE_ENROLL_KEY_DEBOUNCE_MS * RT_TICK_PER_SECOND + 999U) / 1000U);
    const rt_tick_t longpress_ticks = (rt_tick_t) ((FACE_ENROLL_KEY_LONGPRESS_MS * RT_TICK_PER_SECOND + 999U) / 1000U);

    rt_uint8_t pressed = (rt_pin_read(FACE_ENROLL_KEY_PIN) == FACE_ENROLL_KEY_ACTIVE) ? 1U : 0U;
    if (pressed && !prev_pressed)
    {
        press_start_tick = rt_tick_get();
        longpress_handled = 0U;
    }
    else if (pressed && prev_pressed)
    {
        rt_tick_t now = rt_tick_get();
        if (!longpress_handled && ((now - press_start_tick) >= longpress_ticks) && ((now - last_evt_tick) >= debounce_ticks))
        {
            (void) face_db_clear();
            rt_kprintf("[FACE] key long press: db cleared\n");
            longpress_handled = 1U;
            last_evt_tick = now;
        }
    }
    else if (!pressed && prev_pressed)
    {
        rt_tick_t now = rt_tick_get();
        if (!longpress_handled && ((now - press_start_tick) < longpress_ticks) && ((now - last_evt_tick) >= debounce_ticks))
        {
            memset((void *) g_face_enroll_accum, 0, sizeof(g_face_enroll_accum));
            g_face_enroll_count = 0;
            g_face_enroll_req = 1;
            face_set_toast("ENROLL REQ", 1200U);
            rt_kprintf("[FACE] key enroll requested\n");
            last_evt_tick = now;
        }
    }
    prev_pressed = pressed;
}


/* 累积 emb 到注册缓冲区，达到 FACE_ENROLL_SAMPLES 帧后取均值写入数据库；
 * 返回注册成功的用户 ID，否则返回 -1（样本不足或参数无效）*/
static int face_db_enroll_current(const float *emb)
{
    if (!emb)
    {
        return -1;
    }

    for (int i = 0; i < FACE_EMB_DIM; i++)
    {
        g_face_enroll_accum[i] += emb[i];
    }
    if (g_face_enroll_count < 255U)
    {
        g_face_enroll_count++;
    }

    rt_kprintf("[FACE] enroll collect %d/%d\n",
               (int) g_face_enroll_count,
               (int) FACE_ENROLL_SAMPLES);

    if (g_face_enroll_count < FACE_ENROLL_SAMPLES)
    {
        return -1;
    }

    int slot = (int) g_face_db_next_slot;
    float inv = 1.0f / (float) g_face_enroll_count;
    for (int i = 0; i < FACE_EMB_DIM; i++)
    {
        g_face_db[slot][i] = g_face_enroll_accum[i] * inv;
    }
    memset((void *) g_face_enroll_accum, 0, sizeof(g_face_enroll_accum));
    g_face_enroll_count = 0;
    g_face_db_used[slot] = 1U;
    g_face_db_next_slot = (rt_uint8_t) ((slot + 1) % FACE_DB_MAX_USERS);
    g_face_enroll_req = 0;
    (void) face_db_save_fs();
    rt_kprintf("[FACE] enrolled id=%d vec8_m=%d,%d,%d,%d,%d,%d,%d,%d\n",
               slot,
               (int) (g_face_db[slot][0] * 1000.0f), (int) (g_face_db[slot][1] * 1000.0f),
               (int) (g_face_db[slot][2] * 1000.0f), (int) (g_face_db[slot][3] * 1000.0f),
               (int) (g_face_db[slot][4] * 1000.0f), (int) (g_face_db[slot][5] * 1000.0f),
               (int) (g_face_db[slot][6] * 1000.0f), (int) (g_face_db[slot][7] * 1000.0f));
    return slot;
}


/* 在数据库中查找与 emb 最匹配的用户；
 * 返回用户 ID（0~N-1），未找到返回 -1；best_score2_milli 输出余弦²得分（×1000）*/
static int face_db_best_match(const float *emb, int32_t *best_score2_milli)
{
    int best_id = -1;
    int32_t best_m = -1;
    for (int id = 0; id < FACE_DB_MAX_USERS; id++)
    {
        if (!g_face_db_used[id]) continue;

        float dot = 0.0f, n1 = 0.0f, n2 = 0.0f;
        for (int i = 0; i < FACE_EMB_DIM; i++)
        {
            float a = emb[i];
            float b = g_face_db[id][i];
            dot += a * b;
            n1 += a * a;
            n2 += b * b;
        }
        if ((dot <= 0.0f) || (n1 <= 1e-12f) || (n2 <= 1e-12f))
        {
            continue;
        }

        float score2 = (dot * dot) / (n1 * n2); /* cosine^2 in [0,1] */
        int32_t score2_m = (int32_t) (score2 * 1000.0f + 0.5f);
        if (score2_m > best_m)
        {
            best_m = score2_m;
            best_id = id;
        }
    }

    if (best_score2_milli)
    {
        *best_score2_milli = best_m;
    }
    return best_id;
}
#endif

static uint8_t s_lcd_line_buf[ST7789_LCD_WIDTH * 2];
static char s_lcd_last_text[32];
static rt_bool_t s_lcd_text_inited = RT_FALSE;


/* 安全的 D-Cache 无效化（按32字节对齐），buf=起始地址，len_bytes=长度 */
static inline void dcache_invalidate_safe(void *buf, rt_ubase_t len_bytes)
{
#if (BSP_CFG_DCACHE_ENABLED)
    if ((buf == RT_NULL) || (len_bytes == 0U))
    {
        return;
    }

    rt_ubase_t start = (rt_ubase_t) buf;
    rt_ubase_t end   = start + len_bytes;

    start &= ~(rt_ubase_t) 31U;
    end    = (end + 31U) & ~(rt_ubase_t) 31U;

    SCB_InvalidateDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) buf;
    (void) len_bytes;
#endif
}


/* 安全的 D-Cache 清洗（写回内存），buf=起始地址，len_bytes=长度 */
static inline void dcache_clean_safe(void *buf, rt_ubase_t len_bytes)
{
#if (BSP_CFG_DCACHE_ENABLED)
    if ((buf == RT_NULL) || (len_bytes == 0U))
    {
        return;
    }

    rt_ubase_t start = (rt_ubase_t) buf;
    rt_ubase_t end   = start + len_bytes;

    start &= ~(rt_ubase_t) 31U;
    end    = (end + 31U) & ~(rt_ubase_t) 31U;

    SCB_CleanDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
#else
    (void) buf;
    (void) len_bytes;
#endif
}


/* LCD 开机测试：绘制彩条 + "OK" 文字，用于验证屏幕正常 */
static void st7789_show_boot_test(void)
{
    static const uint16_t bar_colors[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
        0xF81F, 0xF800, 0x001F, 0x0000
    };

    int w = ST7789_LCD_WIDTH;
    int h = ST7789_LCD_HEIGHT;

    st7789_set_window(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            uint16_t color = bar_colors[(x * 8) / w];
            s_lcd_line_buf[x * 2] = (uint8_t)(color >> 8);
            s_lcd_line_buf[x * 2 + 1] = (uint8_t)(color & 0xFF);
        }
        st7789_write_pixels(s_lcd_line_buf, (uint32_t)(w * 2));
    }

    {
        const char *msg = "OK";
        int scale = 5;
        int char_w = 5 * scale;
        int char_h = 7 * scale;
        int spacing = scale;
        int text_w = 2 * char_w + spacing;
        int x = (w - text_w) / 2;
        int y = (h - char_h) / 2;
        st7789_draw_text(x, y, msg, 0x0000, 0xFFFF, scale);
    }
}


/* 用指定 RGB565 颜色填充 LCD 矩形区域（裁剪到屏幕边界）*/
static void st7789_fill_rect_solid(int x, int y, int w, int h, uint16_t color)
{
    if ((w <= 0) || (h <= 0))
    {
        return;
    }
    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (ST7789_LCD_WIDTH - 1)) x1 = ST7789_LCD_WIDTH - 1;
    if (y1 > (ST7789_LCD_HEIGHT - 1)) y1 = ST7789_LCD_HEIGHT - 1;
    int rw = x1 - x0 + 1;
    int rh = y1 - y0 + 1;
    if ((rw <= 0) || (rh <= 0))
    {
        return;
    }

    for (int i = 0; i < rw; i++)
    {
        s_lcd_line_buf[i * 2] = (uint8_t)(color >> 8);
        s_lcd_line_buf[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }

    st7789_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
    for (int row = 0; row < rh; row++)
    {
        st7789_write_pixels(s_lcd_line_buf, (uint32_t)(rw * 2));
    }
}


/* 更新 LCD 顶部状态栏文字，内容不变时跳过刷新 */
static void st7789_status_text_update(const char *text)
{
    const char *msg = text ? text : "";
    if (s_lcd_text_inited && (strncmp(msg, s_lcd_last_text, sizeof(s_lcd_last_text)) == 0))
    {
        return;
    }

    st7789_fill_rect_solid(0, 0, ST7789_LCD_WIDTH, ST7789_STATUS_BAR_H, 0x0000);
    st7789_draw_text(2, 2, msg, 0xFFFF, 0x0000, 2);

    strncpy(s_lcd_last_text, msg, sizeof(s_lcd_last_text) - 1);
    s_lcd_last_text[sizeof(s_lcd_last_text) - 1] = '\0';
    s_lcd_text_inited = RT_TRUE;
}


/* 将 RGB565 帧缓冲居中显示到 LCD，跳过顶部 skip_top 行（状态栏区域）*/
static void st7789_blit_rgb565_center_skip_top(const uint16_t *src, int src_w, int src_h, int skip_top)
{
    int dst_w = ST7789_LCD_WIDTH;
    int dst_h = ST7789_LCD_HEIGHT;
    int copy_w = (src_w < dst_w) ? src_w : dst_w;
    int copy_h = (src_h < dst_h) ? src_h : dst_h;
    int x_off = (src_w - copy_w) / 2;
    int y_off = (src_h - copy_h) / 2;

    if (skip_top < 0)
    {
        skip_top = 0;
    }
    if (skip_top >= copy_h)
    {
        return;
    }

    st7789_set_window(0, (uint16_t)skip_top, (uint16_t)(copy_w - 1), (uint16_t)(copy_h - 1));

    for (int y = skip_top; y < copy_h; y++)
    {
        const uint16_t *src_line = src + (y + y_off) * src_w + x_off;
        for (int x = 0; x < copy_w; x++)
        {
            uint16_t p = src_line[x];
            s_lcd_line_buf[x * 2]     = (uint8_t)(p >> 8);
            s_lcd_line_buf[x * 2 + 1] = (uint8_t)(p & 0xFF);
        }
        st7789_write_pixels(s_lcd_line_buf, (uint32_t)(copy_w * 2));
    }
}


/* 在 RGB565 帧缓冲上绘制矩形边框（检测框叠加），thickness=线宽 */
static void draw_rect_rgb565(uint16_t *buf, int w, int h, const det_box_t *box, uint16_t color, int thickness)
{
    int x1 = box->x1;
    int y1 = box->y1;
    int x2 = box->x2;
    int y2 = box->y2;

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    x1 = CLAMP(x1, 0, w - 1);
    y1 = CLAMP(y1, 0, h - 1);
    x2 = CLAMP(x2, 0, w - 1);
    y2 = CLAMP(y2, 0, h - 1);

    if ((x2 <= x1) || (y2 <= y1))
    {
        return;
    }

    if (thickness < 1)
    {
        thickness = 1;
    }

    for (int t = 0; t < thickness; t++)
    {
        int left = x1 + t;
        int right = x2 - t;
        int top = y1 + t;
        int bottom = y2 - t;

        if ((left > right) || (top > bottom))
        {
            break;
        }

        uint16_t *row_top = buf + top * w;
        uint16_t *row_bottom = buf + bottom * w;
        for (int x = left; x <= right; x++)
        {
            row_top[x] = color;
            row_bottom[x] = color;
        }

        for (int y = top; y <= bottom; y++)
        {
            buf[y * w + left] = color;
            buf[y * w + right] = color;
        }
    }
}


/* 将 RGB565 帧缩放到 192×192，转换为 float HWC 格式并归一化到 [0,1]，
 * 用于手势检测 YOLOv5 模型输入 */
static void rgb565_to_rgb888_resize_192_float_hwc(const uint16_t *src, int16_t src_w, int16_t src_h, float *dst)
{
    const int16_t dst_w = 192;
    const int16_t dst_h = 192;
    const int32_t plane_size = dst_w * dst_h;  // 192 * 192 = 36864
    const float inv255 = 1.0f / 255.0f;

    for (int16_t y = 0; y < dst_h; y++)
    {
        int16_t sy = (y * src_h) / dst_h;
        const uint16_t *row = src + sy * src_w;

        for (int16_t x = 0; x < dst_w; x++)
        {
            int16_t sx = (x * src_w) / dst_w;
            uint16_t pix = row[sx];

            uint8_t r = (pix >> 11) & 0x1F;
            uint8_t g = (pix >> 5) & 0x3F;
            uint8_t b = (pix) & 0x1F;

            r = (uint8_t)((r << 3) | (r >> 2));
            g = (uint8_t)((g << 2) | (g >> 4));
            b = (uint8_t)((b << 3) | (b >> 2));

            // Model expects NCHW format [1, 3, 192, 192]
            // RUHMI quant config: mean=0, std=255  ==> input should be in [0,1]
            int32_t pixel_idx = y * dst_w + x;
            dst[0 * plane_size + pixel_idx] = (float)r * inv255;  // R channel [0-1]
            dst[1 * plane_size + pixel_idx] = (float)g * inv255;  // G channel [0-1]
            dst[2 * plane_size + pixel_idx] = (float)b * inv255;  // B channel [0-1]
        }
    }
}

static void yolo_output_stats(const float *out_f, int16_t grid, float *out_max_raw, float *out_max_sig)
{
    const int16_t stride = (5 + CLASS_NUM);
    const int16_t cells = grid * grid;
    float max_raw = -1e9f;
    float max_final = 0.0f;

    // 输出张量格式 [1, 3, grid, grid, 7] = [batch, anchors, y, x, features]
    for (int16_t k = 0; k < ANCHORS; ++k)
    {
        for (int16_t i = 0; i < cells; ++i)
        {
            int32_t base = (int32_t)k * cells * stride + (int32_t)i * stride;
            float obj_raw = out_f[base + 4];
            if (obj_raw > max_raw) max_raw = obj_raw;
            
            // 最终置信度 = sigmoid(obj) * max(sigmoid(class_probs))
            float obj_sig = sigmoidf_fast(obj_raw);
            float max_cls = 0.0f;
            for (int c = 0; c < CLASS_NUM; c++)
            {
                float cls_sig = sigmoidf_fast(out_f[base + 5 + c]);
                if (cls_sig > max_cls) max_cls = cls_sig;
            }
            float final_conf = obj_sig * max_cls;
            if (final_conf > max_final) max_final = final_conf;
        }
    }
    if (out_max_raw) *out_max_raw = max_raw;
    if (out_max_sig) *out_max_sig = max_final;
}

static void input_stats_sample(const float *in_f, float *out_min, float *out_max, float *out_sum)
{
    float min_v = 1e9f;
    float max_v = -1e9f;
    float sum_v = 0.0f;
    const int sample_step = 911; // prime-ish step for sampling
    const int total = 192 * 192 * 3;
    int idx = 0;
    for (int i = 0; i < 256; i++)
    {
        float v = in_f[idx];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum_v += v;
        idx += sample_step;
        if (idx >= total) idx -= total;
    }
    if (out_min) *out_min = min_v;
    if (out_max) *out_max = max_v;
    if (out_sum) *out_sum = sum_v;
}

// Temporary visual compensation for box offset on LCD preview.
// If you later fix the underlying coordinate mapping, set these back to 0.
#define BOX_SHIFT_X 20
#define BOX_SHIFT_Y 20

#if APP_USE_FACE_PIPELINE

/* 将 RGB565 帧缩放到 112×112，转换为 float NCHW 格式并做 MobileFaceNet 归一化
 * (x - 127.5) / 128，用于人脸特征提取模型输入 */
static void rgb565_to_rgb888_resize_112_float_nchw_norm(const uint16_t *src, int16_t src_w, int16_t src_h, float *dst)
{
    const int16_t dst_w = 112;
    const int16_t dst_h = 112;
    const int32_t plane_size = dst_w * dst_h; // 112*112=12544
    const float mean = 127.5f;
    const float inv_std = 1.0f / 128.0f;

    for (int16_t y = 0; y < dst_h; y++)
    {
        int16_t sy = (y * src_h) / dst_h;
        const uint16_t *row = src + sy * src_w;

        for (int16_t x = 0; x < dst_w; x++)
        {
            int16_t sx = (x * src_w) / dst_w;
            uint16_t pix = row[sx];

            uint8_t r = (pix >> 11) & 0x1F;
            uint8_t g = (pix >> 5) & 0x3F;
            uint8_t b = (pix) & 0x1F;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            // MobileFaceNet: input0 [1,3,112,112] (NCHW), normalize (x - 127.5) / 128
            int32_t pixel_idx = y * dst_w + x;
            dst[0 * plane_size + pixel_idx] = ((float)r - mean) * inv_std;
            dst[1 * plane_size + pixel_idx] = ((float)g - mean) * inv_std;
            dst[2 * plane_size + pixel_idx] = ((float)b - mean) * inv_std;
        }
    }
}


/* 从 RGB565 帧中裁剪人脸检测框区域，缩放到 112×112 做 MobileFaceNet 推理;
 * 裁剪框无效时退化为全图缩放 */
static void rgb565_crop_resize_112_float_nchw_norm(const uint16_t *src, int16_t src_w, int16_t src_h,
                                                   int x1, int y1, int x2, int y2, float *dst)
{
    if (!src || !dst)
    {
        return;
    }

    x1 = CLAMP(x1, 0, src_w - 1);
    y1 = CLAMP(y1, 0, src_h - 1);
    x2 = CLAMP(x2, 0, src_w - 1);
    y2 = CLAMP(y2, 0, src_h - 1);
    if (x2 <= x1 || y2 <= y1)
    {
        rgb565_to_rgb888_resize_112_float_nchw_norm(src, src_w, src_h, dst);
        return;
    }

    int crop_w = x2 - x1 + 1;
    int crop_h = y2 - y1 + 1;

    for (int dy = 0; dy < 112; dy++)
    {
        int sy = y1 + (dy * (crop_h - 1)) / 111;
        const uint16_t *row = src + sy * src_w;
        for (int dx = 0; dx < 112; dx++)
        {
            int sx = x1 + (dx * (crop_w - 1)) / 111;
            uint16_t p = row[sx];
            uint8_t r = (uint8_t)((p >> 11) & 0x1F);
            uint8_t g = (uint8_t)((p >> 5) & 0x3F);
            uint8_t b = (uint8_t)(p & 0x1F);
            r = (uint8_t)((r << 3) | (r >> 2));
            g = (uint8_t)((g << 2) | (g >> 4));
            b = (uint8_t)((b << 3) | (b >> 2));

            float rf = ((float)r - 127.5f) * (1.0f / 128.0f);
            float gf = ((float)g - 127.5f) * (1.0f / 128.0f);
            float bf = ((float)b - 127.5f) * (1.0f / 128.0f);

            int idx = dy * 112 + dx;
            dst[idx] = rf;
            dst[112 * 112 + idx] = gf;
            dst[2 * 112 * 112 + idx] = bf;
        }
    }
}

#endif

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>


/* ---- 网络通信配置 ---- */
#define HTTP_PORT 80                        /* 板载 Web 服务器监听端口 */
#define STREAM_SERVER_IP   "192.168.31.133" /* PC 端流服务器 IP */
#define STREAM_SERVER_PORT 9000             /* PC 端视频流接收端口 */
#define PC_CTRL_WEB_PORT   8080             /* PC 端控制 Web 服务端口 */
#define ENABLE_STREAM_CLIENT 1              /* 是否启用视频流推送客户端 */
#define ENABLE_FACE_EMBEDDING 1             /* 是否启用人脸特征提取 */
/* Run embedding only when face exists, then wait N frames before next embedding. */
#define FACE_EMBED_COOLDOWN_FRAMES 15U  /* 嵌入推理的冷却帧数（降低负载）*/
#define FACE_EMBED_FAIL_COOLDOWN_FRAMES 120U /* 推理失败后等待更长时间再重试 */
#define FACE_PIPE_LOG_EVERY 0U          /* 人脸流水线日志频率（0=关闭）*/
#define FACE_EMB_VECTOR_LOG_EVERY 0U   /* 特征向量日志频率（0=关闭）*/

#define STREAM_FRAME_BYTES (CAM_WIDTH * CAM_HEIGHT * 2)
#define STREAM_FMT_RGB565  0
#define STREAM_FMT_JPEG    1

/* BMP Header template for RGB565 */
static const uint8_t bmp_header_template[] = {
    0x42, 0x4D,             /* 'BM' signature */
    0x00, 0x00, 0x00, 0x00, /* File size: 14 + 40 + 12 + image */
    0x00, 0x00, 0x00, 0x00, /* Reserved */
    0x42, 0x00, 0x00, 0x00, /* Offset to pixel data: 14 + 40 + 12 = 66 (0x42) */
    
    /* Info Header (BITMAPINFOHEADER) */
    0x28, 0x00, 0x00, 0x00, /* Header size: 40 bytes */
    0x00, 0x00, 0x00, 0x00, /* Width */
    0x00, 0x00, 0x00, 0x00, /* Height (negative for top-down) */
    0x01, 0x00,             /* Planes: 1 */
    0x10, 0x00,             /* Bits per pixel: 16 */
    0x03, 0x00, 0x00, 0x00, /* Compression: BI_BITFIELDS (3) */
    0x00, 0x00, 0x00, 0x00, /* Image size */
    0x00, 0x00, 0x00, 0x00, /* X pixels per meter */
    0x00, 0x00, 0x00, 0x00, /* Y pixels per meter */
    0x00, 0x00, 0x00, 0x00, /* Colors used */
    0x00, 0x00, 0x00, 0x00, /* Colors important */

    /* Color Masks (RGB565) */
    0x00, 0xF8, 0x00, 0x00, /* Red mask   (0xF800) */
    0xE0, 0x07, 0x00, 0x00, /* Green mask (0x07E0) */
    0x1F, 0x00, 0x00, 0x00  /* Blue mask  (0x001F) */
};

/* HTML Page */
static const char *index_html = 
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n\r\n"
"<html><head><title>Titan Board Cam</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif;text-align:center;background:#222;color:#fff;}"
"img{border:2px solid #555;max-width:100%;height:auto;}</style>"
"</head><body>"
"<h2>Titan Board Live Feed</h2>"
"<img id='cam' src='/capture.bmp' />"
"<p>Running RT-Thread + YOLO</p>"
"<script>"
"setInterval(function(){"
"  var img = document.getElementById('cam');"
"  img.src = '/capture.bmp?t=' + new Date().getTime();"
"}, 200);" // Refresh every 200ms
"</script></body></html>";


/* 可靠发送：循环 send 直到所有数据发出，处理 EAGAIN/EWOULDBLOCK；
 * 返回 0=成功，-1=连接断开或超时 */
static int send_all(int fd, const uint8_t *data, size_t len)
{
    size_t sent = 0;
    int retries = 0;

    while (sent < len)
    {
        size_t remaining = len - sent;
        size_t chunk = (remaining > 1024U) ? 1024U : remaining;
        int ret = send(fd, data + sent, (int)chunk, 0);
        if (ret > 0)
        {
            sent += (size_t)ret;
            retries = 0;
            continue;
        }
        if (ret == 0)
        {
            LOG_E("Image send closed by peer");
            return -1;
        }

        int err = rt_get_errno();
        if ((err == EWOULDBLOCK) || (err == EAGAIN) || (err == ENOBUFS))
        {
            if (++retries > 600)
            {
                LOG_E("Image send retry timeout (errno: %d, sent=%u/%u)", err, (unsigned)sent, (unsigned)len);
                return -1;
            }
            rt_thread_mdelay(5);
            continue;
        }

        LOG_E("Image send failed (errno: %d)", err);
        return -1;
    }

    return 0;
}


/* 向 PC 控制服务器发送小型 HTTP GET 请求，resp 接收响应缓冲（可为 NULL）*/
static int pc_http_get_small(const char *path, char *resp, int resp_sz)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return -1;
    }

    int timeout_ms = 250;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PC_CTRL_WEB_PORT);
    server_addr.sin_addr.s_addr = inet_addr(STREAM_SERVER_IP);
    memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) < 0)
    {
        closesocket(sock);
        return -1;
    }

    char req[320];
    int req_len = rt_snprintf(req, sizeof(req),
                              "GET %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Connection: close\r\n\r\n",
                              path, STREAM_SERVER_IP);
    if ((req_len <= 0) || (send(sock, req, req_len, 0) <= 0))
    {
        closesocket(sock);
        return -1;
    }

    if (resp && (resp_sz > 0))
    {
        int total = 0;
        while (total < (resp_sz - 1))
        {
            int n = recv(sock, resp + total, resp_sz - 1 - total, 0);
            if (n <= 0)
            {
                break;
            }
            total += n;
        }
        resp[total] = '\0';
    }

    closesocket(sock);
    return 0;
}


/* 与 PC 控制服务器定期同步：拉取 LED/报警控制指令，推送运行时状态；
 * 内部限速约 700ms 一次，主循环每帧调用 */
static void pc_sync_with_server(void)
{
    static rt_tick_t next_sync_tick = 0;
    rt_tick_t now = rt_tick_get();
    if ((next_sync_tick != 0) && ((rt_int32_t)(now - next_sync_tick) < 0))
    {
        return;
    }
    next_sync_tick = now + rt_tick_from_millisecond(700);

    char resp[256];
    resp[0] = '\0';
    if (pc_http_get_small("/api/control_pull", resp, sizeof(resp)) == 0)
    {
        char *body = strstr(resp, "\r\n\r\n");
        if (body) body += 4;
        else body = resp;

        if (strstr(body, "led=on")) g_web_led_mode = 1;
        else if (strstr(body, "led=off")) g_web_led_mode = 0;
        else if (strstr(body, "led=auto")) g_web_led_mode = -1;

        if (strstr(body, "alarm=1")) g_web_alarm_enable = 1;
        else if (strstr(body, "alarm=0")) g_web_alarm_enable = 0;

        if (strstr(body, "buzzer=1")) g_web_buzzer_enable = 1;
        else if (strstr(body, "buzzer=0")) g_web_buzzer_enable = 0;
    }

    rt_tick_t now2 = rt_tick_get();
    int32_t left_tick = (int32_t)(g_door_unlock_until - now2);
    int32_t left_ms = 0;
    if (g_door_unlocked && (left_tick > 0))
    {
        left_ms = (int32_t)((left_tick * 1000) / RT_TICK_PER_SECOND);
    }
    const char *face_state = (g_face_stable_match_id >= 0) ? "MATCH" : "SEARCH";
    const char *gest_state = g_gesture_test_mode ? "TEST" : (g_gesture_window_active ? "WINDOW" : "IDLE");
    const char *alarm_state = g_runtime_alarm_active ? "ON" : "OFF";

    char path[420];
    rt_snprintf(path, sizeof(path),
                "/api/runtime_push?door_open=%d&face_state=%s&face_id=%d&face_score_m=%ld&gesture_state=%s&gesture_palm_m=%ld&unlock_left_ms=%ld&alarm_state=%s&led_mode=%s&alarm_enable=%d&buzzer_enable=%d",
                (int)g_door_unlocked, face_state, (int)g_face_stable_match_id,
                (long)g_face_last_score2_m, gest_state, (long)g_runtime_last_palm_m,
                (long)left_ms, alarm_state,
                (g_web_led_mode > 0) ? "on" : ((g_web_led_mode == 0) ? "off" : "auto"),
                (int)g_web_alarm_enable, (int)g_web_buzzer_enable);

    (void)pc_http_get_small(path, RT_NULL, 0);
}


static void store_u16_le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void store_u32_le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void build_frame_header(uint8_t *hdr, uint16_t w, uint16_t h, uint16_t fmt, uint32_t len)
{
    hdr[0] = 'F';
    hdr[1] = 'R';
    hdr[2] = 'A';
    hdr[3] = 'M';
    store_u16_le(&hdr[4], w);
    store_u16_le(&hdr[6], h);
    store_u16_le(&hdr[8], fmt);
    store_u16_le(&hdr[10], 0);
    store_u32_le(&hdr[12], len);
}

static void build_bmp_header(uint8_t *hdr, uint16_t w, uint16_t h)
{
    uint32_t image_size = (uint32_t)w * (uint32_t)h * 2U;
    uint32_t file_size = (uint32_t)sizeof(bmp_header_template) + image_size;
    int32_t top_down_h = -(int32_t)h;

    memcpy(hdr, bmp_header_template, sizeof(bmp_header_template));
    store_u32_le(&hdr[2], file_size);
    store_u32_le(&hdr[18], w);
    store_u32_le(&hdr[22], (uint32_t)top_down_h);
    store_u32_le(&hdr[34], image_size);
}


/* ---- 视频流与 JPEG 缓冲区 ---- */
static uint8_t g_stream_buf[STREAM_FRAME_BYTES] BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ospi1_cs0_noinit");
/* Use regular RAM for JPEG output - OSPI RAM has cache coherency issues for byte-by-byte access */
static uint8_t *g_jpg_data = NULL;
#define JPG_BUF_SIZE (100 * 1024)
static volatile bool g_stream_busy = false;
static rt_sem_t g_stream_sem = RT_NULL;


/* 视频流推送线程：连接 PC 端 stream_server，每帧发送 16 字节头 + RGB565 原始数据 */
static void stream_client_entry(void *param)
{
    int sock = -1;
    struct sockaddr_in server_addr;
    static uint32_t sent_frames = 0;
    
    /* JPEG buffer allocation removed as we are sending raw RGB565 */


    while (1)
    {
        if (sock < 0)
        {
            while (!rt_wlan_is_connected())
            {
                rt_thread_mdelay(200);
            }

            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
            {
                LOG_E("Stream socket create failed");
                rt_thread_mdelay(1000);
                continue;
            }

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(STREAM_SERVER_PORT);
            server_addr.sin_addr.s_addr = inet_addr(STREAM_SERVER_IP);
            memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

            if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) < 0)
            {
                LOG_E("Stream connect failed (errno: %d)", rt_get_errno());
                closesocket(sock);
                sock = -1;
                rt_thread_mdelay(1000);
                continue;
            }

            {
                int timeout_ms = 5000;  // 发送超时 5 秒
                setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
                
                // 增大发送缓冲区以提高大帧吸吐量
                int sndbuf_size = 256 * 1024;  // 256KB
                setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size));
                
                int flag = 1;
                // Disable Nagle for lower latency
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
            }

            LOG_I("Stream connected to %s:%d", STREAM_SERVER_IP, STREAM_SERVER_PORT);
        }

        if (rt_sem_take(g_stream_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        if (sock < 0)
        {
            g_stream_busy = false;
            continue;
        }

        if (!g_stream_busy)
        {
            continue;
        }

        /* Send RGB565 raw data directly */
        {
            uint16_t w = CAM_WIDTH;
            uint16_t h = CAM_HEIGHT;
            uint32_t frame_size = w * h * 2;
            
            uint8_t header[16];
            build_frame_header(header, w, h, STREAM_FMT_RGB565, frame_size);
            
            if (send_all(sock, header, sizeof(header)) != 0 ||
                send_all(sock, g_stream_buf, frame_size) != 0)
            {
                LOG_E("Stream send failed (errno: %d)", rt_get_errno());
                closesocket(sock);
                sock = -1;
            }
            else
            {
                sent_frames++;
                if ((sent_frames % 30U) == 0U)
                {
                    LOG_I("Stream sent %lu frames (RGB565 %d bytes)", 
                          (unsigned long)sent_frames, frame_size);
                }
            }
        }

        g_stream_busy = false;

        /* Drop backlog if sender is slower than capture */
        while (rt_sem_trytake(g_stream_sem) == RT_EOK)
        {
            /* discard */
        }
    }
}

static void serve_client(int conn_fd)
{
    char *buf = rt_malloc(1024);
    if (!buf) {
        closesocket(conn_fd);
        return;
    }

    /* Read request (basic) */
    int bytes_read = recv(conn_fd, buf, 1023, 0);
    if (bytes_read > 0)
    {
        buf[bytes_read] = 0;
        
        if (strstr(buf, "GET /capture.bmp"))
        {
            /* Send BMP Header */
            uint16_t w = CAM_WIDTH;
            uint16_t h = CAM_HEIGHT;
            size_t image_len = (size_t)w * (size_t)h * 2U;
            size_t bmp_len = sizeof(bmp_header_template) + image_len;
            uint8_t bmp_header[sizeof(bmp_header_template)];
            build_bmp_header(bmp_header, w, h);
            int hdr_len = rt_snprintf(buf, 1024,
                                      "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: image/bmp\r\n"
                                      "Content-Length: %u\r\n"
                                      "Connection: close\r\n\r\n",
                                      (unsigned)bmp_len);
            if (hdr_len > 0)
            {
                if (send_all(conn_fd, (const uint8_t *)buf, (size_t)hdr_len) != 0)
                {
                    rt_free(buf);
                    closesocket(conn_fd);
                    return;
                }
            }
            if (send_all(conn_fd, bmp_header, sizeof(bmp_header)) != 0)
            {
                rt_free(buf);
                closesocket(conn_fd);
                return;
            }

            /* Send Raw Data in chunks */
#if (BSP_CFG_DCACHE_ENABLED)
            dcache_clean_safe(g_image_rgb565_buffer, (rt_ubase_t) image_len);
#endif
            (void)send_all(conn_fd, g_image_rgb565_buffer, image_len);
        }
        else if (strstr(buf, "GET /api/runtime"))
        {
            rt_tick_t now_tick = rt_tick_get();
            int32_t left_tick = (int32_t)(g_door_unlock_until - now_tick);
            int32_t left_ms = 0;
            if (g_door_unlocked && (left_tick > 0)) left_ms = (int32_t)((left_tick * 1000) / RT_TICK_PER_SECOND);
            const char *face_state = (g_face_stable_match_id >= 0) ? "MATCH" : "SEARCH";
            const char *gest_state = g_gesture_test_mode ? "TEST" : (g_gesture_window_active ? "WINDOW" : "IDLE");
            const char *alarm_state = g_runtime_alarm_active ? "ON" : "OFF";
            int body_len = rt_snprintf(buf, 1024,
                                       "{\"ok\":true,\"door_open\":%d,\"face_state\":\"%s\",\"face_id\":%d,\"face_score_m\":%ld,\"gesture_state\":\"%s\",\"gesture_palm_m\":%ld,\"unlock_left_ms\":%ld,\"alarm_state\":\"%s\",\"led_mode\":\"%s\",\"alarm_enable\":%d,\"buzzer_enable\":%d}",
                                       (int)g_door_unlocked, face_state, (int)g_face_stable_match_id,
                                       (long)g_face_last_score2_m, gest_state, (long)g_runtime_last_palm_m,
                                       (long)left_ms, alarm_state,
                                       (g_web_led_mode > 0) ? "on" : ((g_web_led_mode == 0) ? "off" : "auto"),
                                       (int)g_web_alarm_enable, (int)g_web_buzzer_enable);
            char hdr[224];
            int hdr_len = rt_snprintf(hdr, sizeof(hdr),
                                      "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: application/json\r\n"
                                      "Content-Length: %d\r\n"
                                      "Connection: close\r\n\r\n", body_len);
            if (hdr_len > 0) (void)send(conn_fd, hdr, hdr_len, 0);
            if (body_len > 0) (void)send(conn_fd, buf, body_len, 0);
        }
        else if (strstr(buf, "GET /api/control"))
        {
            if (strstr(buf, "led=on")) g_web_led_mode = 1;
            else if (strstr(buf, "led=off")) g_web_led_mode = 0;
            else if (strstr(buf, "led=auto")) g_web_led_mode = -1;
            if (strstr(buf, "alarm=on")) g_web_alarm_enable = 1;
            else if (strstr(buf, "alarm=off")) g_web_alarm_enable = 0;
            if (strstr(buf, "buzzer=on")) g_web_buzzer_enable = 1;
            else if (strstr(buf, "buzzer=off")) g_web_buzzer_enable = 0;
            int body_len = rt_snprintf(buf, 1024,
                                       "{\"ok\":true,\"led\":\"%s\",\"alarm\":%d,\"buzzer\":%d}",
                                       (g_web_led_mode > 0) ? "on" : ((g_web_led_mode == 0) ? "off" : "auto"),
                                       (int)g_web_alarm_enable, (int)g_web_buzzer_enable);
            char hdr[192];
            int hdr_len = rt_snprintf(hdr, sizeof(hdr),
                                      "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: application/json\r\n"
                                      "Content-Length: %d\r\n"
                                      "Connection: close\r\n\r\n", body_len);
            if (hdr_len > 0) (void)send(conn_fd, hdr, hdr_len, 0);
            if (body_len > 0) (void)send(conn_fd, buf, body_len, 0);
        }
        else
        {
            /* Send HTML Page */
            send(conn_fd, index_html, strlen(index_html), 0);
        }
    }

    rt_free(buf);
    closesocket(conn_fd);
}


/* 板载 Web 服务器线程（HTTP 端口 80）：
 * - GET /             返回 HTML 实时预览页面
 * - GET /capture.bmp  返回当前帧 BMP 图像
 * - GET /api/runtime  返回 JSON 格式运行时状态
 * - GET /api/control  接受 led/alarm/buzzer 控制参数 */
void web_server_entry(void *param)
{
    int listen_fd = -1;
    int conn_fd = -1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int opt = 1;

    /* Wait for WiFi connection */
    rt_thread_mdelay(5000); 

    while (1)
    {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0)
        {
            LOG_E("Socket create failed");
            rt_thread_mdelay(1000);
            continue;
        }

        /* Enable SO_REUSEADDR to allow restarting the server quickly */
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(HTTP_PORT);
        server_addr.sin_addr.s_addr = INADDR_ANY;
        memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

        if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
        {
            LOG_E("Bind failed");
            closesocket(listen_fd);
            rt_thread_mdelay(1000);
            continue;
        }

        if (listen(listen_fd, 5) == -1)
        {
            LOG_E("Listen failed");
            closesocket(listen_fd);
            rt_thread_mdelay(1000);
            continue;
        }

        LOG_I("Web Server running on port %d", HTTP_PORT);

        while (1)
        {
            client_addr_len = sizeof(struct sockaddr);
            conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
            
            if (conn_fd < 0)
            {
                /* Accept failed, maybe temporary. Print error and retry, but don't kill server */
                LOG_E("Accept failed (errno: %d)", rt_get_errno());
                rt_thread_mdelay(100);
                continue;
            }

            serve_client(conn_fd);
        }

        /* Should not reach here typically */
        closesocket(listen_fd);
    }
}


/* WiFi 自动连接线程：系统稳定后连接配置的 AP */
static void wifi_auto_connect_entry(void *parameter)
{
    /* Wait a few seconds for the system/filesystem to fully stabilize */
    rt_thread_mdelay(2000);

    LOG_I("Auto-connecting to WiFi: %s...", WIFI_SSID);
    
    /* Attempt to connect */
    if (rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD) == RT_EOK)
    {
        LOG_I("WiFi connected successfully!");
    }
    else
    {
        LOG_E("WiFi connection failed!");
    }
}

#define OSPI_OM_RESET               BSP_IO_PORT_12_PIN_07
#define HYPER_RAM_RESET_DELAY()     R_BSP_SoftwareDelay(10UL, BSP_DELAY_UNITS_MICROSECONDS)
#define HYPER_RAM_CFG_REG_0_ADDRESS (0x01000000)

/* Missing definition required by FSP (ra_gen/hal_data.c) */
ospi_b_xspi_command_set_t g_hyper_ram_commands[] =
{
    {
        .protocol = SPI_FLASH_PROTOCOL_8D_8D_8D,
        .frame_format = OSPI_B_FRAME_FORMAT_XSPI_PROFILE_2_EXTENDED,
        .latency_mode = OSPI_B_LATENCY_MODE_FIXED,
        .command_bytes = OSPI_B_COMMAND_BYTES_1,
        .address_bytes = SPI_FLASH_ADDRESS_BYTES_4,

        .read_command = 0xA0,
        .read_dummy_cycles = 11,
        .program_command = 0x20,
        .program_dummy_cycles = 11,

        .address_msb_mask = 0xF0,
        .status_needs_address = false,

        .p_erase_commands = NULL,
    }
};

static uint16_t swap16(uint16_t value)
{
    uint16_t ret;
    ret  = value << 8;
    ret |= value >> 8;
    return ret;
}

static fsp_err_t hyper_ram_config_get(uint32_t address, uint16_t * const p_value_out)
{
    spi_flash_direct_transfer_t xfer = {
            .address = address,
            .address_length = 4,
            .command_length = 2,
            .command = 0xE000,
            .data_length = 2,
            .dummy_cycles = 11,
    };

    fsp_err_t err = R_OSPI_B_DirectTransfer(&g_ospi1_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (err != FSP_SUCCESS)
    {
        LOG_E("HyperRAM config get failed!");
        return err;
    }

    *p_value_out = (uint16_t) xfer.data;
    return FSP_SUCCESS;
}

static fsp_err_t hyper_ram_config_set(uint32_t address, uint16_t value)
{
    spi_flash_direct_transfer_t xfer = {
            .address = address,
            .address_length = 4,
            .command = 0x6000,
            .command_length = 2,
            .data = (uint16_t) value,
            .data_length = 2,
            .dummy_cycles = 0,
    };

    fsp_err_t err = R_OSPI_B_DirectTransfer(&g_ospi1_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (err != FSP_SUCCESS)
    {
        LOG_E("HyperRAM config set failed!");
        return err;
    }
    return FSP_SUCCESS;
}


/* 手动初始化 HyperRAM（OSPI1）：复位引脚 -> 切换 8D-8D-8D 模式 -> 读写 CR0 寄存器 */
static void manual_hyper_ram_init(void)
{
    LOG_I("Manually Initializing HyperRAM...");

    /* Change OM_RESET back to normal IO mode. */
    R_IOPORT_PinCfg(&g_ioport_ctrl,
                    OSPI_OM_RESET,
                    IOPORT_CFG_PORT_DIRECTION_OUTPUT
                        | IOPORT_CFG_DRIVE_HIGH
                        | IOPORT_CFG_PORT_DIRECTION_OUTPUT
                        | IOPORT_CFG_PORT_OUTPUT_HIGH);

    /* Pin reset the OctaFlash */
    R_BSP_PinWrite(OSPI_OM_RESET, BSP_IO_LEVEL_LOW);
    HYPER_RAM_RESET_DELAY();
    R_BSP_PinWrite(OSPI_OM_RESET, BSP_IO_LEVEL_HIGH);
    HYPER_RAM_RESET_DELAY();

    /* Open the interface and immediately transition to 8D-8D-8D mode */
    R_OSPI_B_Open((spi_flash_ctrl_t *)&g_ospi1_ctrl, &g_ospi1_cfg);
    R_OSPI_B_SpiProtocolSet(&g_ospi1_ctrl, SPI_FLASH_PROTOCOL_8D_8D_8D);

    uint16_t cfg_reg0 = 0;
    hyper_ram_config_get(HYPER_RAM_CFG_REG_0_ADDRESS, &cfg_reg0);
    LOG_D("Read CR0 value: 0x%x", swap16(cfg_reg0));

    // Default 0x8F1D: Variable Latency, 3 cycles, etc.
    // Ensure we set it to what works (Fixed latency often preferred for stability if configured)
    uint16_t value0 = 0x8f1d; 
    hyper_ram_config_set(HYPER_RAM_CFG_REG_0_ADDRESS, swap16(value0));
    LOG_D("Set CR0 to 0x%x", value0);

    cfg_reg0 = 0;
    hyper_ram_config_get(HYPER_RAM_CFG_REG_0_ADDRESS, &cfg_reg0);
    LOG_D("Read CR0 value: 0x%x", swap16(cfg_reg0));
}


/* ======================================================
 * hal_entry - 系统主入口函数（RT-Thread 启动后调用）
 * 初始化顺序：
 *   1. HyperRAM 手动初始化
 *   2. GPIO 引脚配置（LED / 蜂鸣器 / 注册按键）
 *   3. FAL 分区表 + LittleFS 文件系统挂载
 *   4. 人脸数据库加载（APP_USE_FACE_PIPELINE 时）
 *   5. LCD（ST7789）初始化并显示开机测试画面
 *   6. 摄像头（CEU）初始化与参数配置
 *   7. NPU（Ethos-U55）初始化
 *   8. 辅助线程启动：WiFi 自动连接 / 视频流 / Web 服务器
 *   9. 主循环：逐帧采集 → NPU 推理 → LCD 显示 → 门锁/报警决策
 * ====================================================== */
void hal_entry(void)
{
    rt_kprintf("\nHello RT-Thread!\n");
    rt_kprintf("===========================================================\n");
    rt_kprintf("This project runs WiFi + camera + NPU face detection.\n");
    rt_kprintf("===========================================================\n");

    /* Ensure HyperRAM is initialized manually */
    manual_hyper_ram_init();

    /* Force user-requested door/alarm pins to GPIO output mode. */
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl,
                           DOOR_LED_PIN,
                           (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);
    rt_pin_mode(LED_PIN_0, PIN_MODE_OUTPUT);
    rt_pin_mode(DOOR_LED_PIN, PIN_MODE_OUTPUT);
    door_led_write(RT_FALSE);
    buzzer_write(RT_FALSE);
#if APP_USE_FACE_PIPELINE
    rt_pin_mode(FACE_ENROLL_KEY_PIN, PIN_MODE_INPUT_PULLUP);
    LOG_I("FACE enroll key pin=%d active=%d", (int) FACE_ENROLL_KEY_PIN, (int) FACE_ENROLL_KEY_ACTIVE);
#endif

    /* 1) FAL + filesystem for WiFi firmware resources */
    rt_bool_t fs_ready = RT_FALSE;
    struct rt_device *mtd_dev = RT_NULL;
    fal_init();

    mtd_dev = fal_mtd_nor_device_create(FS_PARTITION_NAME);
    if (!mtd_dev)
    {
        LOG_E("Can't create a mtd device on '%s' partition.", FS_PARTITION_NAME);
    }
    else
    {
        if (dfs_mount(FS_PARTITION_NAME, "/", "lfs", 0, 0) == 0)
        {
            LOG_I("Filesystem initialized!");
            fs_ready = RT_TRUE;
        }
        else
        {
            dfs_mkfs("lfs", FS_PARTITION_NAME);
            if (dfs_mount(FS_PARTITION_NAME, "/", "lfs", 0, 0) == 0)
            {
                LOG_I("Filesystem initialized!");
                fs_ready = RT_TRUE;
            }
            else
            {
                LOG_E("Failed to initialize filesystem!");
            }
        }
    }

#if APP_USE_FACE_PIPELINE && !APP_GESTURE_ONLY_BOOT
    if (fs_ready)
    {
        int db_rc = face_db_load_fs();
        if (db_rc == -1)
        {
            LOG_I("FACE DB not found, start with empty DB");
        }
        else if (db_rc < 0)
        {
            LOG_W("FACE DB load failed: %d", db_rc);
        }
    }
#endif

    /* 2) LCD init (retry to improve boot reliability) */
    rt_bool_t lcd_ok = RT_FALSE;
    for (uint32_t lcd_try = 0; lcd_try < LCD_INIT_RETRY_COUNT; lcd_try++)
    {
        if (st7789_init() == 0)
        {
            lcd_ok = RT_TRUE;
            break;
        }
        LOG_W("ST7789 init failed, retry %u/%u", (unsigned)(lcd_try + 1U), (unsigned)LCD_INIT_RETRY_COUNT);
        rt_thread_mdelay(LCD_INIT_RETRY_DELAY_MS);
    }
    if (!lcd_ok)
    {
        LOG_E("ST7789 init failed after retries");
        return;
    }
    st7789_show_boot_test();
    rt_thread_mdelay(1000);
    s_lcd_text_inited = RT_FALSE;
    st7789_fill_rect_solid(0, 0, ST7789_LCD_WIDTH, ST7789_STATUS_BAR_H, 0x0000);

#if APP_LCD_ONLY_DEBUG
    LOG_W("LCD-only debug mode: camera/NPU/WiFi disabled");
    while (1)
    {
        static uint8_t phase = 0;
        static const uint16_t colors[] = {0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF};
        st7789_fill_rect_solid(0, ST7789_STATUS_BAR_H, ST7789_LCD_WIDTH, ST7789_LCD_HEIGHT - ST7789_STATUS_BAR_H, colors[phase]);
        st7789_status_text_update("LCD ONLY DEBUG");
        phase = (uint8_t)((phase + 1U) % (sizeof(colors) / sizeof(colors[0])));
        rt_thread_mdelay(800);
    }
#endif

    /* 3) Camera init */
    if (sensor_init() != 0)
    {
        LOG_E("sensor_init failed");
        return;
    }
    sensor_reset();
    if (sensor_set_pixformat(PIXFORMAT_RGB565) != 0)
    {
        LOG_E("sensor_set_pixformat failed");
        return;
    }
    if (sensor_set_framesize(FRAMESIZE_QVGA) != 0)
    {
        LOG_E("sensor_set_framesize failed");
        return;
    }

    /* 浼樺寲鎽勫儚澶村弬鏁颁互鎻愰珮鐢昏川 */
    sensor_set_auto_gain(1, 0, 16);        // 自动增益，最大增益约 16dB
    sensor_set_auto_exposure(1, 1000);      // 自动曝光
    sensor_set_saturation(1);               // 适当提高饱和度
    sensor_set_contrast(1);                 // 适当提高对比度
    sensor_set_brightness(0);               // 亮度保持默认
    /* 4) NPU init */
    if (RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg) != FSP_SUCCESS)
    {
        LOG_E("Failed to start NPU");
        return;
    }
#if !APP_GESTURE_ONLY_BOOT
    /* Auto-connect WiFi after LCD/camera/NPU are stable */
    rt_thread_t tid = rt_thread_create("wifi_acc", wifi_auto_connect_entry, RT_NULL, 4096, 25, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
#endif

#if ENABLE_STREAM_CLIENT
    g_stream_sem = rt_sem_create("cam_tx", 0, RT_IPC_FLAG_FIFO);
    if (!g_stream_sem)
    {
        LOG_E("Stream semaphore create failed");
    }
    else
    {
        rt_thread_t stream_tid = rt_thread_create("cam_tx", stream_client_entry, RT_NULL, 4096, 27, 10);
        if (stream_tid)
        {
            rt_thread_startup(stream_tid);
        }
        else
        {
            LOG_E("Stream thread create failed");
        }
    }
#endif

    rt_thread_t web_tid = rt_thread_create("web80", web_server_entry, RT_NULL, 4096, 26, 10);
    if (web_tid)
    {
        rt_thread_startup(web_tid);
    }
    else
    {
        LOG_E("Web server thread create failed");
    }

#if APP_USE_FACE_PIPELINE
    float *in_f = GetModelInputPtr_input0();
    if (!in_f)
    {
        LOG_E("model input pointer is null!");
        return;
    }
    LOG_I("MODEL: face embedding (MobileFaceNet) in=input0[1,3,112,112] out=128D");
#else
    float *in_f = GetModelInputPtr_images();
    if (!in_f)
    {
        LOG_E("model input pointer is null!");
        return;
    }
    LOG_I("MODEL: gesture detector (float)");
    LOG_I("MODEL: in=%p out1=%p out2=%p",
          in_f,
          GetModelOutputPtr_p5_6x6_70437(),
          GetModelOutputPtr_p4_12x12_70454());
#endif

    LOG_I("MODEL: NPU mode (Ethos-U55)");
    if (ENABLE_FACE_EMBEDDING)
    {
        g_face_embed_auto = 1;
    }
#if APP_USE_FACE_PIPELINE && APP_GESTURE_ONLY_BOOT
    g_gesture_test_mode = 1;
    g_gesture_window_active = 0;
    g_gesture_window_until = 0;
    g_gesture_window_id = -1;
    g_face_embed_auto = 0;
    g_face_embed_once_req = 0;
    g_face_enroll_req = 0;
    rt_kprintf("[GEST] boot gesture-only mode ON\n");
#endif
    while (1)
    {
        static uint32_t queued_frames = 0;
        static uint32_t frame_cnt = 0;
        char conf_text[32];
        rt_bool_t stranger_alarm = RT_FALSE;
        static rt_bool_t s_prev_stranger_alarm = RT_FALSE;
        conf_text[0] = '\0';
        if (g_door_unlocked && ((rt_int32_t) (g_door_unlock_until - rt_tick_get()) <= 0))
        {
            g_door_unlocked = 0;
        }
        rt_bool_t led_on = g_door_unlocked ? RT_TRUE : RT_FALSE;
        if (g_web_led_mode == 1) led_on = RT_TRUE;
        else if (g_web_led_mode == 0) led_on = RT_FALSE;
        door_led_write(led_on);
        static rt_tick_t s_web_buzz_next = 0;
        if (g_web_buzzer_enable)
        {
            rt_tick_t now_bz = rt_tick_get();
            if ((s_web_buzz_next == 0) || ((rt_int32_t)(now_bz - s_web_buzz_next) >= 0))
            {
                buzzer_gpio_tone_play((uint32_t) g_buzzer_freq_hz, 40U);
                s_web_buzz_next = rt_tick_get() + rt_tick_from_millisecond(80);
            }
        }
        else
        {
            s_web_buzz_next = 0;
        }

        /* Heartbeat: if LED never blinks, the loop isn't running or crashes early. */
        led_status = !led_status;
        rt_pin_write(LED_PIN_0, led_status ? PIN_HIGH : PIN_LOW);

#if DEBUG_BYPASS_CAMERA
        memset(g_image_rgb565_buffer, 0x80, CAM_WIDTH * CAM_HEIGHT * 2U);
#else
        sensor_snapshot(&sensor, g_image_rgb565_buffer, 0);
#endif
        dcache_invalidate_safe(g_image_rgb565_buffer, (rt_ubase_t)(CAM_WIDTH * CAM_HEIGHT * 2U));

#if APP_USE_FACE_PIPELINE
        if (!g_gesture_test_mode)
        {
            face_key_poll_and_trigger_enroll();
            if (g_face_enroll_req)
            {
                rt_snprintf(conf_text, sizeof(conf_text), "ENROLL: WAIT FACE");
            }
            else if (g_face_embed_once_req)
            {
                rt_snprintf(conf_text, sizeof(conf_text), "EMB: ONE SHOT");
            }
            else if (g_face_embed_auto)
            {
                rt_snprintf(conf_text, sizeof(conf_text), "EMB: AUTO ON");
            }
            else
            {
                rt_snprintf(conf_text, sizeof(conf_text), "EMB: MANUAL");
            }
        }
        else
        {
            rt_snprintf(conf_text, sizeof(conf_text), "GEST TEST");
        }

        static uint32_t face_log_cnt = 0;
        static uint32_t embed_cooldown = 0;
        static uint32_t embed_fail_cnt = 0;
        int face_n = 0;
        rt_bool_t gesture_focus_mode = RT_FALSE;

        if (g_gesture_window_active &&
            ((rt_int32_t) (g_gesture_window_until - rt_tick_get()) <= 0))
        {
            g_gesture_window_active = 0;
            g_gesture_window_until = 0;
            g_gesture_window_id = -1;
            g_gesture_ok_cnt = 0;
            rt_kprintf("[GEST] window timeout, back to face detect\n");
        }

        if (g_gesture_test_mode)
        {
            gesture_focus_mode = RT_TRUE;
            g_face_stable_match_id = -1;
            g_face_last_score2_m = -1;
            g_gesture_window_active = 0;
            g_gesture_window_until = 0;
            g_gesture_window_id = -1;
        }
        else if (g_gesture_window_active && !g_face_enroll_req)
        {
            gesture_focus_mode = RT_TRUE;
            g_face_stable_match_id = g_gesture_window_id;
            g_face_last_score2_m = -1;
            rt_tick_t left = g_gesture_window_until - rt_tick_get();
            uint32_t left_ms = (uint32_t) (left * 1000U / RT_TICK_PER_SECOND);
            rt_snprintf(conf_text, sizeof(conf_text), "GEST ID:%d %lu.%01lus",
                        g_gesture_window_id,
                        (unsigned long) (left_ms / 1000U),
                        (unsigned long) ((left_ms % 1000U) / 100U));
        }

        (void) face_log_cnt;
        if (embed_cooldown > 0U)
        {
            embed_cooldown--;
        }

        if (!gesture_focus_mode)
        {
            face_npu_switch(FACE_NPU_OWNER_DETECT);
            face_box_t face_boxes[MAX_BOXES];
            face_n = face_detect_run((const uint16_t *) g_image_rgb565_buffer,
                                     (int16_t) CAM_WIDTH, (int16_t) CAM_HEIGHT,
                                     face_boxes, MAX_BOXES);

            if (face_n > 0)
            {
                int best_i = 0;
                for (int i = 1; i < face_n; i++)
                {
                    if (face_boxes[i].score > face_boxes[best_i].score)
                    {
                        best_i = i;
                    }
                }

                int32_t det_best_m = (int32_t) (face_boxes[best_i].score * 1000.0f + 0.5f);
                if (det_best_m < g_face_det_min_score_milli)
                {
                    if ((FACE_SCORE_LOG_EVERY > 0U) && ((face_log_cnt++ % FACE_SCORE_LOG_EVERY) == 0U))
                    {
                        rt_kprintf("[FACE] det low=%ld thr=%ld\n", (long) det_best_m, (long) g_face_det_min_score_milli);
                    }
                    g_face_stable_match_id = -1;
                    g_face_last_score2_m = -1;
                    continue;
                }

#if FACE_PIPE_LOG_EVERY > 0U
            if ((face_log_cnt++ % FACE_PIPE_LOG_EVERY) == 0U)
            {
                int32_t best_m = (int32_t)(face_boxes[best_i].score * 1000.0f);
                rt_kprintf("[FACE] n=%d best_m=%d\n", face_n, best_m);
            }
#endif

            for (int i = 0; i < face_n; i++)
            {
                det_box_t b;
                b.x1 = face_boxes[i].x1;
                b.y1 = face_boxes[i].y1;
                b.x2 = face_boxes[i].x2;
                b.y2 = face_boxes[i].y2;
                b.score = face_boxes[i].score;
                b.cls = 0;
                draw_rect_rgb565((uint16_t *)g_image_rgb565_buffer, CAM_WIDTH, CAM_HEIGHT, &b, (uint16_t)0x07E0, 2);
            }

            int x1 = face_boxes[best_i].x1;
            int y1 = face_boxes[best_i].y1;
            int x2 = face_boxes[best_i].x2;
            int y2 = face_boxes[best_i].y2;

            int w = x2 - x1 + 1;
            int h = y2 - y1 + 1;
            int pad = (w > h ? w : h) / 5;
            x1 -= pad; y1 -= pad; x2 += pad; y2 += pad;

            rt_bool_t do_embed = RT_FALSE;
            if ((embed_cooldown == 0U) && (g_face_embed_auto != 0U))
            {
                do_embed = RT_TRUE;
            }
            if ((embed_cooldown == 0U) && g_face_embed_once_req)
            {
                do_embed = RT_TRUE;
            }
            if (g_face_enroll_req)
            {
                do_embed = RT_TRUE;
            }
            rt_bool_t one_shot_req = (g_face_embed_once_req != 0U) ? RT_TRUE : RT_FALSE;

            if (do_embed)
            {
                if (g_face_enroll_req)
                {
                    rt_snprintf(conf_text, sizeof(conf_text), "ENROLLING...");
                }
                rgb565_crop_resize_112_float_nchw_norm((const uint16_t *)g_image_rgb565_buffer,
                                                       (int16_t)CAM_WIDTH, (int16_t)CAM_HEIGHT,
                                                       x1, y1, x2, y2, in_f);

                dcache_clean_safe(in_f, (rt_ubase_t)(112U * 112U * 3U * sizeof(float)));

                face_npu_switch(FACE_NPU_OWNER_EMBED);
                rt_tick_t infer_t0 = rt_tick_get();
                int emb_rc = RunModel(false);
                rt_tick_t infer_t1 = rt_tick_get();
                if (emb_rc != 0)
                {
                    embed_fail_cnt++;
                    embed_cooldown = FACE_EMBED_FAIL_COOLDOWN_FRAMES;
                    g_face_embed_once_req = 0;
                    if ((embed_fail_cnt % 200U) == 1U)
                    {
                        rt_kprintf("[EMB] invoke failed, cooldown=%lu\n",
                                   (unsigned long) FACE_EMBED_FAIL_COOLDOWN_FRAMES);
                    }
                    continue;
                }
                embed_fail_cnt = 0;

                float *emb = GetModelOutputPtr_output0_70264();

                dcache_invalidate_safe(emb, (rt_ubase_t)(128U * sizeof(float)));

#if APP_USE_FACE_PIPELINE
                rt_bool_t just_enrolled = RT_FALSE;
                if (g_face_enroll_req)
                {
                    int enroll_id = face_db_enroll_current(emb);
                    if (enroll_id >= 0)
                    {
                        just_enrolled = RT_TRUE;
                        face_set_toast("ENROLLED", 1200U);
                        rt_snprintf(conf_text, sizeof(conf_text), "ENROLLED ID:%d", enroll_id);
                    }
                    else
                    {
                        rt_snprintf(conf_text, sizeof(conf_text), "ENROLL %d/%d",
                                    (int) g_face_enroll_count,
                                    (int) FACE_ENROLL_SAMPLES);
                    }
                }

                {
                    static int last_match_id = -2;
                    static int pending_match_id = -1;
                    static rt_uint8_t pending_match_cnt = 0;
                    static int sticky_match_id = -1;
                    static rt_tick_t sticky_match_until = 0;
                    static uint32_t score_log_cnt = 0;
                    static int multi_window_id = -1;
                    static rt_tick_t multi_window_until = 0;
                    static rt_uint8_t multi_match_cnt = 0;
                    int32_t best_score2_m = -1;
                    int best_id = face_db_best_match(emb, &best_score2_m);
                    int cur_match_id = -1;
                    int stable_match_id = -1;
                    int db_count = 0;
                    for (int i = 0; i < FACE_DB_MAX_USERS; i++)
                    {
                        if (g_face_db_used[i]) db_count++;
                    }
                    if ((best_id >= 0) && (best_score2_m >= g_face_match_thr2_milli))
                    {
                        cur_match_id = best_id;
                    }

                    if (cur_match_id >= 0)
                    {
                        if (pending_match_id == cur_match_id)
                        {
                            if (pending_match_cnt < 255U)
                            {
                                pending_match_cnt++;
                            }
                        }
                        else
                        {
                            pending_match_id = cur_match_id;
                            pending_match_cnt = 1U;
                        }
                        if (pending_match_cnt >= g_face_match_stable_frames)
                        {
                            stable_match_id = pending_match_id;
                        }
                    }
                    else
                    {
                        pending_match_id = -1;
                        pending_match_cnt = 0U;
                    }

                    if (stable_match_id >= 0)
                    {
                        sticky_match_id = stable_match_id;
                        sticky_match_until = rt_tick_get() +
                            (rt_tick_t) ((FACE_MATCH_GRACE_MS * RT_TICK_PER_SECOND + 999U) / 1000U);
                    }
                    else if ((sticky_match_id >= 0) &&
                             ((rt_int32_t) (sticky_match_until - rt_tick_get()) > 0))
                    {
                        stable_match_id = sticky_match_id;
                    }
                    else
                    {
                        sticky_match_id = -1;
                        sticky_match_until = 0;
                    }

                    if ((FACE_SCORE_LOG_EVERY > 0U) && ((score_log_cnt++ % FACE_SCORE_LOG_EVERY) == 0U))
                    {
                        rt_kprintf("[FACE] score2_m=%ld thr=%d best=%d cand=%d stable=%d st=%d/%d\n",
                                   (long) best_score2_m,
                                   (int) g_face_match_thr2_milli,
                                   best_id,
                                   cur_match_id,
                                   stable_match_id,
                                   (int) pending_match_cnt,
                                   (int) g_face_match_stable_frames);
                    }
                    g_face_stable_match_id = stable_match_id;
                    g_face_last_score2_m = best_score2_m;

                    if (!g_face_enroll_req && !g_gesture_window_active)
                    {
                        rt_tick_t now_tick = rt_tick_get();
                        if ((stable_match_id >= 0) && (best_score2_m >= g_face_match_thr2_milli))
                        {
                            rt_bool_t window_expired = (multi_window_until == 0) || ((rt_int32_t) (multi_window_until - now_tick) <= 0);
                            if ((multi_window_id != stable_match_id) || window_expired)
                            {
                                multi_window_id = stable_match_id;
                                multi_match_cnt = 1U;
                                multi_window_until = now_tick +
                                    (rt_tick_t) ((FACE_MULTI_MATCH_WINDOW_MS * RT_TICK_PER_SECOND + 999U) / 1000U);
                            }
                            else if (multi_match_cnt < 255U)
                            {
                                multi_match_cnt++;
                            }

                            if (multi_match_cnt >= FACE_MULTI_MATCH_NEED)
                            {
                                g_gesture_window_active = 1;
                                g_gesture_window_id = stable_match_id;
                                g_gesture_window_until = now_tick +
                                    (rt_tick_t) ((GESTURE_WINDOW_MS * RT_TICK_PER_SECOND + 999U) / 1000U);
                                g_gesture_ok_cnt = 0;
                                rt_kprintf("[GEST] window start id=%d for %lu ms (match %d/%d in 10s)\n",
                                           stable_match_id,
                                           (unsigned long) GESTURE_WINDOW_MS,
                                           (int) multi_match_cnt,
                                           (int) FACE_MULTI_MATCH_NEED);
                                multi_match_cnt = 0;
                                multi_window_id = -1;
                                multi_window_until = 0;
                            }
                            else
                            {
                                rt_snprintf(conf_text, sizeof(conf_text), "VERIFY:%d %d/%d",
                                            stable_match_id,
                                            (int) multi_match_cnt,
                                            (int) FACE_MULTI_MATCH_NEED);
                            }
                        }
                        else if ((multi_window_until != 0) && ((rt_int32_t) (multi_window_until - now_tick) <= 0))
                        {
                            multi_window_id = -1;
                            multi_match_cnt = 0;
                            multi_window_until = 0;
                        }
                    }

                    if (!just_enrolled)
                    {
                        if (db_count == 0)
                        {
                            rt_snprintf(conf_text, sizeof(conf_text), "NO FACE DB");
                        }
                        else if (stable_match_id >= 0)
                        {
                            rt_snprintf(conf_text, sizeof(conf_text), "ID:%d S2:%ld",
                                        stable_match_id, (long) best_score2_m);
                        }
                        else if (cur_match_id >= 0)
                        {
                            rt_snprintf(conf_text, sizeof(conf_text), "CHECK:%d %d/%d",
                                        cur_match_id,
                                        (int) pending_match_cnt,
                                        (int) g_face_match_stable_frames);
                        }
                        else
                        {
                            rt_snprintf(conf_text, sizeof(conf_text), "STRANGER S2:%ld",
                                        (long) best_score2_m);
                        }
                    }
                    if (stable_match_id != last_match_id)
                    {
                        if (stable_match_id >= 0)
                        {
                            rt_kprintf("[FACE] match id=%d score2_m=%ld\n",
                                       stable_match_id,
                                       (long) best_score2_m);
                        }
                        else if (last_match_id >= 0)
                        {
                            rt_kprintf("[FACE] match lost\n");
                        }
                        last_match_id = stable_match_id;
                    }
                }
#endif

                if ((g_face_toast_until != 0) && ((rt_int32_t)(g_face_toast_until - rt_tick_get()) > 0))
                {
                    rt_snprintf(conf_text, sizeof(conf_text), "%s", g_face_toast_text);
                }

#if FACE_EMB_VECTOR_LOG_EVERY > 0U
                int32_t n2_milli = 0;
                for (int i = 0; i < 128; i++)
                {
                    float v = emb[i];
                    n2_milli += (int32_t)(v * v * 1000.0f);
                }
                if ((frame_cnt % FACE_EMB_VECTOR_LOG_EVERY) == 0U)
                {
                    int e0 = (int)(emb[0] * 1000.0f);
                    int e1 = (int)(emb[1] * 1000.0f);
                    int e2 = (int)(emb[2] * 1000.0f);
                    int e3 = (int)(emb[3] * 1000.0f);
                    int e4 = (int)(emb[4] * 1000.0f);
                    int e5 = (int)(emb[5] * 1000.0f);
                    int e6 = (int)(emb[6] * 1000.0f);
                    int e7 = (int)(emb[7] * 1000.0f);
                    uint32_t infer_ms = (uint32_t)((infer_t1 - infer_t0) * 1000U / RT_TICK_PER_SECOND);
                    rt_kprintf("EMB0..7(milli)=%d %d %d %d %d %d %d %d | n2(milli)=%ld | infer=%lu ms\n",
                               e0, e1, e2, e3, e4, e5, e6, e7,
                               (long)n2_milli,
                               (unsigned long)infer_ms);
                }
#endif
                if (one_shot_req)
                {
                    uint32_t infer_ms = (uint32_t)((infer_t1 - infer_t0) * 1000U / RT_TICK_PER_SECOND);
                    int e0 = (int) (emb[0] * 1000.0f);
                    int e1 = (int) (emb[1] * 1000.0f);
                    int e2 = (int) (emb[2] * 1000.0f);
                    int e3 = (int) (emb[3] * 1000.0f);
                    int e4 = (int) (emb[4] * 1000.0f);
                    int e5 = (int) (emb[5] * 1000.0f);
                    int e6 = (int) (emb[6] * 1000.0f);
                    int e7 = (int) (emb[7] * 1000.0f);
                    rt_kprintf("[EMB] one-shot done, infer=%lu ms, vec8_m=%d,%d,%d,%d,%d,%d,%d,%d\n",
                               (unsigned long) infer_ms,
                               e0, e1, e2, e3, e4, e5, e6, e7);
                }
                (void) infer_t1;
                embed_cooldown = FACE_EMBED_COOLDOWN_FRAMES;
                g_face_embed_once_req = 0;
            }
        }
        else
        {
#if FACE_PIPE_LOG_EVERY > 0U
            if ((face_log_cnt++ % FACE_PIPE_LOG_EVERY) == 0U)
            {
                rt_kprintf("[FACE] n=0\n");
            }
#endif
            g_face_stable_match_id = -1;
            g_face_last_score2_m = -1;
            g_gesture_ok_cnt = 0;
        }

        }

        if ((g_gesture_test_mode || g_gesture_window_active) && !g_face_enroll_req)
        {
            static uint32_t gesture_div = 0;
            if ((gesture_div++ % GESTURE_RUN_INTERVAL_FRAMES) == 0U)
            {
                gesture_box_t gboxes[GESTURE_MAX_BOXES];
                int32_t best_ok_m = 0;
                int32_t best_palm_m = 0;
                face_npu_switch(FACE_NPU_OWNER_GESTURE);
                int gn = gesture_detect_run((const uint16_t *) g_image_rgb565_buffer,
                                            (int16_t) CAM_WIDTH, (int16_t) CAM_HEIGHT,
                                            gboxes, GESTURE_MAX_BOXES,
                                            &best_ok_m, &best_palm_m);
                g_runtime_last_palm_m = best_palm_m;

                int draw_n = gn;
                if (draw_n > 4) draw_n = 4;
                for (int i = 0; i < draw_n; i++)
                {
                    if (gboxes[i].score < 0.50f)
                    {
                        continue;
                    }
                    det_box_t b;
                    b.x1 = gboxes[i].x1;
                    b.y1 = gboxes[i].y1;
                    b.x2 = gboxes[i].x2;
                    b.y2 = gboxes[i].y2;
                    b.score = gboxes[i].score;
                    b.cls = gboxes[i].cls;
                    draw_rect_rgb565((uint16_t *) g_image_rgb565_buffer, CAM_WIDTH, CAM_HEIGHT,
                                     &b, (b.cls == GESTURE_CLASS_OK) ? (uint16_t) 0x07E0 : (uint16_t) 0xF800, 2);
                }

                if (best_palm_m >= GESTURE_OK_THR_MILLI)
                {
                    if (g_gesture_ok_cnt < 255U)
                    {
                        g_gesture_ok_cnt++;
                    }
                }
                else
                {
                    g_gesture_ok_cnt = 0;
                }

                if (g_gesture_ok_cnt >= GESTURE_OK_STABLE_FRAMES)
                {
                    if (!g_gesture_test_mode)
                    {
                        g_door_unlocked = 1U;
                        g_door_unlock_until = rt_tick_get() +
                            (rt_tick_t) ((DOOR_UNLOCK_HOLD_MS * RT_TICK_PER_SECOND + 999U) / 1000U);
                    }
                    g_gesture_ok_cnt = 0;
                    if (!g_gesture_test_mode)
                    {
                        g_gesture_window_active = 0;
                        g_gesture_window_until = 0;
                        g_gesture_window_id = -1;
                        buzzer_sfx_unlock();
                        face_set_toast("DOOR OPEN", 1200U);
                        rt_kprintf("[DOOR] open id=%d palm_m=%ld\n",
                                   (int) g_face_stable_match_id, (long) best_palm_m);
                    }
                    else
                    {
                        rt_kprintf("[GEST TEST] PALM detected palm_m=%ld\n", (long) best_palm_m);
                    }
                }

                if (g_gesture_test_mode)
                {
                    rt_snprintf(conf_text, sizeof(conf_text), "OK:%ld.%03ld P:%ld.%03ld",
                                (long) (best_ok_m / 1000),
                                (long) (best_ok_m % 1000),
                                (long) (best_palm_m / 1000),
                                (long) (best_palm_m % 1000));
                }
                else
                {
                    rt_snprintf(conf_text, sizeof(conf_text), "ID:%d P:%ld.%03ld",
                                g_gesture_window_id,
                                (long) (best_palm_m / 1000),
                                (long) (best_palm_m % 1000));
                }
            }
        }
        else
        {
            g_gesture_ok_cnt = 0;
            g_runtime_last_palm_m = -1;
        }

        if ((face_n > 0) &&
            (face_db_count_used() > 0) &&
            (g_face_stable_match_id < 0) &&
            !g_gesture_window_active &&
            !g_gesture_test_mode &&
            !g_face_enroll_req &&
            !g_door_unlocked)
        {
            stranger_alarm = RT_TRUE;
        }

        if (stranger_alarm && !s_prev_stranger_alarm && g_web_alarm_enable)
        {
            buzzer_sfx_face_fail();
        }
        s_prev_stranger_alarm = stranger_alarm;
        g_runtime_alarm_active = (stranger_alarm && g_web_alarm_enable) ? 1 : 0;

        /* alarm pattern disabled in continuous buzzer mode */
        pc_sync_with_server();
#else
        rgb565_to_rgb888_resize_192_float_hwc((const uint16_t *)g_image_rgb565_buffer,
                                              (int16_t)CAM_WIDTH,
                                              (int16_t)CAM_HEIGHT,
                                              in_f);
        
        dcache_clean_safe(in_f, (rt_ubase_t)(192U * 192U * 3U * sizeof(float)));
        rt_tick_t infer_t0 = rt_tick_get();
        RunModel(false);
        rt_tick_t infer_t1 = rt_tick_get();

        float *output1 = GetModelOutputPtr_p5_6x6_70437();
        float *output2 = GetModelOutputPtr_p4_12x12_70454();
        dcache_invalidate_safe(output1, (rt_ubase_t)(6U * 6U * 3U * (5U + (uint32_t)CLASS_NUM) * sizeof(float)));
        dcache_invalidate_safe(output2, (rt_ubase_t)(12U * 12U * 3U * (5U + (uint32_t)CLASS_NUM) * sizeof(float)));

        int16_t total = 0;
        static det_box_t pool[540];

        if ((frame_cnt % 30U) == 0U)
        {
            float max_raw_6 = 0.0f, max_sig_6 = 0.0f;
            float max_raw_12 = 0.0f, max_sig_12 = 0.0f;
            yolo_output_stats(output1, GRID_SIZE_2, &max_raw_6, &max_sig_6);
            yolo_output_stats(output2, GRID_SIZE_1, &max_raw_12, &max_sig_12);
            int max_sig_6_m = (int)(max_sig_6 * 1000.0f);
            int max_sig_12_m = (int)(max_sig_12 * 1000.0f);
            int max_raw_6_m = (int)(max_raw_6 * 1000.0f);
            int max_raw_12_m = (int)(max_raw_12 * 1000.0f);
            LOG_I("YOLO stats: max_sig 6x6=%d/1000 (raw=%d/1000), 12x12=%d/1000 (raw=%d/1000)",
                  max_sig_6_m, max_raw_6_m, max_sig_12_m, max_raw_12_m);
            float in_min = 0.0f, in_max = 0.0f, in_sum = 0.0f;
            input_stats_sample(in_f, &in_min, &in_max, &in_sum);
            LOG_I("INPUT stats: min=%d max=%d sum=%d (x100)",
                  (int)(in_min * 100.0f), (int)(in_max * 100.0f), (int)(in_sum * 100.0f));
            uint32_t infer_ms = (uint32_t)((infer_t1 - infer_t0) * 1000U / RT_TICK_PER_SECOND);
            LOG_I("INFER time: %lu ms", (unsigned long)infer_ms);
        }

        total += decode_output_layer(output2, GRID_SIZE_1, 0, (int16_t)CAM_WIDTH, (int16_t)CAM_HEIGHT, CONF_THRESH,
                                     pool + total, (int16_t)(sizeof(pool) / sizeof(pool[0])) - total);

        total += decode_output_layer(output1, GRID_SIZE_2, 1, (int16_t)CAM_WIDTH, (int16_t)CAM_HEIGHT, CONF_THRESH,
                                     pool + total, (int16_t)(sizeof(pool) / sizeof(pool[0])) - total);

        int32_t kept = nms_filter(pool, total, NMS_THRESH);

        // Safety filter: drop boxes below threshold (and any NaN/invalid scores)
        // This also protects against any upstream decode/config mismatch producing garbage boxes.
        int32_t filtered = 0;
        for (int32_t i = 0; i < kept; i++)
        {
            float s = pool[i].score;
            if (!(s == s)) continue;            // NaN
            if (s < CONF_THRESH) continue;      // below conf threshold
            if (filtered != i) pool[filtered] = pool[i];
            filtered++;
        }

        int32_t out_n = MIN(filtered, MAX_BOXES);

        if ((frame_cnt % 30U) == 0U)
        {
            LOG_I("YOLO boxes: total=%d kept=%d", (int)total, (int)out_n);
        }

        int thickness = 2;
        for (int i = 0; i < out_n; i++)
        {
            // Color by class: OK=green, PALM=red
            uint16_t box_color = (pool[i].cls == 1) ? (uint16_t)0xF800 : (uint16_t)0x07E0;
            det_box_t shifted = pool[i];
            shifted.x1 = (int16_t)(shifted.x1 + BOX_SHIFT_X);
            shifted.x2 = (int16_t)(shifted.x2 + BOX_SHIFT_X);
            shifted.y1 = (int16_t)(shifted.y1 + BOX_SHIFT_Y);
            shifted.y2 = (int16_t)(shifted.y2 + BOX_SHIFT_Y);
            draw_rect_rgb565((uint16_t *)g_image_rgb565_buffer, CAM_WIDTH, CAM_HEIGHT, &shifted, box_color, thickness);
        }

        // Overlay per-class confidence on LCD (draw after blit)
        float best_ok = 0.0f;
        float best_palm = 0.0f;
        for (int i = 0; i < out_n; i++)
        {
            if (pool[i].cls == 0 && pool[i].score > best_ok) best_ok = pool[i].score;
            if (pool[i].cls == 1 && pool[i].score > best_palm) best_palm = pool[i].score;
        }
        int ok_c = (int)(best_ok * 100.0f + 0.5f);
        int palm_c = (int)(best_palm * 100.0f + 0.5f);
        if (ok_c < 0) ok_c = 0;
        if (palm_c < 0) palm_c = 0;
        rt_snprintf(conf_text, sizeof(conf_text), "OK:%d.%02d PALM:%d.%02d",
                    ok_c / 100, ok_c % 100,
                    palm_c / 100, palm_c % 100);
        if (out_n == 0)
        {
            det_box_t dbg_box;
            dbg_box.x1 = 4;
            dbg_box.y1 = 4;
            dbg_box.x2 = 28;
            dbg_box.y2 = 28;
            dbg_box.x1 = (int16_t)(dbg_box.x1 + BOX_SHIFT_X);
            dbg_box.x2 = (int16_t)(dbg_box.x2 + BOX_SHIFT_X);
            dbg_box.y1 = (int16_t)(dbg_box.y1 + BOX_SHIFT_Y);
            dbg_box.y2 = (int16_t)(dbg_box.y2 + BOX_SHIFT_Y);
            draw_rect_rgb565((uint16_t *)g_image_rgb565_buffer, CAM_WIDTH, CAM_HEIGHT, &dbg_box, (uint16_t)0x07E0, 2);
        }
        /* alarm pattern disabled in continuous buzzer mode */
        door_led_write(RT_FALSE);
#endif

        if (g_stream_sem)
        {
            if (!g_stream_busy)
            {
                memcpy(g_stream_buf, g_image_rgb565_buffer, STREAM_FRAME_BYTES);
                dcache_clean_safe(g_stream_buf, (rt_ubase_t) STREAM_FRAME_BYTES);
                g_stream_busy = true;
                rt_sem_release(g_stream_sem);
                queued_frames++;
                if ((queued_frames % 30U) == 0U)
                {
                    LOG_I("Stream queued %lu frames", (unsigned long)queued_frames);
                }
            }
        }

        if (g_door_unlocked && ((rt_int32_t) (g_door_unlock_until - rt_tick_get()) > 0))
        {
            rt_snprintf(conf_text, sizeof(conf_text), "DOOR OPEN");
        }

        st7789_blit_rgb565_center_skip_top((const uint16_t *)g_image_rgb565_buffer,
                                           CAM_WIDTH, CAM_HEIGHT,
                                           ST7789_STATUS_BAR_H);
        st7789_status_text_update(conf_text);

        rt_tick_t end = rt_tick_get();
        /* disable per-frame log spam */
        (void) end;

        rt_thread_mdelay(5);

        frame_cnt++;
    }
}






















