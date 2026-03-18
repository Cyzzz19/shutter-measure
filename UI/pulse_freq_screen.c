#include "ui_core.h"
#include "pulseCapture.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "menu_screen.h"
#include "cmsis_os.h" // 如果需要FreeRTOS的xTaskGetTickCount

/* ================= 私有变量 ================= */
static ui_element_cfg_t *cfg_freq_title = NULL;
static ui_element_cfg_t *cfg_freq_value = NULL;
static ui_element_cfg_t *cfg_pulse_title = NULL;
static ui_element_cfg_t *cfg_pulse_value = NULL;
static ui_element_cfg_t *cfg_status = NULL;
static ui_element_cfg_t *cfg_root_listener = NULL;


static ui_element_t *elem_freq_title = NULL;
static ui_element_t *elem_freq_value = NULL;
static ui_element_t *elem_pulse_title = NULL;
static ui_element_t *elem_pulse_value = NULL;
static ui_element_t *elem_status = NULL;
static ui_element_t *elem_root_listener = NULL;

static ui_element_t **screen_elems = NULL;
static ui_screen_t *pulse_freq_screen_ptr = NULL;

/* 脉宽频率数据 */
static float g_last_frequency = 0.0f;   // 最近一次频率(Hz)
static float g_last_pulse_width = 0.0f; // 最近一次脉宽(us)
static uint32_t g_pulse_count = 0;      // 脉冲计数
static uint32_t g_last_update_time = 0; // 最后更新时间
static uint8_t g_signal_present = 0;    // 信号存在标志

/* ================= 辅助函数：固定宽度格式化 ================= */
/* 功能：将字符串右对齐填充到固定宽度，空白补空格 */
static void fmt_fixed(char *out, const char *src, uint8_t width)
{
    uint8_t len = src ? strlen(src) : 0;
    uint8_t pad = (len >= width) ? 0 : width - len;
    
    /* 先填空格 */
    for (uint8_t i = 0; i < pad; i++) *out++ = ' ';
    /* 再填内容 */
    if (src) { while (*src) *out++ = *src++; }
    *out = '\0';
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

/* 功能：格式化数值 (整数.小数 单位)，输出固定宽度 */
static void fmt_measure_fixed(char *buf, uint32_t i_part, uint32_t f_part, uint8_t f_digits, const char *unit, uint8_t total_width)
{
    char tmp[16], *p = tmp;
    
    /* 1. 构建原始字符串: "123.456 kHz" */
    p = u32_to_str((uint8_t*)p, i_part, 1);
    if (f_digits > 0) {
        *p++ = '.';
        p = (char*)u32_to_str((uint8_t*)p, f_part, f_digits);
    }
    if (unit) { *p++ = ' '; while (*unit) *p++ = *unit++; }
    *p = '\0';
    
    /* 2. 右对齐填充到 total_width */
    fmt_fixed(buf, tmp, total_width);
}

/* ================= 优化后的渲染函数 ================= */

/* 标题渲染：高亮时左侧显示三角图标 */
static void render_title(ui_element_t *self)
{
    if (!self || !self->cfg) return;

    /* 1. 清空区域 */
    uint8_t page_s = self->cfg->y / 8, page_e = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_e > 7) page_e = 7;
    for (uint8_t p = page_s; p <= page_e; p++)
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);

    /* 2. 高亮：绘制三角图标  */
    uint8_t text_x = self->cfg->x;
    if (self->state & UI_STATE_HIGHLIGHT) {
        OLED_ShowIcon(text_x, self->cfg->y / 8, 0x00); /* 三角 */
        text_x += 16; /* 图标宽8px */
    }

    /* 3. 绘制文本 */
    if (self->cfg->text)
        OLED_ShowString(text_x, self->cfg->y / 8, (uint8_t*)self->cfg->text, 16);
}

