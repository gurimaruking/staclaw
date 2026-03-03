#pragma once

#include "tool_registry.h"

/**
 * Register GPIO tools (gpio_read, gpio_write) with the registry.
 */
esp_err_t tool_gpio_register(tool_registry_t *reg);
