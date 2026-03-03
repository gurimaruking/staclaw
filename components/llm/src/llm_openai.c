#include "llm_openai.h"
#include "net_http.h"
#include "net_stream.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "llm_openai";

#define OPENAI_API_URL "https://api.openai.com/v1/chat/completions"

typedef struct {
    char api_key[128];
    char model[48];
} openai_ctx_t;

// Build the JSON request body for OpenAI Chat Completions API
static char *openai_build_request(openai_ctx_t *ctx, const char *system_prompt,
                                   const llm_message_t *messages, int msg_count,
                                   const char *tools_json, int max_tokens)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", ctx->model);
    cJSON_AddNumberToObject(root, "max_tokens", max_tokens);
    cJSON_AddBoolToObject(root, "stream", true);

    // Build messages array
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");

    // System message first
    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }

    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_CreateObject();

        switch (messages[i].role) {
        case LLM_ROLE_USER:
            cJSON_AddStringToObject(msg, "role", "user");
            cJSON_AddStringToObject(msg, "content", messages[i].content ? messages[i].content : "");
            break;

        case LLM_ROLE_ASSISTANT:
            cJSON_AddStringToObject(msg, "role", "assistant");
            if (messages[i].tool_name) {
                // Assistant with tool_calls
                if (messages[i].content && messages[i].content[0]) {
                    cJSON_AddStringToObject(msg, "content", messages[i].content);
                } else {
                    cJSON_AddNullToObject(msg, "content");
                }
                cJSON *tool_calls = cJSON_AddArrayToObject(msg, "tool_calls");
                cJSON *tc = cJSON_CreateObject();
                cJSON_AddStringToObject(tc, "id", messages[i].tool_call_id);
                cJSON_AddStringToObject(tc, "type", "function");
                cJSON *func = cJSON_CreateObject();
                cJSON_AddStringToObject(func, "name", messages[i].tool_name);
                cJSON_AddStringToObject(func, "arguments", messages[i].tool_args_json ? messages[i].tool_args_json : "{}");
                cJSON_AddItemToObject(tc, "function", func);
                cJSON_AddItemToArray(tool_calls, tc);
            } else {
                cJSON_AddStringToObject(msg, "content", messages[i].content ? messages[i].content : "");
            }
            break;

        case LLM_ROLE_TOOL_RESULT:
            cJSON_AddStringToObject(msg, "role", "tool");
            cJSON_AddStringToObject(msg, "tool_call_id", messages[i].tool_call_id);
            cJSON_AddStringToObject(msg, "content", messages[i].content ? messages[i].content : "");
            break;

        default:
            break;
        }
        cJSON_AddItemToArray(msgs, msg);
    }

    // Add tools if provided (convert from Claude format to OpenAI format)
    if (tools_json && tools_json[0]) {
        cJSON *claude_tools = cJSON_Parse(tools_json);
        if (claude_tools && cJSON_IsArray(claude_tools)) {
            cJSON *oai_tools = cJSON_CreateArray();
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, claude_tools) {
                cJSON *oai_tool = cJSON_CreateObject();
                cJSON_AddStringToObject(oai_tool, "type", "function");
                cJSON *func = cJSON_CreateObject();
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *desc = cJSON_GetObjectItem(item, "description");
                cJSON *schema = cJSON_GetObjectItem(item, "input_schema");
                if (name) cJSON_AddStringToObject(func, "name", name->valuestring);
                if (desc) cJSON_AddStringToObject(func, "description", desc->valuestring);
                if (schema) cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, true));
                cJSON_AddItemToObject(oai_tool, "function", func);
                cJSON_AddItemToArray(oai_tools, oai_tool);
            }
            cJSON_AddItemToObject(root, "tools", oai_tools);
            cJSON_Delete(claude_tools);
        }
    }

    // Stream options for usage data
    cJSON *stream_opts = cJSON_CreateObject();
    cJSON_AddBoolToObject(stream_opts, "include_usage", true);
    cJSON_AddItemToObject(root, "stream_options", stream_opts);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// SSE callback context for OpenAI
typedef struct {
    llm_response_t *response;
    llm_stream_cb_t stream_cb;
    void *cb_data;
    char *text_buf;
    int   text_len;
    int   text_cap;
    char *tool_args_buf;
    int   tool_args_len;
    int   tool_args_cap;
} oai_sse_ctx_t;

static void oai_append_text(oai_sse_ctx_t *ctx, const char *delta)
{
    int dlen = strlen(delta);
    int needed = ctx->text_len + dlen + 1;
    if (needed > ctx->text_cap) {
        int new_cap = needed * 2;
        char *tmp = realloc(ctx->text_buf, new_cap);
        if (!tmp) return;
        ctx->text_buf = tmp;
        ctx->text_cap = new_cap;
    }
    memcpy(ctx->text_buf + ctx->text_len, delta, dlen);
    ctx->text_len += dlen;
    ctx->text_buf[ctx->text_len] = '\0';
}

static void oai_append_tool_args(oai_sse_ctx_t *ctx, const char *delta)
{
    int dlen = strlen(delta);
    int needed = ctx->tool_args_len + dlen + 1;
    if (needed > ctx->tool_args_cap) {
        int new_cap = needed * 2;
        char *tmp = realloc(ctx->tool_args_buf, new_cap);
        if (!tmp) return;
        ctx->tool_args_buf = tmp;
        ctx->tool_args_cap = new_cap;
    }
    memcpy(ctx->tool_args_buf + ctx->tool_args_len, delta, dlen);
    ctx->tool_args_len += dlen;
    ctx->tool_args_buf[ctx->tool_args_len] = '\0';
}