/* 数值渲染：固定宽度防漂移 */
static void render_value(ui_element_t *self)
{
    if (!self || !self->cfg || !self->data_binding) return;

    char buf[24] = {0}; /* 缓冲区足够容纳格式化结果 */
    uint8_t page_s = self->cfg->y / 8, page_e = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_e > 7) page_e = 7;

    /* 清空区域 */
    for (uint8_t p = page_s; p <= page_e; p++)
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);

    /* ───────── 频率显示 ───────── */
    if (self == elem_freq_value) {
        float *f = (float *)self->data_binding;
        
        /* 无效值处理 */
        if (!isfinite(*f) || *f <= 0.0f || !g_signal_present) {
            fmt_fixed(buf, "--- Hz", 9); /* 固定9字符宽 */
            OLED_ShowString(self->cfg->x, self->cfg->y/8, (uint8_t*)buf, 16);
            return;
        }

        /* 自动单位转换 + 固定宽度格式化 */
        if (*f >= 1000.0f) {
            /* kHz: XXX.XXX (总宽9) */
            uint32_t v = (uint32_t)(*f * 1000.0f + 0.5f);
            fmt_measure_fixed(buf, v/1000, v%1000, 3, "kHz", 9);
        }
        else if (*f >= 1.0f) {
            /* Hz: XXX.XX (总宽8) */
            uint32_t v = (uint32_t)(*f * 100.0f + 0.5f);
            fmt_measure_fixed(buf, v/100, v%100, 2, "Hz", 8);
        }
        else {
            fmt_fixed(buf, "<0.001Hz", 9);
        }
    }
    /* ───────── 脉宽显示 ───────── */
    else if (self == elem_pulse_value) {
        float *p = (float *)self->data_binding;
        
        if (!isfinite(*p) || *p <= 0.0f || !g_signal_present) {
            fmt_fixed(buf, "--- us", 8);
            OLED_ShowString(self->cfg->x, self->cfg->y/8, (uint8_t*)buf, 16);
            return;
        }

        uint32_t us = (uint32_t)(*p + 0.5f);
        if (us >= 1000000) {
            /* 秒: X.XXX s (总宽7) */
            fmt_measure_fixed(buf, us/1000000, (us%1000000)/1000, 3, "s", 7);
        }
        else if (us >= 1000) {
            /* 毫秒: XXX.X ms (总宽8) */
            uint32_t ms = us/1000, rem = us%1000;
            fmt_measure_fixed(buf, ms, (rem+5)/10, 2, "ms", 8); /* 四舍五入 */
        }
        else {
            /* 微秒: XXXX us (总宽7) */
            fmt_measure_fixed(buf, us, 0, 0, "us", 7);
        }
    }
    
    /* 绘制：高亮时左侧预留三角位置 */
    uint8_t draw_x = self->cfg->x;
    if (self->state & UI_STATE_HIGHLIGHT) draw_x += 8;
    OLED_ShowString(draw_x, self->cfg->y/8, (uint8_t*)buf, 16);
}

/* 状态渲染：固定格式 */
static void render_status(ui_element_t *self)
{
    if (!self || !self->cfg) return;

    char buf[20] = {0};
    uint8_t page_s = self->cfg->y / 8, page_e = (self->cfg->y + self->cfg->h - 1) / 8;
    if (page_e > 7) page_e = 7;

    for (uint8_t p = page_s; p <= page_e; p++)
        OLED_Fill(self->cfg->x, p, self->cfg->x + self->cfg->w - 1, p, 0);

    /* 高亮三角 */
    uint8_t text_x = self->cfg->x;
    if (self->state & UI_STATE_HIGHLIGHT) {
        OLED_ShowChar(text_x, self->cfg->y/8, 0x10, 16);
        text_x += 8;
    }

    /* 状态文本 (固定格式) */
    if (!g_signal_present) {
        fmt_fixed(buf, "No Signal", 15);
    }
    else if (self->data_binding) {
        uint32_t *cnt = (uint32_t*)self->data_binding;
        snprintf(buf, sizeof(buf), "Pulse:%-6lu OK", *cnt); /* 左对齐计数 */
    }
    else {
        fmt_fixed(buf, "Unknown", 15);
    }
    
    OLED_ShowString(text_x, self->cfg->y/8, (uint8_t*)buf, 16);
}

static void fmt_measure(char *buf, uint32_t i_part, uint32_t f_part, uint8_t f_width, const char *unit)
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

static bool Global_Event_Handler(ui_element_t *self, ui_event_code_t evt)
{
    (void)self;
    
    if (evt == EVT_PRESS) {
        /* 按下键：打开菜单 */
        const ui_screen_t *menu = menu_screen_get();  /* 需包含 menu_screen.h */
        if (menu) {
            ui_push_screen(menu);
            return true;
        }
    }
    return false;
}

