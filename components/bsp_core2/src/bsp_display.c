#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "lcd";

/* ILI9342C commands */
#define CMD_SWRESET  0x01
#define CMD_SLPOUT   0x11
#define CMD_INVON    0x21
#define CMD_DISPOFF  0x28
#define CMD_DISPON   0x29
#define CMD_CASET    0x2A
#define CMD_PASET    0x2B
#define CMD_RAMWR    0x2C
#define CMD_MADCTL   0x36
#define CMD_PIXFMT   0x3A

/* Work buffer for fill/blit operations */
#define LCD_LINE_BUF_LINES  16
#define LCD_LINE_BUF_SIZE   (BSP_LCD_W * LCD_LINE_BUF_LINES * 2)

static spi_device_handle_t s_spi = NULL;
static uint8_t *s_line_buf = NULL;

/* ---- SPI helpers ---- */

/* Pre-transfer callback: set DC pin from transaction user field */
static void IRAM_ATTR lcd_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(BSP_LCD_DC, (int)t->user);
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_data = {cmd},
        .user = (void *)0,       /* DC=0 for command */
        .flags = SPI_TRANS_USE_TXDATA,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    lcd_cmd(cmd);
    if (len > 0) {
        spi_transaction_t t = {
            .length = len * 8,
            .user = (void *)1,   /* DC=1 for data */
            .flags = SPI_TRANS_USE_TXDATA,
        };
        /* All init data is ≤15 bytes; send via inline tx_data in chunks of 4 */
        const uint8_t *p = data;
        size_t remaining = len;
        while (remaining > 0) {
            int chunk = (remaining > 4) ? 4 : (int)remaining;
            t.length = chunk * 8;
            memset(t.tx_data, 0, 4);
            memcpy(t.tx_data, p, chunk);
            esp_err_t err = spi_device_polling_transmit(s_spi, &t);
            if (err != ESP_OK) return err;
            p += chunk;
            remaining -= chunk;
        }
    }
    return ESP_OK;
}

