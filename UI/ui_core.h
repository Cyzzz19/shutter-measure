#ifndef __UI_CORE_H
#define __UI_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "oled.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ================= 配置宏 ================= */
#define UI_POOL_SIZE        32
#define UI_MAX_SCREEN_ELEMS 12
#define UI_DIRTY_QUEUE_LEN  8
#define UI_MAX_STACK_DEPTH  5

/* ================= 基础类型 ================= */
typedef uint8_t ui_coord_t;
typedef uint8_t ui_size_t;
typedef uint8_t ui_state_t;

/* ================= 状态掩码 ================= */
typedef enum {
    UI_STATE_NORMAL    = 0x00,
    UI_STATE_HIGHLIGHT = 0x01,
    UI_STATE_DISABLED  = 0x02,
    UI_STATE_DIRTY     = 0x04,
    UI_STATE_PRESSED   = 0x08,
    UI_STATE_HIDDEN    = 0x10,
} ui_state_mask_t;

/* ================= 矩形 ================= */
typedef struct {
    ui_coord_t x, y;
    ui_size_t  w, h;
} ui_rect_t;

/* ================= 输入事件 ================= */
typedef enum {
    EVT_NONE = 0, EVT_UP = 1, EVT_DOWN = 2, EVT_PRESS = 3,
    EVT_LONG_UP = 4, EVT_LONG_DOWN = 5, EVT_LONG_PRESS = 6, EVT_DOUBLE_CLICK = 7,
} ui_event_code_t;

/* ================= 输入绑定 ================= */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t     pin;
    bool         active_low;
} ui_button_cfg_t;

typedef struct {
    ui_button_cfg_t up, down, press;
} ui_input_binding_t;

/* ================= 输入状态机 ================= */
typedef struct {
    uint32_t last_press_time, last_release_time;
    uint8_t  state, click_count;
} ui_key_state_t;

typedef struct {
    ui_key_state_t up, down, press;
    ui_event_code_t pending_evt;
    bool evt_ready;
} ui_input_ctx_t;

/* ================= 元素回调 ================= */
struct ui_element_s;
typedef void (*ui_render_fn)(struct ui_element_s *self);
typedef bool (*ui_event_fn)(struct ui_element_s *self, ui_event_code_t evt);

/* ================= 元素配置 ================= */
typedef struct {
    ui_coord_t   x, y, w, h;
    uint8_t      type_id;
    const char  *text;
    ui_render_fn render;
    ui_event_fn  on_event;
} ui_element_cfg_t;

/* ================= 元素状态 ================= */
typedef struct ui_element_s {
    const ui_element_cfg_t *cfg;
    void *data_binding;
    ui_rect_t last_box;
    ui_state_t state;
    uint8_t pool_id;
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
    const ui_element_t **elements;
    void (*on_enter)(const struct ui_screen_s *s);
    void (*on_exit)(const struct ui_screen_s *s);
} ui_screen_t;

/* ================= 全局系统 ================= */
typedef struct {
    const ui_screen_t *screen_stack[UI_MAX_STACK_DEPTH];
    uint8_t stack_size;
    ui_element_t pool[UI_POOL_SIZE];
    uint8_t focused_idx;
    ui_input_ctx_t input;
    ui_dirty_queue_t dirty;
    bool input_bound;
    bool refreshing;
} ui_system_t;

extern ui_system_t g_ui;

/* ================= API ================= */
void ui_init(void);
void ui_set_screen(const ui_screen_t *screen); // 清空栈并跳转
bool ui_push_screen(const ui_screen_t *screen); // 压栈
bool ui_pop_screen(void); // 出栈
const ui_screen_t* ui_get_current_screen(void);

bool ui_input_bind_buttons(const ui_input_binding_t *binding);
bool ui_input_is_bound(void);
void ui_input_unbind(void);
void ui_tick(void);
void ui_mark_dirty(ui_element_t *elem);
void ui_mark_rect_dirty(const ui_rect_t *rect);
void ui_flush(void);
void ui_focus_next(void);
void ui_focus_prev(void);
uint8_t ui_focus_get(void);
void ui_focus_set(uint8_t idx);

static inline ui_element_t* ui_get_focused_element(void) {
    if (g_ui.stack_size == 0) return NULL;
    const ui_screen_t *scr = g_ui.screen_stack[g_ui.stack_size - 1];
    if (!scr || g_ui.focused_idx >= scr->elem_count) return NULL;
    ui_element_t *elem = (ui_element_t*)scr->elements[g_ui.focused_idx];
    if (!elem || !elem->cfg) return NULL;
    return elem;
}

#ifdef __cplusplus
}
#endif
#endif