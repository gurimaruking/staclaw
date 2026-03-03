#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * TTS audio result buffer.
 * Audio is raw PCM 16-bit mono at 24kHz (OpenAI TTS "pcm" format).
 */
typedef struct {
    int16_t *data;          /**< PCM samples (PSRAM allocated) */
    size_t   count;          /**< Number of samples */
    int      sample_rate;   /**< Always 24000 for OpenAI TTS pcm */
} voice_tts_audio_t;

/**
 * Synthesize speech using OpenAI TTS API.
 * @param text       Text to speak
 * @param api_key    OpenAI API key
 * @param voice      Voice name (e.g. "alloy", "echo", "nova")
 * @param out_audio  Output audio buffer (caller must call voice_tts_free)
 * @return ESP_OK on success
 */
esp_err_t voice_tts_synthesize(const char *text, const char *api_key,
                                const char *voice, voice_tts_audio_t *out_audio);

/**
 * Free TTS audio buffer.
 */
void voice_tts_free(voice_tts_audio_t *audio);
