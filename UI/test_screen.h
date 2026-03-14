/**
 * @file    test_screen.h
 * @brief   测试界面公共接口 - 按钮监控演示
 * @details 基于轻量级对象池 + 局部刷新架构
 *          演示：3 按键输入检测、状态反馈、数据绑定、局部刷新
 * 
 * @note    本文件仅声明接口，实现位于 test_screen.c
 *          所有配置数据自动存入 Flash (const)，状态数据存入 RAM 对象池
 */

#ifndef __TEST_SCREEN_H
#define __TEST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 依赖包含 ================= */
/* 问题：为什么这里只包含 "ui_core.h" 而不直接包含 "oled.h"？
   答：保持模块解耦。test_screen 只依赖 UI 框架抽象，不依赖具体显示驱动。
   如果未来更换屏幕（如 SPI TFT），只需修改 ui_core.c，无需改动本文件。 */
#include "ui_core.h"

/* ================= 公共常量 ================= */
/* 问题：这些常量是编译时确定还是运行时可配置？
   答：编译时常量存入 Flash，零 RAM 开销。
   如果需要运行时调整布局，可改为通过 ui_element_cfg_t 指针动态配置。 */
#define TEST_SCREEN_ID          0x01    /* 屏幕唯一标识（用于调试/日志）*/
#define TEST_BTN_COUNT          3       /* 按钮数量 */

/* 按钮枚举：用于外部查询/控制 */
typedef enum {
    TEST_BTN_UP = 0,
    TEST_BTN_DOWN = 1,
    TEST_BTN_OK = 2,
    TEST_BTN_MAX
} test_btn_id_t;

/* ================= 外部声明：测试屏幕对象 ================= */
/* 问题：为什么声明为 const ui_screen_t* 而不是直接导出结构体？
   答：封装实现细节。外部只能通过 ui_set_screen() 切换，不能直接修改内部元素。
   这防止了意外的内存破坏，符合"最小权限"原则。 */
extern const ui_screen_t test_screen;

/* ================= 公共 API ================= */

/**
 * @brief 初始化测试界面（可选，如果对象池已预初始化可省略）
 * @param pool_ptr 指向全局对象池的指针（如果由外部管理）
 * @note 通常由 ui_init() 统一初始化，此函数用于高级用例（如热切换池）
 */
void test_screen_init(ui_element_t *pool_ptr);

/**
 * @brief 更新测试界面逻辑（应在主循环每 10ms 调用）
 * @details 执行：
 *          1. 读取输入事件（通过 g_ui.input）
 *          2. 更新按钮状态（高亮/按下/禁用）
 *          3. 更新计数显示（数据绑定示例）
 *          4. 标记脏区域（触发局部刷新）
 * @note 此函数不直接刷新屏幕，需配合 ui_flush() 使用
 */
void test_screen_update(void);

/**
 * @brief 获取指定按钮的当前状态
 * @param btn 按钮 ID（TEST_BTN_UP/DOWN/OK）
 * @return ui_state_mask_t 状态掩码（可检查 UI_STATE_HIGHLIGHT 等）
 * @note 线程安全：读取原子操作，但建议在 ui_tick 间隙调用
 */
uint8_t test_screen_get_btn_state(test_btn_id_t btn);

/**
 * @brief 设置指定按钮的启用/禁用状态
 * @param btn 按钮 ID
 * @param enabled true=启用，false=禁用（禁用态不响应事件，视觉灰显）
 * @note 调用后自动标记脏区域，下次 ui_flush() 生效
 */
void test_screen_set_btn_enabled(test_btn_id_t btn, bool enabled);

/**
 * @brief 绑定外部变量到状态显示（演示数据绑定）
 * @param var_ptr 指向 uint32_t 变量的指针（如传感器计数、系统状态）
 * @details 绑定后，状态行将显示 "Press: <value>"，value 自动更新
 * @note 如果 var_ptr 为 NULL，则显示静态文本 "Press: --"
 */
void test_screen_bind_counter(uint32_t *var_ptr);

