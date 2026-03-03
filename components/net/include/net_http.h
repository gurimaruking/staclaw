#pragma once

#include "esp_err.h"

typedef struct {
    const char *url;
    const char *method;          // "GET" or "POST"
    const char *body;
    int         body_len;
    const char *content_type;
    const char *extra_headers[8][2]; // key-value pairs, NULL-terminated
    int         header_count;
    int         timeout_ms;
} http_request_t;

typedef struct {
    int   status_code;
    char *body;
    int   body_len;
} http_response_t;

/**
 * Perform a synchronous HTTP(S) request.
 * Response body is heap-allocated; caller must call net_http_response_free().
 */
esp_err_t net_http_request(const http_request_t *req, http_response_t *resp);

/**
 * Free response body.
 */
void net_http_response_free(http_response_t *resp);
