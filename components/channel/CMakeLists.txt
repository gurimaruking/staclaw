idf_component_register(
    SRCS "src/channel_router.c" "src/channel_telegram.c" "src/channel_touch.c" "src/channel_voice.c"
    INCLUDE_DIRS "include"
    REQUIRES net agent config json ui voice
    PRIV_REQUIRES esp_event esp_timer
)
