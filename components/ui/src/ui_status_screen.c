#include "ui_manager.h"
#include "ui_status.h"
#include "bsp_display.h"
#include "bsp_power.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdio.h>

static const char *TAG = "ui_stscrn";

/* Colors */
#define STS_BG      BSP_COLOR_BLACK
#define STS_FG      BSP_COLOR_WHITE
#define STS_LABEL   bsp_display_color(0x80, 0x80, 0x80)
#define STS_VALUE   bsp_display_color(0x80, 0xD0, 0xFF)
#define STS_BTN_BG  bsp_display_color(0x30, 0x30, 0x40)

static void status_on_enter(void)
{
    bsp_display_fill(0, UI_STATUS_BAR_H, BSP_LCD_W, BSP_LCD_H - UI_STATUS_BAR_H, STS_BG);
}

static void status_on_draw(void)
{
    uint16_t y = UI_STATUS_BAR_H + 8;
    uint16_t x = 8;
    uint16_t w = BSP_LCD_W - 16;
    char buf[64];

    /* Title */
    ui_draw_text(x, y, w, "System Status", STS_FG, STS_BG);
    y += 24;

    /* Battery */
    int bat_mv = bsp_power_get_battery_mv();
    int bat_pct = bsp_power_get_battery_percent();
    bool charging = bsp_power_is_charging();
    snprintf(buf, sizeof(buf), "Battery: %d%% (%dmV)%s",
             bat_pct, bat_mv, charging ? " [CHG]" : "");
    ui_draw_text(x, y, w, buf, STS_VALUE, STS_BG);
    y += 20;

    /* Heap */
    snprintf(buf, sizeof(buf), "Heap: %luKB free (%luKB min)",
             (unsigned long)esp_get_free_heap_size() / 1024,
             (unsigned long)esp_get_minimum_free_heap_size() / 1024);
    ui_draw_text(x, y, w, buf, STS_VALUE, STS_BG);
    y += 20;

    /* PSRAM */
    snprintf(buf, sizeof(buf), "PSRAM: %luKB free",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    ui_draw_text(x, y, w, buf, STS_VALUE, STS_BG);
    y += 20;

    /* Uptime */
    uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    snprintf(buf, sizeof(buf), "Uptime: %lu:%02lu:%02lu",
             (unsigned long)hr, (unsigned long)(min % 60), (unsigned long)(sec % 60));
    ui_draw_text(x, y, w, buf, STS_VALUE, STS_BG);
    y += 20;

    /* Version */
    ui_draw_text(x, y, w, "staclaw v0.1.0", STS_LABEL, STS_BG);
    y += 20;

    /* Bottom buttons: [< Chat] [Settings >] */
    uint16_t btn_top = BSP_LCD_H - 32;
    bsp_display_fill(0, btn_top, BSP_LCD_W, 32, STS_BTN_BG);
    ui_draw_text(20, btn_top + 8, 80, "< Chat", BSP_COLOR_WHITE, STS_BTN_BG);
    ui_draw_text(BSP_LCD_W - 100, btn_top + 8, 100, "Settings >", BSP_COLOR_WHITE, STS_BTN_BG);
}

static void status_on_touch(uint16_t x, uint16_t y)
{
    if (y >= BSP_LCD_H - 32) {
        if (x > BSP_LCD_W / 2) {
            ui_manager_switch_screen(UI_SCREEN_SETTINGS);
        } else {
            ui_manager_switch_screen(UI_SCREEN_CHAT);
        }
    }
}

esp_err_t ui_status_screen_init(void)
{
    static const ui_screen_ops_t ops = {
        .on_enter = status_on_enter,
        .on_draw  = status_on_draw,
        .on_touch = status_on_touch,
    };
    ui_manager_register_screen(UI_SCREEN_STATUS, &ops);

    ESP_LOGI(TAG, "Status screen initialized");
    return ESP_OK;
}
