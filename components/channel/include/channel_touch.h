#pragma once

#include "esp_err.h"

/**
 * Initialize the touch channel.
 * Registers a reply callback with the channel router and sets up
 * the chat UI for displaying responses.
 *
 * Provides quick-reply buttons and integrates with the chat screen
 * to show conversation on the LCD.
 */
esp_err_t channel_touch_init(void);

/**
 * Send a message from the touch channel to the agent.
 * This is called when the user taps a quick-reply button.
 * Runs agent processing in a background task.
 */
void channel_touch_send(const char *text);
