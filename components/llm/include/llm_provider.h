#pragma once

#include "esp_err.h"
#include "llm_types.h"

#define LLM_MAX_PROVIDERS 4

typedef struct llm_provider_s {
    const char *name;  // "claude" or "openai"

    esp_err_t (*init)(struct llm_provider_s *self, const char *api_key, const char *model);

    esp_err_t (*chat)(struct llm_provider_s *self,
                      const char *system_prompt,
                      const llm_message_t *messages, int msg_count,
                      const char *tools_json,
                      int max_tokens,
                      llm_response_t *out_response,
                      llm_stream_cb_t stream_cb, void *cb_data);

    void (*destroy)(struct llm_provider_s *self);

    void *ctx;  // Provider-private context
} llm_provider_t;

/**
 * Register a provider implementation.
 */
esp_err_t llm_register_provider(llm_provider_t *provider);

/**
 * Get provider by name.
 */
llm_provider_t *llm_get_provider(const char *name);

/**
 * Get the currently active provider.
 */
llm_provider_t *llm_get_active_provider(void);

/**
 * Set the active provider by name.
 */
esp_err_t llm_set_active_provider(const char *name);
