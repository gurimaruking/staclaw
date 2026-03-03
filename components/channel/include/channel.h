#pragma once

/**
 * Channel IDs for message routing.
 */
typedef enum {
    CHANNEL_NONE = 0,
    CHANNEL_TELEGRAM,
    CHANNEL_TOUCH,
    CHANNEL_VOICE,
} channel_id_t;

/**
 * Incoming message from any channel.
 */
typedef struct {
    channel_id_t channel;
    char *text;              // Heap-allocated, receiver must free
    char *sender_id;         // Telegram chat ID, etc. (heap-allocated)
    void *channel_data;      // Channel-specific opaque data
} channel_message_t;
