#pragma once

#include "esp_err.h"

/**
 * Maximum number of chat messages retained for display.
 */
#define UI_CHAT_MAX_MESSAGES  20

/**
 * Maximum text length per message (truncated if longer).
 */
#define UI_CHAT_MAX_TEXT_LEN  512

/**
 * Message roles for display styling.
 */
typedef enum {
    UI_CHAT_ROLE_USER = 0,
    UI_CHAT_ROLE_ASSISTANT,
    UI_CHAT_ROLE_SYSTEM,
} ui_chat_role_t;

/**
 * Initialize the chat screen (register with UI manager).
 */
esp_err_t ui_chat_init(void);

/**
 * Add a message to the chat display (thread-safe).
 * Text is copied internally. Oldest message is evicted when full.
 */
void ui_chat_add_message(ui_chat_role_t role, const char *text);

/**
 * Update the last assistant message with streaming text.
 * Creates a new assistant message if the last message isn't from assistant.
 */
void ui_chat_stream_update(const char *text);

/**
 * Clear all chat messages from display.
 */
void ui_chat_clear(void);

/**
 * Callback type for quick-reply button presses.
 */
typedef void (*ui_chat_send_cb_t)(const char *text);

/**
 * Register a send callback for quick-reply buttons.
 * When a quick-reply button is pressed, this callback is invoked
 * with the button text.
 */
void ui_chat_set_send_callback(ui_chat_send_cb_t cb);

/**
 * Callback type for mic button press.
 */
typedef void (*ui_chat_mic_cb_t)(void);

/**
 * Register a callback for the mic button.
 * When the mic button is pressed, this callback is invoked.
 */
void ui_chat_set_mic_callback(ui_chat_mic_cb_t cb);
