#include "bsp_power.h"
#include "bsp_core2.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "axp192";

/* ---- AXP192 Register Map ---- */
#define AXP_POWER_STATUS     0x00
#define AXP_CHARGE_STATUS    0x01
#define AXP_OUTPUT_CTRL      0x12   /* Output enable: DC-DC1/2/3, LDO2/3, EXTEN */
#define AXP_DCDC1_VOLT       0x26   /* DC-DC1 voltage (700-3500mV, 25mV step) */
#define AXP_DCDC3_VOLT       0x27   /* DC-DC3 voltage (700-3500mV, 25mV step) */
#define AXP_LDO23_VOLT       0x28   /* LDO2[7:4] / LDO3[3:0] (1800-3300mV, 100mV step) */
#define AXP_VBUS_IPSOUT      0x30   /* VBUS-IPSOUT path setting */
#define AXP_VOFF_SETTING     0x31   /* Voff shutdown voltage */
#define AXP_CHARGE_CTRL1     0x33   /* Charge control 1 */
#define AXP_BACKUP_CHG       0x35   /* Backup battery charge */
#define AXP_ADC_ENABLE1      0x82   /* ADC enable register 1 */
#define AXP_GPIO0_FUNC       0x90   /* GPIO0 function */
#define AXP_GPIO0_LDO_VOLT   0x91   /* GPIO0 LDO voltage */
#define AXP_GPIO1_FUNC       0x92   /* GPIO1 function */
#define AXP_GPIO2_FUNC       0x93   /* GPIO2 function */
#define AXP_GPIO012_LEVEL    0x94   /* GPIO 0/1/2 output level */
#define AXP_BAT_VOLT_H       0x78   /* Battery voltage ADC high byte */
#define AXP_BAT_VOLT_L       0x79   /* Battery voltage ADC low 4 bits */

/* Output control bits (REG 0x12) */
#define OUT_DCDC1   (1 << 0)   /* ESP32 3.3V */
#define OUT_DCDC3   (1 << 1)   /* LCD logic 2.8V */
#define OUT_LDO2    (1 << 2)   /* LCD backlight */
#define OUT_LDO3    (1 << 3)   /* Vibration motor */
#define OUT_DCDC2   (1 << 4)   /* Not used on Core2 */
#define OUT_EXTEN   (1 << 6)   /* External enable */

static i2c_master_dev_handle_t s_dev = NULL;

/* ---- I2C helpers ---- */

static esp_err_t axp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t axp_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t axp_set_bits(uint8_t reg, uint8_t mask)
{
    uint8_t val;
    esp_err_t err = axp_read(reg, &val);
    if (err != ESP_OK) return err;
    return axp_write(reg, val | mask);
}

static esp_err_t axp_clear_bits(uint8_t reg, uint8_t mask)
{
    uint8_t val;
    esp_err_t err = axp_read(reg, &val);
    if (err != ESP_OK) return err;
    return axp_write(reg, val & ~mask);
}

/* ---- Public API ---- */

