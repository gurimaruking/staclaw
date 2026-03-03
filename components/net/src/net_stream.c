#include "net_stream.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "net_stream";

esp_err_t net_stream_sse(const http_request_t *req,
                         sse_event_cb_t callback, void *user_data,
                         int timeout_ms)
{
    esp_http_client_config_t config = {
        .url = req->url,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .is_async = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_method(client, HTTP_METHOD_POST);

    if (req->content_type) {
        esp_http_client_set_header(client, "Content-Type", req->content_type);
    }

    for (int i = 0; i < req->header_count && i < 8; i++) {
        if (req->extra_headers[i][0] && req->extra_headers[i][1]) {
            esp_http_client_set_header(client, req->extra_headers[i][0], req->extra_headers[i][1]);
        }
    }

    if (req->body && req->body_len > 0) {
        esp_http_client_set_post_field(client, req->body, req->body_len);
    }

    esp_err_t err = esp_http_client_open(client, req->body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (req->body && req->body_len > 0) {
        int wlen = esp_http_client_write(client, req->body, req->body_len);
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGD(TAG, "SSE stream opened, status=%d, content_length=%d", status, content_length);

    if (status != 200) {
        // Read error body
        char err_buf[512] = {0};
        esp_http_client_read(client, err_buf, sizeof(err_buf) - 1);
        ESP_LOGE(TAG, "SSE error %d: %.500s", status, err_buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Parse SSE stream
    char line_buf[4096];
    int line_pos = 0;
    char current_event[64] = {0};
    char read_buf[1024];
    bool aborted = false;

    while (!aborted) {
        int rlen = esp_http_client_read(client, read_buf, sizeof(read_buf) - 1);
        if (rlen <= 0) break; // EOF or error

        for (int i = 0; i < rlen && !aborted; i++) {
            char c = read_buf[i];
            if (c == '\n') {
                line_buf[line_pos] = '\0';

                if (line_pos == 0) {
                    // Empty line = end of event (reset)
                    current_event[0] = '\0';
                } else if (strncmp(line_buf, "event:", 6) == 0) {
                    // Event type
                    const char *val = line_buf + 6;
                    while (*val == ' ') val++;
                    strncpy(current_event, val, sizeof(current_event) - 1);
                } else if (strncmp(line_buf, "data:", 5) == 0) {
                    // Data line
                    const char *val = line_buf + 5;
                    while (*val == ' ') val++;

                    if (strcmp(val, "[DONE]") == 0) {
                        aborted = true;
                    } else {
                        bool cont = callback(
                            current_event[0] ? current_event : NULL,
                            val, user_data);
                        if (!cont) aborted = true;
                    }
                }
                line_pos = 0;
            } else if (c != '\r') {
                if (line_pos < (int)sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}
