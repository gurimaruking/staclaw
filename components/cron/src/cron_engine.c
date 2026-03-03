#include "cron_engine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "cron";

static cron_job_t s_jobs[CRON_MAX_JOBS];
static int s_job_count = 0;
static cron_trigger_cb_t s_cb = NULL;
static void *s_cb_data = NULL;

static bool cron_match(const cron_expr_t *expr, const struct tm *tm)
{
    if (expr->minute >= 0 && expr->minute != tm->tm_min)  return false;
    if (expr->hour   >= 0 && expr->hour   != tm->tm_hour) return false;
    if (expr->dom    >= 0 && expr->dom    != tm->tm_mday) return false;
    if (expr->month  >= 0 && expr->month  != (tm->tm_mon + 1)) return false;
    if (expr->dow    >= 0 && expr->dow    != tm->tm_wday) return false;
    return true;
}

static void cron_task(void *arg)
{
    (void)arg;
    int last_minute = -1;

    ESP_LOGI(TAG, "Cron task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Check every 10 seconds

        time_t now;
        time(&now);
        struct tm tm;
        localtime_r(&now, &tm);

        // Only process once per minute
        if (tm.tm_min == last_minute) continue;
        last_minute = tm.tm_min;

        // Check if time is valid (SNTP synced)
        if (tm.tm_year < 120) continue;  // Before 2020 = not synced

        for (int i = 0; i < s_job_count; i++) {
            if (!s_jobs[i].active) continue;
            if (cron_match(&s_jobs[i].expr, &tm)) {
                ESP_LOGI(TAG, "Cron fired: %s -> %s", s_jobs[i].name, s_jobs[i].message);
                if (s_cb) {
                    s_cb(s_jobs[i].name, s_jobs[i].message, s_cb_data);
                }
            }
        }
    }
}

esp_err_t cron_engine_init(cron_trigger_cb_t cb, void *user_data)
{
    s_cb = cb;
    s_cb_data = user_data;
    memset(s_jobs, 0, sizeof(s_jobs));
    s_job_count = 0;

    BaseType_t ret = xTaskCreatePinnedToCore(
        cron_task, "cron_task", 4096, NULL, 2, NULL, 0);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Cron engine initialized");
    return ESP_OK;
}

esp_err_t cron_engine_add(const char *name, const cron_expr_t *expr, const char *message)
{
    // Check for duplicate name, replace if found
    for (int i = 0; i < s_job_count; i++) {
        if (s_jobs[i].active && strcmp(s_jobs[i].name, name) == 0) {
            s_jobs[i].expr = *expr;
            strncpy(s_jobs[i].message, message, sizeof(s_jobs[i].message) - 1);
            ESP_LOGI(TAG, "Cron job updated: %s", name);
            return ESP_OK;
        }
    }

    // Find empty slot or append
    int slot = -1;
    for (int i = 0; i < s_job_count; i++) {
        if (!s_jobs[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        if (s_job_count >= CRON_MAX_JOBS) return ESP_ERR_NO_MEM;
        slot = s_job_count++;
    }

    s_jobs[slot].active = true;
    strncpy(s_jobs[slot].name, name, sizeof(s_jobs[slot].name) - 1);
    s_jobs[slot].expr = *expr;
    strncpy(s_jobs[slot].message, message, sizeof(s_jobs[slot].message) - 1);

    ESP_LOGI(TAG, "Cron job added: %s (slot %d)", name, slot);
    return ESP_OK;
}

esp_err_t cron_engine_remove(const char *name)
{
    for (int i = 0; i < s_job_count; i++) {
        if (s_jobs[i].active && strcmp(s_jobs[i].name, name) == 0) {
            s_jobs[i].active = false;
            ESP_LOGI(TAG, "Cron job removed: %s", name);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

int cron_engine_list(const cron_job_t **out_jobs, int max_count)
{
    int n = 0;
    for (int i = 0; i < s_job_count && n < max_count; i++) {
        if (s_jobs[i].active) {
            out_jobs[n++] = &s_jobs[i];
        }
    }
    return n;
}

esp_err_t cron_expr_parse(const char *str, cron_expr_t *out)
{
    if (!str || !out) return ESP_ERR_INVALID_ARG;

    char fields[5][16];
    memset(fields, 0, sizeof(fields));

    int n = sscanf(str, "%15s %15s %15s %15s %15s",
                   fields[0], fields[1], fields[2], fields[3], fields[4]);
    if (n != 5) return ESP_ERR_INVALID_ARG;

    int8_t *vals[] = {&out->minute, &out->hour, &out->dom, &out->month, &out->dow};
    for (int i = 0; i < 5; i++) {
        if (fields[i][0] == '*') {
            *vals[i] = -1;
        } else {
            int v = 0;
            if (sscanf(fields[i], "%d", &v) != 1) return ESP_ERR_INVALID_ARG;
            *vals[i] = (int8_t)v;
        }
    }
    return ESP_OK;
}

void cron_expr_to_string(const cron_expr_t *expr, char *buf, size_t buf_size)
{
    char parts[5][8];
    const int8_t vals[] = {expr->minute, expr->hour, expr->dom, expr->month, expr->dow};
    for (int i = 0; i < 5; i++) {
        if (vals[i] < 0) {
            strcpy(parts[i], "*");
        } else {
            snprintf(parts[i], sizeof(parts[i]), "%d", vals[i]);
        }
    }
    snprintf(buf, buf_size, "%s %s %s %s %s",
             parts[0], parts[1], parts[2], parts[3], parts[4]);
}
