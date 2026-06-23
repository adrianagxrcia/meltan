#pragma once
#include <stdint.h>
/**
 * @file altavoz.h
 * @brief Driver para el amplificador MAX98357AETE+T (I2S → altavoz 8Ω).
 * @details El MAX98357AETE+T recibe audio digital I2S directamente desde
 * el ESP32-S3-WROOM-1-N8R2, y entrega hasta ~0.7W a 8Ω con alimentación de 3.3V.
 *
 * GPIOs utilizados (Peripheral.SchDoc):
 *   - SD_MODE → GPIO35 (HIGH = amplificador activo, LOW = shutdown)
 *   - BCLK    → GPIO37 (bit clock I2S)
 *   - DIN     → GPIO36 (datos de audio I2S)
 *   - LRCLK   → GPIO38 (left/right clock, define el sample rate)
 *
 * Ganancia configurada en hardware mediante R36 (634kΩ) → 9dB (pin flotante).
 *
 * @author agarcia
 * @date 2026
*/

/**
 * @brief Inicializa SD_MODE e I2S
 * @details Configura GPIO35 como salida en shutdown, e inicializa el canal
 * I2S en modo estándar a 44100Hz, 16 bits, mono
 * @note Llamar una sola vez desde app_main() antes de cualquier reproducción
 */
void altavozInit(void);

/**
 * @brief Activa o desactiva el amplificador mediante SD_MODE
 * @details El MAX98357AETE+T necesita aproximadamente 1ms para salir
 * de shutdown antes de que el audio sea limpio. Se añaden 2ms de margen.
 * @param enable 1 para activar el amplificador, 0 para shutdown
 */
void altavozEnable(int enable);

/**
 * @brief Reproduce un tono PWM a la frecuencia 'y' durante un tiempo 'x'
 * @details Genera una onda cuadrada en el pin DIN mediante LEDC.
 * @param freq_hz     Frecuencia del tono en Hz.
 * @param duration_ms Duración del tono en milisegundos.
 */
void altavozTonePWM(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief Reproduce un tono sinusoidal por I2S a la frecuencia 'x'
 * @details Genera muestras PCM de un seno por software.
 * @param freq_hz     Frecuencia del seno en Hz.
 * @param duration_ms Duración en milisegundos.
 */
void altavozToneI2S(uint32_t freq_hz, uint32_t duration_ms);