#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一为 hal_entry.c 添加中文注释，并修复原有乱码注释行。
读取原始文件，以 ASCII/错误替换模式解码，精确查找关键行并插入/替换注释，
最终以 UTF-8 编码写回。
"""

import re

SRC = r'd:\RT-ThreadStudio\workspace\wfi_board4\src\hal_entry.c'

# ──────────────────────────────────────────────
# 以字节读取，逐行按 ASCII+errors=replace 解码
# ──────────────────────────────────────────────
with open(SRC, 'rb') as f:
    raw_bytes = f.read()

# 统一换行符，避免 \r\n 干扰
raw_bytes = raw_bytes.replace(b'\r\n', b'\n')
raw_lines = raw_bytes.split(b'\n')
lines = [l.decode('utf-8', errors='replace') for l in raw_lines]

# ──────────────────────────────────────────────
# 辅助：在匹配行之前插入注释行
# ──────────────────────────────────────────────
def insert_before(lines, pattern, comment, max_match=1):
    count = 0
    result = []
    for line in lines:
        if count < max_match and re.search(pattern, line):
            result.append(comment)
            count += 1
        result.append(line)
    return result

# 辅助：替换匹配行（整行替换）
def replace_line(lines, pattern, new_line, max_match=1):
    count = 0
    result = []
    for line in lines:
        if count < max_match and re.search(pattern, line):
            result.append(new_line)
            count += 1
        else:
            result.append(line)
    return result

# ──────────────────────────────────────────────
# 1. 文件顶部：版权注释后插入功能说明块
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#include <rtthread\.h>',
    '/* ======================================================\n'
    ' * 主要功能：WiFi + 摄像头 + NPU 人脸/手势识别门禁系统\n'
    ' * 平台：RT-Thread + RA8 MCU + Ethos-U55 NPU\n'
    ' * ====================================================== */\n'
    '\n'
    '/* ---- RT-Thread 及 BSP 头文件 ---- */')

# 移除原来重复的 #include <rtthread.h> 行前的空白注释行（如果已有）
# （此处 insert_before 不会产生重复，因为 #include 行本身保留）

# ──────────────────────────────────────────────
# 2. FAL/DFS/WiFi includes 前加节注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#include <fal\.h>',
    '\n/* ---- 文件系统 / WiFi 驱动 ---- */')

# ──────────────────────────────────────────────
# 3. sensor/st7789/model_select includes 前加节注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#include "sensor\.h"',
    '\n/* ---- 摄像头 / LCD / 模型选择 ---- */')

# ──────────────────────────────────────────────
# 4. WIFI 宏定义注释
# ──────────────────────────────────────────────
lines = replace_line(lines,
    r'#define WIFI_SSID\s+"Xiaomi_C70E"',
    '#define WIFI_SSID           "Xiaomi_C70E"           /* 路由器 SSID */')
lines = replace_line(lines,
    r'#define WIFI_PASSWORD\s+"13618027302"',
    '#define WIFI_PASSWORD       "13618027302"           /* 路由器密码 */')

lines = insert_before(lines,
    r'#define WIFI_SSID',
    '/* ---- WiFi 连接配置 ---- */')

# ──────────────────────────────────────────────
# 5. IO 引脚宏定义注释
# ──────────────────────────────────────────────
lines = replace_line(lines,
    r'#define LED_PIN_0\s+BSP_IO_PORT_00_PIN_12',
    '#define LED_PIN_0           BSP_IO_PORT_00_PIN_12   /* 心跳指示 LED */')
lines = replace_line(lines,
    r'#define DOOR_LED_PIN\s+BSP_IO_PORT_06_PIN_13',
    '#define DOOR_LED_PIN        BSP_IO_PORT_06_PIN_13   /* 门锁绿色 LED（P613）*/')
lines = replace_line(lines,
    r'#define ALARM_BUZZER_PIN\s+BSP_IO_PORT_10_PIN_07',
    '#define ALARM_BUZZER_PIN    BSP_IO_PORT_10_PIN_07   /* 报警蜂鸣器（PA07）*/')
lines = replace_line(lines,
    r'#define FS_PARTITION_NAME\s+"filesystem"',
    '#define FS_PARTITION_NAME   "filesystem"            /* FAL 文件系统分区名 */')

lines = insert_before(lines,
    r'#define LED_PIN_0\s+BSP_IO_PORT_00_PIN_12',
    '\n/* ---- IO 引脚定义 ---- */')

# ──────────────────────────────────────────────
# 6. 摄像头分辨率及调试宏注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#define CAM_WIDTH\s+320',
    '\n/* ---- 摄像头分辨率（QVGA）---- */')
lines = replace_line(lines,
    r'#define DEBUG_BYPASS_CAMERA\s+0',
    '#define DEBUG_BYPASS_CAMERA 0  /* 调试开关：1=跳过摄像头，直接用固定数据 */')
lines = replace_line(lines,
    r'#define DEBUG_BYPASS_NPU\s+0',
    '#define DEBUG_BYPASS_NPU    0  /* 调试开关：1=跳过 NPU 推理 */')

# ──────────────────────────────────────────────
# 7. LCD 宏注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#define LCD_INIT_RETRY_COUNT\s+5U',
    '/* ---- LCD 初始化重试参数 ---- */')
lines = replace_line(lines,
    r'#define LCD_INIT_RETRY_COUNT\s+5U',
    '#define LCD_INIT_RETRY_COUNT     5U     /* ST7789 最大初始化重试次数 */')
lines = replace_line(lines,
    r'#define LCD_INIT_RETRY_DELAY_MS\s+120U',
    '#define LCD_INIT_RETRY_DELAY_MS  120U   /* 每次重试前等待时间（ms）*/')

# ──────────────────────────────────────────────
# 8. 人脸数据库宏注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#define FACE_DB_MAX_USERS\s+3',
    '\n/* ---- 人脸数据库与识别参数 ---- */')
lines = replace_line(lines,
    r'#define FACE_DB_MAX_USERS\s+3',
    '#define FACE_DB_MAX_USERS 3             /* 最多注册用户数 */')
lines = replace_line(lines,
    r'#define FACE_EMB_DIM\s+128',
    '#define FACE_EMB_DIM 128                /* MobileFaceNet 嵌入向量维度 */')
lines = replace_line(lines,
    r'#define FACE_MATCH_THR2_MILLI\s+880',
    '#define FACE_MATCH_THR2_MILLI 880       /* 余弦²相似度阈值（×1000），超过则视为匹配 */')
lines = replace_line(lines,
    r'#define FACE_MATCH_STABLE_FRAMES\s+5U',
    '#define FACE_MATCH_STABLE_FRAMES 5U     /* 连续匹配帧数达到此值才认为稳定匹配 */')
lines = replace_line(lines,
    r'#define FACE_MATCH_GRACE_MS\s+1200U',
    '#define FACE_MATCH_GRACE_MS 1200U       /* 匹配丢失后的宽限时间（ms），防止短暂遮挡 */')
lines = replace_line(lines,
    r'#define FACE_DET_MIN_SCORE_MILLI\s+850',
    '#define FACE_DET_MIN_SCORE_MILLI 850    /* 人脸检测最低置信度（×1000）*/')
lines = replace_line(lines,
    r'#define FACE_MULTI_MATCH_WINDOW_MS\s+10000U',
    '#define FACE_MULTI_MATCH_WINDOW_MS 10000U /* 多次匹配计数窗口（ms）*/')
lines = replace_line(lines,
    r'#define FACE_MULTI_MATCH_NEED\s+3U',
    '#define FACE_MULTI_MATCH_NEED 3U        /* 窗口内需匹配次数，达到后开启手势窗口 */')
lines = replace_line(lines,
    r'#define FACE_ENROLL_SAMPLES\s+4U',
    '#define FACE_ENROLL_SAMPLES 4U          /* 注册时采集的帧数（用于平均特征）*/')
lines = replace_line(lines,
    r'#define FACE_SCORE_LOG_EVERY\s+30U',
    '#define FACE_SCORE_LOG_EVERY 30U        /* 每隔多少帧打印一次得分日志（0=关闭）*/')
lines = replace_line(lines,
    r'#define FACE_ENROLL_KEY_DEBOUNCE_MS\s+250U',
    '#define FACE_ENROLL_KEY_DEBOUNCE_MS 250U    /* 注册按键防抖时间（ms）*/')
lines = replace_line(lines,
    r'#define FACE_ENROLL_KEY_LONGPRESS_MS\s+3000U',
    '#define FACE_ENROLL_KEY_LONGPRESS_MS 3000U  /* 长按识别时间（ms），长按清空数据库 */')
lines = replace_line(lines,
    r'#define FACE_DB_FILE_PATH\s+"/face_db\.bin"',
    '#define FACE_DB_FILE_PATH "/face_db.bin"    /* 人脸数据库文件路径 */')
lines = replace_line(lines,
    r'#define FACE_DB_MAGIC\s+0x46444231U',
    '#define FACE_DB_MAGIC 0x46444231U       /* 数据库文件魔数 "FDB1" */')
lines = replace_line(lines,
    r'#define FACE_DB_VERSION\s+1U',
    '#define FACE_DB_VERSION 1U              /* 数据库文件版本号 */')

# ──────────────────────────────────────────────
# 9. 手势与门锁报警宏注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#define GESTURE_OK_THR_MILLI\s+500',
    '\n/* ---- 手势识别参数 ---- */')
lines = replace_line(lines,
    r'#define GESTURE_OK_THR_MILLI\s+500',
    '#define GESTURE_OK_THR_MILLI 500        /* OK 手势置信度阈值（×1000）*/')
lines = replace_line(lines,
    r'#define GESTURE_OK_STABLE_FRAMES\s+1U',
    '#define GESTURE_OK_STABLE_FRAMES 1U     /* OK 手势稳定帧数 */')
lines = replace_line(lines,
    r'#define GESTURE_RUN_INTERVAL_FRAMES\s+1U',
    '#define GESTURE_RUN_INTERVAL_FRAMES 1U  /* 每隔多少帧运行一次手势检测 */')
lines = replace_line(lines,
    r'#define GESTURE_MAX_BOXES\s+16',
    '#define GESTURE_MAX_BOXES 16            /* 手势检测最大输出框数 */')
lines = replace_line(lines,
    r'#define GESTURE_WINDOW_MS\s+30000U',
    '#define GESTURE_WINDOW_MS 30000U        /* 人脸确认后，手势窗口保持时间（ms）*/')

lines = insert_before(lines,
    r'#define DOOR_UNLOCK_HOLD_MS\s+3000U',
    '\n/* ---- 门锁与报警参数 ---- */')
lines = replace_line(lines,
    r'#define DOOR_UNLOCK_HOLD_MS\s+3000U',
    '#define DOOR_UNLOCK_HOLD_MS 3000U       /* 开门后 LED 保持亮起的时间（ms）*/')
lines = replace_line(lines,
    r'#define ALARM_BUZZER_ON_MS\s+120U',
    '#define ALARM_BUZZER_ON_MS 120U         /* 报警蜂鸣器响的时长（ms）*/')
lines = replace_line(lines,
    r'#define ALARM_BUZZER_OFF_MS\s+880U',
    '#define ALARM_BUZZER_OFF_MS 880U        /* 报警蜂鸣器静默时长（ms）*/')
lines = replace_line(lines,
    r'#define ALARM_BUZZER_PWM_FREQ_HZ\s+3800U',
    '#define ALARM_BUZZER_PWM_FREQ_HZ 3800U  /* 蜂鸣器频率（Hz）*/')
lines = replace_line(lines,
    r'#define ALARM_BUZZER_PWM_DUTY_PERCENT\s+80U',
    '#define ALARM_BUZZER_PWM_DUTY_PERCENT 80U /* 蜂鸣器占空比（%）*/')

# ──────────────────────────────────────────────
# 10. 全局变量注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static volatile bool led_status = false;',
    '\n/* ---- 全局运行时状态变量 ---- */')
lines = replace_line(lines,
    r'static volatile bool led_status = false;',
    'static volatile bool led_status = false;               /* 心跳 LED 当前状态 */')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_face_embed_once_req = 0;',
    'static volatile rt_uint8_t g_face_embed_once_req = 0;  /* 单次特征提取请求标志 */')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_face_embed_auto = 1;',
    'static volatile rt_uint8_t g_face_embed_auto = 1;      /* 自动特征提取开关（1=开启）*/')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_door_unlocked = 0;',
    'static volatile rt_uint8_t g_door_unlocked = 0;        /* 门锁已开（1=已开）*/')
lines = replace_line(lines,
    r'static volatile rt_tick_t g_door_unlock_until = 0;',
    'static volatile rt_tick_t g_door_unlock_until = 0;     /* 门锁自动关闭的超时 tick */')
lines = replace_line(lines,
    r"static volatile int g_web_led_mode = -1;",
    'static volatile int g_web_led_mode = -1;               /* Web 控制 LED 模式：-1=自动, 0=关, 1=开 */')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_web_alarm_enable = 1;',
    'static volatile rt_uint8_t g_web_alarm_enable = 1;     /* Web 控制报警使能 */')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_web_buzzer_enable = 0;',
    'static volatile rt_uint8_t g_web_buzzer_enable = 0;    /* Web 控制蜂鸣器使能 */')
lines = replace_line(lines,
    r'static volatile int32_t g_runtime_last_palm_m = -1;',
    'static volatile int32_t g_runtime_last_palm_m = -1;    /* 最近一次手掌置信度（×1000）*/')
lines = replace_line(lines,
    r'static volatile rt_uint8_t g_runtime_alarm_active = 0;',
    'static volatile rt_uint8_t g_runtime_alarm_active = 0; /* 当前报警激活标志 */')

# ──────────────────────────────────────────────
# 11. 小函数注释（使用 insert_before 在函数定义前加注释）
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static inline void door_led_write\(rt_bool_t on\)',
    '\n/* 控制门锁 LED，低电平点亮（共阳接法）*/')

lines = insert_before(lines,
    r'static void buzzer_pwm_try_init\(void\)',
    '\n/* 尝试初始化蜂鸣器的 GPT（PWM）外设，只执行一次 */')

lines = insert_before(lines,
    r'static inline void buzzer_tone_set\(uint32_t freq_hz, rt_bool_t on\)',
    '\n/* 设置蜂鸣器音调：freq_hz=频率，on=开/关；优先用 PWM，失败则用 GPIO 模拟 */')

lines = insert_before(lines,
    r'static inline void buzzer_write\(rt_bool_t on\)',
    '\n/* 以当前全局频率控制蜂鸣器开/关 */')

lines = insert_before(lines,
    r'static void buzzer_gpio_tone_play\(uint32_t hz, uint32_t ms\)',
    '\n/* 纯 GPIO 模拟方波播放音调（无 PWM 外设时使用），hz=频率，ms=持续时间 */')

lines = insert_before(lines,
    r'static void buzzer_sfx_unlock\(void\)',
    '\n/* 开门成功音效：两声上升音 */')

lines = insert_before(lines,
    r'static void buzzer_sfx_face_fail\(void\)',
    '/* 陌生人/识别失败音效：两声下降警告音 */')

lines = insert_before(lines,
    r'static void alarm_buzzer_update\(rt_bool_t enable\)',
    '\n/* 报警蜂鸣器周期性鸣叫驱动，每帧调用一次（非阻塞）；\n'
    ' * enable=RT_FALSE 时立即停止，enable=RT_TRUE 时按 ON/OFF 时间交替鸣叫 */')

# ──────────────────────────────────────────────
# 12. 人脸数据库函数注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static int face_db_count_used\(void\)',
    '\n/* 返回人脸数据库中已注册的用户数 */')

lines = insert_before(lines,
    r'static void face_set_toast\(const char \*text, rt_uint32_t ms\)',
    '\n/* 设置 LCD 状态栏短暂提示文字，持续 ms 毫秒后自动清除 */')

lines = insert_before(lines,
    r'static void face_db_recompute_next_slot\(void\)',
    '\n/* 重新计算人脸数据库中第一个空闲槽位，写入 g_face_db_next_slot */')

lines = insert_before(lines,
    r'static int face_db_save_fs\(void\)',
    '\n/* 将内存中的人脸数据库序列化并写入文件系统（/face_db.bin）\n'
    ' * 成功返回 0，失败返回负数错误码 */')

lines = insert_before(lines,
    r'static int face_db_load_fs\(void\)',
    '\n/* 从文件系统读取人脸数据库到内存；\n'
    ' * 返回 0=成功，-1=文件不存在，-2=读取错误，-3=格式校验失败 */')

lines = insert_before(lines,
    r'static void face_key_poll_and_trigger_enroll\(void\)',
    '\n/* 轮询注册按键，短按=触发人脸注册，长按(3秒)=清空数据库；\n'
    ' * 内置防抖与长按去重，需每帧调用一次 */')

lines = insert_before(lines,
    r'static int face_db_enroll_current\(const float \*emb\)',
    '\n/* 累积 emb 到注册缓冲区，达到 FACE_ENROLL_SAMPLES 帧后取均值写入数据库；\n'
    ' * 返回注册成功的用户 ID，否则返回 -1（样本不足或参数无效）*/')

lines = insert_before(lines,
    r'static int face_db_best_match\(const float \*emb, int32_t \*best_score2_milli\)',
    '\n/* 在数据库中查找与 emb 最匹配的用户；\n'
    ' * 返回用户 ID（0~N-1），未找到返回 -1；best_score2_milli 输出余弦²得分（×1000）*/')

# ──────────────────────────────────────────────
# 13. LCD 渲染函数注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static inline void dcache_invalidate_safe\(',
    '\n/* 安全的 D-Cache 无效化（按32字节对齐），buf=起始地址，len_bytes=长度 */')

lines = insert_before(lines,
    r'static inline void dcache_clean_safe\(',
    '\n/* 安全的 D-Cache 清洗（写回内存），buf=起始地址，len_bytes=长度 */')

lines = insert_before(lines,
    r'static void st7789_show_boot_test\(void\)',
    '\n/* LCD 开机测试：绘制彩条 + "OK" 文字，用于验证屏幕正常 */')

lines = insert_before(lines,
    r'static void st7789_fill_rect_solid\(',
    '\n/* 用指定 RGB565 颜色填充 LCD 矩形区域（裁剪到屏幕边界）*/')

lines = insert_before(lines,
    r'static void st7789_status_text_update\(',
    '\n/* 更新 LCD 顶部状态栏文字，内容不变时跳过刷新 */')

lines = insert_before(lines,
    r'static void st7789_blit_rgb565_center_skip_top\(',
    '\n/* 将 RGB565 帧缓冲居中显示到 LCD，跳过顶部 skip_top 行（状态栏区域）*/')

lines = insert_before(lines,
    r'static void draw_rect_rgb565\(',
    '\n/* 在 RGB565 帧缓冲上绘制矩形边框（检测框叠加），thickness=线宽 */')

# ──────────────────────────────────────────────
# 14. 图像预处理函数注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static void rgb565_to_rgb888_resize_192_float_hwc\(',
    '\n/* 将 RGB565 帧缩放到 192×192，转换为 float HWC 格式并归一化到 [0,1]，\n'
    ' * 用于手势检测 YOLOv5 模型输入 */')

lines = insert_before(lines,
    r'static void rgb565_to_rgb888_resize_112_float_nchw_norm\(',
    '\n/* 将 RGB565 帧缩放到 112×112，转换为 float NCHW 格式并做 MobileFaceNet 归一化\n'
    ' * (x - 127.5) / 128，用于人脸特征提取模型输入 */')

lines = insert_before(lines,
    r'static void rgb565_crop_resize_112_float_nchw_norm\(',
    '\n/* 从 RGB565 帧中裁剪人脸检测框区域，缩放到 112×112 做 MobileFaceNet 推理;\n'
    ' * 裁剪框无效时退化为全图缩放 */')

# ──────────────────────────────────────────────
# 15. 网络通信宏和函数注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'#define HTTP_PORT 80',
    '\n/* ---- 网络通信配置 ---- */')
lines = replace_line(lines,
    r'#define HTTP_PORT 80',
    '#define HTTP_PORT 80                        /* 板载 Web 服务器监听端口 */')
lines = replace_line(lines,
    r'#define STREAM_SERVER_IP\s+"192\.168\.31\.133"',
    '#define STREAM_SERVER_IP   "192.168.31.133" /* PC 端流服务器 IP */')
lines = replace_line(lines,
    r'#define STREAM_SERVER_PORT\s+9000',
    '#define STREAM_SERVER_PORT 9000             /* PC 端视频流接收端口 */')
lines = replace_line(lines,
    r'#define PC_CTRL_WEB_PORT\s+8080',
    '#define PC_CTRL_WEB_PORT   8080             /* PC 端控制 Web 服务端口 */')
lines = replace_line(lines,
    r'#define ENABLE_STREAM_CLIENT\s+1',
    '#define ENABLE_STREAM_CLIENT 1              /* 是否启用视频流推送客户端 */')
lines = replace_line(lines,
    r'#define ENABLE_FACE_EMBEDDING\s+1',
    '#define ENABLE_FACE_EMBEDDING 1             /* 是否启用人脸特征提取 */')
lines = replace_line(lines,
    r'#define FACE_EMBED_COOLDOWN_FRAMES\s+15U',
    '#define FACE_EMBED_COOLDOWN_FRAMES 15U  /* 嵌入推理的冷却帧数（降低负载）*/')
lines = replace_line(lines,
    r'#define FACE_EMBED_FAIL_COOLDOWN_FRAMES\s+120U',
    '#define FACE_EMBED_FAIL_COOLDOWN_FRAMES 120U /* 推理失败后等待更长时间再重试 */')
lines = replace_line(lines,
    r'#define FACE_PIPE_LOG_EVERY\s+0U',
    '#define FACE_PIPE_LOG_EVERY 0U          /* 人脸流水线日志频率（0=关闭）*/')
lines = replace_line(lines,
    r'#define FACE_EMB_VECTOR_LOG_EVERY\s+0U',
    '#define FACE_EMB_VECTOR_LOG_EVERY 0U   /* 特征向量日志频率（0=关闭）*/')

lines = insert_before(lines,
    r'static int send_all\(',
    '\n/* 可靠发送：循环 send 直到所有数据发出，处理 EAGAIN/EWOULDBLOCK；\n'
    ' * 返回 0=成功，-1=连接断开或超时 */')

lines = insert_before(lines,
    r'static int pc_http_get_small\(',
    '\n/* 向 PC 控制服务器发送小型 HTTP GET 请求，resp 接收响应缓冲（可为 NULL）*/')

lines = insert_before(lines,
    r'static void pc_sync_with_server\(void\)',
    '\n/* 与 PC 控制服务器定期同步：拉取 LED/报警控制指令，推送运行时状态；\n'
    ' * 内部限速约 700ms 一次，主循环每帧调用 */')

# ──────────────────────────────────────────────
# 16. 流缓冲区与流线程注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static uint8_t g_stream_buf\[STREAM_FRAME_BYTES\]',
    '\n/* ---- 视频流与 JPEG 缓冲区 ---- */')

lines = insert_before(lines,
    r'static void stream_client_entry\(void \*param\)',
    '\n/* 视频流推送线程：连接 PC 端 stream_server，每帧发送 16 字节头 + RGB565 原始数据 */')

# ──────────────────────────────────────────────
# 17. Web 服务器线程注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'void web_server_entry\(void \*param\)',
    '\n/* 板载 Web 服务器线程（HTTP 端口 80）：\n'
    ' * - GET /             返回 HTML 实时预览页面\n'
    ' * - GET /capture.bmp  返回当前帧 BMP 图像\n'
    ' * - GET /api/runtime  返回 JSON 格式运行时状态\n'
    ' * - GET /api/control  接受 led/alarm/buzzer 控制参数 */')

# ──────────────────────────────────────────────
# 18. WiFi 连接线程注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static void wifi_auto_connect_entry\(void \*parameter\)',
    '\n/* WiFi 自动连接线程：系统稳定后连接配置的 AP */')

# ──────────────────────────────────────────────
# 19. HyperRAM 初始化注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'static void manual_hyper_ram_init\(void\)',
    '\n/* 手动初始化 HyperRAM（OSPI1）：复位引脚 -> 切换 8D-8D-8D 模式 -> 读写 CR0 寄存器 */')

# ──────────────────────────────────────────────
# 20. hal_entry 主函数注释
# ──────────────────────────────────────────────
lines = insert_before(lines,
    r'void hal_entry\(void\)',
    '\n/* ======================================================\n'
    ' * hal_entry - 系统主入口函数（RT-Thread 启动后调用）\n'
    ' * 初始化顺序：\n'
    ' *   1. HyperRAM 手动初始化\n'
    ' *   2. GPIO 引脚配置（LED / 蜂鸣器 / 注册按键）\n'
    ' *   3. FAL 分区表 + LittleFS 文件系统挂载\n'
    ' *   4. 人脸数据库加载（APP_USE_FACE_PIPELINE 时）\n'
    ' *   5. LCD（ST7789）初始化并显示开机测试画面\n'
    ' *   6. 摄像头（CEU）初始化与参数配置\n'
    ' *   7. NPU（Ethos-U55）初始化\n'
    ' *   8. 辅助线程启动：WiFi 自动连接 / 视频流 / Web 服务器\n'
    ' *   9. 主循环：逐帧采集 → NPU 推理 → LCD 显示 → 门锁/报警决策\n'
    ' * ====================================================== */')

# ──────────────────────────────────────────────
# 21. 修复 4 个原始乱码注释行（用 pattern 匹配残留乱码）
#     原始行包含无效 UTF-8 或乱码 Unicode，整行替换
# ──────────────────────────────────────────────
fixed = []
for line in lines:
    # 乱码行1：输出张量格式注释
    if re.search(r'//.*\[1, 3, grid, grid, 7\]', line) and '\ufffd' in line:
        line = '    // 输出张量格式 [1, 3, grid, grid, 7] = [batch, anchors, y, x, features]'
    # 乱码行2：最终置信度注释
    elif re.search(r'//.*sigmoid\(obj\)', line) and '\ufffd' in line:
        line = '            // 最终置信度 = sigmoid(obj) * max(sigmoid(class_probs))'
    # 乱码行3：发送超时注释
    elif re.search(r'int timeout_ms = 5000', line) and '\ufffd' in line:
        line = '                int timeout_ms = 5000;  // 发送超时 5 秒'
    # 乱码行4：增大发送缓冲区注释
    elif re.search(r'int sndbuf_size = 256 \* 1024', line) and '\ufffd' in line:
        line = '                // 增大发送缓冲区以提高大帧吞吐量'
    fixed.append(line)
lines = fixed

# ──────────────────────────────────────────────
# 22. 摄像头参数初始化注释（替换乱码段）
# ──────────────────────────────────────────────
SENSOR_COMMENT = '    /* 摄像头自动增益/曝光/图像增强参数初始化 */'
lines_out = []
skip_next = False
for i, line in enumerate(lines):
    if skip_next:
        skip_next = False
        continue
    # 检测原始乱码注释（含乱码的行紧挨着 sensor_set_auto_gain），替换它
    if '\ufffd' in line and 'sensor_set_auto' not in line and i + 1 < len(lines) and 'sensor_set_auto_gain' in lines[i+1]:
        lines_out.append(SENSOR_COMMENT)
    else:
        lines_out.append(line)
lines = lines_out

# ──────────────────────────────────────────────
# 23. 写回文件（UTF-8）
# ──────────────────────────────────────────────
content = '\n'.join(lines)
with open(SRC, 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(content)

print(f'Done. Lines: {len(lines)}')
# 验证：检查是否还有替换字符
remaining = sum(1 for l in lines if '\ufffd' in l)
print(f'Lines still with replacement chars: {remaining}')
