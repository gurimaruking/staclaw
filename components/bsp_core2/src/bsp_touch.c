#include "bsp_touch.h"
#include "bsp_core2.h"
#include "bsp_pins.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "touch";

/* FT6336U registers */
#define FT_REG_NUM_TOUCHES  0x02
#define FT_REG_TOUCH1_XH    0x03
#define FT_REG_TOUCH1_XL    0x04
#define FT_REG_TOUCH1_YH    0x05
#define FT_REG_TOUCH1_YL    0x06

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t ft_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

esp_err_t bsp_touch_init(void)
{
    i2c_master_bus_handle_t bus = bsp_get_i2c_bus();
    if (!bus) return ESP_ERR_INVALID_STATE;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_TOUCH_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) return err;

    /* Verify communication - read chip ID */
    uint8_t id = 0;
    err = ft_read_reg(0xA8, &id, 1);  /* FT6336U panel ID register */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336U not responding (may be OK if no touch)");
        /* Don't fail - touch might init later */
        return ESP_OK;
    }
    ESP_LOGI(TAG, "FT6336U initialized (panel ID: 0x%02X)", id);
    return ESP_OK;
}

bool bsp_touch_read(bsp_touch_point_t *point)
{
    if (!s_dev || !point) return false;
    point->pressed = false;
    point->x = 0;
    point->y = 0;

    /* Read touch count + first point in one transaction */
    uint8_t buf[5];  /* num_touches, xh, xl, yh, yl */
    if (ft_read_reg(FT_REG_NUM_TOUCHES, buf, 5) != ESP_OK) {
        return false;
    }

    uint8_t touches = buf[0] & 0x0F;
    if (touches == 0 || touches > 2) return false;

    /* Extract touch coordinates */
    uint16_t x = ((buf[1] & 0x0F) << 8) | buf[2];
    uint16_t y = ((buf[3] & 0x0F) << 8) | buf[4];
    if (x >= BSP_LCD_W) x = BSP_LCD_W - 1;
    if (y >= BSP_LCD_H) y = BSP_LCD_H - 1;

    point->x = x;
    point->y = y;
    point->pressed = true;
    return true;
}
