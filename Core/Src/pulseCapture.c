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
static uint32_t s_error_count = 0U;
static uint32_t s_overflow_cnt = 0U;

/* ================= 私有函数 ================= */
static inline bool is_queue_full(void);
static inline bool is_queue_empty(void);
static bool enqueue_event(const PulseEvent_t *event);
static void configure_timer_hardware(void);

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
    
    // 清零所有状态
    memset(&s_queue, 0, sizeof(s_queue));
    s_edge_state = EDGE_RISING;
    s_last_capture = htim->Instance->CNT;
    s_last_rise_delta = 0U;
    s_has_rise = 0U;
    s_total_events = 0U;
    s_error_count = 0U;
    s_overflow_cnt = 0U;
    
    // 配置硬件
    configure_timer_hardware();
    
    s_initialized = 1U;
    return HAL_OK;
}

static void configure_timer_hardware(void)
{
    // 1. 停止定时器
    s_htim->Instance->CR1 &= ~TIM_CR1_CEN;
    
    // 2. 预分频：60MHz / 60 = 1MHz (1us/tick)
    s_htim->Instance->PSC = TIMER_PRESCALER;
    
    // 3. 32位自由运行计数器
    s_htim->Instance->ARR = TIMER_MAX_TICKS;
    s_htim->Instance->CNT = 0U;
    
    // 4. 配置 CCMR2：通道3输入捕获模式
    // CC3S = 01 (IC3映射到TI3), IC3F = 0000 (无滤波，可后续添加)
    s_htim->Instance->CCMR2 &= ~(0x3U << 4);
    s_htim->Instance->CCMR2 |= (0x1U << 4);
    
    // 5. 配置 CCER：初始捕获上升沿，使能通道3
    s_htim->Instance->CCER &= ~(0x3U << 8);  // 清除 CC3P 和 CC3E
    s_htim->Instance->CCER |= (0x1U << 8);   // CC3E=1, CC3P=0(上升沿)
    
    // 6. 清除所有标志位
    s_htim->Instance->SR = 0U;
    
    // 7. 使能捕获中断（只使能 CC3IE）
    s_htim->Instance->DIER &= ~(TIM_DIER_UIE | TIM_DIER_CC1IE | 
                                TIM_DIER_CC2IE | TIM_DIER_CC4IE);
    s_htim->Instance->DIER |= TIM_DIER_CC3IE;
    
    // 8. NVIC 配置（RTOS 环境下关键！）
    // 优先级数值越大，优先级越低
    // 必须 >= configMAX_SYSCALL_INTERRUPT_PRIORITY
    NVIC_SetPriority(TIM2_IRQn, 6U);
    NVIC_EnableIRQ(TIM2_IRQn);
}

/* ================= 启动/停止 ================= */
HAL_StatusTypeDef PulseCapture_Start(void)
{
    if (!s_initialized || s_htim == NULL) {
        return HAL_ERROR;
    }
    
    // 同步当前计数值
    s_last_capture = s_htim->Instance->CNT;
    s_edge_state = EDGE_RISING;
    
    // 启动定时器
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
void PulseCapture_OnCapture(uint32_t capture_value)
{
    // 安全检查：防止未初始化调用
    if (!s_initialized || s_htim == NULL || s_htim->Instance == NULL) {
        return;
    }
    
    // 计算时间差（自动处理 32 位溢出）
    uint32_t delta = capture_value - s_last_capture;
    s_last_capture = capture_value;
    
    // 创建事件
    PulseEvent_t event;
    event.delta_time = delta;
    
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
    
    // 无锁入队
    if (!enqueue_event(&event)) {
        s_error_count++;  // 队列满，丢弃事件
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
    stats->error_count = s_error_count;
    stats->overflow_cnt = s_overflow_cnt;
}

uint8_t PulseCapture_GetExpectedEdge(void)
{
    return (s_edge_state == EDGE_RISING) ? 1U : 0U;
}