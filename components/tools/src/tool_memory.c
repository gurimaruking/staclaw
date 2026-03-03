#include "tool_memory.h"
#include "memory_store.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tool_mem";

// Map friendly name to SPIFFS path
static const char *resolve_path(const char *file)
{
    if (!file) return NULL;
    if (strcmp(file, "SOUL.md") == 0)   return MEMORY_SOUL_PATH;
    if (strcmp(file, "USER.md") == 0)   return MEMORY_USER_PATH;
    if (strcmp(file, "MEMORY.md") == 0) return MEMORY_MEMORY_PATH;
    return NULL;
}

static esp_err_t read_memory_execute(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\": \"Invalid JSON arguments\"}");
        return ESP_OK;
    }

    const char *file = cJSON_GetStringValue(cJSON_GetObjectItem(args, "file"));
    const char *path = resolve_path(file);
    cJSON_Delete(args);

    if (!path) {
        snprintf(result, result_size,
                 "{\"error\": \"Invalid file. Use SOUL.md, USER.md, or MEMORY.md\"}");
        return ESP_OK;
    }

    char *content = memory_store_read(path);
    if (!content) {
        snprintf(result, result_size, "{\"content\": \"\", \"note\": \"File is empty or not found\"}");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "file", file);
    cJSON_AddStringToObject(root, "content", content);
    cJSON_AddNumberToObject(root, "size", (double)strlen(content));
    free(content);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json) {
        snprintf(result, result_size, "%s", json);
        free(json);
    } else {
        snprintf(result, result_size, "{\"error\": \"JSON build failed\"}");
    }

    ESP_LOGI(TAG, "Read memory: %s", file);
    return ESP_OK;
}

static esp_err_t write_memory_execute(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\": \"Invalid JSON arguments\"}");
        return ESP_OK;
    }

    const char *file = cJSON_GetStringValue(cJSON_GetObjectItem(args, "file"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    cJSON *append_item = cJSON_GetObjectItem(args, "append");
    bool append = append_item && cJSON_IsTrue(append_item);

    const char *path = resolve_path(file);
    if (!path || !content) {
        cJSON_Delete(args);
        snprintf(result, result_size,
                 "{\"error\": \"Missing file or content parameter\"}");
        return ESP_OK;
    }

    // Protect SOUL.md from being overwritten (append only)
    if (strcmp(file, "SOUL.md") == 0 && !append) {
        cJSON_Delete(args);
        snprintf(result, result_size,
                 "{\"error\": \"SOUL.md can only be appended to, not overwritten. Set append=true.\"}");
        return ESP_OK;
    }

    esp_err_t err;
    if (append) {
        // Read existing content, append, write back
        char *existing = memory_store_read(path);
        int existing_len = existing ? strlen(existing) : 0;
        int new_len = strlen(content);
        char *combined = malloc(existing_len + new_len + 2);
        if (!combined) {
            free(existing);
            cJSON_Delete(args);
            snprintf(result, result_size, "{\"error\": \"Out of memory\"}");
            return ESP_OK;
        }
        if (existing) {
            memcpy(combined, existing, existing_len);
            combined[existing_len] = '\n';
            memcpy(combined + existing_len + 1, content, new_len);
            combined[existing_len + 1 + new_len] = '\0';
            free(existing);
        } else {
            memcpy(combined, content, new_len);
            combined[new_len] = '\0';
        }
        err = memory_store_write(path, combined);
        free(combined);
    } else {
        err = memory_store_write(path, content);
    }

    cJSON_Delete(args);

    if (err == ESP_OK) {
        snprintf(result, result_size,
                 "{\"success\": true, \"file\": \"%s\", \"action\": \"%s\"}",
                 file, append ? "appended" : "written");
    } else {
        snprintf(result, result_size,
                 "{\"error\": \"Write failed: %s\"}", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Write memory: %s (%s)", file, append ? "append" : "overwrite");
    return ESP_OK;
}

static const char READ_SCHEMA[] =
    "{\"type\":\"object\","
    "\"properties\":{\"file\":{\"type\":\"string\",\"enum\":[\"SOUL.md\",\"USER.md\",\"MEMORY.md\"],"
    "\"description\":\"Memory file to read\"}},"
    "\"required\":[\"file\"]}";

static const char WRITE_SCHEMA[] =
    "{\"type\":\"object\","
    "\"properties\":{\"file\":{\"type\":\"string\",\"enum\":[\"SOUL.md\",\"USER.md\",\"MEMORY.md\"],"
    "\"description\":\"Memory file to write\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"Content to write\"},"
    "\"append\":{\"type\":\"boolean\",\"description\":\"Append instead of overwrite (default: false)\"}},"
    "\"required\":[\"file\",\"content\"]}";

esp_err_t tool_memory_register(tool_registry_t *reg)
{
    tool_def_t read_tool = {
        .name = "read_memory",
        .description = "Read a persistent memory file. Files: SOUL.md (identity), USER.md (user profile), MEMORY.md (long-term notes).",
        .input_schema_json = READ_SCHEMA,
        .execute = read_memory_execute,
    };
    esp_err_t err = tool_registry_add(reg, &read_tool);
    if (err != ESP_OK) return err;

    tool_def_t write_tool = {
        .name = "write_memory",
        .description = "Write or append to a persistent memory file. SOUL.md can only be appended to.",
        .input_schema_json = WRITE_SCHEMA,
        .execute = write_memory_execute,
    };
    return tool_registry_add(reg, &write_tool);
}
