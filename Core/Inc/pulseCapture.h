#ifndef __PULSE_CAPTURE_H
#define __PULSE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ================= 配置参数 ================= */
#define PULSE_QUEUE_SIZE        64U
#define PULSE_QUEUE_MASK        (PULSE_QUEUE_SIZE - 1U)
#define TIMER_PRESCALER         59U         // 60MHz / 60 = 1MHz
#define TIMER_TICK_US           1.0e-6      // 1 tick = 1us = 1e-6秒
#define TIMER_16BIT_MAX         65536U      // 2^16
#define TIMER_OVERFLOW_SEC      0.065536    // 65536 * 1us = 65.536ms

/* ================= 数据结构 ================= */

typedef struct {
    double timestamp_sec;   // 绝对时间戳（秒）
    float delta_sec;        // 相对时间差（秒）
    uint8_t level;          // 1=上升沿，0=下降沿
    uint8_t reserved[3];
} PulseEventFloat_t;

typedef struct {
    double high_time_sec;   // 高电平时间（秒）
    double period_sec;      // 周期（秒）
    float frequency_hz;     // 频率（Hz）
    float duty_cycle;       // 占空比（%）
    uint8_t is_valid;
    uint8_t reserved[3];
} PulseWidthResultFloat_t;

typedef struct {
    uint32_t total_events;
    uint32_t error_count;
    uint32_t overflow_count;
    double total_time_sec;
} PulseStatsFloat_t;

/* ================= 公共接口 ================= */

HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef PulseCapture_Start(void);
HAL_StatusTypeDef PulseCapture_Stop(void);
void PulseCapture_OnCapture(uint16_t capture_value);  // ← 16位参数

bool PulseCapture_ReadEventFloat(PulseEventFloat_t *event);
bool PulseCapture_ProcessPulseWidthFloat(PulseWidthResultFloat_t *result);
double PulseCapture_GetCurrentTimeSec(void);
void PulseCapture_GetStatsFloat(PulseStatsFloat_t *stats);

uint32_t PulseCapture_GetPendingCount(void);
void PulseCapture_FlushQueue(void);

#ifdef __cplusplus
}
#endif

#endif /* __PULSE_CAPTURE_H */