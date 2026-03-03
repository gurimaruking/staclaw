#pragma once

#include "tool_registry.h"

/**
 * Register memory tools (read_memory, write_memory) with the registry.
 */
esp_err_t tool_memory_register(tool_registry_t *reg);
