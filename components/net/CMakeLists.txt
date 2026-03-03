idf_component_register(
    SRCS "src/net_wifi.c" "src/net_http.c" "src/net_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_http_client esp_event esp_netif config
    PRIV_REQUIRES mbedtls
)
