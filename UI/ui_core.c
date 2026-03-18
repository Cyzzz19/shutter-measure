/* ui_core.c - 完整修复版 (HardFault Free) */
#include "ui_core.h"
#include "cmsis_os.h"

ui_system_t g_ui;

static const struct {
    uint16_t long_press_ms;
    uint16_t double_click_ms;
    uint16_t debounce_ms;
} g_cfg = {1000, 300, 50};

static ui_input_binding_t g_binding = {0};

static bool ui_read_btn(const ui_button_cfg_t *cfg) {
    if (!cfg || !cfg->port) return false;
    bool level = (HAL_GPIO_ReadPin(cfg->port, cfg->pin) == GPIO_PIN_RESET);
    return cfg->active_low ? level : !level;
}

static inline uint8_t ui_pixel_to_page(uint8_t y) {
    return y / 8;
}

static inline void ui_rect_to_pages(ui_rect_t *rect, uint8_t *page_start, uint8_t *page_end) {
    *page_start = rect->y / 8;
    *page_end = (rect->y + rect->h - 1) / 8;
    if (*page_end > 7) *page_end = 7;
}

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
    if (g_ui.dirty.count > UI_DIRTY_QUEUE_LEN) g_ui.dirty.count = UI_DIRTY_QUEUE_LEN;

    ui_rect_t new_rect = *rect;
    
    for (uint8_t i = 0; i < g_ui.dirty.count; ) {
        if (ui_rect_overlap(&new_rect, &g_ui.dirty.rects[i])) {
            ui_rect_merge(&new_rect, &new_rect, &g_ui.dirty.rects[i]);
            if (i < g_ui.dirty.count - 1) {
                uint32_t count_to_move = (uint32_t)g_ui.dirty.count - i - 1;
                memmove(&g_ui.dirty.rects[i], &g_ui.dirty.rects[i+1], 
                        count_to_move * sizeof(ui_rect_t));
            }
            g_ui.dirty.count--;
        } else {
            i++;
        }
    }
    
    if (g_ui.dirty.count >= UI_DIRTY_QUEUE_LEN) {
        g_ui.dirty.rects[0] = (ui_rect_t){0, 0, 128, 64};
        g_ui.dirty.count = 1;
    } else {
        g_ui.dirty.rects[g_ui.dirty.count] = new_rect;
        g_ui.dirty.count++;
    }
}

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
    if (!screen || screen->elem_count == 0 || !screen->elements) return;
    
    if (g_ui.screen) OLED_Clear();
    
    g_ui.screen = screen;
    g_ui.focused_idx = 0;
    g_ui.dirty.count = 0;
    
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg && elem->cfg->on_event && !(elem->state & UI_STATE_DISABLED)) {
            g_ui.focused_idx = i;
            elem->state |= UI_STATE_HIGHLIGHT;
            break;
        }
    }
    
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg) {
            elem->state |= UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
            ui_mark_dirty(elem);
        }
    }
    ui_flush();
}

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

void ui_focus_next(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start_idx = g_ui.focused_idx;
    uint8_t current_idx = g_ui.focused_idx;
    uint8_t try_count = 0;
    ui_element_t *old_focus = ui_get_focused_element();
    
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        ui_mark_dirty(old_focus);
    }
    
    bool found_valid = false;
    do {
        current_idx = (current_idx + 1) % g_ui.screen->elem_count;
        ui_element_t *candidate = (ui_element_t*)g_ui.screen->elements[current_idx];
        
        if (candidate && candidate->cfg && candidate->cfg->on_event && 
            !(candidate->state & UI_STATE_DISABLED)) {
            g_ui.focused_idx = current_idx;
            candidate->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(candidate);
            found_valid = true;
            break;
        }
        
        if (++try_count >= g_ui.screen->elem_count) break;
    } while (current_idx != start_idx);
    
    if (!found_valid) {
        g_ui.focused_idx = start_idx;
        if (old_focus) {
            old_focus->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(old_focus);
        }
    }
    ui_flush();
}

void ui_focus_prev(void) {
    if (!g_ui.screen || g_ui.screen->elem_count == 0) return;
    
    uint8_t start_idx = g_ui.focused_idx;
    uint8_t current_idx = g_ui.focused_idx;
    uint8_t try_count = 0;
    ui_element_t *old_focus = ui_get_focused_element();
    
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        ui_mark_dirty(old_focus);
    }
    
    bool found_valid = false;
    do {
        current_idx = (current_idx == 0) ? g_ui.screen->elem_count - 1 : current_idx - 1;
        ui_element_t *candidate = (ui_element_t*)g_ui.screen->elements[current_idx];
        
        if (candidate && candidate->cfg && candidate->cfg->on_event && 
            !(candidate->state & UI_STATE_DISABLED)) {
            g_ui.focused_idx = current_idx;
            candidate->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(candidate);
            found_valid = true;
            break;
        }
        
        if (++try_count >= g_ui.screen->elem_count) break;
    } while (current_idx != start_idx);
    
    if (!found_valid) {
        g_ui.focused_idx = start_idx;
        if (old_focus) {
            old_focus->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(old_focus);
        }
    }
    ui_flush();
}

