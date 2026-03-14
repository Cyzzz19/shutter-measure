/* test_screen.c - 使用反色函数 */
#include "ui_core.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os.h"

static uint32_t g_press_count_up = 0;
static uint32_t g_press_count_down = 0;
static uint32_t g_press_count_ok = 0;

static ui_element_cfg_t *cfg_title = NULL;
static ui_element_cfg_t *cfg_count_up = NULL;
static ui_element_cfg_t *cfg_count_down = NULL;
static ui_element_cfg_t *cfg_count_ok = NULL;
static ui_element_cfg_t *cfg_btn_up = NULL;
static ui_element_cfg_t *cfg_btn_down = NULL;
static ui_element_cfg_t *cfg_btn_ok = NULL;

static ui_element_t *elem_title = NULL;
static ui_element_t *elem_count_up = NULL;
static ui_element_t *elem_count_down = NULL;
static ui_element_t *elem_count_ok = NULL;
static ui_element_t *elem_btn_up = NULL;
static ui_element_t *elem_btn_down = NULL;
static ui_element_t *elem_btn_ok = NULL;

static ui_element_t **screen_elems = NULL;
static ui_screen_t *test_screen_ptr = NULL;

/* 渲染函数 */
static void render_text(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    /* 清空区域 */
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    /* 高亮：使用反色函数 */
    if (self->state & UI_STATE_HIGHLIGHT) {
        OLED_Invert_Rect(self->cfg->x, self->cfg->y, 
                        self->cfg->x + self->cfg->w - 1, self->cfg->y + 2);
    }
    
    /* 显示文本 */
    char buf[32] = {0};
    if (self->data_binding) {
        uint32_t *val = (uint32_t*)self->data_binding;
        snprintf(buf, sizeof(buf), "%s: %lu", self->cfg->text, *val);
        OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t*)buf, 16);
    } else if (self->cfg->text) {
        OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t*)self->cfg->text, 16);
    }
}

static void render_button(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    /* 清空区域 */
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    const char *btn_text = self->cfg->text ? self->cfg->text : "BTN";
    uint8_t tx = self->cfg->x + (self->cfg->w - strlen(btn_text) * 8) / 2;
    uint8_t ty_page = self->cfg->y / 8;
    
    /* 按下态：反色填充 */
    if (self->state & UI_STATE_PRESSED) {
        OLED_Invert_Rect(self->cfg->x + 1, self->cfg->y,
                        self->cfg->x + self->cfg->w - 2, self->cfg->y + self->cfg->h - 1);
        OLED_ShowString(tx, ty_page, (uint8_t*)btn_text, 16);
    } else if (self->state & UI_STATE_HIGHLIGHT) {
        /* 高亮态：反色边框 */
        OLED_Invert_Rect(self->cfg->x, ty_page,
                        self->cfg->x + self->cfg->w - 1, ty_page + 1);
        OLED_ShowString(self->cfg->x, ty_page, (uint8_t*)"[", 16);
        OLED_ShowString(self->cfg->x + self->cfg->w - 8, ty_page, (uint8_t*)"]", 16);
        OLED_ShowString(tx, ty_page, (uint8_t*)btn_text, 16);
    } else {
        /* 正常态 */
        OLED_ShowString(self->cfg->x, ty_page, (uint8_t*)"[", 16);
        OLED_ShowString(self->cfg->x + self->cfg->w - 8, ty_page, (uint8_t*)"]", 16);
        OLED_ShowString(tx, ty_page, (uint8_t*)btn_text, 16);
    }
}