static bool openai_sse_callback(const char *event, const char *data, void *user_data)
{
    oai_sse_ctx_t *ctx = (oai_sse_ctx_t *)user_data;
    if (!data || !data[0]) return true;

    cJSON *json = cJSON_Parse(data);
    if (!json) return true;

    // Check for usage in final chunk
    cJSON *usage = cJSON_GetObjectItem(json, "usage");
    if (usage) {
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        if (pt) ctx->response->input_tokens = pt->valueint;
        if (ct) ctx->response->output_tokens = ct->valueint;
    }

    cJSON *choices = cJSON_GetObjectItem(json, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(json);
        return true;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
    cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");

    if (finish && !cJSON_IsNull(finish)) {
        ctx->response->stop_reason = strdup(finish->valuestring);
        if (strcmp(finish->valuestring, "tool_calls") == 0) {
            ctx->response->stop_reason = strdup("tool_use"); // Normalize to Claude format
        }
    }

    if (delta) {
        // Text content
        cJSON *content = cJSON_GetObjectItem(delta, "content");
        if (content && cJSON_IsString(content) && content->valuestring[0]) {
            oai_append_text(ctx, content->valuestring);
            if (ctx->stream_cb) ctx->stream_cb(content->valuestring, ctx->cb_data);
        }

        // Tool calls
        cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            cJSON *tc = cJSON_GetArrayItem(tool_calls, 0);
            if (tc) {
                cJSON *id = cJSON_GetObjectItem(tc, "id");
                if (id && cJSON_IsString(id)) {
                    ctx->response->has_tool_call = true;
                    free(ctx->response->tool_call_id);
                    ctx->response->tool_call_id = strdup(id->valuestring);
                }
                cJSON *func = cJSON_GetObjectItem(tc, "function");
                if (func) {
                    cJSON *name = cJSON_GetObjectItem(func, "name");
                    if (name && cJSON_IsString(name)) {
                        free(ctx->response->tool_name);
                        ctx->response->tool_name = strdup(name->valuestring);
                    }
                    cJSON *args = cJSON_GetObjectItem(func, "arguments");
                    if (args && cJSON_IsString(args) && args->valuestring[0]) {
                        oai_append_tool_args(ctx, args->valuestring);
                    }
                }
            }
        }
    }

    cJSON_Delete(json);
    return true;
}

static esp_err_t openai_chat(llm_provider_t *self,
                              const char *system_prompt,
                              const llm_message_t *messages, int msg_count,
                              const char *tools_json, int max_tokens,
                              llm_response_t *out_response,
                              llm_stream_cb_t stream_cb, void *cb_data)
{
    openai_ctx_t *ctx = (openai_ctx_t *)self->ctx;
    memset(out_response, 0, sizeof(*out_response));

    char *body = openai_build_request(ctx, system_prompt, messages, msg_count,
                                       tools_json, max_tokens);
    if (!body) return ESP_ERR_NO_MEM;

    ESP_LOGD(TAG, "Request body length: %d", (int)strlen(body));

    oai_sse_ctx_t sse_ctx = {
        .response = out_response,
        .stream_cb = stream_cb,
        .cb_data = cb_data,
        .text_buf = malloc(1024),
        .text_len = 0,
        .text_cap = 1024,
        .tool_args_buf = malloc(512),
        .tool_args_len = 0,
        .tool_args_cap = 512,
    };
    if (sse_ctx.text_buf) sse_ctx.text_buf[0] = '\0';
    if (sse_ctx.tool_args_buf) sse_ctx.tool_args_buf[0] = '\0';

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", ctx->api_key);

    http_request_t req = {
        .url = OPENAI_API_URL,
        .method = "POST",
        .body = body,
        .body_len = strlen(body),
        .content_type = "application/json",
        .header_count = 1,
        .timeout_ms = 60000,
    };
    req.extra_headers[0][0] = "Authorization";
    req.extra_headers[0][1] = auth_header;

    esp_err_t err = net_stream_sse(&req, openai_sse_callback, &sse_ctx, 60000);

    free(body);

    if (sse_ctx.text_len > 0) {
        out_response->text = sse_ctx.text_buf;
    } else {
        free(sse_ctx.text_buf);
    }

    if (sse_ctx.tool_args_len > 0 && out_response->has_tool_call) {
        out_response->tool_args_json = sse_ctx.tool_args_buf;
    } else {
        free(sse_ctx.tool_args_buf);
    }

    out_response->is_complete = true;

    ESP_LOGI(TAG, "OpenAI response: %d in / %d out tokens, stop=%s, tool=%s",
             out_response->input_tokens, out_response->output_tokens,
             out_response->stop_reason ? out_response->stop_reason : "?",
             out_response->has_tool_call ? out_response->tool_name : "none");

    return err;
}

static esp_err_t openai_init(llm_provider_t *self, const char *api_key, const char *model)
{
    openai_ctx_t *ctx = calloc(1, sizeof(openai_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    strncpy(ctx->api_key, api_key, sizeof(ctx->api_key) - 1);
    strncpy(ctx->model, model, sizeof(ctx->model) - 1);
    self->ctx = ctx;
    ESP_LOGI(TAG, "OpenAI provider initialized (model=%s)", model);
    return ESP_OK;
}

static void openai_destroy(llm_provider_t *self)
{
    free(self->ctx);
    self->ctx = NULL;
}

static llm_provider_t s_openai_provider = {
    .name = "openai",
    .init = openai_init,
    .chat = openai_chat,
    .destroy = openai_destroy,
    .ctx = NULL,
};

esp_err_t llm_openai_register(const char *api_key, const char *model)
{
    esp_err_t err = s_openai_provider.init(&s_openai_provider, api_key, model);
    if (err != ESP_OK) return err;
    return llm_register_provider(&s_openai_provider);
}
