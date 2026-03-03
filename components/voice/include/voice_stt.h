#pragma once

#include "esp_err.h"
#include "voice_capture.h"

/**
 * Maximum length of transcribed text.
 */
#define VOICE_STT_MAX_TEXT  1024

/**
 * Send audio to OpenAI Whisper API for speech-to-text.
 * @param rec       Recording with PCM audio data
 * @param api_key   OpenAI API key
 * @param out_text  Buffer for transcribed text (min VOICE_STT_MAX_TEXT bytes)
 * @return ESP_OK on success
 */
esp_err_t voice_stt_transcribe(const voice_recording_t *rec, const char *api_key, char *out_text);