/**
 * @brief 注册按钮事件回调（高级用法）
 * @param btn 按钮 ID
 * @param callback 用户回调函数，事件触发时调用
 * @param user_data 用户数据指针，透传给回调
 * @details 回调签名：void cb(test_btn_id_t btn, ui_event_code_t evt, void* user_data)
 * @return true=注册成功，false=参数错误
 * @note 回调在中断上下文？NO！在 ui_tick() 主循环中调用，可安全操作 UI
 */
typedef void (*test_btn_callback_t)(test_btn_id_t btn, ui_event_code_t evt, void *user_data);
bool test_screen_register_callback(test_btn_id_t btn, test_btn_callback_t callback, void *user_data);

/* ================= 调试辅助（DEBUG 模式） ================= */
#ifdef UI_DEBUG_ENABLE
/**
 * @brief 打印屏幕内存布局统计（调试用）
 * @details 输出到 UART/ITM：
 *          - 对象池使用率
 *          - 脏队列命中率
 *          - 各元素 RAM/Flash 占用
 * @note 需定义 UI_DEBUG_ENABLE 宏，并链接 printf 重定向
 */
void test_screen_dump_stats(void);

/**
 * @brief 强制重绘指定区域（调试残影用）
 * @param x,y,w,h 区域参数
 * @note 仅调试使用，生产环境应依赖自动脏标记
 */
void test_screen_force_redraw(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
#endif /* UI_DEBUG_ENABLE */

/* ================= 使用示例（注释形式） ================= */
/*
// main.c 中集成示例：
#include "test_screen.h"

static uint32_t my_counter = 0;  // 外部业务变量

void user_task(void) {
    // 1. 初始化（如果未自动初始化）
    test_screen_init(g_ui.pool);
    
    // 2. 绑定外部变量
    test_screen_bind_counter(&my_counter);
    
    // 3. 注册自定义回调
    test_screen_register_callback(TEST_BTN_OK, 
        [](test_btn_id_t btn, ui_event_code_t evt, void *ud) {
            if (evt == EVT_PRESS) {
                my_counter++;  // 业务逻辑
                // 注意：不要直接操作 UI，通过状态绑定或标记脏区域
            }
        }, NULL);
    
    // 4. 主循环集成
    while (1) {
        ui_tick();              // 输入检测 + 事件分发
        test_screen_update();   // 界面逻辑更新
        ui_flush();             // 局部刷新（关键！）
        
        // 业务逻辑...
        if (sensor_ready()) {
            my_counter = read_sensor();  // 自动触发显示更新
        }
    }
}
*/

#ifdef __cplusplus
}
#endif

#endif /* __TEST_SCREEN_H */

/* 
 * ============================================================================
 * 设计思考挑战（请阅读后回答）：
 * 
 * 1. 关于回调注册：
 *    为什么回调函数不直接操作 UI 元素（如修改 state 字段），而是通过
 *    "标记脏区域 + 数据绑定" 的间接方式？如果允许直接操作，会带来什么
 *    线程安全或状态一致性问题？
 * 
 * 2. 关于数据绑定：
 *    test_screen_bind_counter() 绑定的是变量指针。如果这个变量在中断
 *    中被修改（如定时器计数），而 ui_flush() 在主循环中读取，是否需要
 *    添加 volatile 关键字或临界区保护？为什么？
 * 
 * 3. 关于局部刷新边界：
 *    如果按钮文本从 "OK" 变为 "OK!"（长度+1），last_box 的 w 字段是否
 *    需要动态更新？如果不更新，旧字符的残影如何处理？你的渲染函数是否
 *    保证"新内容完全覆盖旧区域"？
 * 
 * 4. 关于内存对齐：
 *    在 STM32F103 (Cortex-M3) 上，未对齐的 32 位访问会触发 HardFault。
 *    我们的 ui_element_t 使用了 __packed，如果渲染函数中直接访问
 *    elem->cfg->x (uint8_t) 并参与 32 位运算，编译器是否会生成安全代码？
 *    是否需要手动转换为中间变量？
 * 
 * 请思考这些问题，它们决定了系统在极限条件下的鲁棒性。
 * ============================================================================
 */