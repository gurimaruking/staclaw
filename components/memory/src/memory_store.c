#include "memory_store.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "memory_store";

esp_err_t memory_store_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MEMORY_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", (int)total, (int)used);
    }

    return ESP_OK;
}

char *memory_store_read(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGD(TAG, "File not found: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > MEMORY_MAX_FILE_SIZE) {
        ESP_LOGW(TAG, "File size invalid or too large: %s (%ld)", path, size);
        fclose(f);
        return NULL;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);

    ESP_LOGD(TAG, "Read %d bytes from %s", (int)read, path);
    return buf;
}

esp_err_t memory_store_write(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open for write: %s", path);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Write incomplete: %s (%d/%d)", path, (int)written, (int)len);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Wrote %d bytes to %s", (int)written, path);
    return ESP_OK;
}

esp_err_t memory_store_append(const char *path, const char *content)
{
    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open for append: %s", path);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Append incomplete: %s", path);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool memory_store_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

void memory_store_deinit(void)
{
    esp_vfs_spiffs_unregister("storage");
    ESP_LOGI(TAG, "SPIFFS unmounted");
}
