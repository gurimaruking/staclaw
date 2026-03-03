#include "ui_status.h"
#include "ui_manager.h"
#include "bsp_display.h"
#include "bsp_power.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_status";

/* Status bar colors */
#define STATUS_BG     bsp_display_color(0x20, 0x20, 0x30)  /* Dark blue-gray */
#define STATUS_FG     BSP_COLOR_WHITE
#define STATUS_DIM    bsp_display_color(0x80, 0x80, 0x80)   /* Gray */

static bool s_wifi_connected = false;
static bool s_busy = false;
static char s_provider[16] = "";

esp_err_t ui_status_init(void)
{
    ESP_LOGI(TAG, "Status bar initialized");
    return ESP_OK;
}

void ui_status_set_wifi(bool connected)
{
    s_wifi_connected = connected;
}

void ui_status_set_provider(const char *name)
{
    if (name) {
        strncpy(s_provider, name, sizeof(s_provider) - 1);
        s_provider[sizeof(s_provider) - 1] = '\0';
    } else {
        s_provider[0] = '\0';
    }
}

void ui_status_set_busy(bool busy)
{
    s_busy = busy;
}

void ui_status_draw(void)
{
    /* Clear status bar area */
    ui_fill_rect(0, 0, BSP_LCD_W, UI_STATUS_BAR_H, STATUS_BG);

    /* Left side: WiFi indicator + provider name */
    uint16_t x = 4;
    uint16_t y = 2;  /* Vertically center 16px font in 20px bar */

    if (s_wifi_connected) {
        ui_draw_char(x, y, 'W', BSP_COLOR_GREEN, STATUS_BG);
    } else {
        ui_draw_char(x, y, 'X', BSP_COLOR_RED, STATUS_BG);
    }
    x += 12;

    /* Provider name */
    if (s_provider[0]) {
        const char *p = s_provider;
        while (*p && x < 160) {
            ui_draw_char(x, y, *p, STATUS_FG, STATUS_BG);
            x += 8;
            p++;
        }
    }

    /* Center: busy indicator */
    if (s_busy) {
        const char *thinking = "...";
        uint16_t tx = (BSP_LCD_W - 24) / 2;
        for (int i = 0; i < 3; i++) {
            ui_draw_char(tx + i * 8, y, thinking[i], BSP_COLOR_GREEN, STATUS_BG);
        }
    }

    /* Right side: battery */
    int bat_pct = bsp_power_get_battery_percent();
    bool charging = bsp_power_is_charging();

    char bat_str[8];
    snprintf(bat_str, sizeof(bat_str), "%d%%", bat_pct);

    /* Choose color based on battery level */
    uint16_t bat_color = BSP_COLOR_GREEN;
    if (bat_pct < 20) {
        bat_color = BSP_COLOR_RED;
    } else if (bat_pct < 50) {
        bat_color = bsp_display_color(0xFF, 0xA0, 0x00);  /* Orange */
    }

    /* Draw battery from right edge */
    uint16_t bx = BSP_LCD_W - 4;
    if (charging) {
        bx -= 8;
        ui_draw_char(bx, y, '+', BSP_COLOR_GREEN, STATUS_BG);
    }

    int len = strlen(bat_str);
    bx -= len * 8;
    for (int i = 0; i < len; i++) {
        ui_draw_char(bx + i * 8, y, bat_str[i], bat_color, STATUS_BG);
    }
}
