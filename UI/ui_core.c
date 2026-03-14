#include "ui_core.h"

ui_system_t g_ui;

static const struct {
    uint16_t long_press_ms;
    uint16_t double_click_ms;
} g_cfg = {1000, 300};

static ui_input_binding_t g_binding = {0};

/* ================= 输入读取 ================= */
static bool ui_read_btn(const ui_button_cfg_t *cfg) {
    if (!cfg || !cfg->port) return false;
    bool level = (HAL_GPIO_ReadPin(cfg->port, cfg->pin) == GPIO_PIN_RESET);
    return cfg->active_low ? level : !level;
}

/* ================= 坐标转换工具（关键修复）================= */
/* 像素 Y 坐标 → SSD1306 页号 (0-7) */
static inline uint8_t ui_pixel_to_page(uint8_t y) {
    return y / 8;
}

/* 像素矩形 → 页范围（用于局部刷新）*/
static inline void ui_rect_to_pages(ui_rect_t *rect, uint8_t *page_start, uint8_t *page_end) {
    *page_start = rect->y / 8;
    *page_end = (rect->y + rect->h - 1) / 8;
    if (*page_end > 7) *page_end = 7;
}

/* ================= 初始化 ================= */
void ui_init(void) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.input_bound = false;
    g_ui.refreshing = false;
    
    for (uint8_t i = 0; i < UI_POOL_SIZE; i++) {
        g_ui.pool[i].pool_id = i;
        g_ui.pool[i].state = UI_STATE_NORMAL;
    }
    
    /* 初始化测试界面元素（关键！）*/
    #ifdef TEST_SCREEN_INIT
    test_screen_init_elements();
    #endif
    
    OLED_Init();
    OLED_Clear();
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen || screen->elem_count == 0) return;
    
    /* 清除旧界面 */
    if (g_ui.screen) {
        OLED_Clear();
    }
    
    g_ui.screen = screen;
    g_ui.focused_idx = 0;
    
    /* 标记所有元素为脏 */
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg) {
            elem->state |= UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    
    ui_flush();
}

/* ================= 输入绑定 ================= */
bool ui_input_bind_buttons(const ui_input_binding_t *binding) {
    if (!binding) return false;
    memcpy(&g_binding, binding, sizeof(g_binding));
    g_ui.input_bound = true;
    memset(&g_ui.input, 0, sizeof(g_ui.input));
    return true;
}

bool ui_input_is_bound(void) { 
    return g_ui.input_bound; 
}

void ui_input_unbind(void) {
    g_ui.input_bound = false;
    memset(&g_binding, 0, sizeof(g_binding));
    memset(&g_ui.input, 0, sizeof(g_ui.input));
}

/* ================= 输入检测 ================= */
static void ui_process_key(ui_key_state_t *key, ui_event_code_t short_evt, 
                           ui_event_code_t long_evt, uint32_t now) {
    if (!key || key->state != 1) return;
    if (now - key->last_press_time >= g_cfg.long_press_ms) {
        key->state = 2;
        g_ui.input.pending_evt = long_evt;
        g_ui.input.evt_ready = true;
    }
}

static void ui_release_key(ui_key_state_t *key, ui_event_code_t short_evt, uint32_t now) {
    if (!key || key->state == 0) return;
    uint32_t dur = now - key->last_press_time;
    key->last_release_time = now;
    
    if (key->state != 2 && dur < g_cfg.long_press_ms) {
        g_ui.input.pending_evt = short_evt;
        g_ui.input.evt_ready = true;
    }
    key->state = 0;
}

