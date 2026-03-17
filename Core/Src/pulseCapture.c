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

// 循环缓冲区（满了直接覆盖）
static volatile uint32_t s_write_idx = 0U;           // 写索引（中断中修改）
static volatile uint32_t s_read_idx = 0U;        
static bool s_read_lock = false;        
static bool s_write_lock = false;        
static PulseEvent_t s_buffer[PULSE_QUEUE_SIZE];      // 缓冲区数组

static EdgeState_t s_edge_state = EDGE_RISING;
static uint32_t s_last_capture = 0U;
static uint32_t s_last_rise_delta = 0U;
static uint32_t s_total_events = 0U;
static uint32_t s_overflow_cnt = 0U;                  // 定时器溢出次数
static float s_time_resolution = 0.0f;                 // 时间分辨率（秒/计数）
static float s_overflow_time = 0.0f;                    // 单次溢出对应的时间

/* ================= 初始化 ================= */
HAL_StatusTypeDef PulseCapture_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    uint32_t timer_clock;
    uint32_t prescaler;
    float count_freq;
    
    // 严格参数检查
    if (htim == NULL || htim->Instance == NULL || channel != TIM_CHANNEL_3) {
        return HAL_ERROR;
    }
    
    // 保存句柄
    s_htim = htim;
    s_channel = channel;
    
    // 直接计算时间分辨率
    timer_clock = HAL_RCC_GetPCLK1Freq() * 2;           // APB1时钟*2通常为定时器时钟
    prescaler = htim->Init.Prescaler + 1;
    count_freq = (float)timer_clock / (float)prescaler;
    s_time_resolution = 1.0f / count_freq;              // 秒/计数
    
    // 预计算溢出时间（假设定时器是16位，最大值65535）
    s_overflow_time = 65536.0f * s_time_resolution;
    
    s_initialized = 1U;
    return HAL_OK;
}

/* ================= 中断回调（由 it.c 调用） ================= */
void PulseCapture_OnCapture(uint32_t capture_value)
{
    uint32_t delta;
    PulseEvent_t event;
    s_write_lock = true;
    // 计算相对于上次捕获的时间差
    delta = capture_value - s_last_capture;
    s_last_capture = capture_value;
    
    // 填充事件数据
    event.delta_time = delta;
    event.time_seconds = (float)capture_value * s_time_resolution + (float)s_overflow_cnt * s_overflow_time;
    event.level = (s_edge_state == EDGE_RISING) ? 1U : 0U;
    
    // 处理边沿状态
    if (s_edge_state == EDGE_RISING) {
        // 上升沿：记录数据，切换到下降沿
        s_last_rise_delta = delta;
        s_htim->Instance->CCER |= (0x2U << 8);          // CC3P=1，下降沿捕获
        s_edge_state = EDGE_FALLING;
    } else {
        // 下降沿：切换到上升沿
        s_htim->Instance->CCER &= ~(0x2U << 8);         // CC3P=0，上升沿捕获
        s_edge_state = EDGE_RISING;
    }
    
    // 直接写入环形缓冲区（覆盖模式）
     
    s_buffer[s_write_idx & PULSE_QUEUE_MASK] = event;

    s_write_idx++;
    s_total_events++;
    s_overflow_cnt = 0U;
    if(!s_read_lock) s_read_idx = s_write_idx; // 让读取函数直接读取最新事件
    s_write_lock = false;
}

/**
 * @brief 定时器溢出中断回调（由 HAL 或 it.c 调用）
 */
void PulseCapture_OnOverflow(void)
{
    s_overflow_cnt++;
}

/* ================= 读取事件 ================= */
bool PulseCapture_ReadEvent(PulseEvent_t *event)
{
    
    if (event == NULL) {
        return false;
    }
    // 读取事件
    *event = s_buffer[s_write_idx & PULSE_QUEUE_MASK];
    
}


/* ================= 脉宽处理 ================= */
bool PulseCapture_ProcessPulseWidth(PulseWidthResult_t *result)
{
    s_read_lock = true;
    if (s_write_lock || result == NULL || s_buffer[s_read_idx & PULSE_QUEUE_MASK].level == 1U) {
        s_read_lock = false;
        return false;
    }

    // 下降沿：计算脉宽
    result->high_time_seconds = s_buffer[s_read_idx & PULSE_QUEUE_MASK].time_seconds;
    result->period_seconds = s_buffer[(s_read_idx - 1) & PULSE_QUEUE_MASK].time_seconds + result->high_time_seconds;
    s_read_lock = false;
    return true;

}
