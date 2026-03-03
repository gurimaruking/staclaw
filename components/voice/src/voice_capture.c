#include "voice_capture.h"
#include "voice_vad.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "voice_cap";

/* Read chunk size in samples (20ms at 16kHz = 320 samples) */
#define CHUNK_SAMPLES  320

esp_err_t voice_capture_alloc(voice_recording_t *rec, int max_ms, int sample_rate)
{
    if (!rec) return ESP_ERR_INVALID_ARG;

    memset(rec, 0, sizeof(*rec));
    rec->sample_rate = sample_rate;
    rec->capacity = (size_t)sample_rate * max_ms / 1000;

    /* Allocate in PSRAM for large buffers */
    rec->data = heap_caps_malloc(rec->capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!rec->data) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes in PSRAM",
                 (unsigned)(rec->capacity * sizeof(int16_t)));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Recording buffer: %u samples (%d ms) in PSRAM",
             (unsigned)rec->capacity, max_ms);
    return ESP_OK;
}

void voice_capture_free(voice_recording_t *rec)
{
    if (rec && rec->data) {
        heap_caps_free(rec->data);
        rec->data = NULL;
    }
    if (rec) {
        rec->count = 0;
        rec->capacity = 0;
    }
}

esp_err_t voice_capture_record(voice_recording_t *rec, int silence_ms, int vad_threshold)
{
    if (!rec || !rec->data) return ESP_ERR_INVALID_ARG;

    rec->count = 0;

    /* Start microphone (retry once if I2S port not yet released) */
    esp_err_t err = bsp_audio_mic_start(rec->sample_rate);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Mic start failed (%s), retrying after delay...",
                 esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(300));
        err = bsp_audio_mic_start(rec->sample_rate);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Mic start failed on retry: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* Auto-calibrate if threshold not set */
    if (vad_threshold <= 0) {
        int16_t cal_buf[CHUNK_SAMPLES];
        size_t read_count = 0;
        bool cal_ok = false;

        /* Read 5 frames of ambient noise, use last successful one */
        for (int i = 0; i < 5; i++) {
            esp_err_t cal_err = bsp_audio_mic_read(cal_buf, CHUNK_SAMPLES, &read_count, 500);
            if (cal_err == ESP_OK && read_count > 0) {
                cal_ok = true;
            }
        }

        if (cal_ok) {
            vad_threshold = voice_vad_calibrate(cal_buf, CHUNK_SAMPLES);
            ESP_LOGI(TAG, "VAD auto-calibrated threshold: %d", vad_threshold);
        } else {
            vad_threshold = 200;
            ESP_LOGW(TAG, "Mic calibration failed, using default threshold: %d", vad_threshold);
        }
    }

    int silence_chunks = (silence_ms * rec->sample_rate) / (CHUNK_SAMPLES * 1000);
    if (silence_chunks < 3) silence_chunks = 3;

    /* Timeout: max wait for initial speech = 8 seconds */
    int wait_timeout_chunks = (8000 * rec->sample_rate) / (CHUNK_SAMPLES * 1000);

    int silent_count = 0;
    int total_chunks = 0;
    bool speech_started = false;
    int16_t chunk_buf[CHUNK_SAMPLES];

    ESP_LOGI(TAG, "Recording started (threshold=%d, silence_chunks=%d, wait_timeout=%d chunks)",
             vad_threshold, silence_chunks, wait_timeout_chunks);

    while (rec->count + CHUNK_SAMPLES <= rec->capacity) {
        size_t read_count = 0;
        err = bsp_audio_mic_read(chunk_buf, CHUNK_SAMPLES, &read_count, 500);
        if (err != ESP_OK || read_count == 0) {
            total_chunks++;
            if (!speech_started && total_chunks >= wait_timeout_chunks) {
                ESP_LOGW(TAG, "Mic read errors, giving up after %d chunks", total_chunks);
                break;
            }
            continue;
        }

        total_chunks++;

        bool is_speech = voice_vad_is_speech(chunk_buf, read_count, vad_threshold);

        if (is_speech) {
            speech_started = true;
            silent_count = 0;
        } else if (speech_started) {
            silent_count++;
        }

        /* Always store samples once speech has started */
        if (speech_started) {
            size_t to_copy = read_count;
            if (rec->count + to_copy > rec->capacity) {
                to_copy = rec->capacity - rec->count;
            }
            memcpy(rec->data + rec->count, chunk_buf, to_copy * sizeof(int16_t));
            rec->count += to_copy;
        }

        /* Stop if enough silence after speech */
        if (speech_started && silent_count >= silence_chunks) {
            ESP_LOGI(TAG, "Silence detected, stopping");
            break;
        }

        /* Timeout waiting for speech to start */
        if (!speech_started && total_chunks >= wait_timeout_chunks) {
            ESP_LOGW(TAG, "No speech detected within %d seconds", 8);
            break;
        }
    }

    bsp_audio_mic_stop();

    ESP_LOGI(TAG, "Recorded %u samples (%u ms)",
             (unsigned)rec->count,
             (unsigned)(rec->count * 1000 / rec->sample_rate));
    return ESP_OK;
}