void ui_tick(void) {
    /* 保护 1：输入未绑定 */
    if (!g_ui.input_bound) return;
    
    /* 保护 2：屏幕未设置 */
    if (g_ui.screen == NULL) return;
    
    /* 保护 3：屏幕元素数量为 0 */
    if (g_ui.screen->elem_count == 0) return;
    
    /* 保护 4：焦点索引越界 */
    if (g_ui.focused_idx >= g_ui.screen->elem_count) return;
    
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    /* 读取按键 */
    bool up = ui_read_btn(&g_binding.up);
    bool down = ui_read_btn(&g_binding.down);
    bool press = ui_read_btn(&g_binding.press);
    
    /* 上拨 */
    if (up) {
        if (ctx->up.state == 0) {
            ctx->up.last_press_time = now;
            ctx->up.state = 1;
        } else {
            ui_process_key(&ctx->up, EVT_UP, EVT_LONG_UP, now);
        }
    } else if (ctx->up.state >= 1) {
        ui_release_key(&ctx->up, EVT_UP, now);
    }
    
    /* 下拨 */
    if (down) {
        if (ctx->down.state == 0) {
            ctx->down.last_press_time = now;
            ctx->down.state = 1;
        } else {
            ui_process_key(&ctx->down, EVT_DOWN, EVT_LONG_DOWN, now);
        }
    } else if (ctx->down.state >= 1) {
        ui_release_key(&ctx->down, EVT_DOWN, now);
    }
    
    /* 确认键 */
    if (press) {
        if (ctx->press.state == 0) {
            ctx->press.last_press_time = now;
            ctx->press.state = 1;
            /* 按下视觉反馈 */
            ui_element_t *elem = ui_get_focused_element();
            if (elem && !(elem->state & UI_STATE_DISABLED)) {
                elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY;
            }
        } else {
            ui_process_key(&ctx->press, EVT_PRESS, EVT_LONG_PRESS, now);
        }
    } else if (ctx->press.state >= 1) {
        ui_release_key(&ctx->press, EVT_PRESS, now);
        /* 释放视觉反馈 */
        ui_element_t *elem = ui_get_focused_element();
        if (elem) {
            elem->state &= ~UI_STATE_PRESSED;
            elem->state |= UI_STATE_DIRTY;
        }
    }
    
    /* ========== 事件分发 - 关键修复 ========== */
    if (ctx->evt_ready && ctx->pending_evt != EVT_NONE) {
        /* 层级 1：检查屏幕指针 */
        if (g_ui.screen == NULL) {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 层级 2：检查焦点索引 */
        if (g_ui.focused_idx >= g_ui.screen->elem_count) {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 层级 3：获取元素指针 */
        const ui_element_t *elem_ptr = g_ui.screen->elements[g_ui.focused_idx];
        
        /* 层级 4：检查元素指针有效性 */
        if (elem_ptr == NULL) {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 层级 5：检查元素配置指针 */
        ui_element_t *elem = (ui_element_t*)elem_ptr;
        if (elem->cfg == NULL) {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 层级 6：检查事件回调函数指针 */
        if (elem->cfg->on_event == NULL) {
            /* 没有回调，静默丢弃事件 */
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 层级 7：检查函数指针是否在 Flash 有效区域 */
        uint32_t fn_addr = (uint32_t)(elem->cfg->on_event);
        if (fn_addr < 0x08000000 || fn_addr > 0x08010000) {
            /* 函数指针不在 Flash 区域，可能是野指针 */
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 所有检查通过，安全调用 */
        #ifdef UI_USE_RTOS
            osKernelLock();
        #endif
        
        bool consumed = elem->cfg->on_event(elem, ctx->pending_evt);
        
        #ifdef UI_USE_RTOS
            osKernelUnlock();
        #endif
        
        if (consumed) {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
        }
    }
}

/* ================= 刷新管理 ================= */
void ui_mark_dirty(ui_element_t *elem) {
    if (!elem) return;
    elem->state |= UI_STATE_DIRTY;
}

void ui_flush(void) {
    if (g_ui.refreshing || !g_ui.screen) return;
    g_ui.refreshing = true;
    
    for (uint8_t i = 0; i < g_ui.screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[i];
        if (!elem || !elem->cfg) continue;
        
        if (elem->state & UI_STATE_DIRTY) {
            /* 直接渲染到 OLED，无需 framebuffer */
            if (elem->cfg->render != NULL) {
                elem->cfg->render(elem);
            }
            elem->state &= ~UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    
    g_ui.refreshing = false;
}

/* ================= 焦点管理 ================= */
void ui_focus_next(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    uint8_t start = g_ui.focused_idx;
    do {
        g_ui.focused_idx = (g_ui.focused_idx + 1) % g_ui.screen->elem_count;
        ui_element_t *e = ui_get_focused_element();
        if (e && !(e->state & UI_STATE_DISABLED)) break;
    } while (g_ui.focused_idx != start);
}

void ui_focus_prev(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    uint8_t start = g_ui.focused_idx;
    do {
        g_ui.focused_idx = (g_ui.focused_idx == 0) ? g_ui.screen->elem_count - 1 : g_ui.focused_idx - 1;
        ui_element_t *e = ui_get_focused_element();
        if (e && !(e->state & UI_STATE_DISABLED)) break;
    } while (g_ui.focused_idx != start);
}

uint8_t ui_focus_get(void) { 
    return g_ui.focused_idx; 
}