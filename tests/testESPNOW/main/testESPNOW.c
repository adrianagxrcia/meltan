/**
 * @file main.c
 * @brief Test ESP-NOW bidireccional
 *
 * Flujo:
 *   1. MELTAN envía CMD_ESTRELLA a la ESP32-C3.
 *   2. C3 dibuja la estrella en su pantalla OLED y responde CMD_OK.
 *   3. MELTAN recibe el CMD_OK e imprime confirmación.
 *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "nvs_flash.h"

// ─── Protocolo ────────────────────────────────────────────────────────────────
// Comandos compartidos entre MELTAN y C3.
// Ambos archivos deben tener exactamente los mismos valores.
#define CMD_ESTRELLA  0x01
#define CMD_OK        0x02

typedef struct {
    uint8_t cmd;
} espnow_msg_t;

static const uint8_t MAC_C3[6] = {0x1C, 0xDB, 0xD4, 0x16, 0x91, 0xD4}; // [ESTO HAY QUE CAMBIARLO DEPENDIENDO DE LA ESP32 A LA QUE SE ENVIE]
static QueueHandle_t s_rx_queue = NULL; // Queue para recibir mensajes desde el callback de ESP-NOW.

// ─── Callbacks ESP-NOW ────────────────────────────────────────────────────────
static void _espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        printf("[espnow] CMD_ESTRELLA entregado a C3.\n");
    } else {
        printf("[espnow] FALLO al entregar CMD_ESTRELLA.\n");
    }
}

static void _espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(espnow_msg_t)) {
        return;
    }
    // Mensaje copiado a la queue para procesarlo en app_main
    espnow_msg_t msg;
    memcpy(&msg, data, sizeof(espnow_msg_t));
    xQueueSendFromISR(s_rx_queue, &msg, NULL);
}

// ─── Init ─────────────────────────────────────────────────────────────────────
static void _espnow_init(void) {
    // NVS es necesario para que WiFi guarde configuración de calibración
    nvs_flash_init();

    // ESP-NOW requiere WiFi en modo STA
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_send_cb(_espnow_send_cb);
    esp_now_register_recv_cb(_espnow_recv_cb);

    // Registro de la C3 como peer
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, MAC_C3, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    printf("[espnow] Init OK.\n");
}

// ─── Punto de entrada ─────────────────────────────────────────────────────────
void app_main(void) {
    printf("\n=== TEST ESP-NOW: MELTAN → C3 ===\n");
    s_rx_queue = xQueueCreate(5, sizeof(espnow_msg_t));
    _espnow_init();

    // Envio CMD_ESTRELLA
    espnow_msg_t msg = { .cmd = CMD_ESTRELLA };
    printf("[meltan] Enviando CMD_ESTRELLA a C3...\n");
    esp_now_send(MAC_C3, (uint8_t *)&msg, sizeof(espnow_msg_t));

    // CMD_OK?
    espnow_msg_t respuesta;
    printf("[meltan] Esperando CMD_OK de C3...\n");
    if (xQueueReceive(s_rx_queue, &respuesta, portMAX_DELAY)) {
        if (respuesta.cmd == CMD_OK) {
            printf("[meltan] ✓ CMD_OK recibido. Test completado.\n");
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}