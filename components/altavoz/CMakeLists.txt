idf_component_register(
    SRCS "altavoz.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)

# El modulo usa funciones matematicas (sinf) de la libreria estandar de C
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
