idf_component_register(
    SRCS
        "main.c"
        "ledRGB.c"
        "motor.c"
        "palanca.c"
        "altavoz.c"
        "comunicacion.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        freertos
        esp_wifi      # incluye esp_now.h — no existe como componente separado en v5.x
        esp_event     # event loop por defecto que necesita el driver WiFi
        nvs_flash     # ESP-NOW lo necesita internamente para almacenar peers
        esp_system
        esp_driver_ledc
        esp_driver_i2s
        bt
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)