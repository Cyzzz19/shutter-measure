/**
 * @file    ui_core.h
 * @brief   轻量级嵌入式 UI 框架核心 - 对象池 + 局部刷新 + 输入绑定
 * @details 专为 STM32F103C8 + 128x64 OLED 设计
 *          - 配置数据存 Flash (const)，状态数据存 RAM (对象池)
 *          - 局部刷新避免全帧传输卡顿
 *          - 运行时绑定 GPIO，支持硬件抽象
 *          - 三键独立状态机，支持长按/双击/组合键
 * 
 * @note    SRAM 占用：~2KB (对象池 + 显存 + 系统上下文)
 *          Flash 占用：~4KB (代码 + 常量配置)
 */

#ifndef __UI_CORE_H
#define __UI_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 依赖包含 ================= */
#include "stm32f1xx_hal.h"
#include "oled.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ================= 编译配置宏 ================= */
/* 在 ui_core.c 或项目配置中定义这些宏 */
#ifndef UI_POOL_SIZE
    #define UI_POOL_SIZE        32      /* 对象池容量 (~448B @14B/elem) */
#endif
#ifndef UI_MAX_SCREEN_ELEMS
    #define UI_MAX_SCREEN_ELEMS 12      /* 单屏最大元素数 */
#endif
#ifndef UI_DIRTY_QUEUE_LEN
    #define UI_DIRTY_QUEUE_LEN  4       /* 脏区域队列长度 */
#endif
#ifndef UI_ENABLE_INPUT_PROTECTION
    /* #define UI_ENABLE_INPUT_PROTECTION */  /* 调试时取消注释 */
#endif
#ifndef UI_USE_RTOS
    /* #define UI_USE_RTOS */               /* 使用 FreeRTOS 时取消注释 */
#endif

/* ================= 基础类型 ================= */
typedef uint8_t  ui_coord_t;   /* 坐标类型：0-127 (x), 0-63 (y) */
typedef uint8_t  ui_size_t;    /* 尺寸类型：0-128 */
typedef uint8_t  ui_state_t;   /* 状态掩码类型 */

/* ================= 状态位掩码 ================= */
/* 8 个状态仅占 1 字节，而非 8 个 bool 占 8 字节 */
typedef enum {
    UI_STATE_NORMAL    = 0x00,
    UI_STATE_HIGHLIGHT = 0x01,  /* 高亮/选中 */
    UI_STATE_DISABLED  = 0x02,  /* 禁用 (不响应事件，视觉灰显) */
    UI_STATE_DIRTY     = 0x04,  /* 需要重绘 (局部刷新关键) */
    UI_STATE_PRESSED   = 0x08,  /* 按下动画态 */
    UI_STATE_HIDDEN    = 0x10,  /* 隐藏 (不渲染不响应) */
    UI_STATE_FOCUS     = 0x20,  /* 键盘焦点 */
    UI_STATE_ANIMATING = 0x40,  /* 动画进行中 */
} ui_state_mask_t;

/* ================= 几何矩形 ================= */
typedef struct __packed {
    ui_coord_t x;
    ui_coord_t y;
    ui_size_t  w;
    ui_size_t  h;
} ui_rect_t;

/* ================= 输入事件 ================= */
typedef enum {
    EVT_NONE         = 0,
    EVT_UP           = 1,   /* 短上拨 (<1000ms) */
    EVT_DOWN         = 2,   /* 短下拨 */
    EVT_PRESS        = 3,   /* 短确认 */
    EVT_LONG_UP      = 4,   /* 长上拨 (>=1000ms) */
    EVT_LONG_DOWN    = 5,   /* 长下拨 */
    EVT_LONG_PRESS   = 6,   /* 长确认 */
    EVT_DOUBLE_CLICK = 7,   /* 双击 (<300ms 间隔) */
    /* 内部事件 */
    EVT_TICK         = 0xFE, /* 时间片心跳 */
    EVT_REFRESH      = 0xFF, /* 强制刷新请求 */
} ui_event_code_t;

/* ================= 输入配置 ================= */
/* 单键配置 */
typedef struct {
    GPIO_TypeDef *port;       /* GPIO 端口：GPIOA, GPIOB... */
    uint16_t     pin;         /* GPIO 引脚：GPIO_PIN_0 ~ GPIO_PIN_15 */
    bool         active_low;  /* 有效电平：true=低电平有效 (上拉输入) */
} ui_button_cfg_t;

/* 三键绑定配置 */
typedef struct {
    ui_button_cfg_t up;       /* 上拨/上一项 */
    ui_button_cfg_t down;     /* 下拨/下一项 */
    ui_button_cfg_t press;    /* 确认/选择 */
} ui_input_binding_t;

/* 输入阈值配置 (可调整) */
typedef struct {
    uint16_t long_press_threshold_ms;   /* 默认 1000ms */
    uint16_t double_click_gap_ms;       /* 默认 300ms */
    uint16_t debounce_ms;               /* 默认 20ms */
} ui_input_cfg_t;

