#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "net_http.h"

/**
 * Callback for each SSE event.
 * @param event  Event type string (e.g., "content_block_delta"), can be NULL
 * @param data   Event data string
 * @param user_data  User-provided context
 * @return true to continue, false to abort stream
 */
typedef bool (*sse_event_cb_t)(const char *event, const char *data, void *user_data);

/**
 * Perform a streaming HTTP POST and parse SSE events.
 * Calls the callback for each "data:" line received.
 */
esp_err_t net_stream_sse(const http_request_t *req,
                         sse_event_cb_t callback, void *user_data,
                         int timeout_ms);
