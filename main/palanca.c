/**
 * @file palanca.c
 * @brief Implementación del driver de la palanca del sistema MELTAN.
 * @see palanca.h para la descripción completa.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "palanca.h"

#define PIN_LEVER  GPIO_NUM_2

static palanca_cb_t s_callback = NULL; // Callback registrado en palancaInit(), invocado desde la ISR

/**
 * @brief Handler de interrupción del GPIO de la palanca.
 * Se dispara en cualquier flanco para detectar tanto la presión como la liberación.
 * @param arg No utilizado (requerido por la firma de gpio_isr_handler_add).
 */
static void IRAM_ATTR _lever_isr_handler(void *arg) {
    if (s_callback) {
        bool pressed = (gpio_get_level(PIN_LEVER) == 1);
        s_callback(pressed);
    }
}

void palancaInit(palanca_cb_t callback) {
    s_callback = callback;
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_LEVER),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // reposo = LOW, presionada = HIGH
        .intr_type    = GPIO_INTR_ANYEDGE,     // detecta presion y liberacion
    };
    gpio_config(&cfg);

    if (callback) {
        // gpio_install_isr_service instala el serivicio ISR global de GPIO.
        // Solo puede llamarse una vez por proyecto; el flag 0 usa config por defecto.
        gpio_install_isr_service(0);
        gpio_isr_handler_add(PIN_LEVER, _lever_isr_handler, NULL);
        printf("[palanca] Init con ISR OK.\n");
    } else {
        printf("[palanca] Init en modo polling OK.\n");
    }
}

bool palancaPressed(void) {
    return (gpio_get_level(PIN_LEVER) == 1);
}