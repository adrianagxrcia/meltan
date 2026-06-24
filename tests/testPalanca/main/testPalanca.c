/**
 * @file testPalanca.c
 * @brief Test de validación del hardware de la palanca (lever).
 * Al arrancar pregunta por el monitor serie qué modo usar:
 *   1 → Polling:  lee el pin cada 50ms en el bucle principal.
 *   2 → ISR:      instala interrupción, el callback imprime el evento. *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "palanca.h"
#include "esp_task_wdt.h"

// Queue para comunicar la ISR con la tarea de impresión.
static QueueHandle_t s_evento_queue = NULL;

// Callback para modo ISR 
static void _cb_palanca(bool pressed) {
    xQueueSendFromISR(s_evento_queue, &pressed, NULL);
}

// Espera un carácter por UART
static char _leer_opcion(void) {
    int c;
    do { c = getchar();                           // bloquea hasta recibir algo
    } while (c == '\n' || c == '\r' || c == EOF); // ignora saltos de línea
    return (char)c;
}

// Punto de entrada
void app_main(void) {
    printf("\n=== TEST PALANCA ===\n");
    printf("Selecciona modo:\n");
    printf("  1 → Polling (lectura cada 50ms)\n");
    printf("  2 → ISR     (interrupcion por flanco)\n");
    printf("> ");

    char opcion = _leer_opcion();
    printf("%c\n", opcion); // eco para confirmar lo que se recibio

    if (opcion == '2') {
        // ── Modo ISR ──────────────────────────────────────────────────────────
        printf("[test] Modo ISR. Presiona la palanca.\n");
        // Creacion de la queue antes de instalar la ISR
        s_evento_queue = xQueueCreate(10, sizeof(bool));
        // palancaInit instala el handler. Cada pulsación dispara _cb_palanca automáticamente.
        palancaInit(_cb_palanca);
        bool pressed;
        while (1) {
            if (xQueueReceive(s_evento_queue, &pressed, portMAX_DELAY)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                xQueueReset(s_evento_queue); // descarta todo lo acumulado durante el rebote
                if (pressed) {
                    printf("[palanca][ISR] PRESIONADA\n");
                } else {
                    printf("[palanca][ISR] LIBERADA\n");
                }
            }
        }
    } else {
        // ── Modo Polling (default) ─────────────────────────────────────────────
        printf("[test] Modo Polling. Presiona la palanca.\n");
        palancaInit(NULL); // NULL = sin ISR
        bool estado_anterior = false;
        while (1) {
            bool estado_actual = palancaPressed();
            if (estado_actual != estado_anterior) {
                // Se espera 20ms y se vuelve a leer para confirmar que el estado es estable y no un rebote mecánico del contacto.
                vTaskDelay(pdMS_TO_TICKS(20));
                bool confirmado = palancaPressed();
                if (confirmado == estado_actual) {
                    if (estado_actual) {
                        printf("[palanca][POLL] PRESIONADA\n");
                    } else {
                        printf("[palanca][POLL] LIBERADA\n");
                    }
                    estado_anterior = estado_actual;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}