/* test_screen.c - 动态分配版本 */
#include "ui_core.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os.h"  /* FreeRTOS 内存分配 */

static uint32_t g_press_count = 0;

/* 全局指针：动态分配的配置和元素 */
static ui_element_cfg_t *cfg_title = NULL;
static ui_element_cfg_t *cfg_status = NULL;
static ui_element_cfg_t *cfg_btn_ok = NULL;

static ui_element_t *elem_title = NULL;
static ui_element_t *elem_status = NULL;
static ui_element_t *elem_btn_ok = NULL;

static ui_element_t **screen_elems = NULL;
static ui_screen_t *test_screen_ptr = NULL;

/* 函数声明 */
static void render_text(ui_element_t *self);
static void render_button(ui_element_t *self);
static bool on_button_event(ui_element_t *self, ui_event_code_t evt);

/* 渲染函数 */
static void render_text(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    if (self->state & UI_STATE_HIGHLIGHT) {
        uint8_t hl_page = self->cfg->y / 8;
        OLED_Fill(self->cfg->x, hl_page, self->cfg->x + self->cfg->w - 1, hl_page, 0xFF);
    }
    
    char buf[32] = {0};
    const char *text = self->cfg->text;
    
    if (self->data_binding) {
        uint32_t *val = (uint32_t*)self->data_binding;
        snprintf(buf, sizeof(buf), "Press: %lu", *val);
        text = buf;
    }
    
    if (text) {
        OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t*)text, 16);
    }
}

static void render_button(ui_element_t *self) {
    if (!self || !self->cfg) return;
    
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;
    
    for (uint8_t p = page_start; p <= page_end; p++) {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }
    
    uint8_t top_page = self->cfg->y / 8;
    OLED_ShowString(self->cfg->x, top_page, (uint8_t*)"[", 16);
    OLED_ShowString(self->cfg->x + self->cfg->w - 8, top_page, (uint8_t*)"]", 16);
    
    if (self->state & UI_STATE_PRESSED) {
        for (uint8_t p = page_start; p <= page_end; p++) {
            OLED_Fill(self->cfg->x + 2, p, self->cfg->x + self->cfg->w - 3, p, 0xFF);
        }
    }
    
    if (self->cfg->text) {
        uint8_t tx = self->cfg->x + (self->cfg->w - strlen(self->cfg->text) * 8) / 2;
        uint8_t ty_page = self->cfg->y / 8;
        OLED_ShowString(tx, ty_page, (uint8_t*)self->cfg->text, 16);
    }
}

static bool on_button_event(ui_element_t *self, ui_event_code_t evt) {
    if (!self) return false;
    
    if (evt == EVT_PRESS) {
        self->state ^= UI_STATE_HIGHLIGHT;
        self->state |= UI_STATE_DIRTY;
        g_press_count++;
        
        /* 同时标记状态行需要刷新 */
        if (elem_status) {
            elem_status->state |= UI_STATE_DIRTY;
        }
        return true;
    }
    if (evt == EVT_LONG_PRESS) {
        g_press_count = 0;
        self->state |= UI_STATE_DIRTY;
        
        /* 同时标记状态行需要刷新 */
        if (elem_status) {
            elem_status->state |= UI_STATE_DIRTY;
        }
        return true;
    }
    return false;
}

