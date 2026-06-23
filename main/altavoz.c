/**
 * @file altavoz.c
 * @brief Implementación del driver para el amplificador MAX98357AETE+T.
 * @see altavoz.h para la descripción completa.
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2s_std.h"
#include "altavoz.h"

#define PIN_SDMODE  GPIO_NUM_35           // SD_MODE: HIGH = amplificador activo, LOW = shutdown
#define PIN_BCLK    GPIO_NUM_37           // BCLK   : bit clock I2S
#define PIN_DIN     GPIO_NUM_36           // DIN    : datos de audio I2S, se usa también para tono PWM
#define PIN_LRCLK   GPIO_NUM_38           // LRCLK  : left/right clock I2S

#define I2S_SAMPLE_RATE  44100            // Sample rate del canal I2S en Hz

#define LEDC_CHANNEL    LEDC_CHANNEL_0    // Canal LEDC para tonos PWM
#define LEDC_TIMER      LEDC_TIMER_0      // Timer LEDC asociado
#define LEDC_RESOLUTION LEDC_TIMER_10_BIT // Resolución PWM: 10 bits
#define LEDC_DUTY_50    512               // Duty 50%: onda cuadrada

static i2s_chan_handle_t s_i2s_tx = NULL; // Handle del canal I2S TX, creado en altavozInit()

void altavozInit(void) {
    // SD_MODE arranca en LOW
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_SDMODE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_SDMODE, 0);

    // Canal I2S en modo TX, ya que solo se manda audio
    // I2S_NUM_AUTO deja al ESP-IDF elegir qué periférico I2S usar.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL);

    // Configuracion I2S:
    // - 44100 Hz: sample rate de calidad de CD
    // - 16 bits por muestra: rango -32768 a 32767
    // - Mono: el MAX98357AETE+T es un amplificador mono
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk  = PIN_BCLK,
            .ws    = PIN_LRCLK,
            .dout  = PIN_DIN,
            .din   = I2S_GPIO_UNUSED,
        },
    };
    i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    i2s_channel_enable(s_i2s_tx);

    printf("[altavoz] Init OK. I2S activo. Amplificador en shutdown.\n");
}

void altavozEnable(int enable) {
    gpio_set_level(PIN_SDMODE, enable);
    if (enable) {
        vTaskDelay(pdMS_TO_TICKS(2));
        printf("[altavoz] Amplificador activo.\n");
    } else {
        printf("[altavoz] Amplificador en shutdown.\n");
    }
}

void altavozTonePWM(uint32_t freq_hz, uint32_t duration_ms) {
    // Configuracion del timer LEDC con la frecuencia 'x'
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    // Asignacion del canal al pin DIN con duty 50% (onda cuadrada).
    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PIN_DIN,
        .duty       = LEDC_DUTY_50,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel_cfg);

    printf("[altavoz][PWM] %luHz durante %lums\n", freq_hz, duration_ms);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Duty 0 = silencio
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
}

void altavozToneI2S(uint32_t freq_hz, uint32_t duration_ms) {
    // Cuántas muestras necesitamos para cubrir duration_ms a 44100 Hz
    uint32_t total_muestras = (I2S_SAMPLE_RATE * duration_ms) / 1000;

    printf("[altavoz][I2S] %luHz durante %lums\n", freq_hz, duration_ms);

    uint32_t muestras_enviadas = 0;
    size_t bytes_escritos = 0;
    // Muestras por ciclo de la onda a esta frecuencia y sample rate
    // Por ejemplo: 44100Hz / 1000Hz = 44.1 muestras por ciclo.
    uint32_t muestras_por_ciclo = I2S_SAMPLE_RATE / freq_hz;
    if (muestras_por_ciclo < 1) {
        muestras_por_ciclo = 1;
    }
    // Buffer de un ciclo interpolado a la frecuencia real
    int16_t ciclo[256];
    uint32_t n = (muestras_por_ciclo < 256) ? muestras_por_ciclo : 256;
    for (uint32_t i = 0; i < n; i++) {
        float angulo = 2.0f * M_PI * i / n;
        ciclo[i] = (int16_t)(16000.0f * sinf(angulo));
    }
    while (muestras_enviadas < total_muestras) {
        i2s_channel_write(s_i2s_tx, ciclo, n * sizeof(int16_t),
                          &bytes_escritos, portMAX_DELAY);
        muestras_enviadas += n;
    }
    // Silencio final, para evitar petardeo
    int16_t silencio[256] = {0};
    i2s_channel_write(s_i2s_tx, silencio, sizeof(silencio), &bytes_escritos, portMAX_DELAY);
    altavozEnable(0);
}