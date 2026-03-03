idf_component_register(
    SRCS "src/agent.c" "src/agent_context.c"
    INCLUDE_DIRS "include"
    REQUIRES llm memory config json tools
    PRIV_REQUIRES esp_event esp_timer
)
