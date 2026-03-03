#include "voice_playback.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "voice_play";

/* Write in chunks to avoid blocking too long */
#define PLAYBACK_CHUNK  1024

esp_err_t voice_playback_play(const int16_t *data, size_t count, int sample_rate)
{
    if (!data || count == 0) return ESP_ERR_INVALID_ARG;

    esp_err_t err = bsp_audio_speaker_start(sample_rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker start failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t offset = 0;
    while (offset < count) {
        size_t chunk = count - offset;
        if (chunk > PLAYBACK_CHUNK) chunk = PLAYBACK_CHUNK;

        err = bsp_audio_speaker_write(data + offset, chunk, 1000);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Write error at offset %u: %s", (unsigned)offset, esp_err_to_name(err));
            break;
        }
        offset += chunk;
    }

    bsp_audio_speaker_stop();

    ESP_LOGD(TAG, "Played %u samples at %d Hz", (unsigned)count, sample_rate);
    return ESP_OK;
}

esp_err_t voice_playback_beep(int freq_hz, int duration_ms)
{
    const int sample_rate = 16000;
    int num_samples = sample_rate * duration_ms / 1000;
    if (num_samples > 16000) num_samples = 16000; /* Max 1 second */

    int16_t buf[256];

    esp_err_t err = bsp_audio_speaker_start(sample_rate);
    if (err != ESP_OK) return err;

    int written = 0;
    while (written < num_samples) {
        int chunk = num_samples - written;
        if (chunk > 256) chunk = 256;

        for (int i = 0; i < chunk; i++) {
            /* Simple sine wave approximation using integer math */
            int t = written + i;
            /* Phase: 0 to period in samples */
            int period = sample_rate / freq_hz;
            int phase = t % period;
            /* Triangle wave as sine approximation, amplitude ~8000 */
            int half = period / 2;
            int val;
            if (phase < half) {
                val = (phase * 16000 / half) - 8000;
            } else {
                val = 8000 - ((phase - half) * 16000 / half);
            }
            /* Apply fade-in/out envelope */
            int fade_samples = sample_rate / 50; /* 20ms fade */
            if (written + i < fade_samples) {
                val = val * (written + i) / fade_samples;
            } else if (written + i > num_samples - fade_samples) {
                val = val * (num_samples - written - i) / fade_samples;
            }
            buf[i] = (int16_t)val;
        }

        bsp_audio_speaker_write(buf, chunk, 500);
        written += chunk;
    }

    bsp_audio_speaker_stop();
    return ESP_OK;
}
