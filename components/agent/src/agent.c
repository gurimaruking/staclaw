#include "agent.h"
#include "agent_context.h"
#include "llm_provider.h"
#include "memory_store.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "agent";

esp_err_t agent_init(agent_t *agent, const agent_config_t *config)
{
    memset(agent, 0, sizeof(*agent));
    agent->config = *config;

    if (agent->config.max_iterations <= 0) agent->config.max_iterations = 5;
    if (agent->config.max_tokens <= 0) agent->config.max_tokens = 1024;

    esp_err_t err = memory_history_init(&agent->history);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init history: %s", esp_err_to_name(err));
        return err;
    }

    agent->initialized = true;
    ESP_LOGI(TAG, "Agent initialized (max_iter=%d, max_tokens=%d)",
             agent->config.max_iterations, agent->config.max_tokens);
    return ESP_OK;
}

esp_err_t agent_process(agent_t *agent, const char *user_text,
                         char **out_text,
                         agent_stream_cb_t stream_cb, void *cb_data)
{
    if (!agent->initialized) return ESP_ERR_INVALID_STATE;

    *out_text = NULL;

    // Get active LLM provider
    llm_provider_t *provider = llm_get_active_provider();
    if (!provider) {
        ESP_LOGE(TAG, "No active LLM provider");
        return ESP_ERR_NOT_FOUND;
    }

    // Add user message to history
    memory_history_add(&agent->history, LLM_ROLE_USER, user_text, NULL, NULL, NULL);

    // Build system prompt from memory files
    char *system_prompt = agent_context_build_system_prompt();

    // ReAct loop
    char *final_text = NULL;
    int iteration = 0;

    while (iteration < agent->config.max_iterations) {
        iteration++;
        ESP_LOGI(TAG, "Agent iteration %d/%d", iteration, agent->config.max_iterations);

        // Flatten history for LLM call
        llm_message_t flat_messages[HISTORY_MAX_MESSAGES];
        int msg_count = agent_context_flatten_history(&agent->history,
                                                       flat_messages,
                                                       HISTORY_MAX_MESSAGES);

        // Get tools JSON from registry
        const char *tools_json = NULL;
        if (agent->config.tools) {
            tools_json = tool_registry_get_json(agent->config.tools);
        }

        // Call LLM
        llm_response_t response;
        esp_err_t err = provider->chat(provider,
                                        system_prompt,
                                        flat_messages, msg_count,
                                        tools_json,
                                        agent->config.max_tokens,
                                        &response,
                                        (llm_stream_cb_t)stream_cb,
                                        cb_data);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LLM call failed: %s", esp_err_to_name(err));
            llm_response_free(&response);
            free(system_prompt);
            return err;
        }

        // Add assistant response to history
        if (response.has_tool_call) {
            // Assistant wants to call a tool
            memory_history_add(&agent->history, LLM_ROLE_ASSISTANT,
                               response.text,
                               response.tool_call_id,
                               response.tool_name,
                               response.tool_args_json);

            ESP_LOGI(TAG, "Tool call: %s (id=%s)",
                     response.tool_name, response.tool_call_id);

            // Execute tool via registry
            char tool_result[2048];
            if (agent->config.tools) {
                tool_registry_dispatch(agent->config.tools,
                                        response.tool_name,
                                        response.tool_args_json ? response.tool_args_json : "{}",
                                        tool_result, sizeof(tool_result));
            } else {
                snprintf(tool_result, sizeof(tool_result),
                         "{\"error\": \"No tools registered\"}");
            }

            ESP_LOGI(TAG, "Tool result: %.128s%s",
                     tool_result,
                     strlen(tool_result) > 128 ? "..." : "");

            memory_history_add(&agent->history, LLM_ROLE_TOOL_RESULT,
                               tool_result,
                               response.tool_call_id,
                               NULL, NULL);

            // Save any text so far
            if (response.text && !final_text) {
                final_text = strdup(response.text);
            }

            llm_response_free(&response);
            continue;  // Next iteration to process tool result
        }

        // No tool call - this is the final response
        if (response.text) {
            free(final_text);
            final_text = strdup(response.text);
        }

        // Add to history
        memory_history_add(&agent->history, LLM_ROLE_ASSISTANT,
                           response.text, NULL, NULL, NULL);

        llm_response_free(&response);
        break;  // Done
    }

    free(system_prompt);

    // Save history periodically
    memory_history_save(&agent->history);

    if (final_text) {
        *out_text = final_text;
        ESP_LOGI(TAG, "Response: %d chars, %d iterations", (int)strlen(final_text), iteration);
    } else {
        *out_text = strdup("(No response generated)");
        ESP_LOGW(TAG, "No response text after %d iterations", iteration);
    }

    return ESP_OK;
}

void agent_clear_history(agent_t *agent)
{
    memory_history_clear(&agent->history);
    memory_history_save(&agent->history);
    ESP_LOGI(TAG, "History cleared");
}

void agent_deinit(agent_t *agent)
{
    if (!agent->initialized) return;
    memory_history_save(&agent->history);
    memory_history_free(&agent->history);
    agent->initialized = false;
    ESP_LOGI(TAG, "Agent deinitialized");
}
