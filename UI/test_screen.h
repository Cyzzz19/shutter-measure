
#ifndef __TEST_SCREEN_H
#define __TEST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif


#include "ui_core.h"


#define TEST_SCREEN_ID          0x01    /* 屏幕唯一标识（用于调试/日志）*/
#define TEST_BTN_COUNT          3       /* 按钮数量 */

/* 按钮枚举：用于外部查询/控制 */
typedef enum {
    TEST_BTN_UP = 0,
    TEST_BTN_DOWN = 1,
    TEST_BTN_OK = 2,
    TEST_BTN_MAX
} test_btn_id_t;


extern const ui_screen_t test_screen;


void test_screen_init(ui_element_t *pool_ptr);


void test_screen_update(void);


uint8_t test_screen_get_btn_state(test_btn_id_t btn);


void test_screen_set_btn_enabled(test_btn_id_t btn, bool enabled);


void test_screen_bind_counter(uint32_t *var_ptr);

typedef void (*test_btn_callback_t)(test_btn_id_t btn, ui_event_code_t evt, void *user_data);
bool test_screen_register_callback(test_btn_id_t btn, test_btn_callback_t callback, void *user_data);

/* ================= 调试辅助（DEBUG 模式） ================= */
#ifdef UI_DEBUG_ENABLE

void test_screen_dump_stats(void);


void test_screen_force_redraw(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
#endif /* UI_DEBUG_ENABLE */


#ifdef __cplusplus
}
#endif

#endif /* __TEST_SCREEN_H */
