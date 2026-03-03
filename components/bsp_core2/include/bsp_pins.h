#pragma once

/**
 * M5Stack Core2 hardware pin definitions.
 * These are fixed by the Core2 PCB design and should not be changed.
 */

/* Internal I2C bus (AXP192 + Touch + IMU) */
#define BSP_I2C_SDA         21
#define BSP_I2C_SCL         22
#define BSP_I2C_FREQ_HZ     400000

/* AXP192 Power Management IC */
#define BSP_AXP192_ADDR     0x34

/* ILI9342C SPI LCD */
#define BSP_LCD_SPI_HOST    SPI3_HOST
#define BSP_LCD_MOSI        23
#define BSP_LCD_SCLK        18
#define BSP_LCD_CS           5
#define BSP_LCD_DC          15

/* FT6336U Capacitive Touch */
#define BSP_TOUCH_ADDR      0x38
#define BSP_TOUCH_INT       39

/* I2S Audio */
#define BSP_I2S_BCLK        12
#define BSP_I2S_LRCK         0
#define BSP_I2S_DATA_OUT     2   /* Speaker (NS4168) */
#define BSP_I2S_DATA_IN     34   /* Mic (SPM1423 PDM) */
