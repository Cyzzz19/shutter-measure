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
#define TIMER_PRESCALER         59U
#define TIMER_TICK_US           1.0e-6f
#define TIMER_16BIT_OVERFLOW_US 65536.0f  // 65.536ms

/* ================= 数据结构 ================= */

typedef struct {
    double timestamp_sec;
    float delta_sec;
    uint8_t level;
    uint8_t reserved[3];
} PulseEventFloat_t;

typedef struct {
    double high_time_sec;
    double period_sec;
    float frequency_hz;
    float duty_cycle;
    uint8_t is_valid;
    uint8_t reserved[3];
} PulseWidthResultFloat_t;

typedef struct {
    uint32_t total_events;
    uint32_t error_count;
    uint32_t overflow_count;
} PulseStatsFloat_t;

/* ================= 公共接口 ================= */

HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef PulseCapture_Start(void);
HAL_StatusTypeDef PulseCapture_Stop(void);

/* HAL库回调函数（在it.c或main.c中调用） */
void PulseCapture_IC_CaptureCallback(void);
void PulseCapture_PeriodElapsedCallback(void);

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