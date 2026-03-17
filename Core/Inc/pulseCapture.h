#ifndef __PULSE_CAPTURE_H
#define __PULSE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ================= 配置参数 ================= */
#define PULSE_QUEUE_SIZE        64U
#define PULSE_QUEUE_MASK        (PULSE_QUEUE_SIZE - 1U)
#define TIMER_TICK_HZ           1000000U  // 1MHz = 1us/tick
#define TIMER_OVERFLOW_SECONDS  0.065536  // 2^32 / 1MHz

/* ================= 数据结构 ================= */

/**
 * @brief 捕获事件（浮点时间戳）
 */
typedef struct {
    double timestamp_sec;   // 绝对时间戳（秒）
    float delta_sec;        // 相对上次事件的时间（秒）
    uint8_t level;          // 电平：1=上升沿，0=下降沿
    uint8_t reserved[3];
} PulseEventFloat_t;

/**
 * @brief 脉宽测量结果（秒为单位）
 */
typedef struct {
    double high_time_sec;   // 高电平持续时间（秒）
    double period_sec;      // 完整周期（秒）
    float frequency_hz;     // 频率（Hz）
    float duty_cycle;       // 占空比（%）
    uint8_t is_valid;
    uint8_t reserved[3];
} PulseWidthResultFloat_t;

/**
 * @brief 统计信息
 */
typedef struct {
    uint32_t total_events;
    uint32_t error_count;
    double total_time_sec;      // 总测量时间
    double avg_period_sec;      // 平均周期
} PulseStatsFloat_t;

volatile uint32_t s_overflow_count = 0U;  // 溢出计数器

/* ================= 公共接口 ================= */

HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef PulseCapture_Start(void);
HAL_StatusTypeDef PulseCapture_Stop(void);
void PulseCapture_OnCapture(uint32_t capture_value);

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