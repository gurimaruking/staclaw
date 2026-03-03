idf_component_register(
    SRCS "src/memory_store.c" "src/memory_history.c"
    INCLUDE_DIRS "include"
    REQUIRES spiffs json llm
)