/* ================= 初始化函数 ================= */
bool pulse_freq_screen_init(void)
{
    /* ================= 1. 分配配置结构体 ================= */
    cfg_freq_title = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_freq_value = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_pulse_title = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_pulse_value = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_status = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));
    cfg_root_listener = (ui_element_cfg_t *)pvPortMalloc(sizeof(ui_element_cfg_t));

    if (!cfg_freq_title || !cfg_freq_value || !cfg_pulse_title ||
        !cfg_pulse_value || !cfg_status || !cfg_root_listener)
    {
        return false;
    }

    /* ================= 2. 初始化配置 ================= */
    /* 频率标题 */
    memset(cfg_freq_title, 0, sizeof(ui_element_cfg_t));
    cfg_freq_title->x = 0; cfg_freq_title->y = 0;
    cfg_freq_title->w = 60; cfg_freq_title->h = 16;
    cfg_freq_title->text = "F:";
    cfg_freq_title->render = render_title;
    cfg_freq_title->on_event = NULL;

    /* 频率数值 */
    memset(cfg_freq_value, 0, sizeof(ui_element_cfg_t));
    cfg_freq_value->x = 20; cfg_freq_value->y = 0;
    cfg_freq_value->w = 68; cfg_freq_value->h = 16;
    cfg_freq_value->text = "--- Hz";
    cfg_freq_value->render = render_value;
    cfg_freq_value->on_event = NULL;

    /* 脉宽标题 */
    memset(cfg_pulse_title, 0, sizeof(ui_element_cfg_t));
    cfg_pulse_title->x = 0; cfg_pulse_title->y = 16;
    cfg_pulse_title->w = 60; cfg_pulse_title->h = 16;
    cfg_pulse_title->text = "PW:";
    cfg_pulse_title->render = render_title;
    cfg_pulse_title->on_event = NULL;

    /* 脉宽数值 */
    memset(cfg_pulse_value, 0, sizeof(ui_element_cfg_t));
    cfg_pulse_value->x = 20; cfg_pulse_value->y = 16;
    cfg_pulse_value->w = 68; cfg_pulse_value->h = 16;
    cfg_pulse_value->text = "--- us";
    cfg_pulse_value->render = render_value;
    cfg_pulse_value->on_event = NULL;

    /* 状态栏 */
    memset(cfg_status, 0, sizeof(ui_element_cfg_t));
    cfg_status->x = 0; cfg_status->y = 48;
    cfg_status->w = 128; cfg_status->h = 16;
    cfg_status->text = "Status";
    cfg_status->render = render_status;
    cfg_status->on_event = NULL;

    /* 根监听器（全屏捕获事件） */
    memset(cfg_root_listener, 0, sizeof(ui_element_cfg_t));
    cfg_root_listener->x = 0; cfg_root_listener->y = 0;
    cfg_root_listener->w = 128; cfg_root_listener->h = 64;
    cfg_root_listener->text = NULL;
    cfg_root_listener->render = NULL;  /* 无需渲染 */
    cfg_root_listener->on_event = Global_Event_Handler;

    /* ================= 3. 分配元素结构体 ================= */
    elem_freq_title = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_freq_value = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_pulse_title = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_pulse_value = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_status = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));
    elem_root_listener = (ui_element_t *)pvPortMalloc(sizeof(ui_element_t));  /* 新增 */

    if (!elem_freq_title || !elem_freq_value || !elem_pulse_title ||
        !elem_pulse_value || !elem_status || !elem_root_listener)
    {
        return false;
    }

    /* ================= 4. 初始化元素 ================= */
    memset(elem_freq_title, 0, sizeof(ui_element_t));
    elem_freq_title->cfg = cfg_freq_title;
    elem_freq_title->last_box = (ui_rect_t){0, 0, 60, 16};
    elem_freq_title->pool_id = 0;

    memset(elem_freq_value, 0, sizeof(ui_element_t));
    elem_freq_value->cfg = cfg_freq_value;
    elem_freq_value->data_binding = &g_last_frequency;
    elem_freq_value->last_box = (ui_rect_t){20, 0, 68, 16};
    elem_freq_value->pool_id = 1;

    memset(elem_pulse_title, 0, sizeof(ui_element_t));
    elem_pulse_title->cfg = cfg_pulse_title;
    elem_pulse_title->last_box = (ui_rect_t){0, 16, 60, 16};
    elem_pulse_title->pool_id = 2;

    memset(elem_pulse_value, 0, sizeof(ui_element_t));
    elem_pulse_value->cfg = cfg_pulse_value;
    elem_pulse_value->data_binding = &g_last_pulse_width;
    elem_pulse_value->last_box = (ui_rect_t){20, 16, 68, 16};
    elem_pulse_value->pool_id = 3;

    memset(elem_status, 0, sizeof(ui_element_t));
    elem_status->cfg = cfg_status;
    elem_status->data_binding = &g_pulse_count;
    elem_status->last_box = (ui_rect_t){0, 48, 128, 16};
    elem_status->pool_id = 4;

    /* 根监听器元素 */
    memset(elem_root_listener, 0, sizeof(ui_element_t));
    elem_root_listener->cfg = cfg_root_listener;
    elem_root_listener->last_box = (ui_rect_t){0, 0, 128, 64};
    elem_root_listener->pool_id = 5;
    elem_root_listener->state = UI_STATE_HIDDEN;  /* 不参与渲染/焦点 */

    /* ================= 5. 创建元素数组 ================= */
    screen_elems = (ui_element_t **)pvPortMalloc(6 * sizeof(ui_element_t *));
    if (!screen_elems) return false;

    screen_elems[0] = elem_freq_title;
    screen_elems[1] = elem_freq_value;
    screen_elems[2] = elem_pulse_title;
    screen_elems[3] = elem_pulse_value;
    screen_elems[4] = elem_status;
    screen_elems[5] = elem_root_listener;  /* 根监听器放末尾 */

    /* ================= 6. 创建屏幕结构体 ================= */
    pulse_freq_screen_ptr = (ui_screen_t *)pvPortMalloc(sizeof(ui_screen_t));
    if (!pulse_freq_screen_ptr) return false;

    pulse_freq_screen_ptr->name = "PulseFreq";
    pulse_freq_screen_ptr->elem_count = 6;  /* 6个元素 */
    pulse_freq_screen_ptr->elements = (const ui_element_t **)screen_elems;
    pulse_freq_screen_ptr->on_enter = NULL;
    pulse_freq_screen_ptr->on_exit = NULL;

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