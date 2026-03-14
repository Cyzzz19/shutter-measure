#include "ui_core.h"
#include <stdio.h>

/* ================= 元素渲染函数 ================= */
/* 文本元素渲染：支持动态绑定数据 */
static bool render_text(ui_element_t *self, uint8_t *fb, const ui_rect_t *clip) {
    if (!self || !self->cfg) return false;
    
    /* 高亮态：反色显示 */
    bool highlight = (self->state & UI_STATE_HIGHLIGHT);
    
    /* 获取显示文本：优先使用绑定数据，否则用配置文本 */
    char buf[32] = {0};
    const char *text = self->cfg->text;
    if (self->data_binding) {
        /* 假设绑定的是 uint32_t* 类型 */
        uint32_t *val = (uint32_t*)self->data_binding;
        snprintf(buf, sizeof(buf), "%s: %lu", text, *val);
        text = buf;
    }
    
    /* 调用底层 OLED 函数绘制 */
    /* 注意：OLED_ShowString 应只修改 framebuffer，不立即刷新 */
    OLED_ShowString(self->cfg->x, self->cfg->y, (uint8_t*)text, 16);
    
    /* 更新 last_box */
    self->last_box = (ui_rect_t){self->cfg->x, self->cfg->y, self->cfg->w, self->cfg->h};
    return true;
}

/* 按钮元素渲染：带边框和状态反馈 */
static bool render_button(ui_element_t *self, uint8_t *fb, const ui_rect_t *clip) {
    if (!self || !self->cfg) return false;
    
    /* 绘制边框 */
    OLED_Fill(self->cfg->x, self->cfg->y, 
              self->cfg->x + self->cfg->w - 1, self->cfg->y + self->cfg->h - 1, 0);
    
    /* 状态样式 */
    if (self->state & UI_STATE_DISABLED) {
        /* 禁用态：虚线边框 + 灰色文本 */
        OLED_DrawPoint(self->cfg->x, self->cfg->y, 1);  // 简化：只画角点
    } else if (self->state & UI_STATE_PRESSED) {
        /* 按下态：填充背景 */
        OLED_Fill(self->cfg->x+1, self->cfg->y+1, 
                  self->cfg->x+self->cfg->w-2, self->cfg->y+self->cfg->h-2, 1);
    } else if (self->state & UI_STATE_HIGHLIGHT) {
        /* 高亮态：反色边框 */
        OLED_Fill(self->cfg->x, self->cfg->y, 
                  self->cfg->x+self->cfg->w-1, self->cfg->y+1, 1);  // 上边框
    }
    
    /* 绘制文本 */
    if (self->cfg->text) {
        uint8_t tx = self->cfg->x + (self->cfg->w - strlen(self->cfg->text)*8)/2;
        uint8_t ty = self->cfg->y + (self->cfg->h - 16)/2;
        OLED_ShowString(tx, ty, (uint8_t*)self->cfg->text, 16);
    }
    
    self->last_box = (ui_rect_t){self->cfg->x, self->cfg->y, self->cfg->w, self->cfg->h};
    return true;
}

/* 事件处理：按钮点击反馈 */
static bool on_button_event(ui_element_t *self, ui_event_code_t evt) {
    if (self->state & UI_STATE_DISABLED) return false;
    
    switch (evt) {
        case EVT_PRESS:
            /* 短按：切换高亮状态（测试用）*/
            self->state ^= UI_STATE_HIGHLIGHT;
            self->state |= UI_STATE_DIRTY;
            ui_mark_dirty(self);
            return true;
        case EVT_LONG_PRESS:
            /* 长按：禁用/启用切换 */
            if (self->state & UI_STATE_DISABLED) {
                self->state &= ~UI_STATE_DISABLED;
            } else {
                self->state |= UI_STATE_DISABLED;
            }
            self->state |= UI_STATE_DIRTY;
            ui_mark_dirty(self);
            return true;
        default:
            return false;
    }
}

/* ================= 配置数据（存放在 Flash） ================= */
/* 元素配置数组 */
static const ui_element_cfg_t cfg_title = {
    .x = 0, .y = 0, .w = 128, .h = 16,
    .type_id = 0,
    .text = "BTN Monitor",
    .render = render_text,
    .on_event = NULL
};

static const ui_element_cfg_t cfg_status = {
    .x = 0, .y = 16, .w = 128, .h = 16,
    .type_id = 0,
    .text = "Press: --",
    .render = render_text,
    .on_event = NULL
};

static const ui_element_cfg_t cfg_btn_up = {
    .x = 10, .y = 32, .w = 30, .h = 20,
    .type_id = 1,
    .text = "UP",
    .render = render_button,
    .on_event = on_button_event
};

static const ui_element_cfg_t cfg_btn_down = {
    .x = 48, .y = 32, .w = 30, .h = 20,
    .type_id = 1,
    .text = "DN",
    .render = render_button,
    .on_event = on_button_event
};

static const ui_element_cfg_t cfg_btn_ok = {
    .x = 86, .y = 32, .w = 30, .h = 20,
    .type_id = 1,
    .text = "OK",
    .render = render_button,
    .on_event = on_button_event
};

/* 状态元素（存放在 RAM 对象池）*/
/* 注意：这些是模板，实际使用时从对象池分配并初始化 */
static ui_element_t elem_title = {
    .cfg = &cfg_title,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 0
};

static ui_element_t elem_status = {
    .cfg = &cfg_status,
    .data_binding = NULL,  // 可绑定一个 uint32_t 变量显示计数
    .state = UI_STATE_NORMAL,
    .pool_id = 1
};

static ui_element_t elem_btn_up = {
    .cfg = &cfg_btn_up,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 2
};

static ui_element_t elem_btn_down = {
    .cfg = &cfg_btn_down,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 3
};

static ui_element_t elem_btn_ok = {
    .cfg = &cfg_btn_ok,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 4
};

/* 屏幕定义 */
static const ui_element_t *screen_elems[] = {
    &elem_title,
    &elem_status,
    &elem_btn_up,
    &elem_btn_down,
    &elem_btn_ok,
};

const ui_screen_t test_screen = {
    .name = "TestScreen",
    .elem_count = 5,
    .elements = screen_elems
};

/* ================= 界面逻辑：实时更新按钮状态 ================= */
/* 全局变量：记录按键次数（演示数据绑定）*/
static uint32_t g_press_count = 0;

void test_screen_update(void) {
    /* 更新状态文本：显示按键次数 */
    elem_status.data_binding = &g_press_count;
    elem_status.state |= UI_STATE_DIRTY;
    ui_mark_dirty(&elem_status);
    
    /* 根据输入事件更新计数 */
    if (g_ui.input.evt_ready) {
        switch (g_ui.input.pending_evt) {
            case EVT_PRESS:
                g_press_count++;
                break;
            case EVT_LONG_PRESS:
                g_press_count = 0;  // 长按清零
                break;
            case EVT_DOUBLE_CLICK:
                g_press_count += 10;  // 双击加 10（测试用）
                break;
            default:
                break;
        }
        g_ui.input.evt_ready = false;
    }
}