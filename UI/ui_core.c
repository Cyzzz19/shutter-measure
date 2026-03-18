#include "ui_core.h"
#include "cmsis_os.h"

ui_system_t g_ui;

static const struct {
    uint16_t long_press_ms, double_click_ms, debounce_ms;
} g_cfg = {1000, 300, 50};

static ui_input_binding_t g_binding = {0};

static bool ui_read_btn(const ui_button_cfg_t *cfg) {
    if (!cfg || !cfg->port) return false;
    bool level = (HAL_GPIO_ReadPin(cfg->port, cfg->pin) == GPIO_PIN_RESET);
    return cfg->active_low ? level : !level;
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
    ui_rect_t new_rect = *rect;
    for (uint8_t i = 0; i < g_ui.dirty.count; ) {
        if (ui_rect_overlap(&new_rect, &g_ui.dirty.rects[i])) {
            ui_rect_merge(&new_rect, &new_rect, &g_ui.dirty.rects[i]);
            if (i < g_ui.dirty.count - 1)
                memmove(&g_ui.dirty.rects[i], &g_ui.dirty.rects[i+1], 
                        (g_ui.dirty.count - i - 1) * sizeof(ui_rect_t));
            g_ui.dirty.count--;
        } else { i++; }
    }
    if (g_ui.dirty.count >= UI_DIRTY_QUEUE_LEN) {
        g_ui.dirty.rects[0] = (ui_rect_t){0, 0, 128, 64};
        g_ui.dirty.count = 1;
    } else {
        g_ui.dirty.rects[g_ui.dirty.count++] = new_rect;
    }
}

void ui_init(void) {
    memset(&g_ui, 0, sizeof(g_ui));
    for (uint8_t i = 0; i < UI_POOL_SIZE; i++) {
        g_ui.pool[i].pool_id = i;
        g_ui.pool[i].state = UI_STATE_NORMAL;
    }
    OLED_Init();
    OLED_Clear();
}

const ui_screen_t* ui_get_current_screen(void) {
    return (g_ui.stack_size > 0) ? g_ui.screen_stack[g_ui.stack_size - 1] : NULL;
}

static void ui_activate_screen(const ui_screen_t *screen, bool is_push) {
    if (!screen) return;
    if (is_push && screen->on_enter) screen->on_enter(screen);
    
    g_ui.focused_idx = 0;
    g_ui.dirty.count = 0;
    for (uint8_t i = 0; i < screen->elem_count; i++) {
        ui_element_t *elem = (ui_element_t*)screen->elements[i];
        if (elem && elem->cfg) {
            elem->state |= UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
            if (elem->cfg->on_event && !(elem->state & UI_STATE_DISABLED)) {
                if (g_ui.focused_idx == 0) { // 简单策略：第一个有效元素
                     elem->state |= UI_STATE_HIGHLIGHT;
                }
            }
            ui_mark_dirty(elem);
        }
    }
    // 修正焦点高亮
    ui_focus_set(g_ui.focused_idx); 
    ui_flush();
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen) return;
    // 退出旧栈
    while (g_ui.stack_size > 0) {
        const ui_screen_t *old = g_ui.screen_stack[--g_ui.stack_size];
        if (old && old->on_exit) old->on_exit(old);
    }
    OLED_Clear();
    g_ui.screen_stack[g_ui.stack_size++] = screen;
    ui_activate_screen(screen, true);
}

bool ui_push_screen(const ui_screen_t *screen) {
    if (!screen || g_ui.stack_size >= UI_MAX_STACK_DEPTH) return false;
    g_ui.screen_stack[g_ui.stack_size++] = screen;
    ui_activate_screen(screen, true);
    return true;
}

