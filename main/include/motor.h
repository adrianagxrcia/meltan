#pragma once
#include <stdint.h>

/**
 * @file motor.h
 * @brief Driver para el motor NEMA 17 42BYGHW609 via DRV8424EPWPR en modo PH/EN.
 * @details GPIOs utilizados:
 *   - nSLEEP → GPIO9  (HIGH = driver activo)
 *   - AEN    → GPIO10 (HIGH = bobina A habilitada)
 *   - APH    → GPIO11 (fase bobina A)
 *   - BEN    → GPIO12 (HIGH = bobina B habilitada)
 *   - BPH    → GPIO13 (fase bobina B)
 *   - nFAULT → GPIO14 (entrada, LOW = fallo activo, open-drain en el chip)
 * @author agarcia
 * @date 2026
*/

/**
 * @brief Dirección de giro del motor.
 */
typedef enum {
    MOTOR_DIR_FORWARD  = 0,
    MOTOR_DIR_BACKWARD = 1,
} motor_dir_t;

/**
 * @brief Resultado de operación del motor.
 */
typedef enum {
    MOTOR_OK    = 0,
    MOTOR_FAULT = 1,  ///< nFAULT activo durante la operación (overcurrent, thermal, UVLO...)
} motor_status_t;

/**
 * @brief Inicializa los GPIOs del motor y deja el driver en sleep.
 * @details los 5 pines de salida y el pin nFAULT como entrada con pull-up interno. 
 * El driver arranca en sleep para no consumir corriente en las bobinas hasta que se llame a motorEnable().
 * @note Llamar una sola vez desde app_main() antes de cualquier movimiento.
 */
void motorInit(void);

/**
 * @brief Despierta el DRV8424EPWPR.
 * @details nSLEEP en HIGH y espera 2ms para que los puentes internos del driver estén operativos (el datasheet exige mínimo 1ms).
 */
void motorEnable(void);

/**
 * @brief Pone el driver en sleep cortando corriente a las bobinas.
 * @details Desactiva AEN y BEN antes de bajar nSLEEP para evitar un estado
 * indeterminado de corriente con el chip durmiendo.
 */
void motorDisable(void);

/**
 * @brief Mueve el motor un número determinado de pasos.
 * @details El índice de secuencia persiste entre llamadas para no perder la posición de la bobina al encadenar movimientos.
 * @param steps    Número de pasos a ejecutar.
 * @param dir      Dirección de giro (motor_dir_t).
 * @param delay_ms Tiempo en ms entre pasos. A menor valor, más velocidad. Con el NEMA 17 42BYGHW609 empezar con 10ms (100 pasos/s).
 * @return MOTOR_OK    si el movimiento se completó correctamente.
 * @return MOTOR_FAULT si nFAULT se activó durante el movimiento.
 * @note actualmente no estamos mirando el nFault, tengo que ver de donde proviene. Ignorandolo el motor se mueve.
 */
motor_status_t motorStep(uint32_t steps, motor_dir_t dir, uint32_t delay_ms);

/**
 * @brief Lee el estado actual de la señal nFAULT del DRV8424EPWPR. *
 * @return MOTOR_OK    si el driver no reporta fallo.
 * @return MOTOR_FAULT si nFAULT está activo (LOW).
 */motor_status_t motorGetFault(void);