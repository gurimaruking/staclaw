#include "voice_stt.h"
#include "net_http.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "voice_stt";

/* WAV header for 16-bit mono PCM */
typedef struct __attribute__((packed)) {
    char     riff_tag[4];     /* "RIFF" */
    uint32_t riff_size;
    char     wave_tag[4];     /* "WAVE" */
    char     fmt_tag[4];      /* "fmt " */
    uint32_t fmt_size;        /* 16 */
    uint16_t audio_format;    /* 1 = PCM */
    uint16_t num_channels;    /* 1 */
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample; /* 16 */
    char     data_tag[4];     /* "data" */
    uint32_t data_size;
} wav_header_t;

static void build_wav_header(wav_header_t *hdr, int sample_rate, size_t num_samples)
{
    uint32_t data_size = num_samples * 2; /* 16-bit samples */

    memcpy(hdr->riff_tag, "RIFF", 4);
    hdr->riff_size = 36 + data_size;
    memcpy(hdr->wave_tag, "WAVE", 4);
    memcpy(hdr->fmt_tag,  "fmt ", 4);
    hdr->fmt_size = 16;
    hdr->audio_format = 1;
    hdr->num_channels = 1;
    hdr->sample_rate = sample_rate;
    hdr->byte_rate = sample_rate * 2;
    hdr->block_align = 2;
    hdr->bits_per_sample = 16;
    memcpy(hdr->data_tag, "data", 4);
    hdr->data_size = data_size;
}

/*
 * Build a multipart/form-data body for the Whisper API.
 * Returns heap-allocated body and sets body_len.
 * Caller must free the returned pointer.
 */
static char *build_multipart_body(const voice_recording_t *rec,
                                   const char *boundary,
                                   int *body_len)
{
    wav_header_t wav_hdr;
    build_wav_header(&wav_hdr, rec->sample_rate, rec->count);

    size_t wav_size = sizeof(wav_header_t) + rec->count * sizeof(int16_t);

    /* Pre-calculate parts */
    const char *model = "whisper-1";
    const char *lang = "en";

    /* Estimate total body size */
    size_t est_size = wav_size + 1024; /* wav + overhead for multipart parts */
    char *body = malloc(est_size);
    if (!body) return NULL;

    int off = 0;

    /* Part 1: file */
    off += snprintf(body + off, est_size - off,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        boundary);

    /* WAV header */
    memcpy(body + off, &wav_hdr, sizeof(wav_header_t));
    off += sizeof(wav_header_t);

    /* PCM data */
    memcpy(body + off, rec->data, rec->count * sizeof(int16_t));
    off += rec->count * sizeof(int16_t);

    /* Part 2: model */
    off += snprintf(body + off, est_size - off,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
        "%s",
        boundary, model);

    /* Part 3: language */
    off += snprintf(body + off, est_size - off,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
        "%s",
        boundary, lang);

    /* Part 4: response_format */
    off += snprintf(body + off, est_size - off,
        "\r\n--%s\r\n"
        "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
        "json",
        boundary);

    /* Closing boundary */
    off += snprintf(body + off, est_size - off,
        "\r\n--%s--\r\n", boundary);

    *body_len = off;
    return body;
}

esp_err_t voice_stt_transcribe(const voice_recording_t *rec, const char *api_key, char *out_text)
{
    if (!rec || !rec->data || rec->count == 0 || !api_key || !out_text) {
        return ESP_ERR_INVALID_ARG;
    }

    out_text[0] = '\0';

    ESP_LOGI(TAG, "Transcribing %u samples (%u ms)...",
             (unsigned)rec->count,
             (unsigned)(rec->count * 1000 / rec->sample_rate));

    const char *boundary = "----StaclawBoundary9876";
    char content_type[80];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);

    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    ESP_LOGI(TAG, "API key prefix: %.8s... (len=%d)", api_key, (int)strlen(api_key));

    int body_len = 0;
    char *body = build_multipart_body(rec, boundary, &body_len);
    if (!body) {
        ESP_LOGE(TAG, "Failed to build multipart body");
        return ESP_ERR_NO_MEM;
    }

    http_request_t req = {
        .url = "https://api.openai.com/v1/audio/transcriptions",
        .method = "POST",
        .body = body,
        .body_len = body_len,
        .content_type = content_type,
        .header_count = 1,
        .timeout_ms = 30000,
    };
    req.extra_headers[0][0] = "Authorization";
    req.extra_headers[0][1] = auth_header;

    http_response_t resp = {0};
    esp_err_t err = net_http_request(&req, &resp);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (resp.status_code != 200) {
        ESP_LOGE(TAG, "Whisper API error %d: %.200s", resp.status_code, resp.body ? resp.body : "");
        net_http_response_free(&resp);
        return ESP_FAIL;
    }

    /* Parse JSON response: {"text": "transcribed text"} */
    cJSON *root = cJSON_Parse(resp.body);
    net_http_response_free(&resp);

    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *text_obj = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text_obj) && text_obj->valuestring) {
        strncpy(out_text, text_obj->valuestring, VOICE_STT_MAX_TEXT - 1);
        out_text[VOICE_STT_MAX_TEXT - 1] = '\0';
        ESP_LOGI(TAG, "Transcription: %.80s%s", out_text,
                 strlen(out_text) > 80 ? "..." : "");
    }

    cJSON_Delete(root);
    return ESP_OK;
}
