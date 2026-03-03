#pragma once

// ---- M5Stack Core2 Pin Definitions ----

// Internal I2C Bus (AXP192, Touch, IMU, RTC)
#define BSP_I2C_INT_SDA     21
#define BSP_I2C_INT_SCL     22
#define BSP_I2C_INT_FREQ    400000

// SPI Display (ILI9342C)
#define BSP_LCD_SPI_HOST    SPI3_HOST
#define BSP_LCD_MOSI        23
#define BSP_LCD_SCLK        18
#define BSP_LCD_CS           5
#define BSP_LCD_DC          15
#define BSP_LCD_RST         -1  // Reset via AXP192
#define BSP_LCD_WIDTH      320
#define BSP_LCD_HEIGHT     240

// Touch (FT6336U on internal I2C)
#define BSP_TOUCH_ADDR      0x38
#define BSP_TOUCH_INT       39

// Power Management (AXP192 on internal I2C)
#define BSP_AXP192_ADDR     0x34

// I2S Audio
#define BSP_I2S_NUM         I2S_NUM_0
#define BSP_I2S_BCLK        12
#define BSP_I2S_LRCK         0
#define BSP_I2S_DATA_OUT     2  // Speaker (NS4168)
#define BSP_I2S_DATA_IN     34  // Mic (SPM1423 PDM)

// External Ports (GPIO available for user tools)
#define BSP_PORT_A_SDA      32  // I2C
#define BSP_PORT_A_SCL      33  // I2C
#define BSP_PORT_B_GPIO1    26  // DAC / GPIO (PWM capable)
#define BSP_PORT_B_GPIO2    36  // ADC / GPIO (INPUT ONLY)
#define BSP_PORT_C_TX       14  // UART TX / GPIO
#define BSP_PORT_C_RX       13  // UART RX / GPIO

// SD Card (optional, shared SPI)
#define BSP_SD_CS            4

// ---- Application Constants ----
#define STACLAW_VERSION     "0.1.0"
#define STACLAW_NAME        "staclaw"

// Agent
#define AGENT_MAX_ITERATIONS      5
#define AGENT_MAX_CONTEXT_TOKENS  16000
#define AGENT_MAX_RESPONSE_TOKENS 2048

// History
#define HISTORY_MAX_TURNS    20

// Tools
#define TOOLS_MAX_COUNT      16

// Cron
#define CRON_MAX_JOBS        16

// Voice
#define VOICE_SAMPLE_RATE    16000
#define VOICE_MAX_RECORD_MS  15000
#define VOICE_SILENCE_MS     1500

// Network
#define HTTP_TIMEOUT_MS      30000
#define TELEGRAM_POLL_TIMEOUT 30

// GPIO Allowlist for tool_gpio (only external port pins)
static const int GPIO_ALLOWED_PINS[] = {26, 36, 14, 13, 32, 33};
#define GPIO_ALLOWED_COUNT   6
