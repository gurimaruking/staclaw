idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES config net llm memory agent channel tools bsp_core2 ui voice cron
    PRIV_REQUIRES esp_event nvs_flash spiffs esp_netif
)
