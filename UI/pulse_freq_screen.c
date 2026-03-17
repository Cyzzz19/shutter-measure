#include "ui_core.h"
#include "pulseCapture.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "cmsis_os.h" // 如果需要FreeRTOS的xTaskGetTickCount

/* ================= 私有变量 ================= */
static ui_element_cfg_t *cfg_freq_title = NULL;
static ui_element_cfg_t *cfg_freq_value = NULL;
static ui_element_cfg_t *cfg_pulse_title = NULL;
static ui_element_cfg_t *cfg_pulse_value = NULL;
static ui_element_cfg_t *cfg_status = NULL;

static ui_element_t *elem_freq_title = NULL;
static ui_element_t *elem_freq_value = NULL;
static ui_element_t *elem_pulse_title = NULL;
static ui_element_t *elem_pulse_value = NULL;
static ui_element_t *elem_status = NULL;

static ui_element_t **screen_elems = NULL;
static ui_screen_t *pulse_freq_screen_ptr = NULL;

/* 脉宽频率数据 */
static float g_last_frequency = 0.0f;   // 最近一次频率(Hz)
static float g_last_pulse_width = 0.0f; // 最近一次脉宽(us)
static uint32_t g_pulse_count = 0;      // 脉冲计数
static uint32_t g_last_update_time = 0; // 最后更新时间
static uint8_t g_signal_present = 0;    // 信号存在标志

/* 渲染函数 - 显示标题 */
static void render_title(ui_element_t *self)
{
    if (!self || !self->cfg)
        return;

    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7)
        page_end = 7;

    /* 清空区域 */
    for (uint8_t p = page_start; p <= page_end; p++)
    {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }

    /* 显示标题文本（可能带边框） */
    if (self->state & UI_STATE_HIGHLIGHT)
    {
        OLED_Invert_Rect(self->cfg->x, self->cfg->y,
                         self->cfg->x + self->cfg->w - 1, self->cfg->y + self->cfg->h - 1);
    }

    OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t *)self->cfg->text, 16);
}

