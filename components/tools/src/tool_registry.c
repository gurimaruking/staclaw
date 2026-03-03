#include "tool_registry.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tool_reg";

esp_err_t tool_registry_init(tool_registry_t *reg)
{
    memset(reg, 0, sizeof(*reg));
    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

esp_err_t tool_registry_add(tool_registry_t *reg, const tool_def_t *tool)
{
    if (reg->count >= TOOLS_MAX_COUNT) {
        ESP_LOGE(TAG, "Tool registry full (%d max)", TOOLS_MAX_COUNT);
        return ESP_ERR_NO_MEM;
    }

    reg->tools[reg->count] = *tool;
    reg->count++;

    // Invalidate cached JSON
    free(reg->definitions_json);
    reg->definitions_json = NULL;

    ESP_LOGI(TAG, "Tool registered: %s (%d total)", tool->name, reg->count);
    return ESP_OK;
}

esp_err_t tool_registry_dispatch(tool_registry_t *reg, const char *tool_name,
                                  const char *args_json, char *result, size_t result_size)
{
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i].name, tool_name) == 0) {
            ESP_LOGI(TAG, "Dispatching tool: %s", tool_name);
            return reg->tools[i].execute(args_json, result, result_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", tool_name);
    snprintf(result, result_size, "{\"error\": \"Unknown tool: %s\"}", tool_name);
    return ESP_ERR_NOT_FOUND;
}

const char *tool_registry_get_json(tool_registry_t *reg)
{
    if (reg->count == 0) return NULL;
    if (reg->definitions_json) return reg->definitions_json;

    // Build Claude-format tools JSON array
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < reg->count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", reg->tools[i].name);
        cJSON_AddStringToObject(tool, "description", reg->tools[i].description);

        cJSON *schema = cJSON_Parse(reg->tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        } else {
            // Fallback: empty object schema
            cJSON *empty = cJSON_CreateObject();
            cJSON_AddStringToObject(empty, "type", "object");
            cJSON_AddItemToObject(empty, "properties", cJSON_CreateObject());
            cJSON_AddItemToObject(tool, "input_schema", empty);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    reg->definitions_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built: %d bytes, %d tools",
             reg->definitions_json ? (int)strlen(reg->definitions_json) : 0,
             reg->count);
    return reg->definitions_json;
}

void tool_registry_free(tool_registry_t *reg)
{
    free(reg->definitions_json);
    reg->definitions_json = NULL;
    reg->count = 0;
}
