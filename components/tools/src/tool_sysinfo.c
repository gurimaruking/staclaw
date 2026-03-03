#include "tool_sysinfo.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tool_sys";

static esp_err_t sysinfo_execute(const char *args_json, char *result, size_t result_size)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    int64_t uptime_us = esp_timer_get_time();
    int uptime_sec = (int)(uptime_us / 1000000);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chip", "ESP32");
    cJSON_AddNumberToObject(root, "cores", chip.cores);
    cJSON_AddNumberToObject(root, "revision", chip.revision);
    cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", (double)esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_seconds", uptime_sec);

    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%dd %dh %dm %ds",
             uptime_sec / 86400,
             (uptime_sec % 86400) / 3600,
             (uptime_sec % 3600) / 60,
             uptime_sec % 60);
    cJSON_AddStringToObject(root, "uptime_formatted", uptime_str);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json) {
        snprintf(result, result_size, "%s", json);
        free(json);
    } else {
        snprintf(result, result_size, "{\"error\": \"JSON build failed\"}");
    }

    ESP_LOGI(TAG, "System info queried");
    return ESP_OK;
}

static const char SYSINFO_SCHEMA[] =
    "{\"type\":\"object\",\"properties\":{},\"required\":[]}";

esp_err_t tool_sysinfo_register(tool_registry_t *reg)
{
    tool_def_t tool = {
        .name = "get_system_info",
        .description = "Get system information: chip, memory, uptime, IDF version.",
        .input_schema_json = SYSINFO_SCHEMA,
        .execute = sysinfo_execute,
    };
    return tool_registry_add(reg, &tool);
}
