#include "tool_cron.h"
#include "cron_engine.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "tool_cron";

/* ---- cron_add ---- */
#define CRON_ADD_SCHEMA \
    "{\"type\":\"object\",\"properties\":{" \
    "\"name\":{\"type\":\"string\",\"description\":\"Unique job name\"}," \
    "\"schedule\":{\"type\":\"string\",\"description\":\"Cron expression: min hour dom month dow (use * for any)\"}," \
    "\"message\":{\"type\":\"string\",\"description\":\"Message to process when triggered\"}" \
    "},\"required\":[\"name\",\"schedule\",\"message\"]}"

static esp_err_t cron_add_exec(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
    const char *schedule = cJSON_GetStringValue(cJSON_GetObjectItem(args, "schedule"));
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(args, "message"));

    if (!name || !schedule || !message) {
        snprintf(result, result_size, "{\"error\":\"Missing required fields\"}");
        cJSON_Delete(args);
        return ESP_OK;
    }

    cron_expr_t expr;
    esp_err_t err = cron_expr_parse(schedule, &expr);
    if (err != ESP_OK) {
        snprintf(result, result_size, "{\"error\":\"Invalid cron expression\"}");
        cJSON_Delete(args);
        return ESP_OK;
    }

    err = cron_engine_add(name, &expr, message);
    if (err != ESP_OK) {
        snprintf(result, result_size, "{\"error\":\"Max jobs reached\"}");
    } else {
        char expr_str[32];
        cron_expr_to_string(&expr, expr_str, sizeof(expr_str));
        snprintf(result, result_size, "{\"status\":\"ok\",\"name\":\"%s\",\"schedule\":\"%s\"}",
                 name, expr_str);
    }

    cJSON_Delete(args);
    return ESP_OK;
}

/* ---- cron_remove ---- */
#define CRON_REMOVE_SCHEMA \
    "{\"type\":\"object\",\"properties\":{" \
    "\"name\":{\"type\":\"string\",\"description\":\"Job name to remove\"}" \
    "},\"required\":[\"name\"]}"

static esp_err_t cron_remove_exec(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
    if (!name) {
        snprintf(result, result_size, "{\"error\":\"Missing name\"}");
        cJSON_Delete(args);
        return ESP_OK;
    }

    esp_err_t err = cron_engine_remove(name);
    if (err == ESP_ERR_NOT_FOUND) {
        snprintf(result, result_size, "{\"error\":\"Job not found: %s\"}", name);
    } else {
        snprintf(result, result_size, "{\"status\":\"ok\",\"removed\":\"%s\"}", name);
    }

    cJSON_Delete(args);
    return ESP_OK;
}

/* ---- cron_list ---- */
#define CRON_LIST_SCHEMA "{\"type\":\"object\",\"properties\":{}}"

static esp_err_t cron_list_exec(const char *args_json, char *result, size_t result_size)
{
    (void)args_json;

    const cron_job_t *jobs[CRON_MAX_JOBS];
    int count = cron_engine_list(jobs, CRON_MAX_JOBS);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "jobs");

    for (int i = 0; i < count; i++) {
        cJSON *job = cJSON_CreateObject();
        cJSON_AddStringToObject(job, "name", jobs[i]->name);
        char expr_str[32];
        cron_expr_to_string(&jobs[i]->expr, expr_str, sizeof(expr_str));
        cJSON_AddStringToObject(job, "schedule", expr_str);
        cJSON_AddStringToObject(job, "message", jobs[i]->message);
        cJSON_AddItemToArray(arr, job);
    }
    cJSON_AddNumberToObject(root, "count", count);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        strncpy(result, json_str, result_size - 1);
        result[result_size - 1] = '\0';
        free(json_str);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_cron_register(tool_registry_t *reg)
{
    static const tool_def_t cron_add = {
        .name = "cron_add",
        .description = "Add or update a scheduled cron job. It will send the message to the agent at the scheduled time.",
        .input_schema_json = CRON_ADD_SCHEMA,
        .execute = cron_add_exec,
    };
    static const tool_def_t cron_remove = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by name.",
        .input_schema_json = CRON_REMOVE_SCHEMA,
        .execute = cron_remove_exec,
    };
    static const tool_def_t cron_list = {
        .name = "cron_list",
        .description = "List all active cron jobs with their schedules.",
        .input_schema_json = CRON_LIST_SCHEMA,
        .execute = cron_list_exec,
    };

    esp_err_t err;
    err = tool_registry_add(reg, &cron_add);
    if (err != ESP_OK) return err;
    err = tool_registry_add(reg, &cron_remove);
    if (err != ESP_OK) return err;
    err = tool_registry_add(reg, &cron_list);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Cron tools registered");
    return ESP_OK;
}
