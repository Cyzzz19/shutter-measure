#include "ui_core.h"

/* 全局系统实例 */
ui_system_t g_ui;

/* 输入配置默认值 */
static const ui_input_cfg_t g_input_cfg = {
    .long_press_threshold_ms = 1000,
    .double_click_gap_ms = 300,
    .debounce_ms = 20
};

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

/* ================= 脏队列管理 ================= */
static void ui_dirty_add(const ui_rect_t *rect) {
    if (g_ui.dirty.count >= UI_DIRTY_QUEUE_LEN) {
        /* 队列满：简单策略 - 标记全局刷新（或丢弃，根据需求）*/
        g_ui.dirty.count = 0;  // 降级为全刷
        ui_rect_t full = {0, 0, 128, 64};
        g_ui.dirty.rects[0] = full;
        g_ui.dirty.count = 1;
        return;
    }
    /* 尝试合并：如果新区域与现有区域重叠或相邻<2 像素，合并 */
    for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
        ui_rect_t *exist = &g_ui.dirty.rects[i];
        if (ui_rect_overlap(rect, exist) || 
            (abs(rect->x - (exist->x + exist->w)) < 2) ||  // 水平相邻
            (abs(rect->y - (exist->y + exist->h)) < 2)) {  // 垂直相邻
            ui_rect_merge(exist, exist, rect);
            return;
        }
    }
    /* 无法合并：添加新区域 */
    g_ui.dirty.rects[g_ui.dirty.count++] = *rect;
}

/* ================= 局部刷新执行 ================= */
void ui_flush(void) {
    if (g_ui.refreshing || g_ui.dirty.count == 0) return;
    g_ui.refreshing = true;
    
    for (uint8_t i = 0; i < g_ui.dirty.count; i++) {
        ui_rect_t *r = &g_ui.dirty.rects[i];
        
        /* 关键：清除残影策略 - 先填充背景色，再绘制新内容 */
        /* 假设你的 OLED 驱动支持局部填充：OLED_Fill */
        OLED_Fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, 0);  // 清黑
        
        /* 遍历所有元素，重绘与脏区域重叠的部分 */
        for (uint8_t j = 0; j < g_ui.screen->elem_count; j++) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[j];  // const cast for render
            if (elem && (elem->state & UI_STATE_DIRTY) && 
                ui_rect_overlap(&elem->last_box, r)) {
                /* 调用元素渲染函数，传入裁剪区域 */
                if (elem->cfg->render) {
                    elem->cfg->render(elem, g_ui.framebuffer, r);
                }
                elem->state &= ~UI_STATE_DIRTY;  // 清除脏标志
            }
        }
        
        /* 通过 I2C 发送局部数据到 OLED */
        /* 假设 OLED_Set_Pos 设置窗口，OLED_Send_Data 发送原始字节 */
        OLED_Set_Pos(r->x, r->y);
        /* 计算显存偏移：128x64 1bpp = 1024 字节，按行排列 */
        uint32_t offset = (r->y / 8) * 128 + r->x;  // 页模式简化计算
        /* 注意：实际发送需考虑页对齐，这里简化示意 */
        for (uint8_t page = r->y/8; page <= (r->y + r->h - 1)/8; page++) {
            OLED_Set_Pos(r->x, page * 8);
            /* 发送一页内的数据... 实际需按 SSD1306 协议处理 */
        }
    }
    
    g_ui.dirty.count = 0;
    g_ui.refreshing = false;
}

