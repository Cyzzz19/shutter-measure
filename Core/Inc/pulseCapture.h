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
#define TIMER_PRESCALER         71U

/* ================= 数据结构 ================= */

typedef struct {
    uint32_t delta_time;        /**< 距离上次事件的时间差（单位：us） */
    float    time_seconds;      /**< 浮点时间（秒） */
    uint8_t  level;             /**< 电平状态：1=上升沿，0=下降沿 */
} PulseEvent_t;

typedef struct {
    float    high_time_seconds; /**< 高电平持续时间（秒） */
    float    period_seconds;    /**< 完整周期（秒） */
} PulseWidthResult_t;

typedef struct {
    uint32_t total_events;      /**< 总事件数 */
    uint32_t overflow_cnt;      /**< 定时器溢出次数 */
    float    time_resolution;   /**< 时间分辨率（秒/计数） */
} PulseStats_t;

/* ================= 公共接口 ================= */

HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef PulseCapture_Start(void);
HAL_StatusTypeDef PulseCapture_Stop(void);

/**
 * @brief 中断回调：由 TIM2_IRQHandler 调用
 * @param capture_value CCR3 寄存器值
 */
void PulseCapture_OnCapture(uint32_t capture_value);

/**
 * @brief 定时器溢出中断回调
 */
void PulseCapture_OnOverflow(void);

/**
 * @brief 从队列读取事件（非阻塞）
 * @param event 输出事件指针
 * @retval true 成功 / false 队列为空
 */
bool PulseCapture_ReadEvent(PulseEvent_t *event);

/**
 * @brief 异步处理脉宽测量
 * @param result 输出结果
 * @retval true 获得有效测量 / false 数据不足
 * @note 在主循环或低优先级任务中调用
 */
bool PulseCapture_ProcessPulseWidth(PulseWidthResult_t *result);

/**
 * @brief 获取待处理事件数量
 */
uint32_t PulseCapture_GetPendingCount(void);
void PulseCapture_FlushQueue(void);

/**
 * @brief 获取统计信息
 */
void PulseCapture_GetStats(PulseStats_t *stats);

/**
 * @brief 获取当前等待的边沿类型
 * @retval 1=等待上升沿 / 0=等待下降沿
 */
uint8_t PulseCapture_GetExpectedEdge(void);

/**
 * @brief 获取当前总计数器值
 */
uint32_t PulseCapture_GetCurrentTotalCounter(void);

/**
 * @brief 获取当前时间（秒）
 */
float PulseCapture_GetCurrentTimeSeconds(void);

/**
 * @brief 获取时间分辨率
 */
float PulseCapture_GetTimeResolution(void);

#ifdef __cplusplus
}
#endif

#endif /* __PULSE_CAPTURE_H */