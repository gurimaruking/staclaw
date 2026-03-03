#include "config_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config";
static staclaw_config_t s_config;
static bool s_initialized = false;

#define NVS_NAMESPACE "staclaw"

static void config_set_defaults(staclaw_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->wifi_ssid, "", sizeof(cfg->wifi_ssid));
    strncpy(cfg->wifi_password, "", sizeof(cfg->wifi_password));

    strncpy(cfg->active_provider, "claude", sizeof(cfg->active_provider));
    strncpy(cfg->claude_model, "claude-sonnet-4-20250514", sizeof(cfg->claude_model));
    strncpy(cfg->openai_model, "gpt-4o", sizeof(cfg->openai_model));
    cfg->max_tokens = 2048;

    cfg->telegram_chat_id = 0;

    strncpy(cfg->stt_provider, "whisper", sizeof(cfg->stt_provider));
    strncpy(cfg->tts_provider, "openai", sizeof(cfg->tts_provider));
    strncpy(cfg->tts_voice, "alloy", sizeof(cfg->tts_voice));
    cfg->vad_threshold = 0;   // 0 = auto-calibrate from ambient noise

    cfg->agent_max_iterations = 5;
    cfg->history_max_turns = 20;

    cfg->backlight_percent = 80;
    cfg->speaker_volume = 128;
}

static esp_err_t nvs_read_str(nvs_handle_t handle, const char *key, char *out, size_t max_len)
{
    size_t len = max_len;
    esp_err_t err = nvs_get_str(handle, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // Keep default
    }
    return err;
}

esp_err_t config_init(staclaw_config_t *config)
{
    config_set_defaults(&s_config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        if (config) *config = s_config;
        s_initialized = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        if (config) *config = s_config;
        s_initialized = true;
        return err;
    }

    nvs_read_str(handle, "wifi_ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid));
    nvs_read_str(handle, "wifi_pass", s_config.wifi_password, sizeof(s_config.wifi_password));
    nvs_read_str(handle, "claude_key", s_config.claude_api_key, sizeof(s_config.claude_api_key));
    nvs_read_str(handle, "openai_key", s_config.openai_api_key, sizeof(s_config.openai_api_key));
    nvs_read_str(handle, "active_llm", s_config.active_provider, sizeof(s_config.active_provider));
    nvs_read_str(handle, "claude_mdl", s_config.claude_model, sizeof(s_config.claude_model));
    nvs_read_str(handle, "openai_mdl", s_config.openai_model, sizeof(s_config.openai_model));
    nvs_read_str(handle, "tg_token", s_config.telegram_token, sizeof(s_config.telegram_token));
    nvs_read_str(handle, "stt_prov", s_config.stt_provider, sizeof(s_config.stt_provider));
    nvs_read_str(handle, "tts_prov", s_config.tts_provider, sizeof(s_config.tts_provider));
    nvs_read_str(handle, "tts_voice", s_config.tts_voice, sizeof(s_config.tts_voice));

    int32_t i32;
    if (nvs_get_i32(handle, "max_tokens", &i32) == ESP_OK) s_config.max_tokens = i32;
    if (nvs_get_i32(handle, "max_iter", &i32) == ESP_OK) s_config.agent_max_iterations = i32;
    if (nvs_get_i32(handle, "hist_turns", &i32) == ESP_OK) s_config.history_max_turns = i32;
    if (nvs_get_i32(handle, "vad_thresh", &i32) == ESP_OK) s_config.vad_threshold = i32;

    int64_t i64;
    if (nvs_get_i64(handle, "tg_chat_id", &i64) == ESP_OK) s_config.telegram_chat_id = i64;

    uint8_t u8;
    if (nvs_get_u8(handle, "backlight", &u8) == ESP_OK) s_config.backlight_percent = u8;
    if (nvs_get_u8(handle, "spk_vol", &u8) == ESP_OK) s_config.speaker_volume = u8;

    nvs_close(handle);

    if (config) *config = s_config;
    s_initialized = true;
    ESP_LOGI(TAG, "Config loaded (provider=%s, model=%s)",
             s_config.active_provider,
             strcmp(s_config.active_provider, "claude") == 0 ? s_config.claude_model : s_config.openai_model);
    return ESP_OK;
}

esp_err_t config_save(const staclaw_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, "wifi_ssid", config->wifi_ssid);
    nvs_set_str(handle, "wifi_pass", config->wifi_password);
    nvs_set_str(handle, "claude_key", config->claude_api_key);
    nvs_set_str(handle, "openai_key", config->openai_api_key);
    nvs_set_str(handle, "active_llm", config->active_provider);
    nvs_set_str(handle, "claude_mdl", config->claude_model);
    nvs_set_str(handle, "openai_mdl", config->openai_model);
    nvs_set_str(handle, "tg_token", config->telegram_token);
    nvs_set_str(handle, "stt_prov", config->stt_provider);
    nvs_set_str(handle, "tts_prov", config->tts_provider);
    nvs_set_str(handle, "tts_voice", config->tts_voice);

    nvs_set_i32(handle, "max_tokens", config->max_tokens);
    nvs_set_i32(handle, "max_iter", config->agent_max_iterations);
    nvs_set_i32(handle, "hist_turns", config->history_max_turns);
    nvs_set_i32(handle, "vad_thresh", config->vad_threshold);
    nvs_set_i64(handle, "tg_chat_id", config->telegram_chat_id);
    nvs_set_u8(handle, "backlight", config->backlight_percent);
    nvs_set_u8(handle, "spk_vol", config->speaker_volume);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        s_config = *config;
        ESP_LOGI(TAG, "Config saved");
    }
    return err;
}

const staclaw_config_t *config_get(void)
{
    return &s_config;
}

esp_err_t config_set_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
