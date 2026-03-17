#include "pulseCapture.h"
#include <string.h>

/* ================= 私有类型 ================= */
typedef enum {
    EDGE_RISING = 0U,
    EDGE_FALLING = 1U
} EdgeState_t;

/* ================= 私有变量 ================= */
static TIM_HandleTypeDef *s_htim = NULL;
static uint32_t s_channel = 0U;
static uint8_t s_initialized = 0U;

// 无锁队列
typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    PulseEventFloat_t buffer[PULSE_QUEUE_SIZE];
} PulseQueueFloat_t;

static PulseQueueFloat_t s_queue = {0U};
static EdgeState_t s_edge_state = EDGE_RISING;
static uint32_t s_last_capture = 0U;
static double s_current_time_sec = 0.0;  // 当前累计时间（秒）
static double s_last_rise_time_sec = 0.0;  // 上次上升沿时间
static uint8_t s_has_rise = 0U;

// 统计信息
static uint32_t s_total_events = 0U;
static uint32_t s_error_count = 0U;
static double s_total_period_sec = 0.0;
static uint32_t s_period_count = 0U;

/* ================= 私有函数 ================= */
static inline bool is_queue_full(void);
static inline bool is_queue_empty(void);
static bool enqueue_event(const PulseEventFloat_t *event);
static void configure_timer_hardware(void);
static double ticks_to_seconds(uint32_t ticks);

/* ================= 工具函数 ================= */
static double ticks_to_seconds(uint32_t ticks)
{
    // 将 tick 数转换为秒
    // 1 tick = 1us = 1e-6 秒
    return (double)ticks * 1.0e-6;
}

/* ================= 初始化 ================= */
HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    if (htim == NULL || htim->Instance == NULL || channel != TIM_CHANNEL_3) {
        return HAL_ERROR;
    }
    
    s_htim = htim;
    s_channel = channel;
    
    // 清零状态
    memset(&s_queue, 0, sizeof(s_queue));
    s_edge_state = EDGE_RISING;
    s_last_capture = htim->Instance->CNT;
    s_current_time_sec = 0.0;
    s_last_rise_time_sec = 0.0;
    s_has_rise = 0U;
    s_total_events = 0U;
    s_error_count = 0U;
    s_total_period_sec = 0.0;
    s_period_count = 0U;
    
    configure_timer_hardware();
    
    s_initialized = 1U;
    return HAL_OK;
}

static void configure_timer_hardware(void)
{

}

HAL_StatusTypeDef PulseCapture_Start(void)
{
    if (!s_initialized || s_htim == NULL) {
        return HAL_ERROR;
    }
    
    s_last_capture = s_htim->Instance->CNT;
    s_current_time_sec = 0.0;
    s_edge_state = EDGE_RISING;
    
    s_htim->Instance->CR1 |= TIM_CR1_CEN;
    
    return HAL_OK;
}

HAL_StatusTypeDef PulseCapture_Stop(void)
{
    if (!s_initialized || s_htim == NULL) {
        return HAL_ERROR;
    }
    
    s_htim->Instance->CR1 &= ~TIM_CR1_CEN;
    return HAL_OK;
}

/* ================= 中断回调 ================= */
void PulseCapture_OnCapture(uint32_t capture_value)
{
    if (!s_initialized || s_htim == NULL) {
        return;
    }
    
    /* ========== 处理累积的溢出 ========== */
    if (s_overflow_count > 0) {
        /* 累加溢出时间：每次溢出 = 0.065536 秒（16位） */
        s_current_time_sec += (double)s_overflow_count * TIMER_OVERFLOW_SECONDS;
        
        /* 清除溢出计数 */
        s_overflow_count = 0U;
    }
    
    /* ========== 计算 Delta（16位模式） ========== */
    uint32_t delta_ticks;
    uint16_t current = (uint16_t)(capture_value & 0xFFFFU);
    uint16_t last = (uint16_t)(s_last_capture & 0xFFFFU);
    
    if (current < last) {
        /* 检测到回绕（理论上不应该发生，因为UIF已处理） */
        delta_ticks = (0xFFFFU - last) + current + 1U;
        s_current_time_sec += TIMER_OVERFLOW_SECONDS;
    } else {
        delta_ticks = current - last;
    }
    
    s_last_capture = current;
    
    /* ========== 更新时间 ========== */
    double delta_sec = ticks_to_seconds(delta_ticks);
    s_current_time_sec += delta_sec;
    
    /* ========== 创建事件 ========== */
    PulseEventFloat_t event;
    event.timestamp_sec = s_current_time_sec;
    event.delta_sec = (float)delta_sec;
    
    if (s_edge_state == EDGE_RISING) {
        event.level = 1U;
        s_last_rise_time_sec = s_current_time_sec;
        s_has_rise = 1U;
        s_htim->Instance->CCER |= (0x2U << 8);  /* 下降沿 */
        s_edge_state = EDGE_FALLING;
    } else {
        event.level = 0U;
        s_htim->Instance->CCER &= ~(0x2U << 8);  /* 上升沿 */
        s_edge_state = EDGE_RISING;
    }
    
    /* 入队 */
    if (!enqueue_event(&event)) {
        s_error_count++;
    }
    
    s_total_events++;
}

