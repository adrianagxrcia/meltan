# meltanMain — Firmware del nodo central

Firmware del nodo central del sistema de control de una caja de condicionamiento
operante, desarrollado como Trabajo de Fin de Grado en la ETSIT (Universidad
Politécnica de Madrid), dentro del proyecto VISNE del laboratorio B105.

El nodo central se ejecuta sobre un **ESP32-S3-WROOM-1-N8R2**. Se comunica con una
Raspberry Pi 4 mediante **BLE** (NimBLE) y con los módulos periféricos ESP32-C3
mediante **ESP-NOW**.

## Estructura del repositorio

```
meltanMain/
├── CMakeLists.txt        Proyecto raíz del programa de control
├── sdkconfig.defaults    Configuración base (NimBLE, consola)
├── components/           Módulos compartidos (un componente por integrado)
│   ├── ledRGB/           ledRGB.c + include/ledRGB.h + CMakeLists.txt
│   ├── motor/
│   ├── palanca/
│   ├── altavoz/
│   └── comunicacion/     BLE (NimBLE) y ESP-NOW
├── main/                 Programa de control (usa los componentes)
│   ├── main.c
│   └── CMakeLists.txt
└── tests/                Un proyecto ESP-IDF independiente por test
    ├── testRGB/          testMotor/, testPalanca/, testAltavoz/ ...
    ├── testSistema/      Prueba conjunta de todo el sistema
    └── queMAC/           Utilidad: lee la dirección MAC del módulo por serie
```

Los módulos viven en `components/`, de modo que el programa de control y los tests
usan el mismo código sin duplicarlo. Cada test es un proyecto independiente que
toma los módulos de `components/` mediante `EXTRA_COMPONENT_DIRS` en su
`CMakeLists.txt` raíz.

## Requisitos

- ESP-IDF v5.3.5 (target `esp32s3`)
- Toolchain de Espressif instalado y la variable `IDF_PATH` definida

## Compilación y carga del programa de control

Desde la raíz del proyecto:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PUERTO flash monitor
```

## Ejecución de un test

Cada test se compila y se carga por separado. Por ejemplo, para el test del motor:

```bash
cd tests/testMotor
idf.py build flash monitor
```

La configuración de NimBLE y de la consola se aplica automáticamente desde el
`sdkconfig.defaults` de cada proyecto. El archivo `sdkconfig` no se versiona
porque ESP-IDF lo genera en la primera compilación.

## Documentación

Las cabeceras de los módulos (`components/<modulo>/include/`) están documentadas
con Doxygen para facilitar la comprensión y la reutilización del código.
