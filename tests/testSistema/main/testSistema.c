/**
 * @file main.c
 * @brief Test del sistema completo — nodo central (MELTAN).
 *
 * Valida el flujo de experimento end-to-end con todos los subsistemas:
 *
 *   Raspberry Pi --[BLE]--> MELTAN --[ESP-NOW]--> ESP32-C3 (estrella OLED)
 *                              |
 *                     palanca + motor + altavoz + LED RGB
 *
 * Flujo de una sesión:
 *   1. Reposo: LED azul, MELTAN anunciándose y esperando comando BLE.
 *   2. La Raspberry Pi escribe "EXPERIMENTO_ON" en la característica CMD.
 *   3. MELTAN enciende el LED verde un instante (comando recibido) y avisa
 *      a la Raspberry Pi.
 *   4. MELTAN ordena por ESP-NOW al C3 que dibuje la estrella (10 s de
 *      parpadeo) y espera su confirmación (CMD_OK).
 *   5. Con la confirmación, LED amarillo y se abre la ventana de 10 s
 *      vigilando la palanca.
 *      - Si se pulsa la palanca dentro de la ventana: recompensa. El motor
 *        gira 200 pasos y, a la vez, suena un tono agradable. Luego vuelve
 *        a reposo (LED azul).
 *      - Si pasan 10 s sin pulsar: vuelve a reposo sin recompensa.
 *   6. Cualquier fallo de comunicación deja el LED rojo un instante.
 *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "ledRGB.h"
#include "motor.h"
#include "palanca.h"
#include "altavoz.h"
#include "comunicacion.h"

static const char *TAG = "test_sistema";

// ─── Parámetros del experimento ───────────────────────────────────────────────
#define VENTANA_PALANCA_MS   10000  // Duración de la ventana de palanca (10 s)
#define RECOMPENSA_PASOS     50     // Pasos del motor al dar recompensa
#define RECOMPENSA_DELAY_MS  10     // Tiempo entre pasos del motor. 10ms = 1 tick
#define TONO_FREQ_HZ         880    // Tono La5
#define TONO_DURACION_MS     300    // Duración del tono de recompensa

// ─── Protocolo ESP-NOW con el ESP32-C3 ────────────────────────────────────────
// Mismos códigos que el test ESP-NOW ya validado.
#define CMD_ESTRELLA  0x01   // MELTAN -> C3: dibuja la estrella y parpadea
#define CMD_OK        0x02   // C3 -> MELTAN: confirmación de recepción

// MAC del ESP32-C3 receptor (módulo de la estrella).
static uint8_t s_c3_mac[6] = { 0x1C, 0xDB, 0xD4, 0x17, 0x48, 0x0C };

// ─── Sincronización entre tareas ──────────────────────────────────────────────
// Cola de pulsaciones de la palanca: la ISR postea aquí, la tarea del
// experimento la consume. Guardamos un bool (true = pulsada).
static QueueHandle_t s_palanca_queue = NULL;

// Cola de comandos de experimento: el callback BLE postea aquí (no puede
// ejecutar el experimento directamente porque bloquearía el stack BLE).
// La tarea del experimento la consume.
static QueueHandle_t s_experimento_queue = NULL;

// Semáforo binario para la confirmación del C3: la tarea del experimento
// espera aquí tras enviar CMD_ESTRELLA; el callback de recepción ESP-NOW
// lo libera al recibir CMD_OK.
static SemaphoreHandle_t s_c3_confirm = NULL;

// ─── Callback de la palanca (contexto ISR) ────────────────────────────────────
static void _palanca_cb(bool pressed) {
    if (pressed) {
        BaseType_t hp_task_woken = pdFALSE;
        bool ev = true;
        xQueueSendFromISR(s_palanca_queue, &ev, &hp_task_woken);
        if (hp_task_woken) {
            portYIELD_FROM_ISR();
        }
    }
}

// ─── Recepción ESP-NOW (confirmación del C3) ──────────────────────────────────
// Se dispara cuando llega un paquete ESP-NOW. Si es CMD_OK, libera el semáforo
// para desbloquear a la tarea del experimento.
static void _espnow_recv_cb(const esp_now_recv_info_t *info,
                            const uint8_t *data, int len) {
    if (len >= 1 && data[0] == CMD_OK) {
        ESP_LOGI(TAG, "C3 confirmo la estrella (CMD_OK)");
        BaseType_t hp_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(s_c3_confirm, &hp_task_woken);
        if (hp_task_woken) {
            portYIELD_FROM_ISR();
        }
    }
}

// ─── Callback BLE (contexto de la tarea del host NimBLE) ───────────────────────
static void _ble_cmd_cb(const meltan_cmd_t *cmd) {
    const char *texto = (const char *)cmd;
    ESP_LOGI(TAG, "Comando BLE recibido");

    if (strncmp(texto, "EXPERIMENTO_ON", 14) == 0) {
        uint8_t ev = 1;
        xQueueSend(s_experimento_queue, &ev, 0);
    }
}

// ─── Tarea: recompensa (motor + altavoz a la vez) ─────────────────────────────
// El altavoz corre en su propia tarea para sonar simultáneamente con el motor.
// La tarea se autoelimina al terminar.
static void _tarea_altavoz(void *param) {
    altavozEnable(1);
    altavozToneI2S(TONO_FREQ_HZ, TONO_DURACION_MS);
    vTaskDelete(NULL);
}

// Lanza recompensa: arranca el tono en paralelo y mueve el motor 200 pasos.
static void _dar_recompensa(void) {
    ESP_LOGI(TAG, "Recompensa: motor + tono simultaneos");

    // Lanzamos el altavoz en una tarea aparte para que suene a la vez que se mueve el motor.
    xTaskCreate(_tarea_altavoz, "altavoz", 4096, NULL, 6, NULL);

    motorEnable();
    motorStep(RECOMPENSA_PASOS, MOTOR_DIR_FORWARD, RECOMPENSA_DELAY_MS);
    motorDisable();
}

// ─── Tarea principal del experimento ──────────────────────────────────────────
static void _tarea_experimento(void *param) {
    uint8_t ev;

    while (1) {
        // Bloqueo hasta que llegue un comando EXPERIMENTO_ON desde el BLE.
        if (xQueueReceive(s_experimento_queue, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Comando recibido -> LED verde un instante y aviso a la Raspi.
        ledRGBset(LED_COLOR_GREEN);
        ble_notify_status((const uint8_t *)"RECIBIDO", 8);
        vTaskDelay(pdMS_TO_TICKS(500));

        // Ordenar la estrella al C3 por ESP-NOW.
        meltan_cmd_t orden = {0};
        orden.cmd = CMD_ESTRELLA;
        orden.payload_len = 0;

        // Vaciamos cualquier confirmación antigua del semáforo.
        xSemaphoreTake(s_c3_confirm, 0);

        if (espnow_send_cmd(MODULE_LED_MATRIX, &orden) != 0) {
            ESP_LOGE(TAG, "Fallo enviando estrella al C3");
            ledRGBset(LED_COLOR_RED);
            ble_notify_status((const uint8_t *)"ERROR_ESPNOW", 12);
            vTaskDelay(pdMS_TO_TICKS(1500));
            ledRGBset(LED_COLOR_BLUE);
            continue;
        }

        // Esperar la confirmación del C3 (máx. 2 s).
        if (xSemaphoreTake(s_c3_confirm, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "El C3 no confirmo a tiempo");
            ledRGBset(LED_COLOR_RED);
            ble_notify_status((const uint8_t *)"ERROR_C3", 8);
            vTaskDelay(pdMS_TO_TICKS(1500));
            ledRGBset(LED_COLOR_BLUE);
            continue;
        }

        // Experimento en curso -> LED amarillo y ventana de palanca.
        ledRGBset(LED_COLOR_WHITE);
        ble_notify_status((const uint8_t *)"EN_CURSO", 8);

        // Vaciamos pulsaciones previas que pudieran haber quedado en la cola.
        xQueueReset(s_palanca_queue);

        // Esperamos una pulsación durante la ventana de 10 s.
        bool pulsada;
        bool exito = (xQueueReceive(s_palanca_queue, &pulsada, pdMS_TO_TICKS(VENTANA_PALANCA_MS)) == pdTRUE);

        if (exito) {
            ESP_LOGI(TAG, "Palanca pulsada dentro de la ventana");
            ble_notify_status((const uint8_t *)"RECOMPENSA", 10);
            _dar_recompensa();
        } else {
            ESP_LOGI(TAG, "Ventana agotada sin pulsacion");
            ble_notify_status((const uint8_t *)"SIN_PULSAR", 10);
        }

        // Vuelta a reposo.
        ledRGBset(LED_COLOR_BLUE);
        ESP_LOGI(TAG, "Sesion terminada. En reposo.");
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "=== TEST SISTEMA COMPLETO (MELTAN) ===");

    // NVS: necesario tanto para WiFi/ESP-NOW como para BLE. Se inicializa
    // una sola vez aquí, antes de los módulos de comunicación.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Event loop por defecto: el driver WiFi postea aquí sus eventos internos. 
    // Sin este loop, esos posts fallan con "failed to post WiFi event ... ret=259" 
    // y ESP-NOW deja de entregar.
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Colas y sincronización.
    s_palanca_queue     = xQueueCreate(4, sizeof(bool));
    s_experimento_queue = xQueueCreate(4, sizeof(uint8_t));
    s_c3_confirm        = xSemaphoreCreateBinary();

    // Inicialización de periféricos.
    ledRGBinit();
    motorInit();
    altavozInit();
    palancaInit(_palanca_cb);   // modo interrupción

    // Comunicaciones: ESP-NOW primero, luego BLE.
    espnow_init();
    espnow_register_peer(MODULE_LED_MATRIX, s_c3_mac);

    // Registramos nuestro callback de recepción ESP-NOW para oír el CMD_OK
    esp_now_register_recv_cb(_espnow_recv_cb);

    // BLE: registra el callback de comandos y arranca el advertising.
    ble_init(_ble_cmd_cb);

    // Tarea principal del experimento.
    xTaskCreate(_tarea_experimento, "experimento", 4096, NULL, 5, NULL);

    // Estado inicial: reposo.
    ledRGBset(LED_COLOR_BLUE);
    ESP_LOGI(TAG, "Sistema listo. En reposo (LED azul).");
}