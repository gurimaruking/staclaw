idf_component_register(
    SRCS "src/voice_vad.c" "src/voice_capture.c" "src/voice_playback.c"
         "src/voice_stt.c" "src/voice_tts.c" "src/voice_pipeline.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp_core2 net json
    PRIV_REQUIRES esp_timer
)
