/**
 * @file    ui_core.c
 * @brief   UI 框架核心实现
 * @note    所有配置数据自动存入 Flash (const)，状态数据存入 RAM
 */

#include "ui_core.h"

/* ================= 全局系统实例 ================= */
ui_system_t g_ui;

/* ================= 输入配置默认值 ================= */
static const ui_input_cfg_t g_input_cfg = {
    .long_press_threshold_ms = 1000,
    .double_click_gap_ms = 300,
    .debounce_ms = 20
};

/* ================= 输入绑定状态 ================= */
static ui_input_binding_t g_input_binding = {0};

/* ================= 工具函数 ================= */
static bool ui_read_button(const ui_button_cfg_t *cfg) {
    if (!cfg || !cfg->port) return false;
    bool level = (HAL_GPIO_ReadPin(cfg->port, cfg->pin) == GPIO_PIN_RESET);
    return cfg->active_low ? level : !level;
}

static void ui_dirty_add(const ui_rect_t *rect) {
    if (!rect || rect->w == 0 || rect->h == 0) return;
    
    if (g_ui.dirty.count >= UI_DIRTY_QUEUE_LEN) {
        /* 队列满：降级为全刷 */
        g_ui.dirty.count = 0;
        ui_rect_t full = {0, 0, 128, 64};
        g_ui.dirty.rects[0] = full;
        g_ui.dirty.count = 1;
        return;
    }
    
    /* 尝试合并重叠或相邻区域 */
    for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
        ui_rect_t *exist = &g_ui.dirty.rects[i];
        if (ui_rect_overlap(rect, exist) ||
            (abs((int)rect->x - (int)(exist->x + exist->w)) < 2) ||
            (abs((int)rect->y - (int)(exist->y + exist->h)) < 2)) {
            ui_rect_merge(exist, exist, rect);
            return;
        }
    }
    
    g_ui.dirty.rects[g_ui.dirty.count++] = *rect;
}

/* ================= 系统初始化 ================= */
void ui_init(uint8_t *fb) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.framebuffer = fb;
    g_ui.input_bound = false;
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
    g_ui.focused_idx = 0;
    
    /* 初始化对象池 */
    for (uint8_t i = 0; i < UI_POOL_SIZE; i++) {
        g_ui.pool[i].pool_id = i;
        g_ui.pool[i].state = UI_STATE_NORMAL;
        g_ui.pool[i].cfg = NULL;
        g_ui.pool[i].data_binding = NULL;
    }
    
    OLED_Clear();
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen) return;
    
    /* 清除旧界面 */
    if (g_ui.screen) {
        for (uint8_t i = 0; i < g_ui.screen->elem_count; i++) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[i];
            if (elem) ui_mark_dirty(elem);
        }
        ui_flush();
    }
    
    g_ui.screen = screen;
    g_ui.focused_idx = 0;
    
    /* 标记新界面所有元素为脏 */
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem) {
            elem->state |= UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
            ui_mark_dirty(elem);
        }
    }
    ui_flush();
}

/* ================= 输入绑定 ================= */
bool ui_input_bind_buttons(const ui_input_binding_t *binding) {
    if (!binding) return false;
    
    /* 参数验证 */
    if ((binding->up.port == NULL && binding->up.pin != 0) ||
        (binding->down.port == NULL && binding->down.pin != 0) ||
        (binding->press.port == NULL && binding->press.pin != 0)) {
        return false;
    }
    
    #ifdef UI_USE_RTOS
        osKernelLock();
    #endif
    
    memcpy(&g_input_binding, binding, sizeof(ui_input_binding_t));
    g_ui.input_bound = true;
    
    /* 重置输入状态机 */
    memset(&g_ui.input, 0, sizeof(ui_input_ctx_t));
    
    #ifdef UI_USE_RTOS
        osKernelUnlock();
    #endif
    
    return true;
}

bool ui_input_is_bound(void) {
    return g_ui.input_bound;
}

void ui_input_unbind(void) {
    #ifdef UI_USE_RTOS
        osKernelLock();
    #endif
    
    g_ui.input_bound = false;
    memset(&g_input_binding, 0, sizeof(ui_input_binding_t));
    memset(&g_ui.input, 0, sizeof(ui_input_ctx_t));
    
    #ifdef UI_USE_RTOS
        osKernelUnlock();
    #endif
}

