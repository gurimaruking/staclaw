#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize AXP192 power management IC via I2C.
 * Sets up power rails for LCD, backlight, and peripherals.
 */
esp_err_t bsp_power_init(void);

/**
 * Set LCD backlight brightness (0-100).
 */
esp_err_t bsp_power_set_lcd_brightness(int percent);

/**
 * Enable/disable vibration motor.
 */
esp_err_t bsp_power_set_vibration(bool enable);

/**
 * Enable/disable speaker amplifier.
 */
esp_err_t bsp_power_set_speaker(bool enable);

/**
 * Get battery voltage in millivolts.
 */
int bsp_power_get_battery_mv(void);

/**
 * Get battery charge percentage (rough estimate).
 */
int bsp_power_get_battery_percent(void);

/**
 * Check if USB power is connected.
 */
bool bsp_power_is_charging(void);
