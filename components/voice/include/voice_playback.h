#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Play PCM audio through the speaker.
 * Blocks until all samples are written.
 * Handles speaker init/deinit internally.
 * @param data        Signed 16-bit PCM samples
 * @param count       Number of samples
 * @param sample_rate Sample rate in Hz
 */
esp_err_t voice_playback_play(const int16_t *data, size_t count, int sample_rate);

/**
 * Play a short beep tone through the speaker.
 * Useful for recording start/stop feedback.
 * @param freq_hz   Tone frequency (e.g. 800)
 * @param duration_ms Tone duration in ms
 */
esp_err_t voice_playback_beep(int freq_hz, int duration_ms);
