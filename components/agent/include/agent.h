#pragma once

#include "esp_err.h"
#include "llm_types.h"
#include "memory_history.h"
#include "tool_registry.h"

/**
 * Agent configuration
 */
typedef struct {
    int max_iterations;      // Max ReAct tool-call iterations (default: 5)
    int max_tokens;          // Max tokens per LLM call (default: 1024)
    tool_registry_t *tools;  // Tool registry (NULL = no tools)
} agent_config_t;

/**
 * Agent state
 */
typedef struct {
    memory_history_t history;
    agent_config_t config;
    bool initialized;
} agent_t;

/**
 * Streaming callback for agent responses.
 * Called with text deltas as they arrive from the LLM.
 */
typedef void (*agent_stream_cb_t)(const char *text_delta, void *user_data);

/**
 * Initialize the agent. Loads history from SPIFFS.
 */
esp_err_t agent_init(agent_t *agent, const agent_config_t *config);

/**
 * Process a user message and generate a response.
 *
 * This runs the ReAct loop:
 *   1. Build context (system prompt + memory + history)
 *   2. Call LLM
 *   3. If tool_use -> execute tool -> add result -> goto 2
 *   4. Return final text response
 *
 * @param agent      Agent state
 * @param user_text  User input text
 * @param out_text   Output: heap-allocated response text (caller must free)
 * @param stream_cb  Optional streaming callback
 * @param cb_data    User data for callback
 * @return ESP_OK on success
 */
esp_err_t agent_process(agent_t *agent, const char *user_text,
                         char **out_text,
                         agent_stream_cb_t stream_cb, void *cb_data);

/**
 * Clear conversation history.
 */
void agent_clear_history(agent_t *agent);

/**
 * Deinitialize the agent.
 */
void agent_deinit(agent_t *agent);
