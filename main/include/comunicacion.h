#pragma once
#include <stdint.h>
#include <stddef.h>

// ─── comunicacion.h ───────────────────────────────────────────────────────────
// Interfaz del módulo de comunicación de la placa Main.
// Dos canales:
//   1. BLE  -> Raspberry Pi 4 (recibe instrucciones de experimento)
//   2. ESP-NOW -> Módulos periféricos externos (envía órdenes de actuación)
// ─────────────────────────────────────────────────────────────────────────────

// ─── Tipos compartidos ────────────────────────────────────────────────────────


/**
 * @brief Identificador de módulo periférico destino para ESP-NOW.
 * @details Cada valor indexa la tabla interna de MACs registradas. Ampliar
 * el enum según se añadan módulos al sistema.
 */
typedef enum {
    MODULE_LED_MATRIX = 0,  // Módulo externo: matriz de LEDs (estímulo visual)
    MODULE_COUNT            // Número total de módulos (centinela, no es un módulo)
} espnow_module_t;

/**
 * @brief Comando intercambiado entre el nodo central y el resto del sistema.
 * @details Estructura compartida por BLE y ESP-NOW. Cabe de sobra en un
 * paquete ESP-NOW (máximo 250 bytes).
 */
typedef struct {
    uint8_t  cmd;           // Código de comando (definir según experimento)
    uint8_t  payload[16];   // Datos adicionales
    uint8_t  payload_len;   // Longitud real de payload
} meltan_cmd_t;

/**
 * @brief Tipo de callback invocado al recibir un comando por BLE.
 * @param cmd Puntero al comando recibido (válido solo durante la llamada).
 */
typedef void (*ble_cmd_cb_t)(const meltan_cmd_t *cmd);


// ─── ESP-NOW ─────────────────────────────────────────────────────────────────

/**
 * @brief Inicializa ESP-NOW sobre WiFi en modo estación.
 * @details Arranca el stack WiFi en modo STA (sin conectarse a ningún punto
 * de acceso) y a continuación inicializa ESP-NOW, registrando el callback
 * interno de confirmación de envío.
 * @note Requiere que NVS esté inicializado previamente.
 */
void espnow_init(void);

/**
 * @brief Registra la MAC de un módulo periférico como peer ESP-NOW.
 * @details Guarda la MAC en la tabla interna y la añade al stack ESP-NOW.
 * Sin este registro, los envíos a esa MAC son rechazados.
 * @param module Identificador del módulo (@ref espnow_module_t).
 * @param mac Dirección MAC de 6 bytes del módulo.
 * @note Llamar una vez por módulo durante el arranque.
 */
void espnow_register_peer(espnow_module_t module, const uint8_t mac[6]);

/**
 * @brief Envía un comando a un módulo periférico registrado por ESP-NOW.
 * @param module Identificador del módulo destino (@ref espnow_module_t).
 * @param cmd Puntero al comando a enviar.
 * @return 0 si el envío se programó correctamente, -1 si el módulo no está
 * registrado o el envío falló.
 */
int espnow_send_cmd(espnow_module_t module, const meltan_cmd_t *cmd);

// ─── BLE ─────────────────────────────────────────────────────────────────────

/**
 * @brief Inicializa el stack BLE (NimBLE) y arranca el advertising.
 * @details Configura el servicio GATT del nodo, registra los callbacks del
 * host y comienza a anunciarse como "MELTAN-MAIN" para que la Raspberry Pi
 * pueda conectarse.
 * @param callback Función a invocar con cada comando recibido (@ref ble_cmd_cb_t).
 * @note Requiere que NVS esté inicializado previamente.
 */
void ble_init(ble_cmd_cb_t callback);

/**
 * @brief Notifica un estado a la Raspberry Pi por la característica STATUS.
 * @param data Puntero a los bytes a notificar.
 * @param len Longitud de los datos en bytes.
 * @return 0 si la notificación se programó, -1 si no hay conexión activa o
 * la operación falló.
 */
int ble_notify_status(const uint8_t *data, uint16_t len);

/**
 * @brief Detiene el stack BLE y libera sus recursos.
 * @details Detiene el puerto NimBLE; la tarea del host finaliza y libera su
 * memoria. Útil para entrar en modos de bajo consumo.
 */
void ble_deinit(void);