/* 渲染函数 - 显示数值 */
static void render_value(ui_element_t *self)
{
    if (!self || !self->cfg)
        return;

    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7)
        page_end = 7;

    /* 清空区域 */
    for (uint8_t p = page_start; p <= page_end; p++)
    {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }

    char buf[32] = {0};

    if (self->data_binding)
    {
        if (self == elem_freq_value)
        {
            /* 显示频率值 */
            float *freq = (float *)self->data_binding;
            if (*freq > 0 && g_signal_present)
            {
                if (*freq >= 1000.0f)
                {
                    // 转换为 kHz 显示
                    uint32_t freq_khz = (uint32_t)(*freq / 1000.0f * 1000); // 保留3位小数
                    uint32_t int_part = freq_khz / 1000;
                    uint32_t frac_part = freq_khz % 1000;
                    snprintf(buf, sizeof(buf), "%lu.%03lu kHz", int_part, frac_part);
                }
                else if (*freq >= 1.0f)
                {
                    // Hz 显示，保留2位小数
                    uint32_t freq_hz = (uint32_t)(*freq * 100);
                    uint32_t int_part = freq_hz / 100;
                    uint32_t frac_part = freq_hz % 100;
                    snprintf(buf, sizeof(buf), "%lu.%02lu Hz", int_part, frac_part);
                }
                else if (*freq > 0.001f)
                {
                    // mHz 显示
                    uint32_t freq_mhz = (uint32_t)(*freq * 1000000); // 转换为 mHz * 1000
                    uint32_t int_part = freq_mhz / 1000;
                    uint32_t frac_part = freq_mhz % 1000;
                    snprintf(buf, sizeof(buf), "%lu.%03lu mHz", int_part, frac_part);
                }
                else
                {
                    strcpy(buf, "<0.001 mHz");
                }
            }
            else
            {
                strcpy(buf, "--- Hz");
            }
        }
        else if (self == elem_pulse_value)
        {
            /* 显示脉宽值 - 整数分块版本 */
            float *pulse = (float *)self->data_binding;
            if (*pulse > 0 && g_signal_present)
            {
                // 将浮点数转换为整数微秒（四舍五入）
                uint32_t pulse_us = (uint32_t)(*pulse + 0.5f); // 微秒

                if (pulse_us >= 1000000)
                {
                    // 大于等于1秒，显示为秒
                    uint32_t seconds = pulse_us / 1000000;
                    uint32_t ms_part = (pulse_us % 1000000) / 1000;
                    snprintf(buf, sizeof(buf), "%lu.%03lu s", seconds, ms_part);
                }
                else if (pulse_us >= 1000)
                {
                    // 大于等于1ms，显示为毫秒
                    uint32_t ms_value = pulse_us / 1000;
                    uint32_t us_part = pulse_us % 1000;

                    if (us_part == 0)
                    {
                        // 整毫秒
                        snprintf(buf, sizeof(buf), "%lu ms", ms_value);
                    }
                    else
                    {
                        // 毫秒.微秒部分（只显示前1-2位有效数字）
                        if (us_part >= 100)
                        {
                            // 显示1位小数（如 1.2ms = 1200us）
                            snprintf(buf, sizeof(buf), "%lu.%lu ms",
                                     ms_value, us_part / 100);
                        }
                        else if (us_part >= 10)
                        {
                            // 显示2位小数（如 1.23ms = 1230us）
                            snprintf(buf, sizeof(buf), "%lu.%02lu ms",
                                     ms_value, us_part / 10);
                        }
                        else
                        {
                            // 显示3位小数（如 1.003ms = 1003us）
                            snprintf(buf, sizeof(buf), "%lu.%03lu ms",
                                     ms_value, us_part);
                        }
                    }
                }
                else
                {
                    // 小于1ms，显示为微秒
                    if (pulse_us >= 100)
                    {
                        snprintf(buf, sizeof(buf), "%lu us", pulse_us);
                    }
                    else if (pulse_us >= 10)
                    {
                        snprintf(buf, sizeof(buf), "%lu us", pulse_us);
                    }
                    else
                    {
                        snprintf(buf, sizeof(buf), "%lu us", pulse_us);
                    }
                }
            }
            else
            {
                strcpy(buf, "--- us");
            }
        }
    }

    OLED_ShowString(self->cfg->x, self->cfg->y / 8, (uint8_t *)buf, 16);
}

/* 渲染函数 - 状态显示 */
static void render_status(ui_element_t *self)
{
    if (!self || !self->cfg)
        return;

    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7)
        page_end = 7;

    /* 清空区域 */
    for (uint8_t p = page_start; p <= page_end; p++)
    {
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);
    }

    char buf[32] = {0};
    uint32_t *count = (uint32_t *)self->data_binding;

    if (g_signal_present)
    {
        snprintf(buf, sizeof(buf), "Pulse:%lu OK", *count);
    }
    else
    {
        snprintf(buf, sizeof(buf), "No Signal");
    }

    /* 状态栏使用反色显示 */

    OLED_ShowString(self->cfg->x + 2, self->cfg->y / 8, (uint8_t *)buf, 16);
}