/* ================= 输入检测（非阻塞状态机） ================= */
void ui_poll_inputs(GPIO_TypeDef *port, uint16_t pin_up, uint16_t pin_down, uint16_t pin_press) {
    ui_input_ctx_t *ctx = &g_ui.input;
    uint32_t now = HAL_GetTick();
    
    /* 读取 GPIO 状态（假设低电平有效）*/
    bool up = (HAL_GPIO_ReadPin(port, pin_up) == GPIO_PIN_RESET);
    bool down = (HAL_GPIO_ReadPin(port, pin_down) == GPIO_PIN_RESET);
    bool press = (HAL_GPIO_ReadPin(port, pin_press) == GPIO_PIN_RESET);
    
    /* 简化：只处理 press 键作为示例，上/下拨同理 */
    if (press) {
        if (ctx->key_state == 0) {  // 上升沿：按下
            ctx->last_press_time = now;
            ctx->key_state = 1;     // PRESSED
            ctx->click_count++;
            /* 标记焦点元素为按下态 + 脏 */
            if (g_ui.focused_idx < g_ui.screen->elem_count) {
                ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
                if (elem && !(elem->state & UI_STATE_DISABLED)) {
                    elem->state |= UI_STATE_PRESSED | UI_STATE_DIRTY;
                    ui_mark_dirty(elem);
                }
            }
        } else if (ctx->key_state == 1) {  // 持续按下：检测长按
            if (now - ctx->last_press_time >= g_input_cfg.long_press_threshold_ms) {
                ctx->key_state = 2;  // LONG_PRESSED
                ctx->pending_evt = EVT_LONG_PRESS;
                ctx->evt_ready = true;
            }
        }
    } else {
        if (ctx->key_state >= 1) {  // 下降沿：释放
            uint32_t duration = now - ctx->last_press_time;
            ctx->last_release_time = now;
            
            if (ctx->key_state == 2) {
                /* 长按已触发，不再触发短按 */
                ctx->pending_evt = EVT_NONE;
            } else if (duration < g_input_cfg.long_press_threshold_ms) {
                /* 短按：检查双击 */
                if (ctx->click_count == 2 && 
                    (now - ctx->last_release_time) < g_input_cfg.double_click_gap_ms) {
                    ctx->pending_evt = EVT_DOUBLE_CLICK;
                    ctx->click_count = 0;
                } else {
                    ctx->pending_evt = EVT_PRESS;
                }
                ctx->evt_ready = true;
            }
            ctx->key_state = 0;
            
            /* 清除元素按下态 + 标记脏 */
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
    
    /* 消抖：简单延时过滤，实际可用定时器 */
    HAL_Delay(g_input_cfg.debounce_ms);
}

void ui_tick(void) {
    /* 每 10ms 调用：驱动长按检测和时间相关动画 */
    ui_poll_inputs(GPIOA, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3);  // 根据你的 GPIO 调整
    
    /* 如果有待处理事件，分发给焦点元素 */
    if (g_ui.input.evt_ready && g_ui.input.pending_evt != EVT_NONE) {
        if (g_ui.focused_idx < g_ui.screen->elem_count) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[g_ui.focused_idx];
            if (elem && elem->cfg->on_event) {
                if (elem->cfg->on_event(elem, g_ui.input.pending_evt)) {
                    /* 事件被消耗：清除标志 */
                    g_ui.input.evt_ready = false;
                    g_ui.input.pending_evt = EVT_NONE;
                }
            }
        }
    }
}

/* ================= 元素管理 ================= */
void ui_mark_dirty(ui_element_t *elem) {
    if (!elem) return;
    elem->state |= UI_STATE_DIRTY;
    /* 添加区域到脏队列：包含 1 像素边框以防残影 */
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

void ui_init(uint8_t *fb) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.framebuffer = fb;
    g_ui.input.key_state = 0;
    OLED_Clear();  // 初始全清
}

void ui_set_screen(const ui_screen_t *screen) {
    if (!screen) return;
    /* 标记旧界面所有元素为脏（用于清除）*/
    if (g_ui.screen) {
        for (uint8_t i = 0; i < g_ui.screen->elem_count; i++) {
            ui_element_t *elem = (ui_element_t*)g_ui.screen->elements[i];
            if (elem) ui_mark_dirty(elem);
        }
        ui_flush();  // 先清除旧内容
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
    ui_flush();  // 绘制新界面
}