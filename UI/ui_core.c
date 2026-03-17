/* ui_core.c - 完整修复版 */
#include "ui_core.h"
#include "cmsis_os.h"

ui_system_t g_ui;

static const struct {
    uint16_t long_press_ms;
    uint16_t double_click_ms;
    uint16_t debounce_ms;
} g_cfg = {1000, 300, 50};

static ui_input_binding_t g_binding = {0};

/* ================= 输入读取（带防抖）================= */
static bool ui_read_btn(const ui_button_cfg_t *cfg) {
    if (!cfg || !cfg->port) return false;
    bool level = (HAL_GPIO_ReadPin(cfg->port, cfg->pin) == GPIO_PIN_RESET);
    return cfg->active_low ? level : !level;
}

/* ================= 坐标转换 ================= */
static inline uint8_t ui_pixel_to_page(uint8_t y) {
    return y / 8;
}

static inline void ui_rect_to_pages(ui_rect_t *rect, uint8_t *page_start, uint8_t *page_end) {
    *page_start = rect->y / 8;
    *page_end = (rect->y + rect->h - 1) / 8;
    if (*page_end > 7) *page_end = 7;
}

/* ================= 脏队列管理 ================= */
static bool ui_rect_overlap(const ui_rect_t *a, const ui_rect_t *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

static void ui_rect_merge(ui_rect_t *out, const ui_rect_t *a, const ui_rect_t *b) {
    ui_coord_t x1 = (a->x < b->x) ? a->x : b->x;
    ui_coord_t y1 = (a->y < b->y) ? a->y : b->y;
    ui_coord_t x2 = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
    ui_coord_t y2 = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
}

static void ui_dirty_add(const ui_rect_t *rect) {
    if (!rect || rect->w == 0 || rect->h == 0) return;
    
    ui_rect_t new_rect = *rect;
    
    // 尝试与现有所有区域合并
    for (uint8_t i = 0; i < g_ui.dirty.count; ) {
        if (ui_rect_overlap(&new_rect, &g_ui.dirty.rects[i])) {
            ui_rect_merge(&new_rect, &new_rect, &g_ui.dirty.rects[i]);
            
            // 移除已合并的区域
            if (i < g_ui.dirty.count - 1) {
                memmove(&g_ui.dirty.rects[i], &g_ui.dirty.rects[i+1], 
                        (g_ui.dirty.count - i - 1) * sizeof(ui_rect_t));
            }
            g_ui.dirty.count--;
            // 继续检查新合并的区域是否与其他重叠
        } else {
            i++;
        }
    }
    
    // 添加合并后的新区域
    if (g_ui.dirty.count >= UI_DIRTY_QUEUE_LEN) {
        // 队列满：合并为全屏刷新
        g_ui.dirty.rects[0] = (ui_rect_t){0, 0, 128, 64};
        g_ui.dirty.count = 1;
    } else {
        g_ui.dirty.rects[g_ui.dirty.count++] = new_rect;
    }
}

/* ================= 初始化 ================= */
void ui_init(void) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.input_bound = false;
    g_ui.refreshing = false;
    g_ui.dirty.count = 0;
    
    for (uint8_t i = 0; i < UI_POOL_SIZE; i++) {
        g_ui.pool[i].pool_id = i;
        g_ui.pool[i].state = UI_STATE_NORMAL;
    }
    
    OLED_Init();
    OLED_Clear();
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen || screen->elem_count == 0) {
        return;
    }
    
    if (g_ui.screen) {
        OLED_Clear();
    }
    
    g_ui.screen = screen;
    g_ui.focused_idx = 0;
    g_ui.dirty.count = 0;
    
    /* 找到第一个可交互元素 */
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        
        if (elem && elem->cfg && elem->cfg->on_event != NULL) {
            if (!(elem->state & UI_STATE_DISABLED)) {
                g_ui.focused_idx = i;
                elem->state |= UI_STATE_HIGHLIGHT;
                break;
            }
        }
    }
    
    /* 关键修复：标记所有元素为脏，并填充脏队列 */
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg) {
            elem->state |= UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
            
            /* 填充脏队列 */
            ui_mark_dirty(elem);
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

bool ui_input_is_bound(void) { return g_ui.input_bound; }

void ui_input_unbind(void) {
    g_ui.input_bound = false;
    memset(&g_binding, 0, sizeof(g_binding));
    memset(&g_ui.input, 0, sizeof(g_ui.input));
}

/* ================= 焦点管理（完整修复版 - 适配已有内联函数）================= */

/**
 * @brief 切换到下一个焦点元素
 * @note 完整修复：正确处理脏区域、边界检查和状态同步
 */
void ui_focus_next(void) {
    /* 参数检查 */
    if (!g_ui.screen || g_ui.screen->elem_count == 0) {
        return;
    }
    
    uint8_t start_idx = g_ui.focused_idx;
    uint8_t current_idx = g_ui.focused_idx;
    uint8_t try_count = 0;
    
    /* 保存旧焦点元素引用 - 使用内联函数 */
    ui_element_t *old_focus = ui_get_focused_element();
    
    /* 清除旧焦点的高亮状态（如果存在） */
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        /* 关键修复：立即标记为脏并添加到脏队列 */
        ui_mark_dirty(old_focus);
    }
    
    /* 寻找下一个可交互元素 */
    bool found_valid = false;
    
    do {
        /* 计算下一个索引（循环） */
        current_idx = (current_idx + 1) % g_ui.screen->elem_count;
        
        /* 获取候选元素 */
        ui_element_t *candidate = (ui_element_t*)g_ui.screen->elements[current_idx];
        
        /* 使用与内联函数一致的检查逻辑 */
        if (candidate && candidate->cfg) {
            /* 检查是否可交互（有事件处理且未禁用） */
            if (candidate->cfg->on_event != NULL && 
                !(candidate->state & UI_STATE_DISABLED)) {
                
                /* 找到有效元素 */
                g_ui.focused_idx = current_idx;
                
                /* 设置高亮状态 */
                candidate->state |= UI_STATE_HIGHLIGHT;
                /* 关键修复：标记新焦点为脏 */
                ui_mark_dirty(candidate);
                
                found_valid = true;
                break;
            }
        }
        
        try_count++;
        
        /* 防止无限循环（所有元素都不可交互的情况） */
        if (try_count >= g_ui.screen->elem_count) {
            break;
        }
        
    } while (current_idx != start_idx);
    
    /* 如果没有找到有效元素，恢复旧焦点 */
    if (!found_valid) {
        g_ui.focused_idx = start_idx;
        
        /* 恢复旧焦点的高亮状态 */
        if (old_focus) {
            old_focus->state |= UI_STATE_HIGHLIGHT;
            /* 关键修复：重新标记旧焦点为脏 */
            ui_mark_dirty(old_focus);
        }
    }
    
    /* 触发一次刷新 */
    ui_flush();
}