/* ================= 初始化函数 ================= */
bool pulse_freq_screen_init(void)
{
    /* 分配配置结构体 */
    cfg_freq_title = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_freq_value = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_pulse_title = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_pulse_value = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_status = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));

    if (!cfg_freq_title || !cfg_freq_value || !cfg_pulse_title ||
        !cfg_pulse_value || !cfg_status)
    {
        return false;
    }

    /* 频率标题 */
    memset(cfg_freq_title, 0, sizeof(ui_element_cfg_t));
    cfg_freq_title->x = 0;
    cfg_freq_title->y = 0;
    cfg_freq_title->w = 60;
    cfg_freq_title->h = 16;
    cfg_freq_title->text = "F:";
    cfg_freq_title->render = render_title;
    cfg_freq_title->on_event = NULL;

    /* 频率数值 */
    memset(cfg_freq_value, 0, sizeof(ui_element_cfg_t));
    cfg_freq_value->x = 20;
    cfg_freq_value->y = 0;
    cfg_freq_value->w = 68;
    cfg_freq_value->h = 16;
    cfg_freq_value->text = "--- Hz";
    cfg_freq_value->render = render_value;
    cfg_freq_value->on_event = NULL;

    /* 脉宽标题 */
    memset(cfg_pulse_title, 0, sizeof(ui_element_cfg_t));
    cfg_pulse_title->x = 0;
    cfg_pulse_title->y = 16;
    cfg_pulse_title->w = 60;
    cfg_pulse_title->h = 16;
    cfg_pulse_title->text = "PW:";
    cfg_pulse_title->render = render_title;
    cfg_pulse_title->on_event = NULL;

    /* 脉宽数值 */
    memset(cfg_pulse_value, 0, sizeof(ui_element_cfg_t));
    cfg_pulse_value->x = 20;
    cfg_pulse_value->y = 16;
    cfg_pulse_value->w = 68;
    cfg_pulse_value->h = 16;
    cfg_pulse_value->text = "--- us";
    cfg_pulse_value->render = render_value;
    cfg_pulse_value->on_event = NULL;

    /* 状态栏 */
    memset(cfg_status, 0, sizeof(ui_element_cfg_t));
    cfg_status->x = 0;
    cfg_status->y = 48;
    cfg_status->w = 128;
    cfg_status->h = 16;
    cfg_status->text = "Status";
    cfg_status->render = render_status;
    cfg_status->on_event = NULL;

    /* 分配元素结构体 */
    elem_freq_title = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_freq_value = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_pulse_title = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_pulse_value = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_status = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));

    if (!elem_freq_title || !elem_freq_value || !elem_pulse_title ||
        !elem_pulse_value || !elem_status)
    {
        return false;
    }

    memset(elem_freq_title, 0, sizeof(ui_element_t));
    elem_freq_title->cfg = cfg_freq_title;
    elem_freq_title->last_box = (ui_rect_t){0, 0, 60, 16};
    elem_freq_title->pool_id = 0;

    memset(elem_freq_value, 0, sizeof(ui_element_t));
    elem_freq_value->cfg = cfg_freq_value;
    elem_freq_value->data_binding = &g_last_frequency;
    elem_freq_value->last_box = (ui_rect_t){60, 0, 68, 16};
    elem_freq_value->pool_id = 1;

    memset(elem_pulse_title, 0, sizeof(ui_element_t));
    elem_pulse_title->cfg = cfg_pulse_title;
    elem_pulse_title->last_box = (ui_rect_t){0, 16, 60, 16};
    elem_pulse_title->pool_id = 2;

    memset(elem_pulse_value, 0, sizeof(ui_element_t));
    elem_pulse_value->cfg = cfg_pulse_value;
    elem_pulse_value->data_binding = &g_last_pulse_width;
    elem_pulse_value->last_box = (ui_rect_t){60, 16, 68, 16};
    elem_pulse_value->pool_id = 3;

    memset(elem_status, 0, sizeof(ui_element_t));
    elem_status->cfg = cfg_status;
    elem_status->data_binding = &g_pulse_count;
    elem_status->last_box = (ui_rect_t){0, 48, 128, 16};
    elem_status->pool_id = 4;

    /* 创建元素数组 */
    screen_elems = (ui_element_t **)pvPortMalloc(5 * sizeof(ui_element_t *));
    if (!screen_elems)
        return false;

    screen_elems[0] = elem_freq_title;
    screen_elems[1] = elem_freq_value;
    screen_elems[2] = elem_pulse_title;
    screen_elems[3] = elem_pulse_value;
    screen_elems[4] = elem_status;

    /* 创建屏幕结构体 */
    pulse_freq_screen_ptr = (ui_screen_t *)pvPortMalloc(sizeof(ui_screen_t));
    if (!pulse_freq_screen_ptr)
        return false;

    pulse_freq_screen_ptr->name = "PulseFreq";
    pulse_freq_screen_ptr->elem_count = 5;
    pulse_freq_screen_ptr->elements = (const ui_element_t **)screen_elems;

    return true;
}

