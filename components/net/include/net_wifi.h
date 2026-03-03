#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t net_wifi_init(const char *ssid, const char *password);
bool net_wifi_is_connected(void);
esp_err_t net_wifi_wait_connected(TickType_t timeout);
void net_wifi_get_ip(char *buf, size_t size);
