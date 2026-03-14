#ifndef __UI_CORE_H
#define __UI_CORE_H

#include "stm32f1xx_hal.h"
#include "oled.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ================= 基础类型 ================= */
/* 问题：为什么坐标用 uint8_t？128x64 屏幕，最大值 127/63，uint8_t 足够。
   每个元素节省 2-4 字节，50 个元素就是 100-200 字节 SRAM。*/
typedef uint8_t ui_coord_t;
typedef uint8_t ui_size_t;

/* 状态位掩码：8 个状态仅占 1 字节，而非 8 个 bool 占 8 字节 */
typedef enum {
    UI_STATE_NORMAL   = 0x00,
    UI_STATE_HIGHLIGHT= 0x01,  // 高亮/选中
    UI_STATE_DISABLED = 0x02,  // 禁用
    UI_STATE_DIRTY    = 0x04,  // 需要重绘（局部刷新关键）
    UI_STATE_PRESSED  = 0x08,  // 按下动画态
} ui_state_mask_t;

/* 矩形区域：用于局部刷新和碰撞检测 */
typedef struct __packed {
    ui_coord_t x, y;
    ui_size_t  w, h;
} ui_rect_t;

/* ================= 输入事件 ================= */
/* 你定义的 7 种事件 + 内部事件 */
typedef enum {
    EVT_NONE         = 0,
    EVT_UP           = 1,   // 短上拨
    EVT_DOWN         = 2,   // 短下拨
    EVT_PRESS        = 3,   // 短确认
    EVT_LONG_UP      = 4,   // 长上拨 (>1000ms)
    EVT_LONG_DOWN    = 5,   // 长下拨
    EVT_LONG_PRESS   = 6,   // 长确认
    EVT_DOUBLE_CLICK = 7,   // 双击 (<300ms)
    /* 内部事件：不来自 GPIO，来自系统 */
    EVT_TICK         = 0xFE, // 时间片心跳（用于动画/长按检测）
    EVT_REFRESH      = 0xFF, // 强制刷新请求
} ui_event_code_t;

/* 输入配置：阈值可调整，避免硬编码 */
typedef struct {
    uint16_t long_press_threshold_ms;   // 默认 1000
    uint16_t double_click_gap_ms;       // 默认 300
    uint16_t debounce_ms;               // 默认 20
} ui_input_cfg_t;

/* 输入上下文：非阻塞状态机，RAM 占用 < 20 字节 */
typedef struct {
    uint32_t last_press_time;
    uint32_t last_release_time;
    uint8_t  key_state;          // 0:IDLE, 1:PRESSED, 2:LONG_PRESSED
    uint8_t  click_count;        // 双击计数
    ui_event_code_t pending_evt; // 待消费事件
    bool     evt_ready;
} ui_input_ctx_t;

/* ================= 元素配置(Flash) vs 状态(RAM) ================= */
/* 前向声明 */
struct ui_element_s;

/* 回调签名：渲染函数返回是否实际修改了像素（用于优化脏队列合并） */
typedef bool (*ui_render_fn)(struct ui_element_s *self, uint8_t *fb, const ui_rect_t *clip);
typedef bool (*ui_event_fn)(struct ui_element_s *self, ui_event_code_t evt);

/* 【配置结构体】-> const -> 存放在 Flash */
/* 关键优化：几何信息、函数指针、静态文本都放 Flash，运行时只读 */
typedef struct {
    ui_coord_t   x, y, w, h;    // 4 字节
    uint8_t      type_id;       // 1 字节：0=Text,1=Button,2=Status...
    const char  *text;          // 4 字节：指向 Flash 字符串
    ui_render_fn render;        // 4 字节：函数指针
    ui_event_fn  on_event;      // 4 字节：事件回调
} ui_element_cfg_t;

/* 【状态结构体】-> 存放在 RAM 对象池 */
/* 使用 __packed 避免对齐浪费：理论 14 字节，实测请验证 sizeof() */
typedef struct __packed ui_element_s {
    const ui_element_cfg_t *cfg;    // 4B: 指向 Flash 配置
    void *data_binding;             // 4B: 绑定数据指针（如变量地址）
    ui_rect_t last_box;             // 4B: 上次渲染区域（用于清除残影）
    uint8_t state;                  // 1B: 状态掩码
    uint8_t pool_id;                // 1B: 在池中的索引（调试用）
    /* 总大小：14 字节（__packed 生效时）*/
} ui_element_t;

/* ================= 对象池与屏幕管理 ================= */
#define UI_POOL_SIZE        32      // 根据 20KB SRAM 调整
#define UI_MAX_SCREEN_ELEMS 12      // 128x64 屏幕，单屏 12 个元素足够
#define UI_DIRTY_QUEUE_LEN  4       // 脏区域队列长度（平衡 SPI 开销）

/* 脏区域队列：局部刷新的核心 */
typedef struct {
    ui_rect_t rects[UI_DIRTY_QUEUE_LEN];
    uint8_t   count;
} ui_dirty_queue_t;

/* 屏幕定义：const -> Flash */
/* 关键决策：使用指针数组（64B/屏）还是索引数组（12B/屏）？
   这里先用指针保证代码清晰，你可后续改为 uint8_t indices[] 节省 52B/屏 */
typedef struct {
    const char *name;
    uint8_t     elem_count;
    const ui_element_t *elements[UI_MAX_SCREEN_ELEMS]; // 指针数组
} ui_screen_t;

/* 全局系统上下文：单例，~200 字节 SRAM */
typedef struct {
    const ui_screen_t *screen;        // 当前界面
    ui_element_t pool[UI_POOL_SIZE];  // 对象池（核心！~448B @14B/elem）
    uint8_t focused_idx;              // 焦点元素索引
    ui_input_ctx_t input;             // 输入上下文
    ui_dirty_queue_t dirty;           // 脏队列
    uint8_t *framebuffer;             // 显存指针（1024B for 128x64 1bpp）
    bool refreshing;                  // 刷新互斥标志
} ui_system_t;

/* ================= 全局 API ================= */
extern ui_system_t g_ui;

/* 初始化 */
void ui_init(uint8_t *fb);
void ui_set_screen(const ui_screen_t *screen);

/* 事件循环：在非阻塞主循环中调用 */
void ui_poll_inputs(GPIO_TypeDef *port, uint16_t pin_up, uint16_t pin_down, uint16_t pin_press);
void ui_tick(void);  // 每 10ms 调用一次，驱动长按/双击检测

/* 刷新管理 */
void ui_mark_dirty(ui_element_t *elem);           // 标记元素为脏
void ui_flush(void);                              // 执行局部刷新

/* 工具函数 */
static inline bool ui_rect_overlap(const ui_rect_t *a, const ui_rect_t *b);
static inline void ui_rect_merge(ui_rect_t *out, const ui_rect_t *a, const ui_rect_t *b);

#endif /* __UI_CORE_H */