bool ui_input_inject_event(ui_event_code_t evt) {
    UI_INPUT_GUARD(false);
    
    if (evt < EVT_UP || evt > EVT_DOUBLE_CLICK) return false;
    
    #ifdef UI_USE_RTOS
        osKernelLock();
    #endif
    
    g_ui.input.pending_evt = evt;
    g_ui.input.evt_ready = true;
    
    #ifdef UI_USE_RTOS
        osKernelUnlock();
    #endif
    
    return true;
}

/* ================= 输入检测 (三键独立状态机) ================= */
static void ui_process_key(ui_key_state_t *key, ui_event_code_t evt_short, 
                           ui_event_code_t evt_long, uint32_t now) {
    ui_input_ctx_t *ctx = &g_ui.input;
    
    if (key->state == 0) return;  /* IDLE */
    
    if (key->state == 1) {  /* PRESSED - 检测长按 */
        if (now - key->last_press_time >= g_input_cfg.long_press_threshold_ms) {
            key->state = 2;  /* LONG_PRESSED */
            ctx->pending_evt = evt_long;
            ctx->evt_ready = true;
        }
    }
}

static void ui_release_key(ui_key_state_t *key, ui_event_code_t evt_short,
                           ui_event_code_t evt_double, uint32_t now) {
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t duration = now - key->last_press_time;
    
    key->last_release_time = now;
    
    if (key->state == 2) {
        ctx->pending_evt = EVT_NONE;  /* 长按已触发 */
    } else if (duration < g_input_cfg.long_press_threshold_ms) {
        if (key->click_count == 2 && 
            (now - key->last_release_time) < g_input_cfg.double_click_gap_ms) {
            ctx->pending_evt = evt_double;
            key->click_count = 0;
        } else {
            ctx->pending_evt = evt_short;
        }
        ctx->evt_ready = true;
    }
    
    key->state = 0;
}

void ui_tick(void) {
    if (!g_ui.input_bound) return;
    
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    /* 读取三键状态 */
    bool up    = ui_read_button(&g_input_binding.up);
    bool down  = ui_read_button(&g_input_binding.down);
    bool press = ui_read_button(&g_input_binding.press);
    
    /* ========== 上拨键 ========== */
    if (up) {
        if (ctx->up.state == 0) {
            ctx->up.last_press_time = now;
            ctx->up.state = 1;
            ctx->up.click_count++;
        } else {
            ui_process_key(&ctx->up, EVT_UP, EVT_LONG_UP, now);
        }
    } else {
        if (ctx->up.state >= 1) {
            ui_release_key(&ctx->up, EVT_UP, EVT_DOUBLE_CLICK, now);
        }
    }
    
    /* ========== 下拨键 ========== */
    if (down) {
        if (ctx->down.state == 0) {
            ctx->down.last_press_time = now;
            ctx->down.state = 1;
            ctx->down.click_count++;
        } else {
            ui_process_key(&ctx->down, EVT_DOWN, EVT_LONG_DOWN, now);
        }
    } else {
        if (ctx->down.state >= 1) {
            ui_release_key(&ctx->down, EVT_DOWN, EVT_DOUBLE_CLICK, now);
        }
    }
    
    /* ========== 确认键 ========== */
    if (press) {
        if (ctx->press.state == 0) {
            ctx->press.last_press_time = now;
            ctx->press.state = 1;
            ctx->press.click_count++;
            
            /* 视觉反馈：按下态 */
            if (g_ui.focused_idx < g_ui.screen->elem_count) {
                ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
                if (elem && !(elem->state & UI_STATE_DISABLED)) {
                    elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY;
                    ui_mark_dirty(elem);
                }
            }
        } else {
            ui_process_key(&ctx->press, EVT_PRESS, EVT_LONG_PRESS, now);
        }
    } else {
        if (ctx->press.state >= 1) {
            ui_release_key(&ctx->press, EVT_PRESS, EVT_DOUBLE_CLICK, now);
            
            /* 视觉反馈：释放 */
            if (g_ui.focused_idx < g_ui.screen->elem_count) {
                ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
                if (elem) {
                    elem->state &= ~UI_STATE_PRESSED;
                    elem->state |= UI_STATE_DIRTY;
                    ui_mark_dirty(elem);
                }
            }
        }
    }
    
    /* ========== 事件分发 ========== */
    if (ctx->evt_ready && ctx->pending_evt != EVT_NONE) {
        if (g_ui.focused_idx < g_ui.screen->elem_count) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
            if (elem && elem->cfg && elem->cfg->on_event) {
                #ifdef UI_USE_RTOS
                    osKernelLock();
                #endif
                
                if (elem->cfg->on_event(elem, ctx->pending_evt)) {
                    ctx->evt_ready = false;
                    ctx->pending_evt = EVT_NONE;
                }
                
                #ifdef UI_USE_RTOS
                    osKernelUnlock();
                #endif
            }
        }
    }
}