bool ui_pop_screen(void) {
    if (g_ui.stack_size <= 1) return false; // 保留至少一个
    const ui_screen_t *old = g_ui.screen_stack[--g_ui.stack_size];
    if (old && old->on_exit) old->on_exit(old);
    
    const ui_screen_t *new = g_ui.screen_stack[g_ui.stack_size - 1];
    ui_activate_screen(new, false);
    return true;
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
    const ui_screen_t *scr = ui_get_current_screen();
    if (!scr) return;
    uint8_t start = g_ui.focused_idx, cur = g_ui.focused_idx, tries = 0;
    ui_element_t *old = ui_get_focused_element();
    if (old) { old->state &= ~UI_STATE_HIGHLIGHT; ui_mark_dirty(old); }
    bool found = false;
    do {
        cur = (cur + 1) % scr->elem_count;
        ui_element_t *cand = (ui_element_t*)scr->elements[cur];
        if (cand && cand->cfg && cand->cfg->on_event && !(cand->state & UI_STATE_DISABLED)) {
            g_ui.focused_idx = cur;
            cand->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(cand);
            found = true; break;
        }
    } while (++tries < scr->elem_count && cur != start);
    if (!found && old) { old->state |= UI_STATE_HIGHLIGHT; ui_mark_dirty(old); }
    ui_flush();
}

void ui_focus_prev(void) {
    const ui_screen_t *scr = ui_get_current_screen();
    if (!scr) return;
    uint8_t start = g_ui.focused_idx, cur = g_ui.focused_idx, tries = 0;
    ui_element_t *old = ui_get_focused_element();
    if (old) { old->state &= ~UI_STATE_HIGHLIGHT; ui_mark_dirty(old); }
    bool found = false;
    do {
        cur = (cur == 0) ? scr->elem_count - 1 : cur - 1;
        ui_element_t *cand = (ui_element_t*)scr->elements[cur];
        if (cand && cand->cfg && cand->cfg->on_event && !(cand->state & UI_STATE_DISABLED)) {
            g_ui.focused_idx = cur;
            cand->state |= UI_STATE_HIGHLIGHT;
            ui_mark_dirty(cand);
            found = true; break;
        }
    } while (++tries < scr->elem_count && cur != start);
    if (!found && old) { old->state |= UI_STATE_HIGHLIGHT; ui_mark_dirty(old); }
    ui_flush();
}

uint8_t ui_focus_get(void) { return g_ui.focused_idx; }
void ui_focus_set(uint8_t idx) {
    const ui_screen_t *scr = ui_get_current_screen();
    if (!scr || idx >= scr->elem_count) return;
    ui_element_t *target = (ui_element_t*)scr->elements[idx];
    if (!target || !target->cfg || (target->state & UI_STATE_DISABLED)) return;
    ui_element_t *old = ui_get_focused_element();
    if (old == target) return;
    if (old) { old->state &= ~UI_STATE_HIGHLIGHT; ui_mark_dirty(old); }
    g_ui.focused_idx = idx;
    target->state |= UI_STATE_HIGHLIGHT;
    ui_mark_dirty(target);
    ui_flush();
}

