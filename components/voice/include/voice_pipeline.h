#pragma once

#include "esp_err.h"

/**
 * Voice pipeline state.
 */
typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_RECORDING,
    VOICE_STATE_TRANSCRIBING,
    VOICE_STATE_THINKING,
    VOICE_STATE_SPEAKING,
} voice_state_t;

/**
 * Callback for pipeline state changes.
 */
typedef void (*voice_state_cb_t)(voice_state_t state, void *user_data);

/**
 * Pipeline configuration.
 */
typedef struct {
    const char *openai_api_key;     /**< OpenAI key (for Whisper + TTS) */
    const char *tts_voice;          /**< TTS voice name (e.g. "alloy") */
    int         vad_threshold;      /**< VAD energy threshold (0=auto) */
    int         max_record_ms;      /**< Max recording duration */
    int         silence_ms;         /**< Silence duration to stop recording */
    voice_state_cb_t state_cb;      /**< State change callback (optional) */
    void       *cb_user_data;       /**< User data for callbacks */
} voice_pipeline_config_t;

/**
 * Initialize the voice pipeline.
 */
esp_err_t voice_pipeline_init(const voice_pipeline_config_t *config);

/**
 * Trigger a voice interaction.
 * Records audio, transcribes, sends to agent, speaks response.
 * This function is blocking - call from a dedicated task.
 * @param out_text  Optional buffer for transcribed user text (can be NULL)
 * @param text_len  Size of out_text buffer
 */
esp_err_t voice_pipeline_run(char *out_text, size_t text_len);

/**
 * Get current pipeline state.
 */
voice_state_t voice_pipeline_get_state(void);
