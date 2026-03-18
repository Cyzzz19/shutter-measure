/* generic_page.c - 通用单文本页面 */
#include "menu_screen.h"
#include "oled.h"
#include <stdlib.h>

typedef struct {
    char title[32];
    char desc[64];
} generic_page_data_t;

static void render_generic_label(ui_element_t *self)
{
    if (!self || !self->cfg || !self->data_binding) return;
    
    generic_page_data_t *data = (generic_page_data_t *)self->data_binding;
    
    /* 清屏 */
    OLED_Fill(0, 0, 127, 7, 0);
    
    /* 居中显示描述文本 */
    uint8_t len = strlen(data->desc);
    uint8_t char_w = 8;  /* 16px字体≈8px宽/字符 */
    uint8_t x = (128 - len * char_w) / 2;
    if (x > 120) x = 0;
    
    OLED_ShowString(x, 3, (uint8_t*)data->desc, 16);
}

static bool generic_page_event(ui_element_t *self, ui_event_code_t evt)
{
    /* 任意键返回菜单 */
    if (evt == EVT_PRESS || evt == EVT_UP || evt == EVT_DOWN) {
        ui_pop_screen();  /* 返回菜单 */
        return true;
    }
    return false;
}

const ui_screen_t *generic_page_create(const char *name, const char *desc)
{
    /* 分配数据 */
    generic_page_data_t *data = (generic_page_data_t *)pvPortMalloc(sizeof(generic_page_data_t));
    if (!data) return NULL;
    strncpy(data->title, name ? name : "", 31);
    strncpy(data->desc, desc ? desc : "", 63);
    data->title[31] = '\0';
    data->desc[63] = '\0';
    
    /* 元素配置 */
    ui_element_cfg_t *cfg = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    ui_element_t *elem = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    ui_screen_t *screen = (ui_screen_t *)pvPortMalloc(sizeof(ui_screen_t));
    
    if (!cfg || !elem || !screen) {
        if (cfg) vPortFree(cfg);
        if (elem) vPortFree(elem);
        if (screen) vPortFree(screen);
        vPortFree(data);
        return NULL;
    }
    
    memset(cfg, 0, sizeof(ui_element_cfg_t));
    cfg->x = 0; cfg->y = 24; cfg->w = 128; cfg->h = 16;
    cfg->text = data->desc;
    cfg->render = render_generic_label;
    cfg->on_event = generic_page_event;
    
    memset(elem, 0, sizeof(ui_element_t));
    elem->cfg = cfg;
    elem->data_binding = data;
    elem->last_box = (ui_rect_t){0, 24, 128, 16};
    
    screen->name = name;
    screen->elem_count = 1;
    screen->elements = (const ui_element_t **)&elem;
    screen->on_enter = NULL;
    screen->on_exit = NULL;
    
    return screen;
}

void generic_page_destroy(const ui_screen_t *screen)
{
    if (!screen || screen->elem_count < 1) return;
    
    ui_element_t *elem = (ui_element_t *)screen->elements[0];
    if (elem && elem->data_binding) vPortFree(elem->data_binding);
    if (elem && elem->cfg) vPortFree((void*)elem->cfg);
    if (elem) vPortFree(elem);
    vPortFree((void*)screen);
}