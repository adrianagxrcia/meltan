/**
 * @file testMotor.c
 * @brief Test de validación del hardware del motor NEMA 17 42BYGHW609.
 *
 * Al arrancar pregunta por el monitor serie qué prueba ejecutar:
 *   1 → Giro hacia adelante N pasos
 *   2 → Giro hacia atrás N pasos
 *   3 → Ciclo continuo adelante/atrás (hasta reset)
 *
 * Permite verificar: dirección, velocidad, detección de fallo nFAULT y correcta secuencia de las bobinas.
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motor.h"

#define PASOS_VUELTA   600 // Número de pasos por prueba: 200 pasos = 1 vuelta completa (1.8°/paso)
#define DELAY_PASO_MS  10  // Delay entre pasos en ms. Bajar si el motor responde bien, subir si vibra sin girar.

static char _leer_opcion(void) {
    int c;
    do { c = getchar();
    } while (c == '\n' || c == '\r' || c == EOF);
    return (char)c;
}

void app_main(void) {
    printf("\n=== TEST MOTOR ===\n");
    printf("NEMA 17 42BYGHW609 + DRV8424EPWPR\n");
    printf("Selecciona prueba:\n");
    printf("  1 → Adelante  3 vueltas (%d pasos)\n", PASOS_VUELTA);
    printf("  2 → Atras     3 vueltas (%d pasos)\n", PASOS_VUELTA);
    printf("  3 → Ciclo continuo adelante/atras\n");
    printf("> ");

    char opcion = _leer_opcion();
    printf("%c\n", opcion);

    motorInit();
    motorEnable();

    if (opcion == '1') {
        printf("[test] Girando adelante %d pasos...\n", PASOS_VUELTA);
        motor_status_t resultado = motorStep(PASOS_VUELTA, MOTOR_DIR_FORWARD, DELAY_PASO_MS);
        if (resultado == MOTOR_OK) {
            printf("[test] Movimiento completado OK.\n");
        } else {
            printf("[test] Movimiento abortado por FAULT.\n");
        }
        motorDisable();

    } else if (opcion == '2') {
        printf("[test] Girando atras %d pasos...\n", PASOS_VUELTA);
        motor_status_t resultado = motorStep(PASOS_VUELTA, MOTOR_DIR_BACKWARD, DELAY_PASO_MS);
        if (resultado == MOTOR_OK) {
            printf("[test] Movimiento completado OK.\n");
        } else {
            printf("[test] Movimiento abortado por FAULT.\n");
        }
        motorDisable();

    } else {
        // Opción 3 o cualquier otra tecla: ciclo continuo
        printf("[test] Ciclo continuo. Resetea para parar.\n");
        while (1) {
            printf("[test] Adelante...\n");
            motor_status_t res = motorStep(PASOS_VUELTA, MOTOR_DIR_FORWARD, DELAY_PASO_MS);

            if (res == MOTOR_FAULT) {
                printf("[test] FAULT detectado. Esperando recuperacion...\n");
                // Hay que esperar a que nFAULT se limpie antes de continuar
                // En un fallo térmico el chip se recupera solo al enfriarse
                while (motorGetFault() == MOTOR_FAULT) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                printf("[test] Fallo limpiado. Reanudando.\n");
                motorEnable();
                continue;
            }
            // Pausa entre adelante y atrás para que el movimiento sea visible
            vTaskDelay(pdMS_TO_TICKS(500));

            printf("[test] Atras...\n");
            res = motorStep(PASOS_VUELTA, MOTOR_DIR_BACKWARD, DELAY_PASO_MS);

            if (res == MOTOR_FAULT) {
                printf("[test] FAULT detectado. Esperando recuperacion...\n");
                while (motorGetFault() == MOTOR_FAULT) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                printf("[test] Fallo limpiado. Reanudando.\n");
                motorEnable();
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    // Bucle final para opciones 1 y 2: no hacemos nada, el motor ya está en sleep
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}