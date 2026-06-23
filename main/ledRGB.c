/**
 * @file ledRGB.c
 * @brief Implementación del driver para el LED RGB APHF1608LSEEQBDZGKC.
 * @see ledRGB.h para la descripción completa.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "ledRGB.h"

#define LED_RED   GPIO_NUM_4
#define LED_GREEN GPIO_NUM_6
#define LED_BLUE  GPIO_NUM_5

/**
 * @brief Aplica directamente los niveles a los tres canales físicos.
 * @param r Nivel canal rojo.  [0=ON; 1=ON]
 * @param g Nivel canal verde. [0=ON; 1=ON]
 * @param b Nivel canal azul.  [0=ON; 1=ON]
 */
static void _led_set_raw(int r, int g, int b) {
    gpio_set_level(LED_RED,   r);
    gpio_set_level(LED_GREEN, g);
    gpio_set_level(LED_BLUE,  b);
}

void ledRGBinit(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_RED) | (1ULL << LED_GREEN) | (1ULL << LED_BLUE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Estado inicial: todo OFF
    _led_set_raw(1, 1, 1);
}

void ledRGBset(led_color_t color) {
    switch (color) {
        case LED_COLOR_RED:     _led_set_raw(0, 1, 1); break; // solo R=LOW
        case LED_COLOR_GREEN:   _led_set_raw(1, 0, 1); break; // solo G=LOW
        case LED_COLOR_BLUE:    _led_set_raw(1, 1, 0); break; // solo B=LOW
        case LED_COLOR_OFF:     _led_set_raw(1, 1, 1); break; // todos HIGH = apagado
        case LED_COLOR_WHITE:   _led_set_raw(0, 0, 0); break; // todos LOW = todos encendidos
        case LED_COLOR_YELLOW:  _led_set_raw(0, 0, 1); break; // R+G
        case LED_COLOR_CYAN:    _led_set_raw(1, 0, 0); break; // G+B
        case LED_COLOR_MAGENTA: _led_set_raw(0, 1, 0); break; // R+B
        default: break;
    }
}