/* ================= 输入状态机 ================= */
/* 单键状态上下文 */
typedef struct {
    uint32_t last_press_time;
    uint32_t last_release_time;
    uint8_t  state;           /* 0:IDLE, 1:PRESSED, 2:LONG_PRESSED */
    uint8_t  click_count;     /* 双击计数 */
} ui_key_state_t;

/* 输入上下文 (~40 字节 RAM) */
typedef struct {
    ui_key_state_t up;
    ui_key_state_t down;
    ui_key_state_t press;
    ui_event_code_t pending_evt;
    bool evt_ready;
} ui_input_ctx_t;

/* ================= 元素回调 ================= */
struct ui_element_s;

typedef bool (*ui_render_fn)(struct ui_element_s *self, uint8_t *fb, const ui_rect_t *clip);
typedef bool (*ui_event_fn)(struct ui_element_s *self, ui_event_code_t evt);

/* ================= 元素配置 (Flash) ================= */
typedef struct {
    ui_coord_t   x, y, w, h;    /* 4 字节：几何信息 */
    uint8_t      type_id;       /* 1 字节：元素类型 */
    const char  *text;          /* 4 字节：静态文本 (Flash) */
    ui_render_fn render;        /* 4 字节：渲染函数指针 */
    ui_event_fn  on_event;      /* 4 字节：事件回调指针 */
} ui_element_cfg_t;             /* 总计：17 字节 (可能对齐到 20 字节) */

/* ================= 元素状态 (RAM) ================= */
typedef struct __packed ui_element_s {
    const ui_element_cfg_t *cfg;    /* 4B: 指向 Flash 配置 */
    void *data_binding;             /* 4B: 绑定数据指针 */
    ui_rect_t last_box;             /* 4B: 上次渲染区域 */
    ui_state_t state;               /* 1B: 状态掩码 */
    uint8_t pool_id;                /* 1B: 池中索引 */
    /* 总计：14 字节 (__packed 生效时) */
} ui_element_t;

/* ================= 脏区域队列 ================= */
typedef struct {
    ui_rect_t rects[UI_DIRTY_QUEUE_LEN];
    uint8_t   count;
} ui_dirty_queue_t;

/* ================= 屏幕定义 ================= */
typedef struct {
    const char *name;
    uint8_t     elem_count;
    const ui_element_t *elements[UI_MAX_SCREEN_ELEMS];
} ui_screen_t;

/* ================= 全局系统上下文 ================= */
typedef struct {
    const ui_screen_t *screen;        /* 当前界面 */
    ui_element_t pool[UI_POOL_SIZE];  /* 对象池 */
    uint8_t focused_idx;              /* 焦点元素索引 */
    ui_input_ctx_t input;             /* 输入上下文 */
    ui_dirty_queue_t dirty;           /* 脏队列 */
    uint8_t *framebuffer;             /* 显存指针 (1024B) */
    bool refreshing;                  /* 刷新互斥标志 */
    bool input_bound;                 /* 输入绑定标志 */
} ui_system_t;

/* ================= 全局实例 ================= */
extern ui_system_t g_ui;

/* ================= 核心 API ================= */

/* --- 系统初始化 --- */
void ui_init(uint8_t *fb);
void ui_set_screen(const ui_screen_t *screen);

/* --- 输入绑定 --- */
bool ui_input_bind_buttons(const ui_input_binding_t *binding);
bool ui_input_is_bound(void);
void ui_input_unbind(void);
bool ui_input_inject_event(ui_event_code_t evt);

/* --- 事件循环 --- */
void ui_tick(void);  /* 每 10ms 调用 */

/* --- 刷新管理 --- */
void ui_mark_dirty(ui_element_t *elem);
void ui_mark_rect_dirty(const ui_rect_t *rect);
void ui_flush(void);

/* --- 焦点管理 --- */
void ui_focus_next(void);
void ui_focus_prev(void);
void ui_focus_set(uint8_t idx);
uint8_t ui_focus_get(void);

/* ================= 工具函数 ================= */
static inline bool ui_rect_overlap(const ui_rect_t *a, const ui_rect_t *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

static inline void ui_rect_merge(ui_rect_t *out, const ui_rect_t *a, const ui_rect_t *b) {
    ui_coord_t x1 = (a->x < b->x) ? a->x : b->x;
    ui_coord_t y1 = (a->y < b->y) ? a->y : b->y;
    ui_coord_t x2 = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    ui_coord_t y2 = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
}

/* ================= 保护宏 ================= */
#ifdef UI_ENABLE_INPUT_PROTECTION
    #define UI_INPUT_GUARD(ret_val) \
        do { \
            if (!g_ui.input_bound) { \
                #ifdef UI_DEBUG_ENABLE \
                    printf("[UI] Input not bound!\n"); \
                #endif \
                return (ret_val); \
            } \
        } while(0)
#else
    #define UI_INPUT_GUARD(ret_val) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __UI_CORE_H */