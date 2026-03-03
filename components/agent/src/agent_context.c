#include "agent_context.h"
#include "memory_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "agent_ctx";

// Default system prompt fallback
static const char *DEFAULT_SYSTEM =
    "You are staclaw, an AI assistant running on an M5Stack Core2 (ESP32). "
    "You are helpful, concise, and aware of your hardware constraints. "
    "Keep responses brief as they are displayed on a small screen or sent via Telegram.";

// Build a context section with runtime info
static int build_context_section(char *buf, int buf_size)
{
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "# CONTEXT\n");

    // Current time (if SNTP synced, otherwise show uptime)
    time_t now;
    time(&now);
    if (now > 1700000000) {  // After ~2023, time is likely SNTP-synced
        struct tm tm;
        localtime_r(&now, &tm);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &tm);
        pos += snprintf(buf + pos, buf_size - pos, "- Current time: %s\n", time_str);
    } else {
        int64_t uptime_us = esp_timer_get_time();
        int uptime_sec = (int)(uptime_us / 1000000);
        pos += snprintf(buf + pos, buf_size - pos,
                        "- Uptime: %dd %dh %dm (clock not synced)\n",
                        uptime_sec / 86400, (uptime_sec % 86400) / 3600,
                        (uptime_sec % 3600) / 60);
    }

    pos += snprintf(buf + pos, buf_size - pos,
                    "- Free heap: %lu KB\n",
                    (unsigned long)(esp_get_free_heap_size() / 1024));
    pos += snprintf(buf + pos, buf_size - pos,
                    "- Platform: M5Stack Core2 (ESP32, ESP-IDF)\n");
    pos += snprintf(buf + pos, buf_size - pos,
                    "- Keep responses concise (Telegram/small screen)\n\n");

    return pos;
}

char *agent_context_build_system_prompt(void)
{
    // Read memory files
    char *soul = memory_store_read(MEMORY_SOUL_PATH);
    char *user = memory_store_read(MEMORY_USER_PATH);
    char *memory = memory_store_read(MEMORY_MEMORY_PATH);

    // Calculate total size
    int total = 256;  // Base space for context section
    if (soul)   total += strlen(soul) + 32;
    if (user)   total += strlen(user) + 32;
    if (memory) total += strlen(memory) + 32;

    if (!soul && !user && !memory) {
        ESP_LOGW(TAG, "No memory files found, using default system prompt");
        return strdup(DEFAULT_SYSTEM);
    }

    total += 128; // Extra space for formatting
    char *prompt = malloc(total);
    if (!prompt) {
        free(soul);
        free(user);
        free(memory);
        return strdup(DEFAULT_SYSTEM);
    }

    int pos = 0;

    if (soul) {
        pos += snprintf(prompt + pos, total - pos,
                        "# SOUL (Identity)\n%s\n\n", soul);
        free(soul);
    }

    if (user) {
        pos += snprintf(prompt + pos, total - pos,
                        "# USER (Profile)\n%s\n\n", user);
        free(user);
    }

    if (memory) {
        pos += snprintf(prompt + pos, total - pos,
                        "# MEMORY (Long-term)\n%s\n\n", memory);
        free(memory);
    }

    // Add runtime context
    pos += build_context_section(prompt + pos, total - pos);

    ESP_LOGI(TAG, "System prompt built: %d bytes", pos);
    return prompt;
}

int agent_context_flatten_history(const memory_history_t *hist,
                                   llm_message_t *flat_messages,
                                   int max_messages)
{
    int count = (hist->count < max_messages) ? hist->count : max_messages;

    // Copy from newest messages if history is larger than max
    int skip = hist->count - count;
    for (int i = 0; i < count; i++) {
        int idx = (hist->start + skip + i) % HISTORY_MAX_MESSAGES;
        flat_messages[i] = hist->messages[idx];  // Shallow copy (pointer sharing)
    }

    return count;
}