esp_err_t bsp_power_init(void)
{
    i2c_master_bus_handle_t bus = bsp_get_i2c_bus();
    if (!bus) return ESP_ERR_INVALID_STATE;

    /* Add AXP192 as I2C device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_AXP192_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) return err;

    /* Verify communication */
    uint8_t status;
    err = axp_read(AXP_POWER_STATUS, &status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AXP192 not responding");
        return err;
    }
    ESP_LOGI(TAG, "AXP192 power status: 0x%02X", status);

    /* VBUS-IPSOUT: VBUS hold voltage limit off, current limit 500mA */
    axp_write(AXP_VBUS_IPSOUT, 0x80);

    /* GPIO0: LDO mode for bus power (Port A/B/C) */
    axp_write(AXP_GPIO0_FUNC, 0x02);
    axp_write(AXP_GPIO0_LDO_VOLT, 0xF0);  /* 3.3V */

    /* DC-DC1: 3.3V for ESP32 (should already be on) */
    axp_write(AXP_DCDC1_VOLT, (3300 - 700) / 25);  /* 0x68 */

    /* DC-DC3: 2.8V for LCD logic */
    axp_write(AXP_DCDC3_VOLT, (2800 - 700) / 25);  /* 0x54 */

    /* LDO2: 3.0V (LCD backlight), LDO3: 2.0V (vibration motor) */
    uint8_t ldo2_val = (3000 - 1800) / 100;  /* 0x0C */
    uint8_t ldo3_val = (2000 - 1800) / 100;  /* 0x02 */
    axp_write(AXP_LDO23_VOLT, (ldo2_val << 4) | ldo3_val);

    /* Enable: DC-DC1, DC-DC3, LDO2 (backlight on), EXTEN. LDO3 off (vibration) */
    axp_write(AXP_OUTPUT_CTRL, OUT_DCDC1 | OUT_DCDC3 | OUT_LDO2 | OUT_EXTEN);

    /* Charging: enable, target 4.2V, current 100mA */
    axp_write(AXP_CHARGE_CTRL1, 0xC0);

    /* Enable battery voltage ADC */
    axp_write(AXP_ADC_ENABLE1, 0xFF);

    /* GPIO1: NMOS open drain (general) */
    axp_write(AXP_GPIO1_FUNC, 0x00);

    /* GPIO2: NMOS open drain (speaker amplifier control) */
    axp_write(AXP_GPIO2_FUNC, 0x00);
    /* Speaker off initially */
    axp_clear_bits(AXP_GPIO012_LEVEL, 0x04);

    /* Shutdown voltage 3.0V */
    axp_write(AXP_VOFF_SETTING, 0x03);

    ESP_LOGI(TAG, "AXP192 initialized (DC-DC3=2.8V, LDO2=3.0V)");
    return ESP_OK;
}

esp_err_t bsp_power_set_lcd_brightness(int percent)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (percent == 0) {
        /* Turn off backlight */
        return axp_clear_bits(AXP_OUTPUT_CTRL, OUT_LDO2);
    }

    /* Enable backlight */
    axp_set_bits(AXP_OUTPUT_CTRL, OUT_LDO2);

    /* Map 1-100% to 2.5V-3.3V (LDO2 range usable for backlight) */
    /* LDO2 register = (voltage_mV - 1800) / 100, range 0x00-0x0F */
    int mv = 2500 + (percent * 8);  /* 2500-3300mV */
    if (mv > 3300) mv = 3300;
    uint8_t ldo2_val = (mv - 1800) / 100;

    /* Preserve LDO3 value */
    uint8_t reg;
    axp_read(AXP_LDO23_VOLT, &reg);
    return axp_write(AXP_LDO23_VOLT, (ldo2_val << 4) | (reg & 0x0F));
}

esp_err_t bsp_power_set_vibration(bool enable)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (enable) {
        return axp_set_bits(AXP_OUTPUT_CTRL, OUT_LDO3);
    } else {
        return axp_clear_bits(AXP_OUTPUT_CTRL, OUT_LDO3);
    }
}

esp_err_t bsp_power_set_speaker(bool enable)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (enable) {
        return axp_set_bits(AXP_GPIO012_LEVEL, 0x04);
    } else {
        return axp_clear_bits(AXP_GPIO012_LEVEL, 0x04);
    }
}

int bsp_power_get_battery_mv(void)
{
    if (!s_dev) return 0;
    uint8_t h, l;
    if (axp_read(AXP_BAT_VOLT_H, &h) != ESP_OK) return 0;
    if (axp_read(AXP_BAT_VOLT_L, &l) != ESP_OK) return 0;
    /* 12-bit value: high byte [7:0] + low byte [7:4], 1.1mV per LSB */
    uint16_t raw = ((uint16_t)h << 4) | (l >> 4);
    return (int)(raw * 1.1f);
}

int bsp_power_get_battery_percent(void)
{
    int mv = bsp_power_get_battery_mv();
    if (mv <= 0) return 0;
    /* Rough linear estimate: 3300mV=0%, 4150mV=100% */
    int pct = (mv - 3300) * 100 / (4150 - 3300);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

bool bsp_power_is_charging(void)
{
    if (!s_dev) return false;
    uint8_t val;
    if (axp_read(AXP_CHARGE_STATUS, &val) != ESP_OK) return false;
    return (val & 0x40) != 0;  /* Bit 6: charging indicator */
}
