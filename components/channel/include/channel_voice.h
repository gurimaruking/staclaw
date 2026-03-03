#pragma once

#include "esp_err.h"

/**
 * Initialize the voice channel.
 * Sets up the voice pipeline and registers with the channel router.
 * Requires OpenAI API key for Whisper STT and TTS.
 *
 * @param openai_api_key  OpenAI API key
 * @param tts_voice       TTS voice name (e.g. "alloy"), NULL for default
 * @param vad_threshold   VAD energy threshold (0 = auto-calibrate)
 */
esp_err_t channel_voice_init(const char *openai_api_key,
                              const char *tts_voice,
                              int vad_threshold);

/**
 * Trigger a voice interaction.
 * Call this when the user presses the "mic" button.
 * Sends a message to the voice task queue.
 * Non-blocking (actual processing happens in background task).
 */
esp_err_t channel_voice_trigger(void);
