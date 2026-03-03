#include "tool_gpio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tool_gpio";

// External port GPIO pins allowed for agent tool access
// Port B: 26 (DAC/GPIO), 36 (ADC, input-only)
// Port C: 14 (UART TX), 13 (UART RX)
// Port A: 32 (I2C SDA), 33 (I2C SCL)
static const int ALLOWED_PINS[] = {26, 36, 14, 13, 32, 33};
#define ALLOWED_PIN_COUNT  6

static bool is_pin_allowed(int pin)
{
    for (int i = 0; i < ALLOWED_PIN_COUNT; i++) {
        if (ALLOWED_PINS[i] == pin) return true;
    }
    return false;
}

static bool is_pin_writable(int pin)
{
    // GPIO36 is input-only on ESP32
    return is_pin_allowed(pin) && pin != 36;
}

static esp_err_t gpio_read_execute(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\": \"Invalid JSON arguments\"}");
        return ESP_OK;
    }

    cJSON *pin_item = cJSON_GetObjectItem(args, "pin");
    if (!pin_item || !cJSON_IsNumber(pin_item)) {
        cJSON_Delete(args);
        snprintf(result, result_size, "{\"error\": \"Missing or invalid pin number\"}");
        return ESP_OK;
    }

    int pin = pin_item->valueint;
    cJSON_Delete(args);

    if (!is_pin_allowed(pin)) {
        snprintf(result, result_size,
                 "{\"error\": \"Pin %d not allowed. Allowed: 26, 36, 14, 13, 32, 33\"}", pin);
        return ESP_OK;
    }

    // Configure as input if not already
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    int level = gpio_get_level((gpio_num_t)pin);

    snprintf(result, result_size, "{\"pin\": %d, \"value\": %d}", pin, level);
    ESP_LOGI(TAG, "GPIO read: pin=%d value=%d", pin, level);
    return ESP_OK;
}

static esp_err_t gpio_write_execute(const char *args_json, char *result, size_t result_size)
{
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(result, result_size, "{\"error\": \"Invalid JSON arguments\"}");
        return ESP_OK;
    }

    cJSON *pin_item = cJSON_GetObjectItem(args, "pin");
    cJSON *val_item = cJSON_GetObjectItem(args, "value");
    if (!pin_item || !cJSON_IsNumber(pin_item) ||
        !val_item || !cJSON_IsNumber(val_item)) {
        cJSON_Delete(args);
        snprintf(result, result_size, "{\"error\": \"Missing pin or value parameter\"}");
        return ESP_OK;
    }

    int pin = pin_item->valueint;
    int value = val_item->valueint;
    cJSON_Delete(args);

    if (!is_pin_writable(pin)) {
        snprintf(result, result_size,
                 "{\"error\": \"Pin %d not writable. Writable pins: 26, 14, 13, 32, 33\"}", pin);
        return ESP_OK;
    }

    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);

    snprintf(result, result_size,
             "{\"pin\": %d, \"value\": %d, \"success\": true}", pin, value ? 1 : 0);
    ESP_LOGI(TAG, "GPIO write: pin=%d value=%d", pin, value ? 1 : 0);
    return ESP_OK;
}

static const char READ_SCHEMA[] =
    "{\"type\":\"object\","
    "\"properties\":{\"pin\":{\"type\":\"integer\","
    "\"description\":\"GPIO pin number. Allowed: 26, 36, 14, 13, 32, 33\"}},"
    "\"required\":[\"pin\"]}";

static const char WRITE_SCHEMA[] =
    "{\"type\":\"object\","
    "\"properties\":{\"pin\":{\"type\":\"integer\","
    "\"description\":\"GPIO pin number. Writable: 26, 14, 13, 32, 33\"},"
    "\"value\":{\"type\":\"integer\",\"description\":\"0=LOW, 1=HIGH\"}},"
    "\"required\":[\"pin\",\"value\"]}";

esp_err_t tool_gpio_register(tool_registry_t *reg)
{
    tool_def_t read_tool = {
        .name = "gpio_read",
        .description = "Read the digital value of a GPIO pin. Allowed pins: 26, 36, 14, 13, 32, 33 (external ports).",
        .input_schema_json = READ_SCHEMA,
        .execute = gpio_read_execute,
    };
    esp_err_t err = tool_registry_add(reg, &read_tool);
    if (err != ESP_OK) return err;

    tool_def_t write_tool = {
        .name = "gpio_write",
        .description = "Set a GPIO pin HIGH or LOW. Writable pins: 26, 14, 13, 32, 33 (pin 36 is input-only).",
        .input_schema_json = WRITE_SCHEMA,
        .execute = gpio_write_execute,
    };
    return tool_registry_add(reg, &write_tool);
}