void ui_tick(void) {
    if (!g_ui.input_bound || g_ui.stack_size == 0 || g_ui.refreshing) return;
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    bool up = ui_read_btn(&g_binding.up);
    bool down = ui_read_btn(&g_binding.down);
    bool press = ui_read_btn(&g_binding.press);

    // 简化状态机逻辑 (同原逻辑，略写以节省空间)
    // Up
    if (up) {
        if (ctx->up.state == 0) { ctx->up.last_press_time = now; ctx->up.state = 1; }
        else if (ctx->up.state == 1 && now - ctx->up.last_press_time >= g_cfg.long_press_ms) {
            ctx->up.state = 2; ctx->pending_evt = EVT_LONG_UP; ctx->evt_ready = true;
        }
    } else if (ctx->up.state >= 1) {
        uint32_t dur = now - ctx->up.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->up.last_release_time = now;
            if (ctx->up.state != 2 && dur < g_cfg.long_press_ms) { ctx->pending_evt = EVT_UP; ctx->evt_ready = true; }
            ctx->up.state = 0;
        }
    }
    // Down
    if (down) {
        if (ctx->down.state == 0) { ctx->down.last_press_time = now; ctx->down.state = 1; }
        else if (ctx->down.state == 1 && now - ctx->down.last_press_time >= g_cfg.long_press_ms) {
            ctx->down.state = 2; ctx->pending_evt = EVT_LONG_DOWN; ctx->evt_ready = true;
        }
    } else if (ctx->down.state >= 1) {
        uint32_t dur = now - ctx->down.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->down.last_release_time = now;
            if (ctx->down.state != 2 && dur < g_cfg.long_press_ms) { ctx->pending_evt = EVT_DOWN; ctx->evt_ready = true; }
            ctx->down.state = 0;
        }
    }
    // Press
    if (press) {
        if (ctx->press.state == 0) {
            ctx->press.last_press_time = now; ctx->press.state = 1;
            ui_element_t *elem = ui_get_focused_element();
            if (elem && !(elem->state & UI_STATE_DISABLED)) { elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY; }
        } else if (ctx->press.state == 1 && now - ctx->press.last_press_time >= g_cfg.long_press_ms) {
            ctx->press.state = 2; ctx->pending_evt = EVT_LONG_PRESS; ctx->evt_ready = true;
        }
    } else if (ctx->press.state >= 1) {
        uint32_t dur = now - ctx->press.last_press_time;
        if (dur >= g_cfg.debounce_ms) {
            ctx->press.last_release_time = now;
            ui_element_t *elem = ui_get_focused_element();
            if (elem) { elem->state &= ~UI_STATE_PRESSED; elem->state |= UI_STATE_DIRTY; }
            if (ctx->press.state != 2 && dur < g_cfg.long_press_ms) { ctx->pending_evt = EVT_PRESS; ctx->evt_ready = true; }
            ctx->press.state = 0;
        }
    }

    if (ctx->evt_ready && ctx->pending_evt != EVT_NONE) {
        if (ctx->pending_evt == EVT_UP) { ui_focus_prev(); ctx->evt_ready = false; ctx->pending_evt = EVT_NONE; return; }
        if (ctx->pending_evt == EVT_DOWN) { ui_focus_next(); ctx->evt_ready = false; ctx->pending_evt = EVT_NONE; return; }
        ui_element_t *elem = ui_get_focused_element();
        if (elem && elem->cfg && elem->cfg->on_event) {
            if (elem->cfg->on_event(elem, ctx->pending_evt)) { ctx->evt_ready = false; ctx->pending_evt = EVT_NONE; }
        } else { ctx->evt_ready = false; ctx->pending_evt = EVT_NONE; }
    }
}

void ui_mark_dirty(ui_element_t *elem) {
    if (!elem || !elem->cfg) return;
    elem->state |= UI_STATE_DIRTY;
    ui_rect_t dirty = {
        .x = (elem->cfg->x > 1) ? elem->cfg->x - 1 : 0,
        .y = (elem->cfg->y > 1) ? elem->cfg->y - 1 : 0,
        .w = elem->cfg->w + 2, .h = elem->cfg->h + 2
    };
    if (dirty.x + dirty.w > 128) dirty.w = 128 - dirty.x;
    if (dirty.y + dirty.h > 64) dirty.h = 64 - dirty.y;
    ui_dirty_add(&dirty);
}
void ui_mark_rect_dirty(const ui_rect_t *rect) { if (rect) ui_dirty_add(rect); }

void ui_flush(void) {
    if (g_ui.refreshing || g_ui.stack_size == 0) return;
    const ui_screen_t *scr = g_ui.screen_stack[g_ui.stack_size - 1];
    if (g_ui.dirty.count == 0) {
        bool has_dirty = false;
        for (uint8_t j = 0; j < scr->elem_count; j++) {
            ui_element_t *elem = (ui_element_t*)scr->elements[j];
            if (elem && (elem->state & UI_STATE_DIRTY)) { has_dirty = true; break; }
        }
        if (!has_dirty) return;
    }
    g_ui.refreshing = true;
    if (g_ui.dirty.count > 0) {
        for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
            for (uint8_t j = 0; j < scr->elem_count; j++) {
                ui_element_t *elem = (ui_element_t*)scr->elements[j];
                if (!elem || !elem->cfg) continue;
                ui_rect_t elem_rect = {elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
                if (ui_rect_overlap(&elem_rect, &g_ui.dirty.rects[i])) elem->state |= UI_STATE_DIRTY;
            }
        }
    }
    for (uint8_t j = 0; j < scr->elem_count; j++) {
        ui_element_t *elem = (ui_element_t*)scr->elements[j];
        if (elem && elem->cfg && (elem->state & UI_STATE_DIRTY)) {
            if (elem->cfg->render) elem->cfg->render(elem);
            elem->state &= ~UI_STATE_DIRTY;
            elem->last_box = (ui_rect_t){elem->cfg->x, elem->cfg->y, elem->cfg->w, elem->cfg->h};
        }
    }
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
}