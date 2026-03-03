#include "llm_provider.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "llm_provider";

static llm_provider_t *s_providers[LLM_MAX_PROVIDERS];
static int s_provider_count = 0;
static llm_provider_t *s_active = NULL;

void llm_message_free(llm_message_t *msg)
{
    if (!msg) return;
    free(msg->content);       msg->content = NULL;
    free(msg->tool_call_id);  msg->tool_call_id = NULL;
    free(msg->tool_name);     msg->tool_name = NULL;
    free(msg->tool_args_json);msg->tool_args_json = NULL;
}

void llm_response_free(llm_response_t *resp)
{
    if (!resp) return;
    free(resp->text);          resp->text = NULL;
    free(resp->tool_call_id);  resp->tool_call_id = NULL;
    free(resp->tool_name);     resp->tool_name = NULL;
    free(resp->tool_args_json);resp->tool_args_json = NULL;
    free(resp->stop_reason);   resp->stop_reason = NULL;
}

esp_err_t llm_register_provider(llm_provider_t *provider)
{
    if (s_provider_count >= LLM_MAX_PROVIDERS) {
        ESP_LOGE(TAG, "Max providers reached");
        return ESP_ERR_NO_MEM;
    }
    s_providers[s_provider_count++] = provider;
    if (!s_active) s_active = provider;
    ESP_LOGI(TAG, "Registered provider: %s", provider->name);
    return ESP_OK;
}

llm_provider_t *llm_get_provider(const char *name)
{
    for (int i = 0; i < s_provider_count; i++) {
        if (strcmp(s_providers[i]->name, name) == 0) {
            return s_providers[i];
        }
    }
    return NULL;
}

llm_provider_t *llm_get_active_provider(void)
{
    return s_active;
}

esp_err_t llm_set_active_provider(const char *name)
{
    llm_provider_t *p = llm_get_provider(name);
    if (!p) {
        ESP_LOGE(TAG, "Provider '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }
    s_active = p;
    ESP_LOGI(TAG, "Active provider set to: %s", name);
    return ESP_OK;
}
