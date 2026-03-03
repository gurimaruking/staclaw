#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Initialize audio subsystem.
 * Must be called before speaker or mic functions.
 * Does NOT start I2S - call speaker_start or mic_start.
 */
esp_err_t bsp_audio_init(void);

/* ---- Speaker (NS4168 via I2S standard mode) ---- */

/**
 * Start speaker I2S output at given sample rate.
 * Enables the NS4168 amplifier via AXP192.
 * Cannot be used simultaneously with mic.
 */
esp_err_t bsp_audio_speaker_start(int sample_rate);

/**
 * Write PCM samples to speaker.
 * @param data   Signed 16-bit PCM samples
 * @param samples Number of samples to write
 * @param timeout_ms Timeout for blocking write
 */
esp_err_t bsp_audio_speaker_write(const int16_t *data, size_t samples, int timeout_ms);

/**
 * Stop speaker and release I2S channel.
 * Disables the NS4168 amplifier.
 */
esp_err_t bsp_audio_speaker_stop(void);

/* ---- Microphone (SPM1423 via I2S PDM mode) ---- */

/**
 * Start microphone I2S PDM input at given sample rate.
 * Cannot be used simultaneously with speaker.
 */
esp_err_t bsp_audio_mic_start(int sample_rate);

/**
 * Read PCM samples from microphone.
 * @param data       Buffer for signed 16-bit PCM samples
 * @param samples    Max samples to read
 * @param read_count Actual samples read (output)
 * @param timeout_ms Timeout for blocking read
 */
esp_err_t bsp_audio_mic_read(int16_t *data, size_t samples, size_t *read_count, int timeout_ms);

/**
 * Stop microphone and release I2S channel.
 */
esp_err_t bsp_audio_mic_stop(void);
