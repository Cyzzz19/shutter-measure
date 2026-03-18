#include "menu_screen.h"
#include "oled.h"
#include <string.h>

/* 菜单项文本 */
const char * const MENU_ITEM_TEXTS[MENU_COUNT] = {
    "Basic Measurement",
    "Front/Rear Curtain Sync",
    "Rotation Test",
    "Gear Settings",
    "Stored Data",
    "Error Statistics Chart",
    "Time Compensation",
    "Clear Data",
    "About"
};

/* 私有变量 */
static ui_element_cfg_t *menu_cfg[MENU_COUNT] = {0};
static ui_element_t *menu_elems[MENU_COUNT] = {0};
static ui_screen_t *menu_screen_ptr = NULL;
static uint8_t menu_selected_idx = 0;

/* 菜单项渲染：高亮时左侧显示▶ */
static void render_menu_item(ui_element_t *self)
{
    if (!self || !self->cfg) return;
    
    uint8_t row = self->cfg->y / 8;
    uint8_t x = self->cfg->x;
    
    /* 清空行 */
    OLED_Fill(x, row, x + self->cfg->w - 1, row, 0);
    
    /* 高亮标记 */
    if (self->state & UI_STATE_HIGHLIGHT) {
        OLED_ShowChar(x, row, 0x10, 16);  /* ▶ 图标 */
        x += 8;
    } else {
        x += 8;  /* 预留位置保持对齐 */
    }
    
    /* 绘制文本 */
    if (self->cfg->text)
        OLED_ShowString(x, row, (uint8_t*)self->cfg->text, 16);
}

/* 菜单事件处理 */
static bool menu_event_handler(ui_element_t *self, ui_event_code_t evt)
{
    if (evt == EVT_UP) {
        menu_selected_idx = (menu_selected_idx + MENU_COUNT - 1) % MENU_COUNT;
        ui_focus_set(menu_selected_idx);
        return true;
    }
    if (evt == EVT_DOWN) {
        menu_selected_idx = (menu_selected_idx + 1) % MENU_COUNT;
        ui_focus_set(menu_selected_idx);
        return true;
    }
    if (evt == EVT_PRESS) {
        /* 跳转到对应页面 */
        const ui_screen_t *page = generic_page_create(
            MENU_ITEM_TEXTS[menu_selected_idx],
            MENU_ITEM_TEXTS[menu_selected_idx]
        );
        if (page) ui_push_screen(page);
        return true;
    }
    return false;
}

/* 菜单进入/退出回调 */
static void menu_on_enter(const ui_screen_t *s)
{
    menu_selected_idx = 0;
    ui_focus_set(0);
}

static void menu_on_exit(const ui_screen_t *s)
{
    /* 清空所有高亮 */
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        if (menu_elems[i]) menu_elems[i]->state &= ~UI_STATE_HIGHLIGHT;
    }
}

/* 初始化菜单屏幕 */
bool menu_screen_init(void)
{
    /* 创建每个菜单项配置 */
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        menu_cfg[i] = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
        if (!menu_cfg[i]) return false;
        
        memset(menu_cfg[i], 0, sizeof(ui_element_cfg_t));
        menu_cfg[i]->x = 0;
        menu_cfg[i]->y = i * 16;          /* 每项16px高 */
        menu_cfg[i]->w = 128;
        menu_cfg[i]->h = 16;
        menu_cfg[i]->text = MENU_ITEM_TEXTS[i];
        menu_cfg[i]->render = render_menu_item;
        menu_cfg[i]->on_event = menu_event_handler;
        menu_cfg[i]->type_id = 1;
        
        menu_elems[i] = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
        if (!menu_elems[i]) return false;
        
        memset(menu_elems[i], 0, sizeof(ui_element_t));
        menu_elems[i]->cfg = menu_cfg[i];
        menu_elems[i]->last_box = (ui_rect_t){0, (ui_coord_t)(i*16), 128, 16};
        menu_elems[i]->pool_id = i;
    }
    
    /* 创建屏幕 */
    menu_screen_ptr = (ui_screen_t *)pvPortMalloc(sizeof(ui_screen_t));
    if (!menu_screen_ptr) return false;
    
    menu_screen_ptr->name = "Menu";
    menu_screen_ptr->elem_count = MENU_COUNT;
    menu_screen_ptr->elements = (const ui_element_t **)menu_elems;
    menu_screen_ptr->on_enter = menu_on_enter;
    menu_screen_ptr->on_exit = menu_on_exit;
    
    return true;
}

const ui_screen_t *menu_screen_get(void)
{
    return (const ui_screen_t *)menu_screen_ptr;
}

void menu_screen_deinit(void)
{
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        if (menu_cfg[i]) vPortFree(menu_cfg[i]);
        if (menu_elems[i]) vPortFree(menu_elems[i]);
    }
    if (menu_screen_ptr) vPortFree(menu_screen_ptr);
}