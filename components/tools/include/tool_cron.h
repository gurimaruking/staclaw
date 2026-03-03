#pragma once

#include "tool_registry.h"

/**
 * Register cron management tools with the registry.
 * Registers: cron_add, cron_remove, cron_list
 */
esp_err_t tool_cron_register(tool_registry_t *reg);
