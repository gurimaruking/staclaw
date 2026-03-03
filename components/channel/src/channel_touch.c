#include "channel_touch.h"
#include "channel.h"
#include "channel_router.h"
#include "ui_chat.h"
#include "ui_status.h"
#include "ui_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ch_touch";

#define TOUCH_MSG_MAX_LEN  256
#define TOUCH_QUEUE_SIZE   4
#define TOUCH_TASK_STACK   (8 * 1024)
#define TOUCH_TASK_PRIO    4

static QueueHandle_t s_msg_queue = NULL;
static TaskHandle_t s_task = NULL;

/* Message struct for the queue */
typedef struct {
    char text[TOUCH_MSG_MAX_LEN];
} touch_msg_t;

/**
 * Reply callback: display assistant response on the chat screen.
 */
static void touch_reply_cb(channel_id_t channel, const char *sender_id,
                            const char *text, void *channel_data)
{
    (void)channel;
    (void)sender_id;
    (void)channel_data;

    if (text) {
        ui_chat_add_message(UI_CHAT_ROLE_ASSISTANT, text);
        ui_status_set_busy(false);
        ui_manager_request_redraw();
    }
}

/**
 * Background task: processes queued messages through the agent.
 */
static void touch_task(void *arg)
{
    touch_msg_t msg;

    ESP_LOGI(TAG, "Touch channel task started");

    while (1) {
        if (xQueueReceive(s_msg_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processing: %.40s%s",
                     msg.text, strlen(msg.text) > 40 ? "..." : "");

            /* Show user message on chat */
            ui_chat_add_message(UI_CHAT_ROLE_USER, msg.text);
            ui_status_set_busy(true);
            ui_manager_request_redraw();

            /* Route through agent */
            channel_message_t cm = {
                .channel = CHANNEL_TOUCH,
                .text = msg.text,
                .sender_id = "touch",
                .channel_data = NULL,
            };

            esp_err_t err = channel_router_handle(&cm);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Agent failed: %s", esp_err_to_name(err));
                ui_status_set_busy(false);
                ui_manager_request_redraw();
            }
        }
    }
}

esp_err_t channel_touch_init(void)
{
    /* Create message queue */
    s_msg_queue = xQueueCreate(TOUCH_QUEUE_SIZE, sizeof(touch_msg_t));
    if (!s_msg_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return ESP_ERR_NO_MEM;
    }

    /* Register reply callback with channel router */
    channel_router_register_reply(CHANNEL_TOUCH, touch_reply_cb);

    /* Start background processing task on Core 1 (same as UI) */
    BaseType_t ret = xTaskCreatePinnedToCore(
        touch_task, "ch_touch", TOUCH_TASK_STACK,
        NULL, TOUCH_TASK_PRIO, &s_task, 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Touch channel initialized");
    return ESP_OK;
}

void channel_touch_send(const char *text)
{
    if (!text || !s_msg_queue) return;

    touch_msg_t msg;
    strncpy(msg.text, text, TOUCH_MSG_MAX_LEN - 1);
    msg.text[TOUCH_MSG_MAX_LEN - 1] = '\0';

    if (xQueueSend(s_msg_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Message queue full, dropping");
    }
}
