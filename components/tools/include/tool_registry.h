#pragma once

#include "esp_err.h"

#ifndef TOOLS_MAX_COUNT
#define TOOLS_MAX_COUNT 16
#endif

/**
 * Tool execution function signature.
 * @param args_json  JSON string of tool arguments
 * @param result     Output buffer for JSON result string
 * @param result_size Size of result buffer
 * @return ESP_OK on success
 */
typedef esp_err_t (*tool_exec_fn_t)(const char *args_json, char *result, size_t result_size);

/**
 * Tool definition.
 */
typedef struct {
    const char      *name;
    const char      *description;
    const char      *input_schema_json;  // JSON Schema for parameters (Claude format)
    tool_exec_fn_t   execute;
} tool_def_t;

/**
 * Tool registry.
 */
typedef struct {
    tool_def_t  tools[TOOLS_MAX_COUNT];
    int         count;
    char       *definitions_json;   // Cached Claude-format tools JSON array
} tool_registry_t;

/**
 * Initialize tool registry.
 */
esp_err_t tool_registry_init(tool_registry_t *reg);

/**
 * Register a tool. The tool_def_t fields must remain valid (use static strings).
 */
esp_err_t tool_registry_add(tool_registry_t *reg, const tool_def_t *tool);

/**
 * Dispatch a tool call by name.
 * @param reg         Registry
 * @param tool_name   Name of tool to execute
 * @param args_json   JSON arguments string
 * @param result      Output buffer for JSON result
 * @param result_size Size of result buffer
 * @return ESP_OK if tool found and executed, ESP_ERR_NOT_FOUND if unknown tool
 */
esp_err_t tool_registry_dispatch(tool_registry_t *reg, const char *tool_name,
                                  const char *args_json, char *result, size_t result_size);

/**
 * Get the cached JSON array of tool definitions (Claude format).
 * Returns NULL if no tools registered.
 */
const char *tool_registry_get_json(tool_registry_t *reg);

/**
 * Free registry resources.
 */
void tool_registry_free(tool_registry_t *reg);
