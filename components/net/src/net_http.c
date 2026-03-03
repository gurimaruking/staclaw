#include "net_http.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "net_http";

// Dynamic buffer for collecting response body
typedef struct {
    char  *data;
    int    len;
    int    cap;
} resp_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *buf = (resp_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (buf) {
            int new_len = buf->len + evt->data_len;
            if (new_len >= buf->cap) {
                int new_cap = (new_len + 1024) * 2;
                char *tmp = realloc(buf->data, new_cap);
                if (!tmp) {
                    ESP_LOGE(TAG, "OOM expanding response buffer");
                    return ESP_FAIL;
                }
                buf->data = tmp;
                buf->cap = new_cap;
            }
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len = new_len;
            buf->data[buf->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t net_http_request(const http_request_t *req, http_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));

    resp_buf_t buf = {
        .data = malloc(4096),
        .len = 0,
        .cap = 4096,
    };
    if (!buf.data) return ESP_ERR_NO_MEM;
    buf.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = req->url,
        .timeout_ms = req->timeout_ms > 0 ? req->timeout_ms : 30000,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf.data);
        return ESP_FAIL;
    }

    // Set method
    if (req->method && strcmp(req->method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    }

    // Set content type
    if (req->content_type) {
        esp_http_client_set_header(client, "Content-Type", req->content_type);
    }

    // Set extra headers
    for (int i = 0; i < req->header_count && i < 8; i++) {
        if (req->extra_headers[i][0] && req->extra_headers[i][1]) {
            esp_http_client_set_header(client, req->extra_headers[i][0], req->extra_headers[i][1]);
        }
    }

    // Set body
    if (req->body && req->body_len > 0) {
        esp_http_client_set_post_field(client, req->body, req->body_len);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        resp->status_code = esp_http_client_get_status_code(client);
        resp->body = buf.data;
        resp->body_len = buf.len;
        ESP_LOGD(TAG, "HTTP %s %s -> %d (%d bytes)",
                 req->method ? req->method : "GET", req->url,
                 resp->status_code, resp->body_len);
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(buf.data);
    }

    esp_http_client_cleanup(client);
    return err;
}

void net_http_response_free(http_response_t *resp)
{
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
    resp->body_len = 0;
}
