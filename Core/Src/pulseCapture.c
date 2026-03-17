#include "pulseCapture.h"
#include <string.h>

/* ================= 私有变量 ================= */
static TIM_HandleTypeDef *s_htim = NULL;
static uint8_t s_initialized = 0U;

// 溢出计数（由中断处理）
volatile uint32_t s_overflow_count = 0U;

// 队列
typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    PulseEventFloat_t buffer[PULSE_QUEUE_SIZE];
} PulseQueueFloat_t;

static PulseQueueFloat_t s_queue = {0U};

// 状态
static uint16_t s_last_capture = 0U;  // 上次捕获值（16位）
static uint8_t s_edge_state = 0U;     // 0=上升沿，1=下降沿
static double s_current_time_sec = 0.0;
static double s_last_rise_time_sec = 0.0;
static uint8_t s_has_rise = 0U;

// 统计
static uint32_t s_total_events = 0U;
static uint32_t s_error_count = 0U;

/* ================= 初始化 ================= */
HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    if (htim == NULL || htim->Instance == NULL || channel != TIM_CHANNEL_3) {
        return HAL_ERROR;
    }
    
    s_htim = htim;
    
   
    s_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef PulseCapture_Start(void)
{
    if (!s_initialized || s_htim == NULL) {
        return HAL_ERROR;
    }
    
    s_last_capture = (uint16_t)(s_htim->Instance->CNT & 0xFFFFU);
    s_current_time_sec = 0.0;
    s_overflow_count = 0U;
    s_edge_state = 0U;
    
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

/* ================= 中断回调（由 it.c 调用） ================= */
void PulseCapture_OnCapture(uint16_t capture_value)
{
    if (!s_initialized || s_htim == NULL) {
        return;
    }
    
    /* ========== 处理累积的溢出 ========== */
    if (s_overflow_count > 0) {
        // 浮点计算：溢出次数 × 0.065536秒
        s_current_time_sec += (double)s_overflow_count * TIMER_OVERFLOW_SEC;
        s_overflow_count = 0U;
    }
    
    /* ========== 计算 Delta（浮点） ========== */
    uint16_t current = capture_value;
    uint16_t last = s_last_capture;
    
    float delta_sec;
    
    if (current >= last) {
        // 正常情况：直接相减
        uint16_t delta_ticks = current - last;
        delta_sec = (float)delta_ticks * TIMER_TICK_US;  // 浮点转换
    } else {
        // 回绕情况（理论上不应该，因为UIF已处理）
        uint16_t delta_ticks = (TIMER_16BIT_MAX - last) + current;
        delta_sec = (float)delta_ticks * TIMER_TICK_US;
        s_current_time_sec += TIMER_OVERFLOW_SEC;
    }
    
    s_last_capture = current;
    s_current_time_sec += (double)delta_sec;
    
    /* ========== 创建事件 ========== */
    PulseEventFloat_t event;
    event.timestamp_sec = s_current_time_sec;
    event.delta_sec = delta_sec;
    
    if (s_edge_state == 0U) {
        // 上升沿
        event.level = 1U;
        s_last_rise_time_sec = s_current_time_sec;
        s_has_rise = 1U;
        
        // 切换到下降沿
        s_htim->Instance->CCER |= (0x2U << 8);
        s_edge_state = 1U;
    } else {
        // 下降沿
        event.level = 0U;
        
        // 切换到上升沿
        s_htim->Instance->CCER &= ~(0x2U << 8);
        s_edge_state = 0U;
    }
    
    /* ========== 入队 ========== */
    uint32_t head = s_queue.head;
    if ((head - s_queue.tail) < PULSE_QUEUE_SIZE) {
        s_queue.buffer[head & PULSE_QUEUE_MASK] = event;
        __DMB();
        s_queue.head = head + 1U;
        s_total_events++;
    } else {
        s_error_count++;
    }
}

/* ================= 队列操作 ================= */
bool PulseCapture_ReadEventFloat(PulseEventFloat_t *event)
{
    if (event == NULL || s_queue.head == s_queue.tail) {
        return false;
    }
    
    uint32_t tail = s_queue.tail;
    *event = s_queue.buffer[tail & PULSE_QUEUE_MASK];
    __DMB();
    s_queue.tail = tail + 1U;
    
    return true;
}

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
        // 上升沿，等待下降沿
        return false;
    }
    
    // 下降沿：计算脉宽
    if (s_has_rise) {
        double high_time = s_last_rise_time_sec - (s_current_time_sec - (double)event.delta_sec);
        double period = (double)event.delta_sec;
        
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

double PulseCapture_GetCurrentTimeSec(void)
{
    if (!s_initialized || s_htim == NULL) {
        return 0.0;
    }
    
    __disable_irq();
    
    uint16_t cnt = (uint16_t)(s_htim->Instance->CNT & 0xFFFFU);
    uint16_t last = s_last_capture;
    
    double time = s_current_time_sec;
    
    // 加上未处理的溢出
    if (s_overflow_count > 0) {
        time += (double)s_overflow_count * TIMER_OVERFLOW_SEC;
    }
    
    // 加上当前计数
    if (cnt >= last) {
        uint16_t delta = cnt - last;
        time += (double)delta * TIMER_TICK_US;
    } else {
        uint16_t delta = (TIMER_16BIT_MAX - last) + cnt;
        time += (double)delta * TIMER_TICK_US;
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
    stats->overflow_count = s_overflow_count;
    stats->total_time_sec = s_current_time_sec;
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