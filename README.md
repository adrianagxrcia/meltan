# meltan — Firmware del nodo central

Firmware del nodo central del sistema de control de una caja de condicionamiento
operante, desarrollado como Trabajo de Fin de Grado en la ETSIT (Universidad
Politécnica de Madrid), dentro del proyecto VISNE del laboratorio B105.

El nodo central se ejecuta sobre un **ESP32-S3-WROOM-1-N8R2**. Se comunica con una
Raspberry Pi 4 mediante **BLE** (NimBLE) y con los módulos periféricos ESP32-C3
mediante **ESP-NOW**.

## Documentación del código

**https://adrianagxrcia.github.io/meltan/**

## Estructura del repositorio

```
meltan/
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

## El formato de comando: `meltan_cmd_t`

Toda la comunicación del nodo central usa una misma estructura, tanto hacia la
Raspberry Pi por BLE como hacia los módulos periféricos por ESP-NOW. Está definida
en `components/comunicacion/include/comunicacion.h`:

```c
typedef struct {
    uint8_t  cmd;           // Código de comando
    uint8_t  payload[16];   // Datos adicionales
    uint8_t  payload_len;   // Longitud real de payload
} meltan_cmd_t;
```

La estructura ocupa 18 bytes y cabe de sobra en un paquete ESP-NOW (máximo 250
bytes). El campo `cmd` indica la acción; `payload` lleva los datos que esa acción
necesite, y `payload_len` dice cuántos bytes de `payload` son válidos. Un comando
sin datos usa `payload_len = 0` e ignora el contenido de `payload`.

### Ejemplo 1: comando sin datos

En la prueba del sistema completo, el nodo central pide al módulo de la matriz de
LEDs que muestre el estímulo visual. Es un comando simple, sin datos asociados:

```c
meltan_cmd_t orden = {0};
orden.cmd = CMD_ESTRELLA;   // acción: mostrar el estímulo
orden.payload_len = 0;      // sin datos: payload no se usa

espnow_send_cmd(MODULE_LED_MATRIX, &orden);
```

El receptor solo necesita leer `cmd` para saber qué hacer. El `payload` queda
vacío porque la orden no transporta información adicional.

### Ejemplo 2: comando con datos

Si en el futuro un detector de infrarrojos, por ejemplo, ha de informar de la 
posición donde detecta al animal, esa posición (coordenadas X e Y) viajaría 
dentro del `payload`.

```c
meltan_cmd_t deteccion = {0};
deteccion.cmd = CMD_POSICION_IR;   // acción: posición de un detector IR
deteccion.payload[0] = 45;         // coordenada X
deteccion.payload[1] = 12;         // coordenada Y
deteccion.payload_len = 2;         // dos datos válidos

espnow_send_cmd(MODULE_DETECTOR_IR, &deteccion);
```

El receptor lee `cmd` para saber que es una posición y saca las coordenadas del
`payload`:
 
```c
uint8_t x = cmd->payload[0];
uint8_t y = cmd->payload[1];
```
 
Quedan catorce bytes libres en el `payload` para futuros datos, como un
identificador de sensor o una marca de tiempo, siempre dentro de la misma
estructura.