/**
 * @file motor.c
 * @brief Implementación del driver del motor NEMA 17 42BYGHW609.
 * @see motor.h para la descripción completa.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "motor.h"

// GPIOs
#define PIN_NSLEEP  GPIO_NUM_9   // (HIGH = driver activo)
#define PIN_AEN     GPIO_NUM_10  // (HIGH = bobina A habilitada)
#define PIN_APH     GPIO_NUM_11  // (fase bobina A)
#define PIN_BEN     GPIO_NUM_12  // (HIGH = bobina B habilitada)
#define PIN_BPH     GPIO_NUM_13  // (fase bobina B)
#define PIN_NFAULT  GPIO_NUM_14  // (entrada, LOW = fallo activo, open-drain en el chip)

// ─── Tabla de pasos full-step ─────────────────────────────────────────────────
/**
 * @brief Tabla de pasos full-step hacia adelante (HORARIO). Formato de cada fila: {AEN, APH, BEN, BPH}
 * @details DRV8424EPWPR modo PH/EN (Table 7-2 del datasheet):
 * 
 *   xEN=1, xPH=1 → corriente forward  (XOUT1 → XOUT2)
 * 
 *   xEN=1, xPH=0 → corriente reverse  (XOUT2 → XOUT1)
 */
static const uint8_t stepFWD[4][4] = {
    {1, 1, 1, 0},  // A=fwd, B=rev
    {1, 0, 1, 0},  // A=rev, B=rev
    {1, 0, 1, 1},  // A=rev, B=fwd
    {1, 1, 1, 1},  // A=fwd, B=fwd
};
/**
 * @brief Tabla de pasos full-step hacia atrás. (ANTIHORARIO)
 */
static const uint8_t stepBWD[4][4] = {
    {1, 1, 1, 1},
    {1, 0, 1, 1},
    {1, 0, 1, 0},
    {1, 1, 1, 0},
};

/**
 * @brief Índice actual en la secuencia de pasos.
 * @details Persiste entre llamadas a motorStep() para no perder la posición de la bobina al encadenar movimientos consecutivos.
 */
static uint8_t s_stepIndex = 0;

// ─── Privado ──────────────────────────────────────────────────────────────────
/**
 * @brief Aplica una fila de la tabla de pasos a los GPIOs del driver.
 * @param table Puntero a la tabla de pasos (stepFWD o stepBWD).
 * @param idx   Índice de fila dentro de la tabla (0-3).
 */
static void _applyStep(const uint8_t table[4][4], uint8_t idx) {
    gpio_set_level(PIN_AEN, table[idx][0]);
    gpio_set_level(PIN_APH, table[idx][1]);
    gpio_set_level(PIN_BEN, table[idx][2]);
    gpio_set_level(PIN_BPH, table[idx][3]);
}

// ─── Públicas ─────────────────────────────────────────────────────────────────
void motorInit(void) {
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_NSLEEP) | (1ULL << PIN_AEN) |
                        (1ULL << PIN_APH)    | (1ULL << PIN_BEN) |
                        (1ULL << PIN_BPH),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    // nFAULT: se activa el pull-up interno del ESP32-S3 como capa de seguridad
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_NFAULT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    // Iniciacion en sleep: corta corriente a bobinas, reduce consumo
    gpio_set_level(PIN_NSLEEP, 0);
    gpio_set_level(PIN_AEN, 0);
    gpio_set_level(PIN_BEN, 0);

    printf("[motor] Init OK. Driver en sleep.\n");
}

void motorEnable(void) {
    // Según datasheet DRV8424EPWPR: necesita ~1ms para salir de sleep antes de que los puentes estén operativos. Damos 2ms de margen.
    gpio_set_level(PIN_NSLEEP, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
    printf("[motor] Driver activo.\n");
}

void motorDisable(void) {
    // Cortamos bobinas antes del sleep para no quedar en un estado de corriente medio aplicada con el chip durmiendo.
    gpio_set_level(PIN_AEN, 0);
    gpio_set_level(PIN_BEN, 0);
    gpio_set_level(PIN_NSLEEP, 0);
    printf("[motor] Driver en sleep.\n");
}

motor_status_t motorGetFault(void) {
    if (gpio_get_level(PIN_NFAULT) == 0) {
        return MOTOR_FAULT;
    } else {
        return MOTOR_OK;
    }
}

motor_status_t motorStep(uint32_t steps, motor_dir_t dir, uint32_t delay_ms) {
    const uint8_t (*table)[4];
    if (dir == MOTOR_DIR_FORWARD) {
        table = stepFWD;
    } else {
        table = stepBWD;
    }

    for (uint32_t i = 0; i < steps; i++) {
        _applyStep(table, s_stepIndex);
        s_stepIndex = (s_stepIndex + 1) % 4;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return MOTOR_OK;
}

/* motorStep TENIENDO EN CUENTA NFAULT
motor_status_t motorStep(uint32_t steps, motor_dir_t dir, uint32_t delay_ms)
{
    const uint8_t (*table)[4];
    if (dir == MOTOR_DIR_FORWARD) {
        table = stepFWD;
    } else {
        table = stepBWD;
    }

    // Ignoramos nFAULT durante los primeros 50ms tras arrancar el movimiento.
    // El DRV8424EPWPR necesita tiempo de estabilización después de salir
    // de sleep — durante ese periodo nFAULT puede dar falsos positivos.
    TickType_t arranque = xTaskGetTickCount();
    uint32_t ignorar_ms = 50;

    for (uint32_t i = 0; i < steps; i++) {
        bool estabilizado = (xTaskGetTickCount() - arranque) >= pdMS_TO_TICKS(ignorar_ms);

        if (estabilizado && motorGetFault() == MOTOR_FAULT) {
            gpio_set_level(PIN_AEN, 0);
            gpio_set_level(PIN_BEN, 0);
            printf("[motor] FAULT en paso %lu. Abortando.\n", i);
            return MOTOR_FAULT;
        }

        _applyStep(table, s_stepIndex);
        s_stepIndex = (s_stepIndex + 1) % 4;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return MOTOR_OK;
}
*/