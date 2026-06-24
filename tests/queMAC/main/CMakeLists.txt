idf_component_register(
    SRCS "queMAC.c"
    REQUIRES
        esp_wifi
        esp_event
        nvs_flash
)
