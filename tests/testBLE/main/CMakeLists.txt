idf_component_register(
    SRCS "testBLE.c"
    REQUIRES
        bt            # pila NimBLE (BLE)
        nvs_flash     # NimBLE lo usa para almacenar datos
)
