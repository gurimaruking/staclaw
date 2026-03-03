#pragma once

#include "esp_err.h"

/**
 * Initialize Telegram bot channel.
 * Starts a long-polling task that receives messages and routes them
 * through the channel router.
 *
 * @param bot_token  Telegram Bot API token
 */
esp_err_t channel_telegram_init(const char *bot_token);

/**
 * Send a message to a specific Telegram chat.
 */
esp_err_t channel_telegram_send(const char *chat_id, const char *text);

/**
 * Stop the Telegram polling task.
 */
void channel_telegram_stop(void);
