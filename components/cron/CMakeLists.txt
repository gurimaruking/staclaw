idf_component_register(
    SRCS "src/cron_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES esp_timer
)
