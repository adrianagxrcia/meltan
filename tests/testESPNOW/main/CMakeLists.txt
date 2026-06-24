idf_component_register(
    SRCS "testESPNOW.c"
    REQUIRES
        esp_wifi      # incluye esp_now.h
        esp_event     # event loop que necesita el driver WiFi
        nvs_flash     # ESP-NOW lo usa internamente
)