/* ================= 反初始化函数 ================= */
void pulse_freq_screen_deinit(void)
{
    if (cfg_freq_title)
        vPortFree(cfg_freq_title);
    if (cfg_freq_value)
        vPortFree(cfg_freq_value);
    if (cfg_pulse_title)
        vPortFree(cfg_pulse_title);
    if (cfg_pulse_value)
        vPortFree(cfg_pulse_value);
    if (cfg_status)
        vPortFree(cfg_status);
    if (elem_freq_title)
        vPortFree(elem_freq_title);
    if (elem_freq_value)
        vPortFree(elem_freq_value);
    if (elem_pulse_title)
        vPortFree(elem_pulse_title);
    if (elem_pulse_value)
        vPortFree(elem_pulse_value);
    if (elem_status)
        vPortFree(elem_status);
    if (screen_elems)
        vPortFree(screen_elems);
    if (pulse_freq_screen_ptr)
        vPortFree(pulse_freq_screen_ptr);
}

/* ================= 获取屏幕指针 ================= */
const ui_screen_t *pulse_freq_screen_get(void)
{
    return (const ui_screen_t *)pulse_freq_screen_ptr;
}

/* ================= 更新显示数据 ================= */
void pulse_freq_screen_update(void)
{
    PulseWidthResult_t result;
    static uint32_t last_processing_time = 0;
    uint32_t current_time = xTaskGetTickCount();

    /* 限制处理频率，避免过于频繁的更新 */
    if (current_time - last_processing_time < 50)
    { // 至少20Hz更新
        return;
    }
    last_processing_time = current_time;

    /* 处理脉宽事件 */
    if (PulseCapture_ProcessPulseWidth(&result))
    {
        if (result.is_valid && result.high_time_us > 0)
        {
            /* 计算频率 = 1 / 周期 */
            float period_seconds = result.period_seconds;
            if (period_seconds > 0)
            {
                g_last_frequency = 1.0f / period_seconds; // 频率 (Hz)
            }

            /* 保存脉宽 */
            g_last_pulse_width = (float)result.high_time_us; // 微秒

            /* 更新计数 */
            g_pulse_count++;
            g_last_update_time = current_time;
            g_signal_present = 1;

            /* 标记需要重绘的元素 */
            if (elem_freq_value)
                elem_freq_value->state |= UI_STATE_DIRTY;
            if (elem_pulse_value)
                elem_pulse_value->state |= UI_STATE_DIRTY;
            if (elem_status)
                elem_status->state |= UI_STATE_DIRTY;
        }
    }

    /* 检查信号是否丢失（超过500ms无新脉冲） */
    if (g_signal_present && (current_time - g_last_update_time > 500))
    {
        g_signal_present = 0;
        if (elem_status)
            elem_status->state |= UI_STATE_DIRTY;
    }
}

/* ================= 辅助函数 ================= */

/**
 * @brief 获取当前频率值
 */
float pulse_freq_get_current(void)
{
    return g_last_frequency;
}

/**
 * @brief 获取当前脉宽值
 */
float pulse_freq_get_pulse_width(void)
{
    return g_last_pulse_width;
}

/**
 * @brief 重置计数
 */
void pulse_freq_reset_count(void)
{
    g_pulse_count = 0;
    g_signal_present = 0;
    g_last_frequency = 0;
    g_last_pulse_width = 0;

    /* 标记所有元素需要重绘 */
    if (elem_freq_value)
        elem_freq_value->state |= UI_STATE_DIRTY;
    if (elem_pulse_value)
        elem_pulse_value->state |= UI_STATE_DIRTY;
    if (elem_status)
        elem_status->state |= UI_STATE_DIRTY;
}