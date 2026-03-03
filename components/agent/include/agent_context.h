#pragma once

#include "memory_history.h"

/**
 * Build the system prompt for the agent.
 * Combines SOUL.md + USER.md + MEMORY.md into a single system prompt.
 * Caller must free the returned string.
 */
char *agent_context_build_system_prompt(void);

/**
 * Flatten history ring buffer into a contiguous array for LLM calls.
 * Copies pointers (not data) into flat_messages.
 * Returns number of messages.
 */
int agent_context_flatten_history(const memory_history_t *hist,
                                   llm_message_t *flat_messages,
                                   int max_messages);