/**
 * @brief 切换到上一个焦点元素
 * @note 完整修复：正确处理脏区域、边界检查和状态同步
 */
void ui_focus_prev(void) {
    /* 参数检查 */
    if (!g_ui.screen || g_ui.screen->elem_count == 0) {
        return;
    }
    
    uint8_t start_idx = g_ui.focused_idx;
    uint8_t current_idx = g_ui.focused_idx;
    uint8_t try_count = 0;
    
    /* 保存旧焦点元素引用 - 使用内联函数 */
    ui_element_t *old_focus = ui_get_focused_element();
    
    /* 清除旧焦点的高亮状态（如果存在） */
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        /* 关键修复：立即标记为脏并添加到脏队列 */
        ui_mark_dirty(old_focus);
    }
    
    /* 寻找上一个可交互元素 */
    bool found_valid = false;
    
    do {
        /* 计算上一个索引（循环） */
        if (current_idx == 0) {
            current_idx = g_ui.screen->elem_count - 1;
        } else {
            current_idx--;
        }
        
        /* 获取候选元素 */
        ui_element_t *candidate = (ui_element_t*)g_ui.screen->elements[current_idx];
        
        /* 使用与内联函数一致的检查逻辑 */
        if (candidate && candidate->cfg) {
            /* 检查是否可交互（有事件处理且未禁用） */
            if (candidate->cfg->on_event != NULL && 
                !(candidate->state & UI_STATE_DISABLED)) {
                
                /* 找到有效元素 */
                g_ui.focused_idx = current_idx;
                
                /* 设置高亮状态 */
                candidate->state |= UI_STATE_HIGHLIGHT;
                /* 关键修复：标记新焦点为脏 */
                ui_mark_dirty(candidate);
                
                found_valid = true;
                break;
            }
        }
        
        try_count++;
        
        /* 防止无限循环（所有元素都不可交互的情况） */
        if (try_count >= g_ui.screen->elem_count) {
            break;
        }
        
    } while (current_idx != start_idx);
    
    /* 如果没有找到有效元素，恢复旧焦点 */
    if (!found_valid) {
        g_ui.focused_idx = start_idx;
        
        /* 恢复旧焦点的高亮状态 */
        if (old_focus) {
            old_focus->state |= UI_STATE_HIGHLIGHT;
            /* 关键修复：重新标记旧焦点为脏 */
            ui_mark_dirty(old_focus);
        }
    }
    
    /* 触发一次刷新 */
    ui_flush();
}