/* Send pixel data via SPI FIFO (no DMA). Max 64 bytes per transaction. */
static esp_err_t lcd_send_pixels(const uint8_t *data, size_t len)
{
    const int MAX_CHUNK = 64;   /* ESP32 SPI FIFO = 64 bytes */
    const uint8_t *p = data;
    while (len > 0) {
        int chunk = (len > MAX_CHUNK) ? MAX_CHUNK : (int)len;
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = p,
            .user = (void *)1,       /* DC=1 */
        };
        esp_err_t err = spi_device_polling_transmit(s_spi, &t);
        if (err != ESP_OK) return err;
        p += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static esp_err_t lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;
    uint8_t ca[4] = {x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t pa[4] = {y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF};
    lcd_cmd_data(CMD_CASET, ca, 4);
    lcd_cmd_data(CMD_PASET, pa, 4);
    lcd_cmd(CMD_RAMWR);
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t bsp_display_init(void)
{
    esp_err_t err;

    /* Allocate work buffer in internal SRAM */
    s_line_buf = heap_caps_malloc(LCD_LINE_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_line_buf) {
        ESP_LOGE(TAG, "Line buffer alloc failed (%d bytes)", LCD_LINE_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    /* Configure DC pin as GPIO output */
    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << BSP_LCD_DC,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc_cfg);

    /* Initialize SPI bus — NO DMA (pure FIFO/polling mode) */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BSP_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,   /* No DMA, no descriptor chain */
    };
    err = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Add LCD as SPI device */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000,  /* 40 MHz */
        .mode = 0,
        .spics_io_num = BSP_LCD_CS,
        .queue_size = 1,
        .pre_cb = lcd_pre_cb,
    };
    err = spi_bus_add_device(BSP_LCD_SPI_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ILI9342C full initialization sequence (M5Stack Core2) */
    lcd_cmd(CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* ---- Power & timing control ---- */
    { uint8_t d[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
      lcd_cmd_data(0xCB, d, sizeof(d)); }
    { uint8_t d[] = {0x00, 0xC1, 0x30};
      lcd_cmd_data(0xCF, d, sizeof(d)); }
    { uint8_t d[] = {0x85, 0x00, 0x78};
      lcd_cmd_data(0xE8, d, sizeof(d)); }
    { uint8_t d[] = {0x00, 0x00};
      lcd_cmd_data(0xEA, d, sizeof(d)); }
    { uint8_t d[] = {0x64, 0x03, 0x12, 0x81};
      lcd_cmd_data(0xED, d, sizeof(d)); }
    { uint8_t d[] = {0x20};
      lcd_cmd_data(0xF7, d, sizeof(d)); }

    { uint8_t d[] = {0x23}; lcd_cmd_data(0xC0, d, 1); }   /* Power Control 1 */
    { uint8_t d[] = {0x10}; lcd_cmd_data(0xC1, d, 1); }   /* Power Control 2 */
    { uint8_t d[] = {0x3E, 0x28}; lcd_cmd_data(0xC5, d, 2); } /* VCOM 1 */
    { uint8_t d[] = {0x86}; lcd_cmd_data(0xC7, d, 1); }   /* VCOM 2 */

    /* ---- Pixel & orientation ---- */
    { uint8_t d[] = {0x08}; lcd_cmd_data(CMD_MADCTL, d, 1); }
    { uint8_t d[] = {0x55}; lcd_cmd_data(CMD_PIXFMT, d, 1); }

    { uint8_t d[] = {0x00, 0x18}; lcd_cmd_data(0xB1, d, 2); } /* Frame Rate */
    { uint8_t d[] = {0x08, 0x82, 0x27}; lcd_cmd_data(0xB6, d, 3); } /* Display Function */

    /* ---- Gamma ---- */
    { uint8_t d[] = {0x00}; lcd_cmd_data(0xF2, d, 1); }
    { uint8_t d[] = {0x01}; lcd_cmd_data(0x26, d, 1); }
    { uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,
                     0x4E,0xF1,0x37,0x07,0x10,0x03,
                     0x0E,0x09,0x00};
      lcd_cmd_data(0xE0, d, sizeof(d)); }
    { uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,
                     0x31,0xC1,0x48,0x08,0x0F,0x0C,
                     0x31,0x36,0x0F};
      lcd_cmd_data(0xE1, d, sizeof(d)); }

    lcd_cmd(CMD_INVON);
    lcd_cmd(CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Clear screen to black */
    bsp_display_fill(0, 0, BSP_LCD_W, BSP_LCD_H, BSP_COLOR_BLACK);

    ESP_LOGI(TAG, "ILI9342C initialized (%dx%d, 40MHz SPI, no DMA)", BSP_LCD_W, BSP_LCD_H);
    return ESP_OK;
}

esp_err_t bsp_display_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_spi || !s_line_buf) return ESP_ERR_INVALID_STATE;
    if (x + w > BSP_LCD_W || y + h > BSP_LCD_H) return ESP_ERR_INVALID_ARG;

    lcd_set_window(x, y, w, h);

    /* Fill work buffer with color */
    uint16_t *buf16 = (uint16_t *)s_line_buf;
    int buf_pixels = LCD_LINE_BUF_SIZE / 2;
    int fill_pixels = (buf_pixels < w * LCD_LINE_BUF_LINES) ? buf_pixels : w * LCD_LINE_BUF_LINES;
    for (int i = 0; i < fill_pixels; i++) {
        buf16[i] = color;
    }

    /* Send in chunks */
    int total_pixels = w * h;
    while (total_pixels > 0) {
        int chunk = (total_pixels < fill_pixels) ? total_pixels : fill_pixels;
        lcd_send_pixels(s_line_buf, chunk * 2);
        total_pixels -= chunk;
    }

    return ESP_OK;
}

esp_err_t bsp_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (!s_spi) return ESP_ERR_INVALID_STATE;
    if (x + w > BSP_LCD_W || y + h > BSP_LCD_H) return ESP_ERR_INVALID_ARG;

    lcd_set_window(x, y, w, h);

    /* Send pixel data directly via inline transfers */
    lcd_send_pixels((const uint8_t *)data, w * h * 2);

    return ESP_OK;
}
