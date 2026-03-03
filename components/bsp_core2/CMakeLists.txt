idf_component_register(
    SRCS "src/bsp_core2.c" "src/bsp_power.c" "src/bsp_display.c" "src/bsp_touch.c" "src/bsp_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES esp_timer
)
