idf_component_register(
    SRCS "src/llm_provider.c" "src/llm_claude.c" "src/llm_openai.c"
    INCLUDE_DIRS "include"
    REQUIRES net config json
    PRIV_REQUIRES esp_timer
)
