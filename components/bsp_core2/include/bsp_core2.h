#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * Initialize all M5Stack Core2 BSP subsystems.
 * Sets up: I2C bus, AXP192 power, LCD display, touch controller.
 */
esp_err_t bsp_core2_init(void);

/**
 * Get the internal I2C bus handle (shared by AXP192, touch, IMU).
 * Valid only after bsp_core2_init().
 */
i2c_master_bus_handle_t bsp_get_i2c_bus(void);
