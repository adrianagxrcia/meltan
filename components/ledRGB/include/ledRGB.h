#pragma once

/**
 * @file ledRGB.h
 * @brief Driver para el LED RGB APHF1608LSEEQBDZGKC (ánodo común).
 * @details Conexiones:
 *   - RED   → R37 (560Ω) → D31 pin R → GPIO4
 *   - GREEN → R39 (100Ω) → D31 pin G → GPIO5
 *   - BLUE  → R38 (100Ω) → D31 pin B → GPIO6
 *
 * Ánodo común: LOW = canal encendido, HIGH = apagado.
 *
 * @note El canal RED tiene 560Ω frente a los 100Ω de GREEN y BLUE,
 * por lo que su intensidad es menor. El blanco puro (R+G+B) queda
 * visualmente amarillento — ajustar combinaciones según resultado visual.
 *
 * @author agarcia
 * @date 2026
*/

/**
 * @brief Colores disponibles para el LED RGB.
 * @details Usar el enum en lugar de valores numéricos permite al compilador
 * detectar valores inválidos y hace el código más legible.
 */
typedef enum {
    LED_COLOR_OFF     = 0, ///< Todos los canales apagados
    LED_COLOR_RED,         ///< Solo canal R
    LED_COLOR_GREEN,       ///< Solo canal G
    LED_COLOR_BLUE,        ///< Solo canal B
    LED_COLOR_WHITE,       ///< R+G+B (todos LOW)
    LED_COLOR_YELLOW,      ///< R+G
    LED_COLOR_CYAN,        ///< G+B
    LED_COLOR_MAGENTA,     ///< R+B
} led_color_t;

/**
 * @brief Inicializa los GPIOs del LED RGB.
 * @details Configura GPIO4, GPIO5 y GPIO6 como salidas digitales
 * sin pull-up ni pull-down. Apaga (HIGH) todos los canales al inicio
 * para partir de un estado conocido.
 * @note Llamar una sola vez desde app_main() antes de cualquier uso.
 */
void ledRGBinit(void);

/**
 * @brief Cambia el color del LED inmediatamente.
 * @details Aplica la combinación de canales correspondiente al color
 * indicado. El cambio es instantáneo.
 * @param color Color a mostrar (@ref led_color_t).
 */
void ledRGBset(led_color_t color);