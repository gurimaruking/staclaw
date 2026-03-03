#include "voice_pipeline.h"
#include "voice_capture.h"
#include "voice_vad.h"
#include "voice_stt.h"
#include "voice_tts.h"
#include "voice_playback.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "voice_pipe";

static voice_pipeline_config_t s_config;
static voice_state_t s_state = VOICE_STATE_IDLE;
static bool s_initialized = false;

static void set_state(voice_state_t st)
{
    s_state = st;
    if (s_config.state_cb) {
        s_config.state_cb(st, s_config.cb_user_data);
    }
}

esp_err_t voice_pipeline_init(const voice_pipeline_config_t *config)
{
    if (!config || !config->openai_api_key) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    /* Defaults */
    if (s_config.max_record_ms <= 0) s_config.max_record_ms = 15000;
    if (s_config.silence_ms <= 0)    s_config.silence_ms = 1500;
    if (!s_config.tts_voice || !s_config.tts_voice[0]) s_config.tts_voice = "alloy";

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) return err;

    s_initialized = true;
    ESP_LOGI(TAG, "Voice pipeline initialized (max_rec=%dms, silence=%dms, voice=%s)",
             s_config.max_record_ms, s_config.silence_ms, s_config.tts_voice);
    return ESP_OK;
}

esp_err_t voice_pipeline_run(char *out_text, size_t text_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_state != VOICE_STATE_IDLE) {
        ESP_LOGW(TAG, "Pipeline busy (state=%d)", s_state);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    /* --- 1. Record --- */
    set_state(VOICE_STATE_RECORDING);

    /* Skip beep — speaker and mic share I2S_NUM_0, switching
     * between STD TX (speaker) and PDM RX (mic) is unreliable.
     * TODO: re-add beep once I2S port recycling is resolved. */
    ESP_LOGI(TAG, "Recording starting (no beep)...");

    voice_recording_t rec;
    err = voice_capture_alloc(&rec, s_config.max_record_ms, 16000);
    if (err != ESP_OK) {
        set_state(VOICE_STATE_IDLE);
        return err;
    }

    err = voice_capture_record(&rec, s_config.silence_ms, s_config.vad_threshold);
    if (err != ESP_OK || rec.count == 0) {
        ESP_LOGW(TAG, "Recording failed or empty");
        voice_capture_free(&rec);
        set_state(VOICE_STATE_IDLE);
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    /* --- 2. Transcribe --- */
    set_state(VOICE_STATE_TRANSCRIBING);

    char stt_text[VOICE_STT_MAX_TEXT];
    err = voice_stt_transcribe(&rec, s_config.openai_api_key, stt_text);
    voice_capture_free(&rec);

    if (err != ESP_OK || stt_text[0] == '\0') {
        ESP_LOGW(TAG, "Transcription failed or empty");
        set_state(VOICE_STATE_IDLE);
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "User said: %s", stt_text);

    /* Copy transcription to caller if requested */
    if (out_text && text_len > 0) {
        strncpy(out_text, stt_text, text_len - 1);
        out_text[text_len - 1] = '\0';
    }

    /* --- 3. (Agent processing happens in channel_voice) --- */
    set_state(VOICE_STATE_IDLE);
    return ESP_OK;
}

voice_state_t voice_pipeline_get_state(void)
{
    return s_state;
}
