/**
 * @file main.c
 * @brief Test de validación de la comunicación BLE entre la Raspberry Pi y MELTAN.
 *
 * MELTAN actúa como servidor BLE (peripheral) con un servicio GATT propio:
 *   - Característica CMD    (write):  la Raspberry Pi escribe comandos aquí.
 *   - Característica STATUS (notify): MELTAN notifica respuestas a la Raspberry Pi.
 *
 * Flujo:
 *   1. MELTAN se anuncia como "MELTAN-MAIN" y espera conexión.
 *   2. La Raspberry Pi (cliente bleak) se conecta y se suscribe a STATUS.
 *   3. Por cada comando que la Raspberry Pi escribe en CMD, MELTAN responde
 *      con "OK:<comando>" mediante una notificación en STATUS.
 *
 * @author agarcia
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern void ble_store_config_init(void);

static const char *TAG = "MELTAN";

#define MELTAN_SVC_UUID   0x1234
#define MELTAN_CHR_CMD    0x1235
#define MELTAN_CHR_STATUS 0x1236

static uint16_t s_status_handle = 0;
static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_own_addr_type;

static int _gap_event_cb(struct ble_gap_event *event, void *arg);

static int _chr_cmd_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[64] = {0};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';
    ESP_LOGI(TAG, "CMD recibido de Raspi: \"%s\"", buf);

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_status_handle != 0) {
        char resp[72];
        snprintf(resp, sizeof(resp), "OK:%s", buf);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(resp, strlen(resp));
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
            ESP_LOGI(TAG, "STATUS notificado: \"%s\"", resp);
        }
    }
    return 0;
}

// Callback de acceso para STATUS. Aunque la característica solo se usa por
// notify, NimBLE exige un access_cb no nulo en cada característica. Como no
// admite lectura directa, devolvemos longitud cero.
static int _chr_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    return 0;
}

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
            { 0 }
        },
    },
    { 0 }
};

static void _start_advertising(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    const char *name;
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error en adv_set_fields; rc=%d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, _gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error en adv_start; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising como 'MELTAN-MAIN'...");
}

static int _gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "evento conexion; status=%d", event->connect.status);
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
            } else {
                _start_advertising();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "desconectada; reiniciando advertising");
            _start_advertising();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            _start_advertising();
            break;
        default:
            break;
    }
    return 0;
}

static void _on_reset(int reason) {
    ESP_LOGE(TAG, "Stack reiniciado. Razon: %d", reason);
}

static void _on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr fallo; rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto fallo; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "_on_sync OK, arrancando advertising");
    _start_advertising();
}

void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task arrancada");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {
    int rc;
    ESP_LOGI(TAG, "=== TEST BLE MELTAN ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "nvs_flash_init: %d", ret);

    ret = nimble_port_init();
    ESP_LOGI(TAG, "nimble_port_init: %d", ret);
    if (ret != ESP_OK) return;

    ble_hs_cfg.reset_cb = _on_reset;
    ble_hs_cfg.sync_cb  = _on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    ESP_LOGI(TAG, "gatts_count_cfg: %d", rc);

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    ESP_LOGI(TAG, "gatts_add_svcs: %d", rc);

    rc = ble_svc_gap_device_name_set("MELTAN-MAIN");
    ESP_LOGI(TAG, "device_name_set: %d", rc);

    ble_store_config_init();

    ESP_LOGI(TAG, "arrancando host task...");
    nimble_port_freertos_init(ble_host_task);
}