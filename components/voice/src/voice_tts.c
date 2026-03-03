#include "voice_tts.h"
#include "net_http.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "voice_tts";

esp_err_t voice_tts_synthesize(const char *text, const char *api_key,
                                const char *voice, voice_tts_audio_t *out_audio)
{
    if (!text || !api_key || !out_audio) return ESP_ERR_INVALID_ARG;

    memset(out_audio, 0, sizeof(*out_audio));
    out_audio->sample_rate = 24000;

    if (!voice || !voice[0]) voice = "alloy";

    ESP_LOGI(TAG, "TTS: %.60s%s (voice=%s)", text, strlen(text) > 60 ? "..." : "", voice);

    /* Build JSON body */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "tts-1");
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", voice);
    cJSON_AddStringToObject(root, "response_format", "pcm");

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_body) return ESP_ERR_NO_MEM;

    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    http_request_t req = {
        .url = "https://api.openai.com/v1/audio/speech",
        .method = "POST",
        .body = json_body,
        .body_len = (int)strlen(json_body),
        .content_type = "application/json",
        .header_count = 1,
        .timeout_ms = 30000,
    };
    req.extra_headers[0][0] = "Authorization";
    req.extra_headers[0][1] = auth_header;

    http_response_t resp = {0};
    esp_err_t err = net_http_request(&req, &resp);
    free(json_body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (resp.status_code != 200) {
        ESP_LOGE(TAG, "TTS API error %d: %.200s", resp.status_code, resp.body ? resp.body : "");
        net_http_response_free(&resp);
        return ESP_FAIL;
    }

    /* Response is raw PCM: 24kHz 16-bit mono little-endian */
    if (resp.body_len < 2) {
        ESP_LOGE(TAG, "TTS response too small: %d bytes", resp.body_len);
        net_http_response_free(&resp);
        return ESP_FAIL;
    }

    size_t num_samples = resp.body_len / sizeof(int16_t);

    /* Copy to PSRAM */
    out_audio->data = heap_caps_malloc(num_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!out_audio->data) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %u samples", (unsigned)num_samples);
        net_http_response_free(&resp);
        return ESP_ERR_NO_MEM;
    }

    memcpy(out_audio->data, resp.body, num_samples * sizeof(int16_t));
    out_audio->count = num_samples;

    net_http_response_free(&resp);

    ESP_LOGI(TAG, "TTS audio: %u samples (%u ms at %d Hz)",
             (unsigned)num_samples,
             (unsigned)(num_samples * 1000 / out_audio->sample_rate),
             out_audio->sample_rate);
    return ESP_OK;
}

void voice_tts_free(voice_tts_audio_t *audio)
{
    if (audio && audio->data) {
        heap_caps_free(audio->data);
        audio->data = NULL;
    }
    if (audio) {
        audio->count = 0;
    }
}
