#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// --- 配置宏 (请根据实际 MCU 验证 TIM2_CLK) ---
#define TIM2_CLK          72000000UL  // 假设 TIM2 计数器时钟为 72MHz
#define QUEUE_SIZE        64          // 队列深度，需为 2 的幂以便快速取模
#define QUEUE_MASK        (QUEUE_SIZE - 1)

// --- 事件数据结构 ---
typedef struct {
    uint32_t delta_time;  // 距离上一次边沿的时间 ticks
    uint8_t  level;       // 当前电平状态：1=Rising, 0=Falling
    uint8_t  reserved;    // 对齐填充
} CaptureEvent_t;

// --- 无锁环形队列 (Single Producer Single Consumer) ---
typedef struct {
    CaptureEvent_t buffer[QUEUE_SIZE];
    volatile uint32_t head;  // ISR 写索引
    volatile uint32_t tail;  // Task 读索引
} CaptureQueue_t;

static CaptureQueue_t g_capture_queue;

// --- 脉冲测量状态机 ---
typedef enum {
    STATE_IDLE,
    STATE_WAITING_FALLING,
    STATE_ERROR_OVERFLOW
} PulseMeasureState_t;

static volatile PulseMeasureState_t g_pulse_state = STATE_IDLE;
static uint32_t g_rising_timestamp = 0;