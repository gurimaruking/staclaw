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

/* DMA transfer buffer: 16 lines at a time */
#define LCD_LINE_BUF_LINES  16
#define LCD_LINE_BUF_SIZE   (BSP_LCD_W * LCD_LINE_BUF_LINES * 2)

static spi_device_handle_t s_spi = NULL;
static uint8_t *s_dma_buf = NULL;

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
            .tx_buffer = data,
            .user = (void *)1,   /* DC=1 for data */
        };
        /* Short data can use inline tx_data */
        if (len <= 4) {
            t.flags = SPI_TRANS_USE_TXDATA;
            memcpy(t.tx_data, data, len);
            t.tx_buffer = NULL;
        }
        return spi_device_polling_transmit(s_spi, &t);
    }
    return ESP_OK;
}

static esp_err_t lcd_send_data(const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void *)1,       /* DC=1 */
    };
    return spi_device_transmit(s_spi, &t);  /* DMA for large transfers */
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

    /* Allocate DMA buffer */
    s_dma_buf = heap_caps_malloc(LCD_LINE_BUF_SIZE, MALLOC_CAP_DMA);
    if (!s_dma_buf) {
        ESP_LOGE(TAG, "DMA buffer alloc failed (%d bytes)", LCD_LINE_BUF_SIZE);
        return ESP_ERR_NO_MEM;
    }

    /* Configure DC pin as GPIO output */
    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << BSP_LCD_DC,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc_cfg);

    /* Initialize SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BSP_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_LINE_BUF_SIZE,
    };
    err = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Add LCD as SPI device */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000,  /* 40 MHz */
        .mode = 0,
        .spics_io_num = BSP_LCD_CS,
        .queue_size = 7,
        .pre_cb = lcd_pre_cb,
    };
    err = spi_bus_add_device(BSP_LCD_SPI_HOST, &dev_cfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ILI9342C initialization sequence */
    lcd_cmd(CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Display inversion ON (required for ILI9342C correct colors) */
    lcd_cmd(CMD_INVON);

    /* Pixel format: 16-bit RGB565 */
    uint8_t pixfmt = 0x55;
    lcd_cmd_data(CMD_PIXFMT, &pixfmt, 1);

    /* Memory access control: landscape, RGB order */
    /* MY=0, MX=1, MV=1, ML=0, BGR=1, MH=0 = 0x68 for Core2 landscape */
    uint8_t madctl = 0x08;
    lcd_cmd_data(CMD_MADCTL, &madctl, 1);

    lcd_cmd(CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Clear screen to black */
    bsp_display_fill(0, 0, BSP_LCD_W, BSP_LCD_H, BSP_COLOR_BLACK);

    ESP_LOGI(TAG, "ILI9342C initialized (%dx%d, 40MHz SPI)", BSP_LCD_W, BSP_LCD_H);
    return ESP_OK;
}

esp_err_t bsp_display_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_spi || !s_dma_buf) return ESP_ERR_INVALID_STATE;
    if (x + w > BSP_LCD_W || y + h > BSP_LCD_H) return ESP_ERR_INVALID_ARG;

    lcd_set_window(x, y, w, h);

    /* Fill DMA buffer with color */
    uint16_t *buf16 = (uint16_t *)s_dma_buf;
    int buf_pixels = LCD_LINE_BUF_SIZE / 2;
    int fill_pixels = (buf_pixels < w * LCD_LINE_BUF_LINES) ? buf_pixels : w * LCD_LINE_BUF_LINES;
    for (int i = 0; i < fill_pixels; i++) {
        buf16[i] = color;
    }

    /* Send in chunks */
    int total_pixels = w * h;
    while (total_pixels > 0) {
        int chunk = (total_pixels < fill_pixels) ? total_pixels : fill_pixels;
        lcd_send_data(s_dma_buf, chunk * 2);
        total_pixels -= chunk;
    }

    return ESP_OK;
}

esp_err_t bsp_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (!s_spi) return ESP_ERR_INVALID_STATE;
    if (x + w > BSP_LCD_W || y + h > BSP_LCD_H) return ESP_ERR_INVALID_ARG;

    lcd_set_window(x, y, w, h);

    /* Send pixel data in chunks via DMA buffer */
    int total_bytes = w * h * 2;
    const uint8_t *src = (const uint8_t *)data;
    while (total_bytes > 0) {
        int chunk = (total_bytes < LCD_LINE_BUF_SIZE) ? total_bytes : LCD_LINE_BUF_SIZE;
        memcpy(s_dma_buf, src, chunk);
        lcd_send_data(s_dma_buf, chunk);
        src += chunk;
        total_bytes -= chunk;
    }

    return ESP_OK;
}
