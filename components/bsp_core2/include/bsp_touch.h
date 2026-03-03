#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
} bsp_touch_point_t;

/**
 * Initialize FT6336U touch controller.
 */
esp_err_t bsp_touch_init(void);

/**
 * Read current touch state.
 * Returns true if a touch is detected.
 */
bool bsp_touch_read(bsp_touch_point_t *point);
