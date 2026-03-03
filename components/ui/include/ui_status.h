#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Status bar height in pixels. Displayed at top of every screen.
 */
#define UI_STATUS_BAR_H  20

/**
 * Initialize the status bar module.
 */
esp_err_t ui_status_init(void);

/**
 * Draw the status bar at the top of the screen.
 * Called by the UI manager before drawing screen content.
 */
void ui_status_draw(void);

/**
 * Update WiFi connection status indicator.
 */
void ui_status_set_wifi(bool connected);

/**
 * Update LLM provider name shown in status bar.
 */
void ui_status_set_provider(const char *name);

/**
 * Set the "thinking" indicator (shown while agent is processing).
 */
void ui_status_set_busy(bool busy);
