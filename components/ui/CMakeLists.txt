idf_component_register(
    SRCS "src/ui_manager.c" "src/ui_chat.c" "src/ui_status.c" "src/ui_status_screen.c" "src/ui_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp_core2 llm
    PRIV_REQUIRES freertos esp_timer
)
