#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Audio recording buffer.
 * Allocated in PSRAM for large recordings.
 */
typedef struct {
    int16_t *data;          /**< PCM sample buffer (PSRAM) */
    size_t   capacity;      /**< Max samples the buffer can hold */
    size_t   count;          /**< Actual samples recorded */
    int      sample_rate;   /**< Sample rate in Hz */
} voice_recording_t;

/**
 * Allocate a recording buffer in PSRAM.
 * @param rec         Recording struct to initialize
 * @param max_ms      Maximum recording duration in milliseconds
 * @param sample_rate Sample rate (e.g. 16000)
 */
esp_err_t voice_capture_alloc(voice_recording_t *rec, int max_ms, int sample_rate);

/**
 * Free a recording buffer.
 */
void voice_capture_free(voice_recording_t *rec);

/**
 * Record audio from microphone with VAD-based auto-stop.
 * Blocks until silence is detected or max duration is reached.
 * @param rec             Pre-allocated recording buffer
 * @param silence_ms      Milliseconds of silence to trigger stop
 * @param vad_threshold   Energy threshold for voice activity (0 = auto)
 */
esp_err_t voice_capture_record(voice_recording_t *rec, int silence_ms, int vad_threshold);