/* ================= 刷新管理 ================= */
void ui_mark_dirty(ui_element_t *elem) {
    if (!elem || !elem->cfg) return;
    
    elem->state |= UI_STATE_DIRTY;
    
    /* 添加区域到脏队列 (含 1 像素边框防残影) */
    ui_rect_t dirty = {
        .x = (elem->last_box.x > 1) ? elem->last_box.x - 1 : 0,
        .y = (elem->last_box.y > 1) ? elem->last_box.y - 1 : 0,
        .w = elem->last_box.w + 2,
        .h = elem->last_box.h + 2
    };
    
    /* 边界裁剪 */
    if (dirty.x + dirty.w > 128) dirty.w = 128 - dirty.x;
    if (dirty.y + dirty.h > 64) dirty.h = 64 - dirty.y;
    
    ui_dirty_add(&dirty);
}

void ui_mark_rect_dirty(const ui_rect_t *rect) {
    if (rect) ui_dirty_add(rect);
}

void ui_flush(void) {
    if (g_ui.refreshing || g_ui.dirty.count == 0) return;
    
    g_ui.refreshing = true;
    
    for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
        ui_rect_t *r = &g_ui.dirty.rects[i];
        
        /* 清除区域 (防残影) */
        OLED_Fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, 0);
        
        /* 重绘重叠元素 */
        for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];
            if (elem && (elem->state & UI_STATE_DIRTY) &&
                ui_rect_overlap(&elem->last_box, r)) {
                if (elem->cfg && elem->cfg->render) {
                    elem->cfg->render(elem, g_ui.framebuffer, r);
                }
                elem->state &= ~UI_STATE_DIRTY;
            }
        }
        
        /* 发送局部数据到 OLED */
        OLED_Set_Pos(r->x, r->y);
        /* 注意：实际发送需根据 SSD1306 页模式处理 */
    }
    
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
}

/* ================= 焦点管理 ================= */
void ui_focus_next(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start = g_ui.focused_idx;
    do {
        g_ui.focused_idx = (g_ui.focused_idx + 1) % g_ui.screen->elem_count;
        ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
        if (elem && !(elem->state & UI_STATE_DISABLED) && !(elem->state & UI_STATE_HIDDEN)) {
            break;
        }
    } while (g_ui.focused_idx != start);
}

void ui_focus_prev(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start = g_ui.focused_idx;
    do {
        g_ui.focused_idx = (g_ui.focused_idx == 0) ? 
                           g_ui.screen->elem_count - 1 : g_ui.focused_idx - 1;
        ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
        if (elem && !(elem->state & UI_STATE_DISABLED) && !(elem->state & UI_STATE_HIDDEN)) {
            break;
        }
    } while (g_ui.focused_idx != start);
}

void ui_focus_set(uint8_t idx) {
    if (!g_ui.screen || idx >= g_ui.screen->elem_count) return;
    
    ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[idx];
    if (elem && !(elem->state & UI_STATE_DISABLED) && !(elem->state & UI_STATE_HIDDEN)) {
        g_ui.focused_idx = idx;
    }
}

uint8_t ui_focus_get(void) {
    return g_ui.focused_idx;
}