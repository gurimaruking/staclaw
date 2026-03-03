#include "channel_telegram.h"
#include "channel.h"
#include "channel_router.h"
#include "net_http.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ch_telegram";

#define TELEGRAM_API_BASE "https://api.telegram.org/bot"
#define POLL_TIMEOUT_SEC  30
#define MAX_URL_LEN       256
#define MAX_BODY_LEN      4096

static char s_bot_token[64];
static TaskHandle_t s_poll_task = NULL;
static bool s_running = false;
static int s_last_update_id = 0;

// Build Telegram API URL
static void build_url(char *buf, size_t buf_size, const char *method)
{
    snprintf(buf, buf_size, "%s%s/%s", TELEGRAM_API_BASE, s_bot_token, method);
}

// Reply callback for channel router
static void telegram_reply_cb(channel_id_t channel, const char *sender_id,
                                const char *text, void *channel_data)
{
    if (channel != CHANNEL_TELEGRAM || !sender_id || !text) return;
    channel_telegram_send(sender_id, text);
}

esp_err_t channel_telegram_send(const char *chat_id, const char *text)
{
    char url[MAX_URL_LEN];
    build_url(url, sizeof(url), "sendMessage");

    // Build JSON body
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);
    cJSON_AddStringToObject(body, "parse_mode", "Markdown");

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return ESP_ERR_NO_MEM;

    http_request_t req = {
        .url = url,
        .method = "POST",
        .body = body_str,
        .body_len = strlen(body_str),
        .content_type = "application/json",
        .timeout_ms = 10000,
    };

    http_response_t resp = {0};
    esp_err_t err = net_http_request(&req, &resp);

    if (err != ESP_OK || resp.status_code != 200) {
        ESP_LOGE(TAG, "sendMessage failed: err=%s, status=%d",
                 esp_err_to_name(err), resp.status_code);
    }

    free(body_str);
    free(resp.body);
    return err;
}

static void process_updates(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (!ok || !cJSON_IsTrue(ok)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *update = NULL;
    cJSON_ArrayForEach(update, result) {
        // Track update_id for offset
        cJSON *uid = cJSON_GetObjectItem(update, "update_id");
        if (uid) {
            int id = uid->valueint;
            if (id >= s_last_update_id) {
                s_last_update_id = id + 1;
            }
        }

        // Extract message
        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!message) continue;

        cJSON *text = cJSON_GetObjectItem(message, "text");
        if (!text || !cJSON_IsString(text)) continue;

        // Extract chat_id
        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        if (!chat) continue;
        cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
        if (!chat_id) continue;

        char chat_id_str[24];
        snprintf(chat_id_str, sizeof(chat_id_str), "%lld",
                 (long long)chat_id->valuedouble);

        ESP_LOGI(TAG, "Message from %s: %s", chat_id_str, text->valuestring);

        // Route through channel router
        channel_message_t msg = {
            .channel = CHANNEL_TELEGRAM,
            .text = strdup(text->valuestring),
            .sender_id = strdup(chat_id_str),
            .channel_data = NULL,
        };

        channel_router_handle(&msg);

        free(msg.text);
        free(msg.sender_id);
    }

    cJSON_Delete(root);
}

static void telegram_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Telegram poll task started");

    while (s_running) {
        char url[MAX_URL_LEN];
        build_url(url, sizeof(url), "getUpdates");

        // Build request with long polling
        cJSON *body = cJSON_CreateObject();
        cJSON_AddNumberToObject(body, "offset", s_last_update_id);
        cJSON_AddNumberToObject(body, "timeout", POLL_TIMEOUT_SEC);
        cJSON_AddNumberToObject(body, "limit", 5);

        // Only get text messages
        cJSON *allowed = cJSON_AddArrayToObject(body, "allowed_updates");
        cJSON_AddItemToArray(allowed, cJSON_CreateString("message"));

        char *body_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);

        if (!body_str) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        http_request_t req = {
            .url = url,
            .method = "POST",
            .body = body_str,
            .body_len = strlen(body_str),
            .content_type = "application/json",
            .timeout_ms = (POLL_TIMEOUT_SEC + 10) * 1000,  // Extra buffer
        };

        http_response_t resp = {0};
        esp_err_t err = net_http_request(&req, &resp);

        free(body_str);

        if (err == ESP_OK && resp.status_code == 200 && resp.body) {
            process_updates(resp.body);
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "Poll failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(5000));  // Back off on error
        }

        free(resp.body);
    }

    ESP_LOGI(TAG, "Telegram poll task stopped");
    s_poll_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t channel_telegram_init(const char *bot_token)
{
    if (!bot_token || !bot_token[0]) {
        ESP_LOGE(TAG, "Bot token is empty");
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_bot_token, bot_token, sizeof(s_bot_token) - 1);
    s_running = true;

    // Register reply callback
    channel_router_register_reply(CHANNEL_TELEGRAM, telegram_reply_cb);

    // Start polling task on core 0
    BaseType_t ret = xTaskCreatePinnedToCore(
        telegram_poll_task,
        "telegram",
        8192,
        NULL,
        3,           // Priority
        &s_poll_task,
        0            // Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Telegram channel initialized");
    return ESP_OK;
}

void channel_telegram_stop(void)
{
    s_running = false;
    // Task will exit on next poll timeout
    ESP_LOGI(TAG, "Telegram stop requested");
}