uint8_t ui_focus_get(void) { return g_ui.focused_idx; }

void ui_focus_set(uint8_t idx) {
    if (!g_ui.screen || idx >= g_ui.screen->elem_count) return;
    
    ui_element_t *target = (ui_element_t*)g_ui.screen->elements[idx];
    if (!target || !target->cfg || (target->state & UI_STATE_DISABLED)) return;
    
    ui_element_t *old_focus = ui_get_focused_element();
    if (old_focus == target) return;
    
    if (old_focus) {
        old_focus->state &= ~UI_STATE_HIGHLIGHT;
        ui_mark_dirty(old_focus);
    }
    
    g_ui.focused_idx = idx;
    target->state |= UI_STATE_HIGHLIGHT;
    ui_mark_dirty(target);
    ui_flush();
}

void ui_tick(void) {
    if (!g_ui.input_bound || !g_ui.screen || g_ui.refreshing) return;
    
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    bool up = ui_read_btn(&g_binding.up);
    bool down = ui_read_btn(&g_binding.down);
    bool press = ui_read_btn(&g_binding.press);
    
    if (up) {
        if (ctx->up.state == 0) {
            ctx->up.last_press_time = now;
            ctx->up.state = 1;
        } else if (ctx->up.state == 1 && now - ctx->up.last_press_time >= g_cfg.long_press_ms) {
            ctx->up.state = 2;
            ctx->pending_evt = EVT_LONG_UP;
            ctx->evt_ready = true;
        }
    } else if (ctx->up.state >= 1) {
        uint32_t dur = now - ctx->up.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->up.last_release_time = now;
            if (ctx->up.state != 2 && dur < g_cfg.long_press_ms) {
                ctx->pending_evt = EVT_UP;
                ctx->evt_ready = true;
            }
            ctx->up.state = 0;
        }
    }
    
    if (down) {
        if (ctx->down.state == 0) {
            ctx->down.last_press_time = now;
            ctx->down.state = 1;
        } else if (ctx->down.state == 1 && now - ctx->down.last_press_time >= g_cfg.long_press_ms) {
            ctx->down.state = 2;
            ctx->pending_evt = EVT_LONG_DOWN;
            ctx->evt_ready = true;
        }
    } else if (ctx->down.state >= 1) {
        uint32_t dur = now - ctx->down.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->down.last_release_time = now;
            if (ctx->down.state != 2 && dur < g_cfg.long_press_ms) {
                ctx->pending_evt = EVT_DOWN;
                ctx->evt_ready = true;
            }
            ctx->down.state = 0;
        }
    }
    
    if (press) {
        if (ctx->press.state == 0) {
            ctx->press.last_press_time = now;
            ctx->press.state = 1;
            ui_element_t *elem = ui_get_focused_element();
            if (elem && !(elem->state & UI_STATE_DISABLED)) {
                elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY;
            }
        } else if (ctx->press.state == 1 && now - ctx->press.last_press_time >= g_cfg.long_press_ms) {
            ctx->press.state = 2;
            ctx->pending_evt = EVT_LONG_PRESS;
            ctx->evt_ready = true;
        }
    } else if (ctx->press.state >= 1) {
        uint32_t dur = now - ctx->press.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->press.last_release_time = now;
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
    
    if (ctx->evt_ready && ctx->pending_evt != EVT_NONE) {
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
        
        ui_element_t *elem = ui_get_focused_element();
        if (elem && elem->cfg && elem->cfg->on_event) {
            if (elem->cfg->on_event(elem, ctx->pending_evt)) {
                ctx->evt_ready = false;
                ctx->pending_evt = EVT_NONE;
            }
        } else {
            ctx->evt_ready = false;
            ctx->pending_evt = EVT_NONE;
        }
    }
}

void ui_mark_dirty(ui_element_t *elem) {
    if (!elem || !elem->cfg) return;
    
    elem->state |= UI_STATE_DIRTY;
    
    ui_rect_t dirty = {
        .x = (elem->cfg->x > 1) ? elem->cfg->x - 1 : 0,
        .y = (elem->cfg->y > 1) ? elem->cfg->y - 1 : 0,
        .w = elem->cfg->w + 2,
        .h = elem->cfg->h + 2
    };
    
    if (dirty.x + dirty.w > 128) dirty.w = 128 - dirty.x;
    if (dirty.y + dirty.h > 64) dirty.h = 64 - dirty.y;
    
    ui_dirty_add(&dirty);
}

void ui_mark_rect_dirty(const ui_rect_t *rect) {
    if (rect) ui_dirty_add(rect);
}

void ui_flush(void) {
    if (g_ui.refreshing || !g_ui.screen) return;
    
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
    
    for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
        ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];
        if (elem && elem->cfg && (elem->state & UI_STATE_DIRTY)) {
            if (elem->cfg->render) elem->cfg->render(elem);
            elem->state &= ~UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
}