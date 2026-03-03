#pragma once

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(STACLAW_EVENT);

typedef enum {
    STACLAW_EVENT_USER_MESSAGE,      // New message from any channel
    STACLAW_EVENT_AGENT_RESPONSE,    // Agent produced a response
    STACLAW_EVENT_TOOL_CALL,         // Agent wants to call a tool
    STACLAW_EVENT_VOICE_START,       // Voice input started
    STACLAW_EVENT_VOICE_END,         // Voice input ended
    STACLAW_EVENT_CRON_FIRE,         // Cron job triggered
    STACLAW_EVENT_WIFI_CONNECTED,    // WiFi connected
    STACLAW_EVENT_WIFI_DISCONNECTED, // WiFi disconnected
} staclaw_event_id_t;

// Event data for STACLAW_EVENT_USER_MESSAGE
typedef struct {
    int channel_id;      // 0=touch, 1=telegram, 2=voice
    char *text;          // Message text (caller must free)
    char *metadata;      // Channel-specific metadata (can be NULL)
} staclaw_msg_event_t;
