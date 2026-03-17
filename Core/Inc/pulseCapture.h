#ifndef __PULSE_CAPTURE_H
#define __PULSE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ================= 配置参数 ================= */
#define PULSE_QUEUE_SIZE        64U         // 队列深度（必须是2的幂）
#define PULSE_QUEUE_MASK        (PULSE_QUEUE_SIZE - 1U)
#define TIMER_PRESCALER         71U         // 60MHz / 60 = 1MHz (1us/tick)
#define TIMER_MAX_TICKS         0xFFFFFFFFU // 32位计数器

/* ================= 数据结构 ================= */

/**
 * @brief 捕获事件：中断中生成，主循环中消费
 */
typedef struct {
    uint32_t delta_time;    /**< 距离上次事件的时间差（单位：us） */
    uint8_t  level;         /**< 电平状态：1=上升沿，0=下降沿 */
    uint8_t  reserved[3];   /**< 4字节对齐 */
} PulseEvent_t;

/**
 * @brief 脉宽测量结果
 */
typedef struct {
    uint32_t high_time_us;  /**< 高电平持续时间（us） */
    uint32_t period_us;     /**< 完整周期（us） */
    uint8_t  is_valid;      /**< 数据有效标志 */
    uint8_t  reserved[3];
} PulseWidthResult_t;

/**
 * @brief 统计信息
 */
typedef struct {
    uint32_t total_events;  /**< 总事件数 */
    uint32_t error_count;   /**< 错误计数（队列满等） */
    uint32_t overflow_cnt;  /**< 定时器溢出次数 */
} PulseStats_t;

/* ================= 公共接口 ================= */

/**
 * @brief 初始化脉冲捕获模块
 * @param htim TIM2句柄（必须已配置）
 * @param channel 捕获通道（必须为 TIM_CHANNEL_3）
 * @retval HAL_OK 成功 / HAL_ERROR 失败
 */
HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel);

/**
 * @brief 启动捕获
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef PulseCapture_Start(void);

/**
 * @brief 停止捕获
 * @retval HAL_OK 成功
 */
HAL_StatusTypeDef PulseCapture_Stop(void);

/**
 * @brief 中断回调：由 TIM2_IRQHandler 调用
 * @param capture_value CCR3 寄存器值
 */
void PulseCapture_OnCapture(uint32_t capture_value);

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

/**
 * @brief 清空队列
 */
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

#ifdef __cplusplus
}
#endif

#endif /* __PULSE_CAPTURE_H */