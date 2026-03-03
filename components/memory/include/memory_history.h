#pragma once

#include "llm_types.h"
#include "esp_err.h"

/**
 * Conversation history ring buffer.
 * Stores the most recent N messages for context window management.
 * Persists to SPIFFS as JSON for crash recovery.
 */

#define HISTORY_MAX_MESSAGES  20
#define HISTORY_FILE_PATH     "/spiffs/history.json"

typedef struct {
    llm_message_t messages[HISTORY_MAX_MESSAGES];
    int count;      // Current number of messages
    int start;      // Ring buffer start index
} memory_history_t;

/**
 * Initialize the history buffer. Loads from SPIFFS if available.
 */
esp_err_t memory_history_init(memory_history_t *hist);

/**
 * Add a message to history. Oldest message is evicted when full.
 * Content strings are strdup'd internally.
 */
esp_err_t memory_history_add(memory_history_t *hist, llm_role_t role,
                              const char *content,
                              const char *tool_call_id,
                              const char *tool_name,
                              const char *tool_args_json);

/**
 * Get a flat array of messages for LLM context.
 * Returns pointers into the ring buffer (do NOT free).
 * out_messages must point to an array of at least HISTORY_MAX_MESSAGES pointers.
 * Returns the number of messages written.
 */
int memory_history_get_messages(const memory_history_t *hist,
                                 const llm_message_t **out_messages,
                                 int max_count);

/**
 * Save history to SPIFFS.
 */
esp_err_t memory_history_save(const memory_history_t *hist);

/**
 * Clear all messages.
 */
void memory_history_clear(memory_history_t *hist);

/**
 * Free all allocated message content in the history.
 */
void memory_history_free(memory_history_t *hist);
