#include "channel_router.h"
#include "net_wifi.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ch_router";

static agent_t *s_agent = NULL;
static channel_reply_cb_t s_reply_cbs[4] = {0};  // Indexed by channel_id_t

esp_err_t channel_router_init(agent_t *agent)
{
    s_agent = agent;
    memset(s_reply_cbs, 0, sizeof(s_reply_cbs));
    ESP_LOGI(TAG, "Channel router initialized");
    return ESP_OK;
}

void channel_router_register_reply(channel_id_t channel, channel_reply_cb_t cb)
{
    if (channel < 4) {
        s_reply_cbs[channel] = cb;
    }
}

esp_err_t channel_router_handle(const channel_message_t *msg)
{
    if (!s_agent || !msg || !msg->text) {
        return ESP_ERR_INVALID_ARG;
    }

    // Wait for WiFi before making API calls
    if (!net_wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, waiting...");
        esp_err_t wifi_err = net_wifi_wait_connected(pdMS_TO_TICKS(10000));
        if (wifi_err != ESP_OK) {
            ESP_LOGE(TAG, "WiFi not available");
            if (s_reply_cbs[msg->channel]) {
                s_reply_cbs[msg->channel](msg->channel, msg->sender_id,
                                           "WiFi not connected. Please wait.", msg->channel_data);
            }
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Message from channel %d: %.40s%s",
             msg->channel, msg->text,
             strlen(msg->text) > 40 ? "..." : "");

    // Process through agent
    char *response = NULL;
    esp_err_t err = agent_process(s_agent, msg->text, &response, NULL, NULL);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Agent processing failed: %s", esp_err_to_name(err));
        // Send error response
        const char *error_msg = "Sorry, I encountered an error processing your message.";
        if (s_reply_cbs[msg->channel]) {
            s_reply_cbs[msg->channel](msg->channel, msg->sender_id,
                                       error_msg, msg->channel_data);
        }
        return err;
    }

    // Send response back through the originating channel
    if (response && s_reply_cbs[msg->channel]) {
        s_reply_cbs[msg->channel](msg->channel, msg->sender_id,
                                   response, msg->channel_data);
    }

    free(response);
    return ESP_OK;
}
