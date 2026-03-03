idf_component_register(
    SRCS "src/tool_registry.c" "src/tool_sysinfo.c" "src/tool_memory.c" "src/tool_gpio.c" "src/tool_cron.c"
    INCLUDE_DIRS "include"
    REQUIRES json memory cron
    PRIV_REQUIRES driver esp_timer
)
