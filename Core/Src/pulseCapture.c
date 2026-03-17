#include "pulseCapture.h"
#include <string.h>
#include <math.h>

/* ================= 私有类型 ================= */
typedef enum {
    EDGE_RISING = 0U,
    EDGE_FALLING = 1U
} EdgeState_t;

/* ================= 私有变量 ================= */
static TIM_HandleTypeDef *s_htim = NULL;
static uint32_t s_channel = 0U;
static uint8_t s_initialized = 0U;

// 无锁环形缓冲区（单生产者-单消费者）
typedef struct {
    volatile uint32_t head;  // 写指针（中断中修改）
    volatile uint32_t tail;  // 读指针（主循环中修改）
    PulseEvent_t buffer[PULSE_QUEUE_SIZE];
} PulseQueue_t;

static PulseQueue_t s_queue = {0U};
static EdgeState_t s_edge_state = EDGE_RISING;
static uint32_t s_last_capture = 0U;
static uint32_t s_last_rise_delta = 0U;
static uint8_t s_has_rise = 0U;
static uint32_t s_total_events = 0U;
static uint32_t s_overflow_cnt = 0U;      // 定时器溢出次数
static float s_time_resolution = 0.0f;     // 时间分辨率（秒/计数）

/* ================= 私有函数 ================= */
static inline bool is_queue_full(void);
static inline bool is_queue_empty(void);
static bool enqueue_event(const PulseEvent_t *event);
static float calculate_time_from_counter(uint32_t total_counter);

/* ================= 初始化 ================= */
HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    // 严格参数检查
    if (htim == NULL || htim->Instance == NULL || channel != TIM_CHANNEL_3) {
        return HAL_ERROR;
    }
    
    // 保存句柄
    s_htim = htim;
    s_channel = channel;
    
    // 计算时间分辨率（假设定时器时钟为 60MHz）
    // 定时器计数频率 = 60MHz / (prescaler + 1)
    // 这里预设 prescaler = 71，得到 1MHz (1us/计数)
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq() * 2;  // APB1时钟*2通常为定时器时钟
    uint32_t prescaler = htim->Init.Prescaler + 1;
    float count_freq = (float)timer_clock / (float)prescaler;
    s_time_resolution = 1.0f / count_freq;  // 秒/计数
    
    s_initialized = 1U;
    return HAL_OK;
}


/* ================= 中断回调（由 it.c 调用） ================= */
void PulseCapture_OnCapture(uint32_t capture_value)
{
    // 计算相对于上次捕获的时间差（用于脉宽测量）
    uint32_t delta = capture_value - s_last_capture;
    s_last_capture = capture_value;
    
    // 创建事件
    PulseEvent_t event;
    event.delta_time = delta;
    event.time_seconds = capture_value * s_time_resolution + s_overflow_cnt * 65536 * s_time_resolution;
    
    if (s_edge_state == EDGE_RISING) {
        // ============ 上升沿 ============
        event.level = 1U;
        s_last_rise_delta = delta;
        s_has_rise = 1U;
        
        // 切换到下降沿捕获
        s_htim->Instance->CCER |= (0x2U << 8);  // CC3P=1
        s_edge_state = EDGE_FALLING;
        
    } else {
        // ============ 下降沿 ============
        event.level = 0U;
        
        // 切换到上升沿捕获
        s_htim->Instance->CCER &= ~(0x2U << 8);  // CC3P=0
        s_edge_state = EDGE_RISING;
    }
    s_total_events++;
    s_overflow_cnt = 0U;
}

/**
 * @brief 定时器溢出中断回调（由 HAL 或 it.c 调用）
 */
void PulseCapture_OnOverflow(void)
{
    s_overflow_cnt++;
}


/**
 * @brief 获取当前总计数器值（不触发捕获）
 */
uint32_t PulseCapture_GetCurrentTotalCounter(void)
{
    if (!s_initialized || s_htim == NULL) {
        return 0U;
    }
    
    uint32_t current_cnt = s_htim->Instance->CNT;
    return calculate_total_counter(current_cnt);
}

/**
 * @brief 获取当前时间（秒）
 */
float PulseCapture_GetCurrentTimeSeconds(void)
{
    uint32_t total_counter = PulseCapture_GetCurrentTotalCounter();
    return calculate_time_from_counter(total_counter);
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

static bool enqueue_event(const PulseEvent_t *event)
{
    if (event == NULL || is_queue_full()) {
        return false;
    }
    
    uint32_t head = s_queue.head;
    s_queue.buffer[head & PULSE_QUEUE_MASK] = *event;
    
    // 内存屏障：确保写操作完成后再更新指针
    __DMB();
    s_queue.head = head + 1U;
    
    return true;
}

bool PulseCapture_ReadEvent(PulseEvent_t *event)
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

uint32_t PulseCapture_GetPendingCount(void)
{
    return s_queue.head - s_queue.tail;
}

void PulseCapture_FlushQueue(void)
{
    s_queue.head = s_queue.tail;
    __DMB();
}

/* ================= 脉宽处理 ================= */
bool PulseCapture_ProcessPulseWidth(PulseWidthResult_t *result)
{
    if (result == NULL) {
        return false;
    }
    
    PulseEvent_t event;
    
    // 读取事件
    if (!PulseCapture_ReadEvent(&event)) {
        return false;
    }
    
    if (event.level == 1U) {
        // 上升沿：记录时间，等待下降沿
        return false;
    }
    
    // 下降沿：计算脉宽
    if (s_has_rise) {
        result->high_time_us = s_last_rise_delta;
        result->period_us = s_last_rise_delta + event.delta_time;
        result->high_time_seconds = calculate_time_from_counter(s_last_rise_delta);
        result->period_seconds = calculate_time_from_counter(s_last_rise_delta + event.delta_time);
        result->capture_time = event.time_seconds;  // 捕获时刻的时间
        result->is_valid = 1U;
        s_has_rise = 0U;  // 重置标志
        return true;
    }
    
    // 无效序列（下降沿前没有上升沿）
    return false;
}

/* ================= 统计信息 ================= */
void PulseCapture_GetStats(PulseStats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    
    stats->total_events = s_total_events;
    stats->overflow_cnt = s_overflow_cnt;
    stats->time_resolution = s_time_resolution;
}

uint8_t PulseCapture_GetExpectedEdge(void)
{
    return (s_edge_state == EDGE_RISING) ? 1U : 0U;
}

float PulseCapture_GetTimeResolution(void)
{
    return s_time_resolution;
}