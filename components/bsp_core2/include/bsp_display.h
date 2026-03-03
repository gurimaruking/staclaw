#pragma once

#include "esp_err.h"
#include <stdint.h>

#define BSP_LCD_W  320
#define BSP_LCD_H  240

/* Pre-computed RGB565 colors (byte-swapped for SPI) */
#define BSP_COLOR_BLACK   0x0000
#define BSP_COLOR_WHITE   0xFFFF
#define BSP_COLOR_RED     0x00F8
#define BSP_COLOR_GREEN   0xE007
#define BSP_COLOR_BLUE    0x1F00
#define BSP_COLOR_GRAY    0x1084
#define BSP_COLOR_DARKGRAY 0x6B4A

/**
 * Initialize ILI9342C SPI LCD.
 */
esp_err_t bsp_display_init(void);

/**
 * Fill a rectangle with a single color.
 */
esp_err_t bsp_display_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * Blit pixel data (RGB565, byte-swapped) to a region.
 * Data must be in DMA-capable memory for best performance.
 */
esp_err_t bsp_display_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);

/**
 * Convert RGB888 to byte-swapped RGB565 for SPI.
 */
static inline uint16_t bsp_display_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);  /* byte-swap for SPI MSB-first */
}
