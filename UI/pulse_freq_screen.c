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

static uint8_t *u32_to_str(uint8_t *dst, uint32_t val, uint8_t min_width)
{
    uint8_t tmp[12], *p = tmp;
    do { *p++ = (val % 10) + '0'; val /= 10; } while (val);
    
    /* 补零 */
    while (p - tmp < min_width) *p++ = '0';
    
    /* 反转写入 dst */
    while (p > tmp) *dst++ = *--p;
    return dst;
}

static void fmt_measure(char *buf, uint32_t i_part, uint32_t f_part, 
                        uint8_t f_width, const char *unit)
{
    uint8_t *p = (uint8_t *)buf;
    
    /* 整数部分 */
    p = u32_to_str(p, i_part, 1);
    *p++ = '.';
    
    /* 小数部分 (固定宽度补零) */
    p = u32_to_str(p, f_part, f_width);
    
    /* 单位 */
    if (unit) {
        *p++ = ' ';
        while (*unit) *p++ = *unit++;
    }
    *p = '\0';
}

/* 渲染函数 - 显示数值 */
static void render_value(ui_element_t *self)
{
    if (!self || !self->cfg || !self->data_binding || !pulse_freq_screen_ptr) 
        return;

    char buf[20]; 
    uint8_t page_start = self->cfg->y / 8;
    uint8_t page_end = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_end > 7) page_end = 7;

    for (uint8_t p = page_start; p <= page_end; p++)
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);

    /* ───────── 频率显示 ───────── */
    if (self == elem_freq_value) 
    {
        float *f = (float *)self->data_binding;
        /* ✅ 防御：对齐 + 有效性 */
        if ((uint32_t)f % 4 != 0 || !isfinite(*f) || *f <= 0.0f || !g_signal_present) {
            OLED_ShowString(self->cfg->x, self->cfg->y/8, (uint8_t*)"--- Hz", 16);
            return;
        }

        if (*f >= 1000.0f) {
            uint32_t v = (uint32_t)(*f / 1000.0f * 1000.0f);
            fmt_measure(buf, v/1000, v%1000, 3, "kHz");
        }
        else if (*f >= 1.0f) {
            uint32_t v = (uint32_t)(*f * 100.0f);
            fmt_measure(buf, v/100, v%100, 2, "Hz");
        }
        else if (*f > 0.001f) {
            uint32_t v = (uint32_t)(*f * 1000000.0f);
            fmt_measure(buf, v/1000, v%1000, 3, "mHz");
        }
        else {
            OLED_ShowString(self->cfg->x, self->cfg->y/8, (uint8_t*)"<0.001 mHz", 16);
            return;
        }
    }
    /* ───────── 脉宽显示 ───────── */
    else if (self == elem_pulse_value) 
    {
        float *p = (float *)self->data_binding;
        if ((uint32_t)p % 4 != 0 || !isfinite(*p) || *p <= 0.0f || !g_signal_present) {
            OLED_ShowString(self->cfg->x, self->cfg->y/8, (uint8_t*)"--- us", 16);
            return;
        }

        uint32_t us = (uint32_t)(*p + 0.5f);
        if (us >= 1000000) {
            fmt_measure(buf, us/1000000, (us%1000000)/1000, 3, "s");
        }
        else if (us >= 1000) {
            uint32_t ms = us / 1000, rem = us % 1000;
            if (rem == 0) fmt_measure(buf, ms, 0, 0, "ms"); // 特殊：无小数
            else if (rem >= 100) fmt_measure(buf, ms, rem/100, 1, "ms");
            else if (rem >= 10)  fmt_measure(buf, ms, rem/10, 2, "ms");
            else                 fmt_measure(buf, ms, rem, 3, "ms");
        }
        else {
            fmt_measure(buf, us, 0, 0, "us"); // 特殊：无小数
        }
    }
    else { return; }

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
    
    if (!self->data_binding)
    {
        strcpy(buf, "No Signal");
        OLED_ShowString(self->cfg->x + 2, self->cfg->y / 8, (uint8_t *)buf, 16);
        return;
    }
    
    uint32_t *count = (uint32_t *)self->data_binding;

    if (g_signal_present)
    {
        snprintf(buf, sizeof(buf), "Pulse:%lu OK", *count);
    }
    else
    {
        snprintf(buf, sizeof(buf), "No Signal");
    }

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
        if (result.high_time_seconds > 0)
        {
            /* 计算频率 = 1 / 周期 */
            float period_seconds = result.high_time_seconds;
            if (period_seconds > 0)
            {
                g_last_frequency = 1.0f / period_seconds; // 频率 (Hz)
            }

            /* 保存脉宽 */
            g_last_pulse_width = (float)result.high_time_seconds * 1000000; 

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