cmake_minimum_required(VERSION 3.16)

# Reutiliza los modulos comunes de components/ sin duplicar codigo
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Construye solo los componentes que este test necesita.
# Asi se evita compilar modulos no usados (p. ej. comunicacion y su pila BLE).
set(COMPONENTS main ledRGB)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(testRGB)
