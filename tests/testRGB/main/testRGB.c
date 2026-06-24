/**
 * @file testRGB.c
 * @brief Test de validación del hardware del LED RGB APHF1608LSEEQBDZGKC.
 *
 * Al arrancar pregunta por el monitor serie qué prueba ejecutar:
 *   1 → Ciclo automático por todos los colores
 *   2 → Selección manual de color
 *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ledRGB.h"

#define DELAY_CICLO_MS 1000

static char _leer_opcion(void)
{
    int c;
    do {
        c = getchar();
        if (c == EOF) {
            // No hay dato todavía — cedemos el CPU en vez de quemar ciclos.
            // Esto mantiene satisfecho al watchdog mientras esperamos input.
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } while (c == '\n' || c == '\r' || c == EOF);
    return (char)c;
}

static void _ciclo_colores(uint32_t delay_ms) {
    const led_color_t secuencia[] = {
        LED_COLOR_RED, LED_COLOR_GREEN, LED_COLOR_BLUE,
        LED_COLOR_WHITE, LED_COLOR_YELLOW, LED_COLOR_CYAN,
        LED_COLOR_MAGENTA, LED_COLOR_OFF
    };
    const char *nombres[] = {
        "ROJO", "VERDE", "AZUL", "BLANCO",
        "AMARILLO", "CYAN", "MAGENTA", "APAGADO"
    };

    int n = sizeof(secuencia) / sizeof(secuencia[0]);
    for (int i = 0; i < n; i++) {
        ledRGBset(secuencia[i]);
        printf("[ledRGB] %s\n", nombres[i]);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void) {
    printf("\n=== TEST LED RGB ===\n");
    printf("APHF1608LSEEQBDZGKC — catodo comun\n");
    printf("Selecciona prueba:\n");
    printf("  1 → Ciclo automatico (1s por color)\n");
    printf("  2 → Seleccion manual de color\n");
    printf("> ");

    char opcion = _leer_opcion();
    printf("%c\n", opcion);

    ledRGBinit();

    if (opcion == '2') {
        // ── Modo manual ───────────────────────────────────────────────────────
        // Permite verificar cada canal de forma independiente para detectar canales que no responden o colores incorrectos.
        while (1) {
            printf("\nColor:\n");
            printf("  r → Rojo\n");
            printf("  g → Verde\n");
            printf("  b → Azul\n");
            printf("  w → Blanco\n");
            printf("  y → Amarillo\n");
            printf("  c → Cyan\n");
            printf("  m → Magenta\n");
            printf("  0 → Apagado\n");
            printf("> ");

            char color = _leer_opcion();
            printf("%c\n", color);

            if (color == 'r') {
                ledRGBset(LED_COLOR_RED);
                printf("[ledRGB] ROJO\n");
            } else if (color == 'g') {
                ledRGBset(LED_COLOR_GREEN);
                printf("[ledRGB] VERDE\n");
            } else if (color == 'b') {
                ledRGBset(LED_COLOR_BLUE);
                printf("[ledRGB] AZUL\n");
            } else if (color == 'w') {
                ledRGBset(LED_COLOR_WHITE);
                printf("[ledRGB] BLANCO\n");
            } else if (color == 'y') {
                ledRGBset(LED_COLOR_YELLOW);
                printf("[ledRGB] AMARILLO\n");
            } else if (color == 'c') {
                ledRGBset(LED_COLOR_CYAN);
                printf("[ledRGB] CYAN\n");
            } else if (color == 'm') {
                ledRGBset(LED_COLOR_MAGENTA);
                printf("[ledRGB] MAGENTA\n");
            } else if (color == '0') {
                ledRGBset(LED_COLOR_OFF);
                printf("[ledRGB] APAGADO\n");
            } else {
                printf("[ledRGB] Opcion no reconocida.\n");
            }
        }

    } else {
        // Opción 1 o cualquier otra tecla: ciclo automático continuo
        printf("[test] Ciclo automatico. Resetea para parar.\n");
        while (1) {
            _ciclo_colores(DELAY_CICLO_MS);
        }
    }
}