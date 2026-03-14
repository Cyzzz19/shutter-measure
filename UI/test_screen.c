#include "ui_core.h"
#include <string.h>
#include <stdio.h>

static uint32_t g_press_count = 0;

/* ================= 渲染函数 - 修正 Y 坐标为页模式 ================= */
static void render_text(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    /* 清区域：Y 坐标转换为页号 */
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    /* 高亮反色（顶部）*/
    if (self->state & UI_STATE_HIGHLIGHT) {
        uint8_t hl_page = self->cfg->y / 8;
        OLED_Fill(self->cfg->x, hl_page, self->cfg->x + self->cfg->w - 1, hl_page, 0xFF);
    }
    
    /* 显示文本：Y 坐标必须 /8 转换为页号 */
    char buf[32] = {0};
    const char *text = self->cfg->text;
    
    if (self->data_binding) {
        uint32_t *val = (uint32_t*)self->data_binding;
        snprintf(buf, sizeof(buf), "Press: %lu", *val);
        text = buf;
    }
    
    if (text) {
        /* 关键修复：y / 8 转换为页号 */
        OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t*)text, 16);
    }
}

static void render_button(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    /* 清区域：Y 坐标转换为页号 */
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    /* 画边框 */
    uint8_t top_page = self->cfg->y / 8;
    OLED_ShowString(self->cfg->x, top_page, (uint8_t*)"[", 16);
    OLED_ShowString(self->cfg->x + self->cfg->w - 8, top_page, (uint8_t*)"]", 16);
    
    /* 按下态填充 */
    if (self->state & UI_STATE_PRESSED) {
        for (uint8_t p = page_start; p <= page_end; p++) {
            OLED_Fill(self->cfg->x + 2, p, self->cfg->x + self->cfg->w - 3, p, 0xFF);
        }
    }
    
    /* 显示文本：Y 坐标必须 /8 */
    if (self->cfg->text) {
        uint8_t tx = self->cfg->x + (self->cfg->w - strlen(self->cfg->text) * 8) / 2;
        uint8_t ty_page = self->cfg->y / 8;
        OLED_ShowString(tx, ty_page, (uint8_t*)self->cfg->text, 16);
    }
}

/* ================= 事件回调 ================= */
static bool on_button_event(ui_element_t *self, ui_event_code_t evt) {
    if (!self) return false;
    
    if (evt == EVT_PRESS) {
        self->state ^= UI_STATE_HIGHLIGHT;
        self->state |= UI_STATE_DIRTY;
        g_press_count++;
        return true;
    }
    if (evt == EVT_LONG_PRESS) {
        g_press_count = 0;
        self->state |= UI_STATE_DIRTY;
        return true;
    }
    return false;
}

/* ================= 配置 - 使用像素坐标（直观） ================= */
static const ui_element_cfg_t cfg_title = {
    .x = 0, .y = 0, .w = 128, .h = 16,
    .text = "BTN Monitor",
    .render = render_text,
    .on_event = NULL
};

static const ui_element_cfg_t cfg_status = {
    .x = 0, .y = 16, .w = 128, .h = 16,
    .text = "Press: 0",
    .render = render_text,
    .on_event = NULL
};

static const ui_element_cfg_t cfg_btn_ok = {
    .x = 48, .y = 32, .w = 32, .h = 20,
    .text = "OK",
    .render = render_button,
    .on_event = on_button_event
};

/* ================= 元素状态 ================= */
static ui_element_t elem_title = {
    .cfg = &cfg_title,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 0,
    .last_box = {0, 0, 128, 16}
};

static ui_element_t elem_status = {
    .cfg = &cfg_status,
    .data_binding = &g_press_count,
    .state = UI_STATE_NORMAL,
    .pool_id = 1,
    .last_box = {0, 16, 128, 16}
};

static ui_element_t elem_btn_ok = {
    .cfg = &cfg_btn_ok,
    .data_binding = NULL,
    .state = UI_STATE_NORMAL,
    .pool_id = 2,
    .last_box = {48, 32, 32, 20}
};

static const ui_element_t *screen_elems[] = {
    &elem_title,
    &elem_status,
    &elem_btn_ok
};

const ui_screen_t test_screen = {
    .name = "Test",
    .elem_count = 3,
    .elements = screen_elems
};

void test_screen_update(void) {
    elem_status.state |= UI_STATE_DIRTY;
}