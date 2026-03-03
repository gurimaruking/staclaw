#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * SPIFFS-based persistent file storage for agent memory.
 *
 * Files stored on SPIFFS partition:
 *   /spiffs/SOUL.md    - Agent personality (read-only at runtime)
 *   /spiffs/USER.md    - User profile (writable by agent)
 *   /spiffs/MEMORY.md  - Long-term memory (writable by agent)
 */

#define MEMORY_MOUNT_POINT  "/spiffs"
#define MEMORY_SOUL_PATH    "/spiffs/SOUL.md"
#define MEMORY_USER_PATH    "/spiffs/USER.md"
#define MEMORY_MEMORY_PATH  "/spiffs/MEMORY.md"
#define MEMORY_MAX_FILE_SIZE 8192

/**
 * Initialize SPIFFS and mount the partition.
 */
esp_err_t memory_store_init(void);

/**
 * Read a file from SPIFFS into a heap-allocated buffer.
 * Caller must free the returned string.
 * Returns NULL on error or if file does not exist.
 */
char *memory_store_read(const char *path);

/**
 * Write content to a file on SPIFFS (overwrite).
 */
esp_err_t memory_store_write(const char *path, const char *content);

/**
 * Append content to a file on SPIFFS.
 */
esp_err_t memory_store_append(const char *path, const char *content);

/**
 * Check if a file exists on SPIFFS.
 */
bool memory_store_exists(const char *path);

/**
 * Deinitialize and unmount SPIFFS.
 */
void memory_store_deinit(void);
