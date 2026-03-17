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
static volatile uint32_t s_read_idx = 0U;            // 读索引（主循环中修改）
static PulseEvent_t s_buffer[PULSE_QUEUE_SIZE];      // 缓冲区数组

static EdgeState_t s_edge_state = EDGE_RISING;
static uint32_t s_last_capture = 0U;
static uint32_t s_last_rise_delta = 0U;
static uint8_t s_has_rise = 0U;
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
    uint32_t write_idx;
    uint32_t next_idx;
    
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
        s_has_rise = 1U;
        s_htim->Instance->CCER |= (0x2U << 8);          // CC3P=1，下降沿捕获
        s_edge_state = EDGE_FALLING;
    } else {
        // 下降沿：切换到上升沿
        s_htim->Instance->CCER &= ~(0x2U << 8);         // CC3P=0，上升沿捕获
        s_edge_state = EDGE_RISING;
    }
    
    // 直接写入环形缓冲区（覆盖模式）
    write_idx = s_write_idx;
    s_buffer[write_idx & PULSE_QUEUE_MASK] = event;
    
    // 更新写指针，如果追上读指针则读指针+1（覆盖最旧数据）
    next_idx = write_idx + 1U;
    if ((next_idx - s_read_idx) >= PULSE_QUEUE_SIZE) {
        // 缓冲区满了，需要覆盖，读指针向前移动
        s_read_idx = next_idx - PULSE_QUEUE_SIZE + 1U;
    }
    
    // 内存屏障，确保数据写入完成后再更新写指针
    __DMB();
    s_write_idx = next_idx;
    
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

/* ================= 读取事件 ================= */
bool PulseCapture_ReadEvent(PulseEvent_t *event)
{
    uint32_t read_idx;
    uint32_t write_idx;
    
    if (event == NULL) {
        return false;
    }
    
    // 检查队列是否为空（直接比较索引）
    write_idx = s_write_idx;
    read_idx = s_read_idx;
    
    if (read_idx == write_idx) {
        return false;                                   // 队列为空
    }
    
    // 读取事件
    *event = s_buffer[read_idx & PULSE_QUEUE_MASK];
    
    // 内存屏障，确保读取完成后再更新读指针
    __DMB();
    s_read_idx = read_idx + 1U;
    
}


/* ================= 脉宽处理 ================= */
bool PulseCapture_ProcessPulseWidth(PulseWidthResult_t *result)
{
    PulseEvent_t event;
    uint32_t read_idx;
    uint32_t write_idx;
    
    if (result == NULL) {
        return false;
    }
    
    // 直接检查队列是否为空
    write_idx = s_write_idx;
    read_idx = s_read_idx;
    
    if (read_idx == write_idx) {
        return false;                                   // 队列为空
    }
    
    // 读取事件
    event = s_buffer[read_idx & PULSE_QUEUE_MASK];
    
    // 内存屏障，确保读取完成后再更新读指针
    __DMB();
    s_read_idx = read_idx + 1U;
    
    // 如果是上升沿，只记录不处理
    if (event.level == 1U) {
        return false;
    }
    
    // 下降沿：计算脉宽
    if (s_has_rise) {
        result->high_time_seconds = event.time_seconds;
        event = s_buffer[(read_idx & PULSE_QUEUE_MASK)?(read_idx & PULSE_QUEUE_MASK) - 1:0];
        result->period_seconds = event.time_seconds + result->high_time_seconds;
        s_has_rise = 0U;
        return true;
    }
    
    return false;
}
