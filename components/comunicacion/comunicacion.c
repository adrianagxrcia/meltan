#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "comunicacion.h"

// ble_store_config_init() pertenece al componente bt pero no tiene cabecera
// pública; se declara como externa. Es imprescindible llamarla antes de
// arrancar la tarea del host, o la sincronización del stack no se completa.
extern void ble_store_config_init(void);

// ─── comunicacion.c ───────────────────────────────────────────────────────────
// Gestión de comunicaciones de la placa Main.
//
// Arquitectura de comunicación:
//   PC -> [interfaz web] -> Raspberry Pi 4 -> [BLE] -> ESP32-S3 Main
//                                                          |
//                                                  [ESP-NOW] -> Módulos externos
//
// La placa Main actúa como nodo central: recibe órdenes por BLE desde la
// Raspberry Pi y las distribuye por ESP-NOW a los módulos periféricos.
//
// ESP-NOW usa la banda 2.4 GHz sin router, directo entre ESPs. Cada peer
// (módulo externo) se identifica por su dirección MAC.
// ─────────────────────────────────────────────────────────────────────────────

static const char *TAG = "comunicacion";

// Tabla de MACs de los módulos registrados.
// Índice = espnow_module_t, valor = dirección MAC (6 bytes).
static uint8_t s_peer_macs[MODULE_COUNT][6] = {0};
static bool    s_peer_registered[MODULE_COUNT] = {false};

// Callback BLE guardado para invocarlo al recibir comandos de la Raspberry Pi.
static ble_cmd_cb_t s_ble_callback = NULL;

// ─── ESP-NOW ─────────────────────────────────────────────────────────────────

// Callback interno de ESP-NOW: se invoca al confirmarse un envío.
// sent_status indica si el peer recibió el paquete (ACK a nivel MAC).
static void _espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t sent_status) {
    if (sent_status == ESP_NOW_SEND_SUCCESS) {
        printf("[espnow] Paquete entregado a " MACSTR "\n", MAC2STR(mac_addr));
    } else {
        printf("[espnow] FALLO enviando a " MACSTR "\n", MAC2STR(mac_addr));
    }
}

void espnow_init(void) {
    // ESP-NOW requiere WiFi inicializado en modo STA (station).
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_now_init();
    esp_now_register_send_cb(_espnow_send_cb);

    printf("[espnow] Init OK.\n");
}

void espnow_register_peer(espnow_module_t module, const uint8_t mac[6]) {
    if (module >= MODULE_COUNT) return;

    // Guardamos la MAC en la tabla local para referencia rápida
    memcpy(s_peer_macs[module], mac, 6);
    s_peer_registered[module] = true;

    // Registramos el peer en el stack ESP-NOW. Sin este paso esp_now_send()
    // rechazará la MAC destino.
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = 0;      // 0 = mismo canal que el WiFi actual
    peer_info.encrypt = false;  // sin cifrado por ahora

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err == ESP_OK) {
        printf("[espnow] Peer modulo %d registrado: " MACSTR "\n",
               module, MAC2STR(mac));
    } else {
        printf("[espnow] Error registrando peer modulo %d: %s\n",
               module, esp_err_to_name(err));
    }
}

int espnow_send_cmd(espnow_module_t module, const meltan_cmd_t *cmd) {
    if (module >= MODULE_COUNT || !s_peer_registered[module]) {
        printf("[espnow] Modulo %d no registrado.\n", module);
        return -1;
    }

    // esp_now_send serializa el buffer y lo envía. Máximo 250 bytes por
    // paquete; meltan_cmd_t cabe de sobra.
    esp_err_t err = esp_now_send(
        s_peer_macs[module],
        (const uint8_t *)cmd,
        sizeof(meltan_cmd_t)
    );

    return (err == ESP_OK) ? 0 : -1;
}

// ─── BLE (NimBLE) ─────────────────────────────────────────────────────────────
//
// MELTAN actúa como servidor BLE (peripheral) con un servicio GATT propio:
//   - Característica CMD    (write):  la Raspberry Pi escribe comandos aquí.
//   - Característica STATUS (notify): MELTAN notifica respuestas a la Raspberry Pi.
//
// El comando recibido se interpreta como una estructura meltan_cmd_t y se
// entrega a s_ble_callback, que la capa superior registra en ble_init().

// UUIDs de 16 bits del servicio y sus características.
#define MELTAN_SVC_UUID    0x1234
#define MELTAN_CHR_CMD     0x1235
#define MELTAN_CHR_STATUS  0x1236

// Handle de la característica STATUS (lo rellena el stack al registrar el
// servicio) y handle de la conexión activa con la Raspberry Pi.
static uint16_t s_status_handle = 0;
static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;

// Tipo de dirección propia inferido por el stack al sincronizarse.
static uint8_t  s_own_addr_type;

// Declaración adelantada: _start_advertising y _gap_event_cb se llaman mutuamente.
static int _gap_event_cb(struct ble_gap_event *event, void *arg);

