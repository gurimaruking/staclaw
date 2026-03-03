#include "bsp_core2.h"
#include "bsp_pins.h"
#include "bsp_power.h"
#include "bsp_display.h"
#include "bsp_touch.h"
#include "esp_log.h"

static const char *TAG = "bsp";
static i2c_master_bus_handle_t s_i2c_bus = NULL;

i2c_master_bus_handle_t bsp_get_i2c_bus(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_core2_init(void)
{
    esp_err_t err;

    /* 1. Initialize internal I2C bus (AXP192 + Touch + IMU) */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)",
             BSP_I2C_SDA, BSP_I2C_SCL);

    /* 2. AXP192 power management - must be first (powers LCD, backlight) */
    err = bsp_power_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Power init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. LCD display */
    err = bsp_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 4. Touch controller */
    err = bsp_touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "M5Stack Core2 BSP initialized");
    return ESP_OK;
}
