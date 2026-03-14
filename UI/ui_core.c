/* ui_core.c - 完整修复版 */

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

/* ================= 初始化 ================= */
void ui_init(void) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.input_bound = false;
    g_ui.refreshing = false;
    
    for (uint8_t i = 0; i < UI_POOL_SIZE; i++) {
        g_ui.pool[i].pool_id = i;
        g_ui.pool[i].state = UI_STATE_NORMAL;
    }
    
    OLED_Init();
    OLED_Clear();
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen || screen->elem_count == 0) return;
    
    if (g_ui.screen) {
        OLED_Clear();
    }
    
    g_ui.screen = screen;
    g_ui.focused_idx = 0;
    
    /* 关键：自动找到第一个可交互元素作为初始焦点 */
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg && elem->cfg->on_event != NULL) {
            if (!(elem->state & UI_STATE_DISABLED)) {
                g_ui.focused_idx = i;
                break;
            }
        }
    }
    
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

/* ================= 焦点管理 - 关键修复 ================= */
void ui_focus_next(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start = g_ui.focused_idx;
    uint8_t count = 0;
    
    do {
        g_ui.focused_idx = (g_ui.focused_idx + 1) % g_ui.screen->elem_count;
        ui_element_t *e = ui_get_focused_element();
        
        /* 只选择有事件回调的元素 */
        if (e && e->cfg && e->cfg->on_event != NULL) {
            if (!(e->state & UI_STATE_DISABLED)) {
                return;
            }
        }
        
        count++;
        if (count >= g_ui.screen->elem_count) break;
    } while (g_ui.focused_idx != start);
}

void ui_focus_prev(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start = g_ui.focused_idx;
    uint8_t count = 0;
    
    do {
        g_ui.focused_idx = (g_ui.focused_idx == 0) ? 
                           g_ui.screen->elem_count - 1 : g_ui.focused_idx - 1;
        ui_element_t *e = ui_get_focused_element();
        
        /* 只选择有事件回调的元素 */
        if (e && e->cfg && e->cfg->on_event != NULL) {
            if (!(e->state & UI_STATE_DISABLED)) {
                return;
            }
        }
        
        count++;
        if (count >= g_ui.screen->elem_count) break;
    } while (g_ui.focused_idx != start);
}

uint8_t ui_focus_get(void) { 
    return g_ui.focused_idx; 
}

void ui_focus_set(uint8_t idx) {
    if (!g_ui.screen || idx >= g_ui.screen->elem_count) return;
    
    ui_element_t *e = (ui_element_t*)g_ui.screen->elements[idx];
    if (e && e->cfg && e->cfg->on_event != NULL) {
        g_ui.focused_idx = idx;
    }
}

/* ================= 输入检测 ================= */
void ui_tick(void) {
    if (!g_ui.input_bound || !g_ui.screen) return;
    
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    bool up = ui_read_btn(&g_binding.up);
    bool down = ui_read_btn(&g_binding.down);
    bool press = ui_read_btn(&g_binding.press);
    
    /* 上拨键状态机 */
    if (up) {
        if (ctx->up.state == 0) {
            ctx->up.last_press_time = now;
            ctx->up.state = 1;
        } else if (ctx->up.state == 1) {
            if (now - ctx->up.last_press_time >= g_cfg.long_press_ms) {
                ctx->up.state = 2;
                ctx->pending_evt = EVT_LONG_UP;
                ctx->evt_ready = true;
            }
        }
    } else if (ctx->up.state >= 1) {
        uint32_t dur = now - ctx->up.last_press_time;
        ctx->up.last_release_time = now;
        if (ctx->up.state != 2 && dur < g_cfg.long_press_ms) {
            ctx->pending_evt = EVT_UP;
            ctx->evt_ready = true;
        }
        ctx->up.state = 0;
    }
    
    /* 下拨键状态机 */
    if (down) {
        if (ctx->down.state == 0) {
            ctx->down.last_press_time = now;
            ctx->down.state = 1;
        } else if (ctx->down.state == 1) {
            if (now - ctx->down.last_press_time >= g_cfg.long_press_ms) {
                ctx->down.state = 2;
                ctx->pending_evt = EVT_LONG_DOWN;
                ctx->evt_ready = true;
            }
        }
    } else if (ctx->down.state >= 1) {
        uint32_t dur = now - ctx->down.last_press_time;
        ctx->down.last_release_time = now;
        if (ctx->down.state != 2 && dur < g_cfg.long_press_ms) {
            ctx->pending_evt = EVT_DOWN;
            ctx->evt_ready = true;
        }
        ctx->down.state = 0;
    }
    
    /* 确认键状态机 */
    if (press) {
        if (ctx->press.state == 0) {
            ctx->press.last_press_time = now;
            ctx->press.state = 1;
            
            /* 按下视觉反馈 */
            ui_element_t *elem = ui_get_focused_element();
            if (elem && !(elem->state & UI_STATE_DISABLED)) {
                elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY;
            }
        } else if (ctx->press.state == 1) {
            if (now - ctx->press.last_press_time >= g_cfg.long_press_ms) {
                ctx->press.state = 2;
                ctx->pending_evt = EVT_LONG_PRESS;
                ctx->evt_ready = true;
            }
        }
    } else if (ctx->press.state >= 1) {
        uint32_t dur = now - ctx->press.last_press_time;
        ctx->press.last_release_time = now;
        
        /* 释放视觉反馈 */
        ui_element_t *elem = ui_get_focused_element();
        if (elem) {
            elem->state &= ~UI_STATE_PRESSED;
            elem->state |= UI_STATE_DIRTY;
        }
        
        if (ctx->press.state != 2 && dur < g_cfg.long_press_ms) {
            ctx->pending_evt = EVT_PRESS;
            ctx->evt_ready = true;
        }
        ctx->press.state = 0;
    }
    
    /* ========== 事件分发 - 关键修复 ========== */
    if (ctx->evt_ready && ctx->pending_evt != EVT_NONE) {
        /* 上拨/下拨用于切换焦点，不传递给元素 */
        if (ctx->pending_evt == EVT_UP) {
            ui_focus_prev();
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        if (ctx->pending_evt == EVT_DOWN) {
            ui_focus_next();
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
            return;
        }
        
        /* 其他事件传递给当前焦点元素 */
        ui_element_t *elem = ui_get_focused_element();
        if (elem && elem->cfg && elem->cfg->on_event != NULL) {
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
        } else {
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
            if (elem->cfg->render != NULL) {
                elem->cfg->render(elem);
            }
            elem->state &= ~UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    
    g_ui.refreshing = false;
}