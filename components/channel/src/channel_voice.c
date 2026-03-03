#include "channel_voice.h"
#include "channel.h"
#include "channel_router.h"
#include "voice_pipeline.h"
#include "voice_stt.h"
#include "voice_tts.h"
#include "voice_playback.h"
#include "ui_chat.h"
#include "ui_status.h"
#include "ui_manager.h"
#include "net_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ch_voice";

static QueueHandle_t s_trigger_queue = NULL;
static const char *s_api_key = NULL;
static const char *s_tts_voice = NULL;

/* Reply callback: speak the agent's response */
static void voice_reply_cb(channel_id_t channel, const char *sender_id,
                            const char *text, void *channel_data)
{
    (void)channel;
    (void)sender_id;
    (void)channel_data;

    /* Show response on chat screen */
    ui_chat_add_message(UI_CHAT_ROLE_ASSISTANT, text);
    ui_manager_request_redraw();

    /* Synthesize and play speech */
    if (s_api_key && text && text[0]) {
        voice_tts_audio_t audio;
        esp_err_t err = voice_tts_synthesize(text, s_api_key, s_tts_voice, &audio);
        if (err == ESP_OK && audio.data) {
            voice_playback_play(audio.data, audio.count, audio.sample_rate);
            voice_tts_free(&audio);
        } else {
            ESP_LOGW(TAG, "TTS failed, text-only response");
        }
    }

    ui_status_set_busy(false);
    ui_manager_request_redraw();
}

static void voice_state_cb(voice_state_t state, void *user_data)
{
    (void)user_data;
    const char *state_names[] = {"Idle", "Recording...", "Transcribing...", "Thinking...", "Speaking..."};
    if (state < sizeof(state_names) / sizeof(state_names[0])) {
        ESP_LOGI(TAG, "Voice state: %s", state_names[state]);
    }
}

static void voice_task(void *arg)
{
    (void)arg;
    uint8_t trigger;

    ESP_LOGI(TAG, "Voice task started");

    while (1) {
        if (xQueueReceive(s_trigger_queue, &trigger, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "Voice interaction triggered");

        /* Check WiFi before starting (STT needs network) */
        if (!net_wifi_is_connected()) {
            ESP_LOGW(TAG, "WiFi not connected, waiting...");
            ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "WiFi connecting...");
            ui_manager_request_redraw();
            if (net_wifi_wait_connected(pdMS_TO_TICKS(10000)) != ESP_OK) {
                ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "No WiFi");
                ui_manager_request_redraw();
                continue;
            }
        }

        ui_status_set_busy(true);
        ui_manager_request_redraw();
        ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "Listening...");

        /* Record and transcribe */
        char user_text[VOICE_STT_MAX_TEXT];
        esp_err_t err = voice_pipeline_run(user_text, sizeof(user_text));

        if (err != ESP_OK || user_text[0] == '\0') {
            ESP_LOGW(TAG, "Voice pipeline returned no text");
            ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "No speech detected");
            ui_status_set_busy(false);
            ui_manager_request_redraw();
            continue;
        }

        /* Show user's transcribed text */
        ui_chat_add_message(UI_CHAT_ROLE_USER, user_text);
        ui_manager_request_redraw();

        /* Route through agent */
        channel_message_t msg = {
            .channel = CHANNEL_VOICE,
            .text = user_text,
            .sender_id = "voice",
            .channel_data = NULL,
        };

        err = channel_router_handle(&msg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Router handle failed: %s", esp_err_to_name(err));
            ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "Agent error");
            ui_status_set_busy(false);
            ui_manager_request_redraw();
        }
        /* Reply callback handles the rest (TTS + display) */
    }
}

esp_err_t channel_voice_init(const char *openai_api_key,
                              const char *tts_voice,
                              int vad_threshold)
{
    if (!openai_api_key || !openai_api_key[0]) {
        ESP_LOGW(TAG, "No OpenAI API key, voice disabled");
        return ESP_ERR_INVALID_ARG;
    }

    s_api_key = openai_api_key;
    s_tts_voice = tts_voice;

    /* Initialize voice pipeline */
    voice_pipeline_config_t pipe_cfg = {
        .openai_api_key = openai_api_key,
        .tts_voice = tts_voice,
        .vad_threshold = vad_threshold,
        .max_record_ms = 15000,
        .silence_ms = 1500,
        .state_cb = voice_state_cb,
        .cb_user_data = NULL,
    };

    esp_err_t err = voice_pipeline_init(&pipe_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pipeline init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Register reply callback for voice channel */
    channel_router_register_reply(CHANNEL_VOICE, voice_reply_cb);

    /* Create trigger queue */
    s_trigger_queue = xQueueCreate(2, sizeof(uint8_t));
    if (!s_trigger_queue) return ESP_ERR_NO_MEM;

    /* Create voice task */
    BaseType_t ret = xTaskCreatePinnedToCore(
        voice_task, "voice_task", 12288, NULL, 4, NULL, 1);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Voice channel initialized");
    return ESP_OK;
}

esp_err_t channel_voice_trigger(void)
{
    if (!s_trigger_queue) return ESP_ERR_INVALID_STATE;

    uint8_t trigger = 1;
    if (xQueueSend(s_trigger_queue, &trigger, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Voice trigger queue full");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
