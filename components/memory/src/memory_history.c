#include "memory_history.h"
#include "memory_store.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "memory_history";

static inline int ring_index(int start, int offset, int cap)
{
    return (start + offset) % cap;
}

static void free_message_fields(llm_message_t *msg)
{
    free(msg->content);       msg->content = NULL;
    free(msg->tool_call_id);  msg->tool_call_id = NULL;
    free(msg->tool_name);     msg->tool_name = NULL;
    free(msg->tool_args_json); msg->tool_args_json = NULL;
}

esp_err_t memory_history_init(memory_history_t *hist)
{
    memset(hist, 0, sizeof(*hist));

    // Try loading from SPIFFS
    char *json_str = memory_store_read(HISTORY_FILE_PATH);
    if (!json_str) {
        ESP_LOGI(TAG, "No saved history, starting fresh");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    int arr_size = cJSON_GetArraySize(root);
    for (int i = 0; i < arr_size && hist->count < HISTORY_MAX_MESSAGES; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *role_j = cJSON_GetObjectItem(item, "role");
        cJSON *content_j = cJSON_GetObjectItem(item, "content");

        if (!role_j) continue;

        int idx = hist->count;
        llm_message_t *msg = &hist->messages[idx];

        const char *role_str = role_j->valuestring;
        if (strcmp(role_str, "user") == 0) msg->role = LLM_ROLE_USER;
        else if (strcmp(role_str, "assistant") == 0) msg->role = LLM_ROLE_ASSISTANT;
        else if (strcmp(role_str, "tool_result") == 0) msg->role = LLM_ROLE_TOOL_RESULT;
        else continue;

        if (content_j && cJSON_IsString(content_j))
            msg->content = strdup(content_j->valuestring);

        cJSON *tid = cJSON_GetObjectItem(item, "tool_call_id");
        if (tid && cJSON_IsString(tid)) msg->tool_call_id = strdup(tid->valuestring);

        cJSON *tn = cJSON_GetObjectItem(item, "tool_name");
        if (tn && cJSON_IsString(tn)) msg->tool_name = strdup(tn->valuestring);

        cJSON *ta = cJSON_GetObjectItem(item, "tool_args");
        if (ta && cJSON_IsString(ta)) msg->tool_args_json = strdup(ta->valuestring);

        hist->count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d messages from history", hist->count);
    return ESP_OK;
}

esp_err_t memory_history_add(memory_history_t *hist, llm_role_t role,
                              const char *content,
                              const char *tool_call_id,
                              const char *tool_name,
                              const char *tool_args_json)
{
    if (hist->count >= HISTORY_MAX_MESSAGES) {
        // Evict oldest message
        int oldest = hist->start;
        free_message_fields(&hist->messages[oldest]);
        hist->start = (hist->start + 1) % HISTORY_MAX_MESSAGES;
        hist->count--;
    }

    int idx = ring_index(hist->start, hist->count, HISTORY_MAX_MESSAGES);
    llm_message_t *msg = &hist->messages[idx];

    msg->role = role;
    msg->content = content ? strdup(content) : NULL;
    msg->tool_call_id = tool_call_id ? strdup(tool_call_id) : NULL;
    msg->tool_name = tool_name ? strdup(tool_name) : NULL;
    msg->tool_args_json = tool_args_json ? strdup(tool_args_json) : NULL;

    hist->count++;
    return ESP_OK;
}

int memory_history_get_messages(const memory_history_t *hist,
                                 const llm_message_t **out_messages,
                                 int max_count)
{
    int n = (hist->count < max_count) ? hist->count : max_count;
    for (int i = 0; i < n; i++) {
        int idx = ring_index(hist->start, i, HISTORY_MAX_MESSAGES);
        out_messages[i] = &hist->messages[idx];
    }
    return n;
}

esp_err_t memory_history_save(const memory_history_t *hist)
{
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < hist->count; i++) {
        int idx = ring_index(hist->start, i, HISTORY_MAX_MESSAGES);
        const llm_message_t *msg = &hist->messages[idx];

        cJSON *item = cJSON_CreateObject();

        const char *role_str = "user";
        switch (msg->role) {
        case LLM_ROLE_USER:        role_str = "user"; break;
        case LLM_ROLE_ASSISTANT:   role_str = "assistant"; break;
        case LLM_ROLE_TOOL_RESULT: role_str = "tool_result"; break;
        default: break;
        }
        cJSON_AddStringToObject(item, "role", role_str);

        if (msg->content)
            cJSON_AddStringToObject(item, "content", msg->content);
        if (msg->tool_call_id)
            cJSON_AddStringToObject(item, "tool_call_id", msg->tool_call_id);
        if (msg->tool_name)
            cJSON_AddStringToObject(item, "tool_name", msg->tool_name);
        if (msg->tool_args_json)
            cJSON_AddStringToObject(item, "tool_args", msg->tool_args_json);

        cJSON_AddItemToArray(root, item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) return ESP_ERR_NO_MEM;

    esp_err_t err = memory_store_write(HISTORY_FILE_PATH, json_str);
    free(json_str);

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Saved %d messages to history", hist->count);
    }
    return err;
}

void memory_history_clear(memory_history_t *hist)
{
    for (int i = 0; i < hist->count; i++) {
        int idx = ring_index(hist->start, i, HISTORY_MAX_MESSAGES);
        free_message_fields(&hist->messages[idx]);
    }
    hist->count = 0;
    hist->start = 0;
}

void memory_history_free(memory_history_t *hist)
{
    memory_history_clear(hist);
}