/**
 * @brief 获取当前焦点索引
 */
uint8_t ui_focus_get(void) { 
    return g_ui.focused_idx; 
}

/**
 * @brief 设置指定索引为焦点
 * @param idx 要设置为焦点的元素索引
 * @note 完整修复：正确处理脏区域和状态同步
 */
void ui_focus_set(uint8_t idx) {
    /* 参数检查 */
    if (!g_ui.screen || idx >= g_ui.screen->elem_count) {
        return;
    }
    
    ui_element_t *target = (ui_element_t*)g_ui.screen->elements[idx];
    
    /* 检查目标元素有效性（与内联函数一致） */
    if (!target || !target->cfg) {
        return;
    }
    
    /* 如果目标元素被禁用，不能设置为焦点 */
    if (target->state & UI_STATE_DISABLED) {
        return;
    }
    
    /* 获取当前焦点元素 - 使用内联函数 */
    ui_element_t *old_focus = ui_get_focused_element();
    
    /* 如果已经是同一个元素，无需处理 */
    if (old_focus == target) {
        return;
    }
    
    /* 清除旧焦点的高亮状态（如果存在） */
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        /* 关键修复：标记旧焦点为脏 */
        ui_mark_dirty(old_focus);
    }
    
    /* 设置新焦点 */
    g_ui.focused_idx = idx;
    
    /* 设置新焦点的高亮状态 */
    target->state |= UI_STATE_HIGHLIGHT;
    /* 关键修复：标记新焦点为脏 */
    ui_mark_dirty(target);
    
    /* 触发一次刷新 */
    ui_flush();
}

/* ================= 输入检测（防抖）================= */
void ui_tick(void) {
    if (!g_ui.input_bound || !g_ui.screen) return;
    
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    bool up = ui_read_btn(&g_binding.up);
    bool down = ui_read_btn(&g_binding.down);
    bool press = ui_read_btn(&g_binding.press);
    
    /* 上拨键状态机（带防抖）*/
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
        if (dur >= g_cfg.debounce_ms) {  /* 防抖 */
            ctx->up.last_release_time = now;
            if (ctx->up.state != 2 && dur < g_cfg.long_press_ms) {
                ctx->pending_evt = EVT_UP;
                ctx->evt_ready = true;
            }
            ctx->up.state = 0;
        }
    }
    
    /* 下拨键状态机（带防抖）*/
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
        if (dur >= g_cfg.debounce_ms) {  /* 防抖 */
            ctx->down.last_release_time = now;
            if (ctx->down.state != 2 && dur < g_cfg.long_press_ms) {
                ctx->pending_evt = EVT_DOWN;
                ctx->evt_ready = true;
            }
            ctx->down.state = 0;
        }
    }
    
    /* 确认键状态机（带防抖）*/
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
        if (dur >= g_cfg.debounce_ms) {  /* 防抖 */
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
    }
    
    /* ========== 事件分发 ========== */
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
            bool consumed = elem->cfg->on_event(elem, ctx->pending_evt);
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

/* ================= 刷新管理 - 关键修复 ================= */
void ui_mark_dirty(ui_element_t *elem) {
    if (!elem || !elem->cfg) return;
    
    elem->state |= UI_STATE_DIRTY;
    
    /* 添加区域到脏队列（含 1 像素边框防残影）*/
    ui_rect_t dirty = {
        .x = (elem->cfg->x > 1) ? elem->cfg->x - 1 : 0,
        .y = (elem->cfg->y > 1) ? elem->cfg->y - 1 : 0,
        .w = elem->cfg->w + 2,
        .h = elem->cfg->h + 2
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
    if (g_ui.refreshing || !g_ui.screen) return;
    
    // 如果没有脏区域且没有脏元素，直接返回
    if (g_ui.dirty.count == 0) {
        bool has_dirty = false;
        for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];
            if (elem && (elem->state & UI_STATE_DIRTY)) {
                has_dirty = true;
                break;
            }
        }
        if (!has_dirty) return;
    }
    
    g_ui.refreshing = true;
    
    // 如果有脏区域，用它们标记元素
    if (g_ui.dirty.count > 0) {
        for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
            ui_rect_t *r = &g_ui.dirty.rects[i];
            for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
                ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];
                if (!elem || !elem->cfg) continue;
                
                ui_rect_t elem_rect = {elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
                if (ui_rect_overlap(&elem_rect, r)) {
                    elem->state |= UI_STATE_DIRTY;
                }
            }
        }
    }
    
    // 渲染所有脏元素
    for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
        ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];
        if (elem && elem->cfg && (elem->state & UI_STATE_DIRTY)) {
            if (elem->cfg->render) {
                elem->cfg->render(elem);
            }
            elem->state &= ~UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
}