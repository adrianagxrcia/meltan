#pragma once
#include <stdbool.h>

/**
 * @file palanca.h
 * @brief Driver de lectura de la palanca (lever) del sistema MELTAN.
 * @details La palanca está conectada al conector J32 (B2B-PH-K-S) del esquemático Peripheral.SchDoc. La señal entra por GPIO2 del ESP32-S3-WROOM-1-N8R2.
 * Lógica eléctrica: reposo → LOW, presionada → HIGH.
 * El módulo soporta dos modos de uso:
 * - **Polling**: llamar periódicamente a @ref palancaPressed().
 * - **Interrupción**: registrar un callback en @ref palancaInit() que se invoca automáticamente en cada cambio de estado.
 * @author agarcia
 * @date 2026
*/

/**
 * @brief Tipo del callback invocado cuando la palanca cambia de estado.
 * @details La función se ejecuta en contexto de ISR. **No** usar vTaskDelay() ni printf() dentro, **SOLO** usar xQueueSendFromISR() para pasar el evento a una tarea normal 
 * que se encargue de procesarlo e imprimirlo.
 * @param pressed true si la palanca acaba de presionarse, false si se liberó.
 */
typedef void (*palanca_cb_t)(bool pressed);

/**
 * @brief Inicializa el GPIO de la palanca.
 * @details GPIO2 como entrada con pull-down interno. Si se proporciona un callback, instala el servicio de interrupciones de GPIO y registra el handler para detección en ambos flancos (ANYEDGE).
 * @param callback Función a invocar en cada cambio de estado, o NULL para usar únicamente el modo polling.
 * @note Llamar una sola vez desde app_main() antes de cualquier lectura.
 */
void palancaInit(palanca_cb_t callback);

/**
 * @brief Lee el estado actual de la palanca.
 * @details Útil para polling periódico sin necesidad de callback.
 * @return true  si la palanca está presionada en este instante.
 * @return false si la palanca está en reposo.
 */
bool palancaPressed(void);