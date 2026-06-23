/**
 * @file main.c
 * @brief Test de validación del hardware del altavoz (MAX98357AETE+T + 8Ω).
 *
 * Al arrancar pregunta por el monitor serie qué prueba ejecutar:
 *   1 → Secuencia de tonos PWM (onda cuadrada, sin I2S)
 *   2 → Seno por I2S (test del protocolo completo)
 *
 * La opción 1 verifica que el amplificador y el altavoz responden.
 * La opción 2 verifica que la cadena I2S funciona end-to-end.
 *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "altavoz.h"

static char _leer_opcion(void) {
    int c;
    do { 
        c = getchar();
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(10)); }
    } while (c == '\n' || c == '\r' || c == EOF);
    return (char)c;
}

void app_main(void) {
    printf("\n=== TEST ALTAVOZ ===\n");
    printf("MAX98357AETE+T + altavoz 8ohm\n");
    printf("Selecciona prueba:\n");
    printf("  1 → Secuencia de tonos PWM (onda cuadrada)\n");
    printf("  2 → Seno 1kHz por I2S\n");
    printf("> ");

    char opcion = _leer_opcion();
    printf("%c\n", opcion);

    altavozInit();
    altavozEnable(1);

    if (opcion == '2') {
        // Modo I2S: genera un seno puro a 1kHz.
        // Si suena limpio y sin distorsión, el protocolo I2S está correcto.
        // Si no suena nada pero el PWM sí funcionaba, el problema es de
        // configuración I2S (pines, sample rate, o formato de slot).
        printf("[test] Seno 1kHz por I2S. Resetea para parar.\n");
        while (1) {
            altavozEnable(1);
            altavozToneI2S(1000, 2000);
            vTaskDelay(pdMS_TO_TICKS(300)); // pausa entre tonos
        }

    } else {
        // Secuencia PWM.
        // Recorre frecuencias representativas para verificar respuesta
        // del altavoz en distintos rangos (grave, medio, agudo).
        printf("[test] Secuencia PWM. Resetea para parar.\n");
        while (1) {
            printf("[test] 200 Hz (grave)\n");
            altavozTonePWM(200, 800);
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("[test] 500 Hz\n");
            altavozTonePWM(500, 800);
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("[test] 1000 Hz (referencia)\n");
            altavozTonePWM(1000, 800);
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("[test] 2000 Hz\n");
            altavozTonePWM(2000, 800);
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("[test] 4000 Hz (agudo)\n");
            altavozTonePWM(4000, 800);
            vTaskDelay(pdMS_TO_TICKS(200));

            printf("[test] Silencio\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}