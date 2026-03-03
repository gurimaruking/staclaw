#pragma once

#include "channel.h"
#include "agent.h"
#include "esp_err.h"

/**
 * Response callback: called by router to send response back to originating channel.
 */
typedef void (*channel_reply_cb_t)(channel_id_t channel, const char *sender_id,
                                    const char *text, void *channel_data);

/**
 * Initialize the channel router with agent and reply callbacks.
 */
esp_err_t channel_router_init(agent_t *agent);

/**
 * Register a reply callback for a specific channel.
 */
void channel_router_register_reply(channel_id_t channel, channel_reply_cb_t cb);

/**
 * Route an incoming message through the agent and send response back.
 * This function runs the agent synchronously, so call from an appropriate task.
 */
esp_err_t channel_router_handle(const channel_message_t *msg);
