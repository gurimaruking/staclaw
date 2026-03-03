#include "llm_claude.h"
#include "net_http.h"
#include "net_stream.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "llm_claude";

#define CLAUDE_API_URL "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_VERSION "2023-06-01"

typedef struct {
    char api_key[128];
    char model[48];
} claude_ctx_t;

// Build the JSON request body for Claude Messages API
static char *claude_build_request(claude_ctx_t *ctx, const char *system_prompt,
                                  const llm_message_t *messages, int msg_count,
                                  const char *tools_json, int max_tokens)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", ctx->model);
    cJSON_AddNumberToObject(root, "max_tokens", max_tokens);
    cJSON_AddBoolToObject(root, "stream", true);

    if (system_prompt && system_prompt[0]) {
        cJSON_AddStringToObject(root, "system", system_prompt);
    }

    // Build messages array
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");
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
                // Assistant message with tool_use
                cJSON *content_arr = cJSON_AddArrayToObject(msg, "content");
                if (messages[i].content && messages[i].content[0]) {
                    cJSON *text_block = cJSON_CreateObject();
                    cJSON_AddStringToObject(text_block, "type", "text");
                    cJSON_AddStringToObject(text_block, "text", messages[i].content);
                    cJSON_AddItemToArray(content_arr, text_block);
                }
                cJSON *tool_block = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_block, "type", "tool_use");
                cJSON_AddStringToObject(tool_block, "id", messages[i].tool_call_id);
                cJSON_AddStringToObject(tool_block, "name", messages[i].tool_name);
                cJSON *input = cJSON_Parse(messages[i].tool_args_json);
                cJSON_AddItemToObject(tool_block, "input", input ? input : cJSON_CreateObject());
                cJSON_AddItemToArray(content_arr, tool_block);
            } else {
                cJSON_AddStringToObject(msg, "content", messages[i].content ? messages[i].content : "");
            }
            break;

        case LLM_ROLE_TOOL_RESULT:
            cJSON_AddStringToObject(msg, "role", "user");
            {
                cJSON *content_arr = cJSON_AddArrayToObject(msg, "content");
                cJSON *result_block = cJSON_CreateObject();
                cJSON_AddStringToObject(result_block, "type", "tool_result");
                cJSON_AddStringToObject(result_block, "tool_use_id", messages[i].tool_call_id);
                cJSON_AddStringToObject(result_block, "content", messages[i].content ? messages[i].content : "");
                cJSON_AddItemToArray(content_arr, result_block);
            }
            break;

        default:
            break;
        }
        cJSON_AddItemToArray(msgs, msg);
    }

    // Add tools if provided
    if (tools_json && tools_json[0]) {
        cJSON *tools = cJSON_Parse(tools_json);
        if (tools) {
            cJSON_AddItemToObject(root, "tools", tools);
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// SSE callback context
typedef struct {
    llm_response_t *response;
    llm_stream_cb_t stream_cb;
    void *cb_data;
    // Accumulation buffers
    char *text_buf;
    int   text_len;
    int   text_cap;
    char *tool_args_buf;
    int   tool_args_len;
    int   tool_args_cap;
} sse_ctx_t;

static void append_text(sse_ctx_t *ctx, const char *delta)
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

static void append_tool_args(sse_ctx_t *ctx, const char *delta)
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

static bool claude_sse_callback(const char *event, const char *data, void *user_data)
{
    sse_ctx_t *ctx = (sse_ctx_t *)user_data;
    if (!data || !data[0]) return true;

    cJSON *json = cJSON_Parse(data);
    if (!json) return true;

    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(json, "type"));
    if (!type) {
        cJSON_Delete(json);
        return true;
    }

    if (strcmp(type, "content_block_start") == 0) {
        cJSON *cb = cJSON_GetObjectItem(json, "content_block");
        if (cb) {
            const char *cb_type = cJSON_GetStringValue(cJSON_GetObjectItem(cb, "type"));
            if (cb_type && strcmp(cb_type, "tool_use") == 0) {
                ctx->response->has_tool_call = true;
                const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(cb, "id"));
                const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(cb, "name"));
                if (id) ctx->response->tool_call_id = strdup(id);
                if (name) ctx->response->tool_name = strdup(name);
            }
        }
    } else if (strcmp(type, "content_block_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(json, "delta");
        if (delta) {
            const char *delta_type = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "type"));
            if (delta_type && strcmp(delta_type, "text_delta") == 0) {
                const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "text"));
                if (text) {
                    append_text(ctx, text);
                    if (ctx->stream_cb) ctx->stream_cb(text, ctx->cb_data);
                }
            } else if (delta_type && strcmp(delta_type, "input_json_delta") == 0) {
                const char *partial = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "partial_json"));
                if (partial) {
                    append_tool_args(ctx, partial);
                }
            }
        }
    } else if (strcmp(type, "message_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(json, "delta");
        if (delta) {
            const char *stop = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "stop_reason"));
            if (stop) ctx->response->stop_reason = strdup(stop);
        }
        cJSON *usage = cJSON_GetObjectItem(json, "usage");
        if (usage) {
            cJSON *out_tok = cJSON_GetObjectItem(usage, "output_tokens");
            if (out_tok) ctx->response->output_tokens = out_tok->valueint;
        }
    } else if (strcmp(type, "message_start") == 0) {
        cJSON *message = cJSON_GetObjectItem(json, "message");
        if (message) {
            cJSON *usage = cJSON_GetObjectItem(message, "usage");
            if (usage) {
                cJSON *in_tok = cJSON_GetObjectItem(usage, "input_tokens");
                if (in_tok) ctx->response->input_tokens = in_tok->valueint;
            }
        }
    }

    cJSON_Delete(json);
    return true;
}