// Callback de acceso de la característica CMD. Se dispara cuando la Raspberry Pi
// escribe en ella. Copiamos los bytes recibidos a un meltan_cmd_t y, si la capa
// superior registró un callback, se lo entregamos.
static int _chr_cmd_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    meltan_cmd_t cmd = {0};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    // No copiamos más de lo que cabe en la estructura.
    if (len > sizeof(meltan_cmd_t)) {
        len = sizeof(meltan_cmd_t);
    }
    ble_hs_mbuf_to_flat(ctxt->om, &cmd, len, NULL);

    printf("[ble] CMD recibido: cmd=0x%02X payload_len=%d\n",
           cmd.cmd, cmd.payload_len);

    // Entregamos el comando a la lógica de aplicación.
    if (s_ble_callback != NULL) {
        s_ble_callback(&cmd);
    }

    return 0;
}

// Callback de acceso de la característica STATUS. Solo se usa por notify, pero
// NimBLE exige un access_cb no nulo en cada característica (si es NULL,
// ble_gatts_count_cfg falla con EINVAL y el host no llega a sincronizar).
static int _chr_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return 0;
}

// Definición del servicio GATT: un servicio primario con dos características.
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(MELTAN_SVC_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = BLE_UUID16_DECLARE(MELTAN_CHR_CMD),
                .access_cb = _chr_cmd_write_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid       = BLE_UUID16_DECLARE(MELTAN_CHR_STATUS),
                .access_cb  = _chr_status_access_cb,
                .val_handle = &s_status_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }   // fin de características
        },
    },
    { 0 }   // fin de servicios
};

// Arranca el advertising como "MELTAN-MAIN". Se llama desde _on_sync (primer
// arranque) y desde _gap_event_cb (al desconectarse la Raspberry Pi).
static void _start_advertising(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    const char *name;
    int rc;

    // Flags estándar (descubrimiento general, sin soporte BR/EDR clásico) y
    // nivel de potencia automático.
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    // El nombre que se anuncia es el configurado con ble_svc_gap_device_name_set.
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error en adv_set_fields; rc=%d", rc);
        return;
    }

    // Advertising conectable y descubrible, sin caducidad.
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, _gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error en adv_start; rc=%d", rc);
        return;
    }

    printf("[ble] Advertising como 'MELTAN-MAIN'...\n");
}

// Manejador de eventos GAP: conexión, desconexión y fin de advertising.
static int _gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // status 0 = conexión establecida.
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                printf("[ble] Raspberry Pi conectada. Handle: %d\n", s_conn_handle);
            } else {
                // Conexión fallida: volvemos a anunciarnos.
                _start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            // Al desconectarse, reanudamos el advertising para aceptar
            // una nueva conexión.
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            printf("[ble] Raspberry Pi desconectada. Reiniciando advertising.\n");
            _start_advertising();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            // El advertising caducó (no debería con BLE_HS_FOREVER, pero por
            // robustez lo reiniciamos).
            _start_advertising();
            break;

        default:
            break;
    }
    return 0;
}

// Se dispara si el controlador BLE reinicia el stack por un error.
static void _on_reset(int reason) {
    ESP_LOGE(TAG, "Stack BLE reiniciado. Razon: %d", reason);
}

// Se dispara cuando el host NimBLE termina de sincronizarse con el controlador.
// Es el único punto seguro para empezar a anunciarse.
static void _on_sync(void) {
    int rc;

    // Aseguramos que hay una dirección BLE válida (preferimos la pública).
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr fallo; rc=%d", rc);
        return;
    }

    // Inferimos automáticamente el tipo de dirección a usar para anunciarnos.
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto fallo; rc=%d", rc);
        return;
    }

    _start_advertising();
}

// Tarea FreeRTOS del host NimBLE. nimble_port_run() procesa los eventos del
// stack y solo retorna cuando se detiene el puerto.
static void _ble_host_task(void *param) {
    printf("[ble] Host task arrancada.\n");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init(ble_cmd_cb_t callback) {
    // Guardamos el callback de aplicación para invocarlo al recibir comandos.
    s_ble_callback = callback;

    // Inicializamos el stack NimBLE. En ESP-IDF v5.x esta llamada también
    // inicializa el HCI internamente (no hay que llamar a esp_nimble_hci_init).
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "fallo al iniciar nimble; ret=%d", ret);
        return;
    }

    // Registramos los callbacks de estado del host antes de arrancar la tarea.
    ble_hs_cfg.reset_cb = _on_reset;
    ble_hs_cfg.sync_cb  = _on_sync;

    // Registramos el servicio GATT.
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    // Nombre con el que MELTAN se anuncia.
    ble_svc_gap_device_name_set("MELTAN-MAIN");

    // Inicializamos el almacenamiento del host. Imprescindible: sin esta
    // llamada el host no completa la sincronización con el controlador.
    ble_store_config_init();

    // Arrancamos la tarea del host. A partir de aquí, cuando el stack
    // sincronice se disparará _on_sync y comenzará el advertising.
    nimble_port_freertos_init(_ble_host_task);

    printf("[ble] Init OK.\n");
}

int ble_notify_status(const uint8_t *data, uint16_t len) {
    // Solo podemos notificar si hay una conexión activa y la característica
    // STATUS está registrada.
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_status_handle == 0) {
        return -1;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return -1;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
    return (rc == 0) ? 0 : -1;
}

void ble_deinit(void) {
    // Detiene el puerto NimBLE; la tarea del host saldrá de nimble_port_run()
    // y liberará sus recursos en nimble_port_freertos_deinit().
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
        printf("[ble] Deinit OK.\n");
    } else {
        ESP_LOGE(TAG, "fallo al detener nimble; rc=%d", rc);
    }
}