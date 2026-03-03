#pragma once

#include "llm_provider.h"

/**
 * Create and register the Anthropic Claude provider.
 */
esp_err_t llm_claude_register(const char *api_key, const char *model);