static esp_err_t claude_chat(llm_provider_t *self,
                             const char *system_prompt,
                             const llm_message_t *messages, int msg_count,
                             const char *tools_json, int max_tokens,
                             llm_response_t *out_response,
                             llm_stream_cb_t stream_cb, void *cb_data)
{
    claude_ctx_t *ctx = (claude_ctx_t *)self->ctx;
    memset(out_response, 0, sizeof(*out_response));

    char *body = claude_build_request(ctx, system_prompt, messages, msg_count,
                                      tools_json, max_tokens);
    if (!body) return ESP_ERR_NO_MEM;

    ESP_LOGD(TAG, "Request body length: %d", (int)strlen(body));

    // Setup SSE context
    sse_ctx_t sse_ctx = {
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

    // Build request
    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "%s", ctx->api_key);

    http_request_t req = {
        .url = CLAUDE_API_URL,
        .method = "POST",
        .body = body,
        .body_len = strlen(body),
        .content_type = "application/json",
        .header_count = 2,
        .timeout_ms = 60000,
    };
    req.extra_headers[0][0] = "x-api-key";
    req.extra_headers[0][1] = auth_header;
    req.extra_headers[1][0] = "anthropic-version";
    req.extra_headers[1][1] = ANTHROPIC_VERSION;

    esp_err_t err = net_stream_sse(&req, claude_sse_callback, &sse_ctx, 60000);

    free(body);

    // Transfer accumulated text
    if (sse_ctx.text_len > 0) {
        out_response->text = sse_ctx.text_buf;
    } else {
        free(sse_ctx.text_buf);
    }

    // Transfer accumulated tool args
    if (sse_ctx.tool_args_len > 0 && out_response->has_tool_call) {
        out_response->tool_args_json = sse_ctx.tool_args_buf;
    } else {
        free(sse_ctx.tool_args_buf);
    }

    out_response->is_complete = true;

    ESP_LOGI(TAG, "Claude response: %d in / %d out tokens, stop=%s, tool=%s",
             out_response->input_tokens, out_response->output_tokens,
             out_response->stop_reason ? out_response->stop_reason : "?",
             out_response->has_tool_call ? out_response->tool_name : "none");

    return err;
}

static esp_err_t claude_init(llm_provider_t *self, const char *api_key, const char *model)
{
    claude_ctx_t *ctx = calloc(1, sizeof(claude_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    strncpy(ctx->api_key, api_key, sizeof(ctx->api_key) - 1);
    strncpy(ctx->model, model, sizeof(ctx->model) - 1);
    self->ctx = ctx;
    ESP_LOGI(TAG, "Claude provider initialized (model=%s)", model);
    return ESP_OK;
}

static void claude_destroy(llm_provider_t *self)
{
    free(self->ctx);
    self->ctx = NULL;
}

static llm_provider_t s_claude_provider = {
    .name = "claude",
    .init = claude_init,
    .chat = claude_chat,
    .destroy = claude_destroy,
    .ctx = NULL,
};

esp_err_t llm_claude_register(const char *api_key, const char *model)
{
    esp_err_t err = s_claude_provider.init(&s_claude_provider, api_key, model);
    if (err != ESP_OK) return err;
    return llm_register_provider(&s_claude_provider);
}