/* ================= 队列操作 ================= */
static inline bool is_queue_full(void)
{
    return ((s_queue.head - s_queue.tail) >= PULSE_QUEUE_SIZE);
}

static inline bool is_queue_empty(void)
{
    return (s_queue.head == s_queue.tail);
}

static bool enqueue_event(const PulseEventFloat_t *event)
{
    if (event == NULL || is_queue_full()) {
        return false;
    }
    
    uint32_t head = s_queue.head;
    s_queue.buffer[head & PULSE_QUEUE_MASK] = *event;
    __DMB();
    s_queue.head = head + 1U;
    
    return true;
}

bool PulseCapture_ReadEventFloat(PulseEventFloat_t *event)
{
    if (event == NULL || is_queue_empty()) {
        return false;
    }
    
    uint32_t tail = s_queue.tail;
    *event = s_queue.buffer[tail & PULSE_QUEUE_MASK];
    __DMB();
    s_queue.tail = tail + 1U;
    
    return true;
}

/* ================= 脉宽处理 ================= */
bool PulseCapture_ProcessPulseWidthFloat(PulseWidthResultFloat_t *result)
{
    if (result == NULL) {
        return false;
    }
    
    PulseEventFloat_t event;
    
    if (!PulseCapture_ReadEventFloat(&event)) {
        return false;
    }
    
    if (event.level == 1U) {
        // 上升沿
        return false;
    }
    
    // 下降沿：计算脉宽
    if (s_has_rise) {
        double high_time = event.timestamp_sec - s_last_rise_time_sec;
        double period = high_time + event.delta_sec;
        
        result->high_time_sec = high_time;
        result->period_sec = period;
        result->frequency_hz = (float)(1.0 / period);
        result->duty_cycle = (float)(high_time / period * 100.0);
        result->is_valid = 1U;
        
        s_has_rise = 0U;
        return true;
    }
    
    return false;
}

/* ================= 统计信息 ================= */
double PulseCapture_GetCurrentTimeSec(void)
{
    if (!s_initialized || s_htim == NULL) {
        return 0.0;
    }
    
    /* 禁用中断，保证原子性 */
    __disable_irq();
    
    /* 读取当前计数值 */
    uint16_t cnt = (uint16_t)(s_htim->Instance->CNT & 0xFFFFU);
    uint16_t last = (uint16_t)(s_last_capture & 0xFFFFU);
    
    /* 计算当前时间 */
    double time = s_current_time_sec;
    
    /* 加上未处理的溢出 */
    if (s_overflow_count > 0) {
        time += (double)s_overflow_count * TIMER_OVERFLOW_SECONDS;
    }
    
    /* 加上当前计数 */
    if (cnt < last) {
        time += TIMER_OVERFLOW_SECONDS + ticks_to_seconds(cnt);
    } else {
        time += ticks_to_seconds(cnt - last);
    }
    
    __enable_irq();
    
    return time;
}

void PulseCapture_GetStatsFloat(PulseStatsFloat_t *stats)
{
    if (stats == NULL) {
        return;
    }
    
    stats->total_events = s_total_events;
    stats->error_count = s_error_count;
    stats->total_time_sec = s_current_time_sec;
    
    if (s_period_count > 0) {
        stats->avg_period_sec = s_total_period_sec / s_period_count;
    } else {
        stats->avg_period_sec = 0.0;
    }
}

uint32_t PulseCapture_GetPendingCount(void)
{
    return s_queue.head - s_queue.tail;
}

void PulseCapture_FlushQueue(void)
{
    s_queue.head = s_queue.tail;
    __DMB();
}