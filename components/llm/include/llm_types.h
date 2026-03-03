#pragma once

#include <stdbool.h>

typedef enum {
    LLM_ROLE_SYSTEM,
    LLM_ROLE_USER,
    LLM_ROLE_ASSISTANT,
    LLM_ROLE_TOOL_RESULT,
} llm_role_t;

typedef struct {
    llm_role_t  role;
    char       *content;         // Text content (can be NULL if tool_use)
    char       *tool_call_id;    // Non-NULL for tool_use or tool_result
    char       *tool_name;       // Tool name for tool_use
    char       *tool_args_json;  // Tool arguments as JSON string
} llm_message_t;

typedef struct {
    char  *text;                // Accumulated text response
    bool   has_tool_call;
    char  *tool_call_id;
    char  *tool_name;
    char  *tool_args_json;
    bool   is_complete;
    int    input_tokens;
    int    output_tokens;
    char  *stop_reason;         // "end_turn", "tool_use", "max_tokens"
} llm_response_t;

// Callback for streaming text deltas
typedef void (*llm_stream_cb_t)(const char *text_delta, void *user_data);

/**
 * Free all heap-allocated fields in an llm_message_t.
 */
void llm_message_free(llm_message_t *msg);

/**
 * Free all heap-allocated fields in an llm_response_t.
 */
void llm_response_free(llm_response_t *resp);
