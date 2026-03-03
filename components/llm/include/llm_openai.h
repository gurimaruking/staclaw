#pragma once

#include "llm_provider.h"

/**
 * Create and register the OpenAI GPT provider.
 */
esp_err_t llm_openai_register(const char *api_key, const char *model);
