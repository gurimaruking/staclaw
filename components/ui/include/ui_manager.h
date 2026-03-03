#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Screen IDs for the UI manager.
 */
typedef enum {
    UI_SCREEN_CHAT = 0,
    UI_SCREEN_STATUS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_MAX,
} ui_screen_t;

/**
 * Screen lifecycle callbacks.
 */
typedef struct {
    void (*on_enter)(void);     // Called when screen becomes active
    void (*on_leave)(void);     // Called when screen is deactivated
    void (*on_draw)(void);      // Redraw the screen
    void (*on_touch)(uint16_t x, uint16_t y);  // Touch event
} ui_screen_ops_t;

/**
 * Initialize the UI manager and start the UI task.
 * Must be called after bsp_core2_init().
 */
esp_err_t ui_manager_init(void);

/**
 * Register a screen with the UI manager.
 */
void ui_manager_register_screen(ui_screen_t id, const ui_screen_ops_t *ops);

/**
 * Switch to a different screen.
 */
void ui_manager_switch_screen(ui_screen_t id);

/**
 * Get the currently active screen.
 */
ui_screen_t ui_manager_get_screen(void);

/**
 * Request a screen redraw (thread-safe, can be called from any task).
 */
void ui_manager_request_redraw(void);

/* ---- Low-level drawing primitives (use BSP display underneath) ---- */

/**
 * Draw a single character at pixel position.
 * Returns the width advance (UI_FONT_W).
 */
int ui_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);

/**
 * Draw a string at pixel position. Handles newlines and word-wrap.
 * Returns the Y position after the last line drawn.
 */
int ui_draw_text(uint16_t x, uint16_t y, uint16_t max_w, const char *text,
                 uint16_t fg, uint16_t bg);

/**
 * Fill a rectangle with a color.
 */
void ui_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
