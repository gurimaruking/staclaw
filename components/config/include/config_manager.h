#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    // WiFi
    char wifi_ssid[33];
    char wifi_password[65];

    // LLM
    char claude_api_key[128];
    char openai_api_key[128];
    char active_provider[16];     // "claude" or "openai"
    char claude_model[48];
    char openai_model[48];
    int  max_tokens;

    // Telegram
    char    telegram_token[64];
    int64_t telegram_chat_id;     // Allowed chat ID (0 = accept any)

    // Voice
    char stt_provider[16];        // "whisper"
    char tts_provider[16];        // "openai"
    char tts_voice[16];           // "alloy"
    int  vad_threshold;

    // Agent
    int agent_max_iterations;
    int history_max_turns;

    // Display
    uint8_t backlight_percent;
    uint8_t speaker_volume;
} staclaw_config_t;

/**
 * Initialize config system and load from NVS.
 * Missing keys get default values.
 */
esp_err_t config_init(staclaw_config_t *config);

/**
 * Save current config to NVS.
 */
esp_err_t config_save(const staclaw_config_t *config);

/**
 * Get a read-only pointer to the active config.
 */
const staclaw_config_t *config_get(void);

/**
 * Set a single string config value by key name.
 */
esp_err_t config_set_string(const char *key, const char *value);
