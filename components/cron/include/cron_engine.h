#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef CRON_MAX_JOBS
#define CRON_MAX_JOBS 16
#endif

/**
 * Cron expression (simplified 5-field: min hour dom month dow).
 * -1 means "any" (wildcard *).
 */
typedef struct {
    int8_t minute;   // 0-59 or -1 (any)
    int8_t hour;     // 0-23 or -1 (any)
    int8_t dom;      // 1-31 or -1 (any)
    int8_t month;    // 1-12 or -1 (any)
    int8_t dow;      // 0-6 (Sun=0) or -1 (any)
} cron_expr_t;

/**
 * A scheduled job.
 */
typedef struct {
    bool        active;
    char        name[32];
    cron_expr_t expr;
    char        message[128];  // Message to send to agent when triggered
} cron_job_t;

/**
 * Callback for cron triggers.
 */
typedef void (*cron_trigger_cb_t)(const char *job_name, const char *message, void *user_data);

/**
 * Initialize the cron engine. Starts a background task that checks once per minute.
 * @param cb        Callback when a job fires
 * @param user_data User data for callback
 */
esp_err_t cron_engine_init(cron_trigger_cb_t cb, void *user_data);

/**
 * Add a cron job.
 * @param name    Unique name (max 31 chars)
 * @param expr    Cron expression
 * @param message Message to send when triggered
 * @return ESP_OK, ESP_ERR_NO_MEM if full
 */
esp_err_t cron_engine_add(const char *name, const cron_expr_t *expr, const char *message);

/**
 * Remove a cron job by name.
 */
esp_err_t cron_engine_remove(const char *name);

/**
 * List all active jobs.
 * @param out_jobs  Output array of job pointers
 * @param max_count Size of output array
 * @return Number of active jobs
 */
int cron_engine_list(const cron_job_t **out_jobs, int max_count);

/**
 * Parse a simplified cron expression string: "min hour dom month dow"
 * Use * for any.  Example: "0 9 * * 1-5" = 9:00 AM weekdays
 * Only supports single values and *, not ranges/lists (except dow hack).
 */
esp_err_t cron_expr_parse(const char *str, cron_expr_t *out);

/**
 * Format a cron expression to string.
 */
void cron_expr_to_string(const cron_expr_t *expr, char *buf, size_t buf_size);