/* ================= 初始化函数（动态分配）================= */
bool test_screen_init(void) {
    /* 分配配置结构体 */
    cfg_title = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_status = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_btn_ok = (ui_element_cfg_t*)pvPortMalloc(sizeof(ui_element_cfg_t));
    
    if (!cfg_title || !cfg_status || !cfg_btn_ok) {
        printf("[UI] Failed to allocate configs!\n");
        return false;
    }
    
    /* 初始化配置 */
    memset(cfg_title, 0, sizeof(ui_element_cfg_t));
    cfg_title->x = 0;
    cfg_title->y = 0;
    cfg_title->w = 128;
    cfg_title->h = 16;
    cfg_title->type_id = 0;
    cfg_title->text = "BTN Monitor";
    cfg_title->render = render_text;
    cfg_title->on_event = NULL;
    
    memset(cfg_status, 0, sizeof(ui_element_cfg_t));
    cfg_status->x = 0;
    cfg_status->y = 16;
    cfg_status->w = 128;
    cfg_status->h = 16;
    cfg_status->type_id = 0;
    cfg_status->text = "Press: 0";
    cfg_status->render = render_text;
    cfg_status->on_event = NULL;
    
    memset(cfg_btn_ok, 0, sizeof(ui_element_cfg_t));
    cfg_btn_ok->x = 48;
    cfg_btn_ok->y = 32;
    cfg_btn_ok->w = 32;
    cfg_btn_ok->h = 20;
    cfg_btn_ok->type_id = 1;
    cfg_btn_ok->text = "OK";
    cfg_btn_ok->render = render_button;
    cfg_btn_ok->on_event = on_button_event;
    
    /* 分配元素结构体 */
    elem_title = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_status = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    elem_btn_ok = (ui_element_t*)pvPortMalloc(sizeof(ui_element_t));
    
    if (!elem_title || !elem_status || !elem_btn_ok) {
        printf("[UI] Failed to allocate elements!\n");
        return false;
    }
    
    /* 初始化元素 */
    memset(elem_title, 0, sizeof(ui_element_t));
    elem_title->cfg = cfg_title;
    elem_title->data_binding = NULL;
    elem_title->last_box.x = 0;
    elem_title->last_box.y = 0;
    elem_title->last_box.w = 128;
    elem_title->last_box.h = 16;
    elem_title->state = UI_STATE_NORMAL;
    elem_title->pool_id = 0;
    
    memset(elem_status, 0, sizeof(ui_element_t));
    elem_status->cfg = cfg_status;
    elem_status->data_binding = &g_press_count;
    elem_status->last_box.x = 0;
    elem_status->last_box.y = 16;
    elem_status->last_box.w = 128;
    elem_status->last_box.h = 16;
    elem_status->state = UI_STATE_NORMAL;
    elem_status->pool_id = 1;
    
    memset(elem_btn_ok, 0, sizeof(ui_element_t));
    elem_btn_ok->cfg = cfg_btn_ok;
    elem_btn_ok->data_binding = NULL;
    elem_btn_ok->last_box.x = 48;
    elem_btn_ok->last_box.y = 32;
    elem_btn_ok->last_box.w = 32;
    elem_btn_ok->last_box.h = 20;
    elem_btn_ok->state = UI_STATE_NORMAL;
    elem_btn_ok->pool_id = 2;
    
    /* 分配屏幕数组 */
    screen_elems = (ui_element_t**)pvPortMalloc(3 * sizeof(ui_element_t*));
    if (!screen_elems) {
        printf("[UI] Failed to allocate screen array!\n");
        return false;
    }
    
    screen_elems[0] = elem_title;
    screen_elems[1] = elem_status;
    screen_elems[2] = elem_btn_ok;
    
    /* 分配屏幕结构体 */
    test_screen_ptr = (ui_screen_t*)pvPortMalloc(sizeof(ui_screen_t));
    if (!test_screen_ptr) {
        printf("[UI] Failed to allocate screen!\n");
        return false;
    }
    
    test_screen_ptr->name = "Test";
    test_screen_ptr->elem_count = 3;
    test_screen_ptr->elements = (const ui_element_t**)screen_elems;
    
    printf("[UI] Test screen initialized successfully!\n");
    printf("  cfg_title:    0x%08X\n", (uint32_t)cfg_title);
    printf("  elem_title:   0x%08X\n", (uint32_t)elem_title);
    printf("  screen:       0x%08X\n", (uint32_t)test_screen_ptr);
    
    return true;
}

/* 清理函数（可选）*/
void test_screen_deinit(void) {
    if (cfg_title) vPortFree(cfg_title);
    if (cfg_status) vPortFree(cfg_status);
    if (cfg_btn_ok) vPortFree(cfg_btn_ok);
    if (elem_title) vPortFree(elem_title);
    if (elem_status) vPortFree(elem_status);
    if (elem_btn_ok) vPortFree(elem_btn_ok);
    if (screen_elems) vPortFree(screen_elems);
    if (test_screen_ptr) vPortFree(test_screen_ptr);
    
    cfg_title = cfg_status = cfg_btn_ok = NULL;
    elem_title = elem_status = elem_btn_ok = NULL;
    screen_elems = NULL;
    test_screen_ptr = NULL;
}

/* 获取屏幕指针 */
const ui_screen_t* test_screen_get(void) {
    return (const ui_screen_t*)test_screen_ptr;
}

void test_screen_update(void) {
}