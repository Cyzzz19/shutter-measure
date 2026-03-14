/**
 * @file    test_screen.h
 * @brief   测试界面公共接口 - 动态分配版本
 */

#ifndef __TEST_SCREEN_H
#define __TEST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_core.h"

/* ================= 公共 API ================= */

/**
 * @brief 初始化测试界面（动态分配）
 * @return true=成功，false=内存分配失败
 * @note 必须在 ui_init() 前调用
 */
bool test_screen_init(void);

/**
 * @brief 清理测试界面资源
 * @note 可选，用于重新初始化或低功耗模式
 */
void test_screen_deinit(void);

/**
 * @brief 获取屏幕指针
 * @return const ui_screen_t* 屏幕对象指针
 */
const ui_screen_t* test_screen_get(void);

/**
 * @brief 更新测试界面逻辑（每 10ms 调用）
 * @note 标记脏区域，不直接刷新
 */
void test_screen_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_SCREEN_H */