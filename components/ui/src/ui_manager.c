#include "ui_manager.h"
#include "ui_font.h"
#include "ui_status.h"
#include "bsp_display.h"
#include "bsp_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_mgr";

#define UI_TASK_STACK   (6 * 1024)
#define UI_TASK_PRIO    5
#define UI_POLL_MS      50    /* Touch polling / redraw interval */

/* Character rendering line buffer: one font-row of one character (8 pixels) */
static uint16_t s_char_buf[UI_FONT_W * UI_FONT_H];

static ui_screen_ops_t s_screens[UI_SCREEN_MAX];
static ui_screen_t s_active_screen = UI_SCREEN_CHAT;
static bool s_redraw_pending = true;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task = NULL;

/* ---- Drawing primitives ---- */

int ui_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
    const uint8_t *glyph = ui_font_glyph(c);

    /* Render glyph into pixel buffer */
    for (int row = 0; row < UI_FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < UI_FONT_W; col++) {
            s_char_buf[row * UI_FONT_W + col] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }

    /* Clip to screen bounds */
    uint16_t w = UI_FONT_W;
    uint16_t h = UI_FONT_H;
    if (x + w > BSP_LCD_W) w = BSP_LCD_W - x;
    if (y + h > BSP_LCD_H) h = BSP_LCD_H - y;

    if (w > 0 && h > 0) {
        /* If clipped, blit row by row; otherwise blit entire character */
        if (w == UI_FONT_W && h == UI_FONT_H) {
            bsp_display_blit(x, y, UI_FONT_W, UI_FONT_H, s_char_buf);
        } else {
            for (int row = 0; row < h; row++) {
                bsp_display_blit(x, y + row, w, 1, &s_char_buf[row * UI_FONT_W]);
            }
        }
    }

    return UI_FONT_W;
}

int ui_draw_text(uint16_t x, uint16_t y, uint16_t max_w, const char *text,
                 uint16_t fg, uint16_t bg)
{
    uint16_t cx = x;
    uint16_t cy = y;
    uint16_t right = x + max_w;

    while (*text) {
        if (*text == '\n') {
            /* Clear rest of line */
            if (cx < right) {
                bsp_display_fill(cx, cy, right - cx, UI_FONT_H, bg);
            }
            cx = x;
            cy += UI_FONT_H;
            text++;
            continue;
        }

        /* Word wrap: if char doesn't fit, go to next line */
        if (cx + UI_FONT_W > right) {
            cx = x;
            cy += UI_FONT_H;
        }

        /* Stop if we go below screen */
        if (cy + UI_FONT_H > BSP_LCD_H) break;

        ui_draw_char(cx, cy, *text, fg, bg);
        cx += UI_FONT_W;
        text++;
    }

    /* Clear rest of last line */
    if (cx < right && cy < BSP_LCD_H) {
        bsp_display_fill(cx, cy, right - cx, UI_FONT_H, bg);
    }

    return cy + UI_FONT_H;
}

void ui_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    bsp_display_fill(x, y, w, h, color);
}

/* ---- Screen management ---- */

void ui_manager_register_screen(ui_screen_t id, const ui_screen_ops_t *ops)
{
    if (id < UI_SCREEN_MAX && ops) {
        s_screens[id] = *ops;
    }
}

void ui_manager_switch_screen(ui_screen_t id)
{
    if (id >= UI_SCREEN_MAX || id == s_active_screen) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Leave current screen */
    if (s_screens[s_active_screen].on_leave) {
        s_screens[s_active_screen].on_leave();
    }

    s_active_screen = id;

    /* Enter new screen */
    if (s_screens[s_active_screen].on_enter) {
        s_screens[s_active_screen].on_enter();
    }

    s_redraw_pending = true;
    xSemaphoreGive(s_mutex);
}

ui_screen_t ui_manager_get_screen(void)
{
    return s_active_screen;
}

void ui_manager_request_redraw(void)
{
    s_redraw_pending = true;
}

/* ---- UI task ---- */

static void ui_task(void *arg)
{
    bsp_touch_point_t touch;
    TickType_t last_touch_tick = 0;

    ESP_LOGI(TAG, "UI task started");

    /* Initial full redraw */
    if (s_screens[s_active_screen].on_enter) {
        s_screens[s_active_screen].on_enter();
    }
    s_redraw_pending = true;

    while (1) {
        /* Handle touch input */
        if (bsp_touch_read(&touch) && touch.pressed) {
            TickType_t now = xTaskGetTickCount();
            /* Debounce: 200ms between touch events */
            if (now - last_touch_tick > pdMS_TO_TICKS(200)) {
                last_touch_tick = now;

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (s_screens[s_active_screen].on_touch) {
                    s_screens[s_active_screen].on_touch(touch.x, touch.y);
                }
                xSemaphoreGive(s_mutex);
            }
        }

        /* Handle redraws */
        if (s_redraw_pending) {
            s_redraw_pending = false;

            xSemaphoreTake(s_mutex, portMAX_DELAY);

            /* Always draw status bar first */
            ui_status_draw();

            /* Draw active screen */
            if (s_screens[s_active_screen].on_draw) {
                s_screens[s_active_screen].on_draw();
            }

            xSemaphoreGive(s_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
    }
}

/* ---- Init ---- */

esp_err_t ui_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Note: s_screens is static (zero-initialized at startup).
     * Screen callbacks are registered BEFORE this function is called,
     * so we must NOT clear s_screens here. */

    BaseType_t ret = xTaskCreatePinnedToCore(
        ui_task, "ui_task", UI_TASK_STACK,
        NULL, UI_TASK_PRIO, &s_task, 1  /* Core 1: UI + main */
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI manager initialized");
    return ESP_OK;
}
