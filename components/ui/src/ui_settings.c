#include "ui_settings.h"
#include "ui_manager.h"
#include "ui_status.h"
#include "bsp_display.h"
#include "bsp_power.h"
#include "llm_provider.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_settings";

/* Colors */
#define SET_BG      BSP_COLOR_BLACK
#define SET_FG      BSP_COLOR_WHITE
#define SET_LABEL   bsp_display_color(0x80, 0x80, 0x80)
#define SET_VALUE   bsp_display_color(0x80, 0xD0, 0xFF)
#define SET_BTN_BG  bsp_display_color(0x30, 0x30, 0x40)
#define SET_BTN_HL  bsp_display_color(0x40, 0x50, 0x70)

/* Layout */
#define ROW_H       28
#define MARGIN_X    8
#define BTN_W       48
#define BTN_H       24

static int s_brightness = 80;  // 0-100

static uint16_t row_y(int idx)
{
    return UI_STATUS_BAR_H + 30 + idx * ROW_H;
}

static void draw_provider_row(void)
{
    uint16_t y = row_y(0);
    uint16_t x = MARGIN_X;
    uint16_t w = BSP_LCD_W - 2 * MARGIN_X;

    bsp_display_fill(x, y, w, ROW_H, SET_BG);
    ui_draw_text(x, y + 4, 100, "LLM:", SET_LABEL, SET_BG);

    /* Provider buttons */
    llm_provider_t *active = llm_get_active_provider();
    const char *active_name = active ? active->name : "";

    /* Claude button */
    uint16_t bx = 110;
    bool is_claude = (strcmp(active_name, "claude") == 0);
    bsp_display_fill(bx, y + 2, BTN_W, BTN_H, is_claude ? SET_BTN_HL : SET_BTN_BG);
    ui_draw_text(bx + 4, y + 6, BTN_W - 8, "Claude", SET_FG, is_claude ? SET_BTN_HL : SET_BTN_BG);

    /* OpenAI button */
    bx = 170;
    bool is_openai = (strcmp(active_name, "openai") == 0);
    bsp_display_fill(bx, y + 2, BTN_W + 8, BTN_H, is_openai ? SET_BTN_HL : SET_BTN_BG);
    ui_draw_text(bx + 4, y + 6, BTN_W, "OpenAI", SET_FG, is_openai ? SET_BTN_HL : SET_BTN_BG);
}

static void draw_brightness_row(void)
{
    uint16_t y = row_y(1);
    uint16_t x = MARGIN_X;
    uint16_t w = BSP_LCD_W - 2 * MARGIN_X;

    bsp_display_fill(x, y, w, ROW_H, SET_BG);
    ui_draw_text(x, y + 4, 100, "Bright:", SET_LABEL, SET_BG);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", s_brightness);
    ui_draw_text(150, y + 4, 40, buf, SET_VALUE, SET_BG);

    /* - button */
    bsp_display_fill(200, y + 2, 32, BTN_H, SET_BTN_BG);
    ui_draw_text(210, y + 6, 20, "-", SET_FG, SET_BTN_BG);

    /* + button */
    bsp_display_fill(244, y + 2, 32, BTN_H, SET_BTN_BG);
    ui_draw_text(254, y + 6, 20, "+", SET_FG, SET_BTN_BG);
}

static void settings_on_enter(void)
{
    bsp_display_fill(0, UI_STATUS_BAR_H, BSP_LCD_W, BSP_LCD_H - UI_STATUS_BAR_H, SET_BG);
}

static void settings_on_draw(void)
{
    ui_draw_text(MARGIN_X, UI_STATUS_BAR_H + 4, 200, "Settings", SET_FG, SET_BG);

    draw_provider_row();
    draw_brightness_row();

    /* Back button at bottom */
    uint16_t btn_top = BSP_LCD_H - 32;
    bsp_display_fill(0, btn_top, BSP_LCD_W, 32, SET_BTN_BG);
    ui_draw_text(BSP_LCD_W / 2 - 24, btn_top + 8, 80, "< Chat", BSP_COLOR_WHITE, SET_BTN_BG);
}

static void settings_on_touch(uint16_t x, uint16_t y)
{
    /* Back button */
    if (y >= BSP_LCD_H - 32) {
        ui_manager_switch_screen(UI_SCREEN_CHAT);
        return;
    }

    /* Provider row */
    uint16_t py = row_y(0);
    if (y >= py && y < py + ROW_H) {
        if (x >= 110 && x < 110 + BTN_W) {
            /* Claude */
            if (llm_get_provider("claude")) {
                llm_set_active_provider("claude");
                ui_status_set_provider("claude");
                ESP_LOGI(TAG, "Switched to Claude");
                ui_manager_request_redraw();
            }
        } else if (x >= 170 && x < 170 + BTN_W + 8) {
            /* OpenAI */
            if (llm_get_provider("openai")) {
                llm_set_active_provider("openai");
                ui_status_set_provider("openai");
                ESP_LOGI(TAG, "Switched to OpenAI");
                ui_manager_request_redraw();
            }
        }
        return;
    }

    /* Brightness row */
    uint16_t by = row_y(1);
    if (y >= by && y < by + ROW_H) {
        if (x >= 200 && x < 232) {
            /* Decrease */
            s_brightness -= 20;
            if (s_brightness < 20) s_brightness = 20;
            bsp_power_set_lcd_brightness(s_brightness);
            ui_manager_request_redraw();
        } else if (x >= 244 && x < 276) {
            /* Increase */
            s_brightness += 20;
            if (s_brightness > 100) s_brightness = 100;
            bsp_power_set_lcd_brightness(s_brightness);
            ui_manager_request_redraw();
        }
        return;
    }
}

esp_err_t ui_settings_init(void)
{
    static const ui_screen_ops_t ops = {
        .on_enter = settings_on_enter,
        .on_draw  = settings_on_draw,
        .on_touch = settings_on_touch,
    };
    ui_manager_register_screen(UI_SCREEN_SETTINGS, &ops);

    ESP_LOGI(TAG, "Settings screen initialized");
    return ESP_OK;
}