/* 事件回调 */
static bool on_button_event(ui_element_t *self, ui_event_code_t evt) {
    if (!self) return false;
    
    if (evt != EVT_PRESS && evt != EVT_LONG_PRESS) return false;
    
    if (self == elem_btn_up) {
        if (evt == EVT_PRESS) g_press_count_up++;
        else if (evt == EVT_LONG_PRESS) g_press_count_up = 0;
        if (elem_count_up) elem_count_up->state |= UI_STATE_DIRTY;
        return true;
    }
    
    if (self == elem_btn_down) {
        if (evt == EVT_PRESS) g_press_count_down++;
        else if (evt == EVT_LONG_PRESS) g_press_count_down = 0;
        if (elem_count_down) elem_count_down->state |= UI_STATE_DIRTY;
        return true;
    }
    
    if (self == elem_btn_ok) {
        if (evt == EVT_PRESS) g_press_count_ok++;
        else if (evt == EVT_LONG_PRESS) g_press_count_ok = 0;
        if (elem_count_ok) elem_count_ok->state |= UI_STATE_DIRTY;
        return true;
    }
    
    return false;
}

/* 初始化函数 */
bool test_screen_init(void) {
    /* 分配配置 */
    cfg_title = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_count_up = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_count_down = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_count_ok = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_btn_up = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_btn_down = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_btn_ok = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    
    if (!cfg_title || !cfg_count_up || !cfg_count_down || !cfg_count_ok ||
        !cfg_btn_up || !cfg_btn_down || !cfg_btn_ok) return false;
    
    /* 标题 */
    memset(cfg_title, 0, sizeof(ui_element_cfg_t));
    cfg_title->x = 0; cfg_title->y = 0; cfg_title->w = 128; cfg_title->h = 16;
    cfg_title->text = "Key Test";
    cfg_title->render = render_text;
    cfg_title->on_event = NULL;
    
    /* 计数 */
    memset(cfg_count_up, 0, sizeof(ui_element_cfg_t));
    cfg_count_up->x = 0; cfg_count_up->y = 16; cfg_count_up->w = 40; cfg_count_up->h = 16;
    cfg_count_up->text = "UP";
    cfg_count_up->render = render_text;
    cfg_count_up->on_event = NULL;
    
    memset(cfg_count_down, 0, sizeof(ui_element_cfg_t));
    cfg_count_down->x = 44; cfg_count_down->y = 16; cfg_count_down->w = 40; cfg_count_down->h = 16;
    cfg_count_down->text = "DN";
    cfg_count_down->render = render_text;
    cfg_count_down->on_event = NULL;
    
    memset(cfg_count_ok, 0, sizeof(ui_element_cfg_t));
    cfg_count_ok->x = 88; cfg_count_ok->y = 16; cfg_count_ok->w = 40; cfg_count_ok->h = 16;
    cfg_count_ok->text = "OK";
    cfg_count_ok->render = render_text;
    cfg_count_ok->on_event = NULL;
    
    /* 按钮 */
    memset(cfg_btn_up, 0, sizeof(ui_element_cfg_t));
    cfg_btn_up->x = 0; cfg_btn_up->y = 32; cfg_btn_up->w = 40; cfg_btn_up->h = 20;
    cfg_btn_up->text = "UP";
    cfg_btn_up->render = render_button;
    cfg_btn_up->on_event = on_button_event;
    
    memset(cfg_btn_down, 0, sizeof(ui_element_cfg_t));
    cfg_btn_down->x = 44; cfg_btn_down->y = 32; cfg_btn_down->w = 40; cfg_btn_down->h = 20;
    cfg_btn_down->text = "DN";
    cfg_btn_down->render = render_button;
    cfg_btn_down->on_event = on_button_event;
    
    memset(cfg_btn_ok, 0, sizeof(ui_element_cfg_t));
    cfg_btn_ok->x = 88; cfg_btn_ok->y = 32; cfg_btn_ok->w = 40; cfg_btn_ok->h = 20;
    cfg_btn_ok->text = "OK";
    cfg_btn_ok->render = render_button;
    cfg_btn_ok->on_event = on_button_event;
    
    /* 分配元素 */
    elem_title = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_count_up = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_count_down = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_count_ok = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_btn_up = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_btn_down = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_btn_ok = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    
    if (!elem_title || !elem_count_up || !elem_count_down || !elem_count_ok ||
        !elem_btn_up || !elem_btn_down || !elem_btn_ok) return false;
    
    memset(elem_title, 0, sizeof(ui_element_t));
    elem_title->cfg = cfg_title;
    elem_title->last_box = (ui_rect_t){0, 0, 128, 16};
    elem_title->pool_id = 0;
    
    memset(elem_count_up, 0, sizeof(ui_element_t));
    elem_count_up->cfg = cfg_count_up;
    elem_count_up->data_binding = &g_press_count_up;
    elem_count_up->last_box = (ui_rect_t){0, 16, 40, 16};
    elem_count_up->pool_id = 1;
    
    memset(elem_count_down, 0, sizeof(ui_element_t));
    elem_count_down->cfg = cfg_count_down;
    elem_count_down->data_binding = &g_press_count_down;
    elem_count_down->last_box = (ui_rect_t){44, 16, 40, 16};
    elem_count_down->pool_id = 2;
    
    memset(elem_count_ok, 0, sizeof(ui_element_t));
    elem_count_ok->cfg = cfg_count_ok;
    elem_count_ok->data_binding = &g_press_count_ok;
    elem_count_ok->last_box = (ui_rect_t){88, 16, 40, 16};
    elem_count_ok->pool_id = 3;
    
    memset(elem_btn_up, 0, sizeof(ui_element_t));
    elem_btn_up->cfg = cfg_btn_up;
    elem_btn_up->last_box = (ui_rect_t){0, 32, 40, 20};
    elem_btn_up->state = UI_STATE_HIGHLIGHT;
    elem_btn_up->pool_id = 4;
    
    memset(elem_btn_down, 0, sizeof(ui_element_t));
    elem_btn_down->cfg = cfg_btn_down;
    elem_btn_down->last_box = (ui_rect_t){44, 32, 40, 20};
    elem_btn_down->pool_id = 5;
    
    memset(elem_btn_ok, 0, sizeof(ui_element_t));
    elem_btn_ok->cfg = cfg_btn_ok;
    elem_btn_ok->last_box = (ui_rect_t){88, 32, 40, 20};
    elem_btn_ok->pool_id = 6;
    
    screen_elems = (ui_element_t**)pvPortMalloc(7 * sizeof(ui_element_t*));
    if (!screen_elems) return false;
    
    screen_elems[0] = elem_title;
    screen_elems[1] = elem_count_up;
    screen_elems[2] = elem_count_down;
    screen_elems[3] = elem_count_ok;
    screen_elems[4] = elem_btn_up;
    screen_elems[5] = elem_btn_down;
    screen_elems[6] = elem_btn_ok;
    
    test_screen_ptr = (ui_screen_t*)pvPortMalloc(sizeof(ui_screen_t));
    if (!test_screen_ptr) return false;
    
    test_screen_ptr->name = "KeyTest";
    test_screen_ptr->elem_count = 7;
    test_screen_ptr->elements = (const ui_element_t**)screen_elems;
    
    return true;
}

void test_screen_deinit(void) {
    if (cfg_title) vPortFree(cfg_title);
    if (cfg_count_up) vPortFree(cfg_count_up);
    if (cfg_count_down) vPortFree(cfg_count_down);
    if (cfg_count_ok) vPortFree(cfg_count_ok);
    if (cfg_btn_up) vPortFree(cfg_btn_up);
    if (cfg_btn_down) vPortFree(cfg_btn_down);
    if (cfg_btn_ok) vPortFree(cfg_btn_ok);
    if (elem_title) vPortFree(elem_title);
    if (elem_count_up) vPortFree(elem_count_up);
    if (elem_count_down) vPortFree(elem_count_down);
    if (elem_count_ok) vPortFree(elem_count_ok);
    if (elem_btn_up) vPortFree(elem_btn_up);
    if (elem_btn_down) vPortFree(elem_btn_down);
    if (elem_btn_ok) vPortFree(elem_btn_ok);
    if (screen_elems) vPortFree(screen_elems);
    if (test_screen_ptr) vPortFree(test_screen_ptr);
}

const ui_screen_t* test_screen_get(void) {
    return (const ui_screen_t*)test_screen_ptr;
}

void test_screen_update(void) {
    /* 空函数 */
}