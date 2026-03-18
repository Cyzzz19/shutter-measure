#ifndef __MENU_SCREEN_H
#define __MENU_SCREEN_H

#include "ui_core.h"

/* 菜单项枚举 */
typedef enum {
    MENU_BASIC_MEASUREMENT = 0,
    MENU_FRONT_REAR_SYNC,
    MENU_ROTATION_TEST,
    MENU_GEAR_SETTINGS,
    MENU_STORED_DATA,
    MENU_ERROR_STATS,
    MENU_TIME_COMPENSATION,
    MENU_CLEAR_DATA,
    MENU_ABOUT,
    MENU_COUNT
} menu_item_t;

/* 菜单项文本 */
extern const char * const MENU_ITEM_TEXTS[MENU_COUNT];

/* 初始化/获取菜单屏幕 */
bool menu_screen_init(void);
const ui_screen_t *menu_screen_get(void);
void menu_screen_deinit(void);

/* 通用功能页创建 */
const ui_screen_t *generic_page_create(const char *name, const char *desc);
void generic_page_destroy(const ui_screen_t *screen);

#endif