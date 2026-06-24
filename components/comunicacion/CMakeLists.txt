idf_component_register(
    SRCS "comunicacion.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi      # incluye esp_now.h, no existe como componente separado
        esp_event     # event loop por defecto que necesita el driver WiFi
        nvs_flash     # ESP-NOW y NimBLE lo usan para almacenar datos
        bt            # pila NimBLE (BLE)
)
