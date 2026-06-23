/**
 * @file queMAC.c
 * @brief Test de lectura de dirección MAC via WiFi en modo Station.
 *
 * Inicializa el stack WiFi del ESP32-S3-WROOM-1-N8R2 en modo Station
 * únicamente para poder leer su dirección MAC y mostrarla por UART.
 * No se realiza ninguna conexión a red.
 *
 * @note Este archivo es un test auxiliar. La MAC obtenida aquí es la
 *       que debe registrarse en los peers ESP-NOW del sistema MELTAN.
 *
 * @author  agarcia
 * @date    2025
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"

/**
 * @brief Punto de entrada principal del test queMAC.
 *
 * Ejecuta la secuencia mínima necesaria para arrancar el driver WiFi
 * y leer la MAC de la interfaz Station. La imprime una sola vez por
 * consola serie y luego queda en un bucle idle.
 *
 * Secuencia:
 *  1. Inicializa NVS (requisito del driver WiFi para guardar su config).
 *  2. Crea el event loop por defecto (requisito interno de esp_wifi).
 *  3. Inicializa el driver WiFi con configuración por defecto.
 *  4. Configura el modo Station y arranca el driver.
 *  5. Lee e imprime la dirección MAC de la interfaz STA.
 *  6. Bucle infinito con delay de 1 s (idle, CPU libre para FreeRTOS).
 */
void app_main(void) {
    /* --- 1. NVS --------------------------------------------------------
     * El driver WiFi usa NVS para persistir su configuración interna.
     * Si la partición NVS está llena o tiene una versión incompatible,
     * se borra y se reinicializa limpia antes de continuar.
     * ESP_ERROR_CHECK detiene la ejecución con panic si hay error. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --- 2. Event loop -------------------------------------------------
     * ESP-IDF usa un event loop centralizado para propagar eventos del
     * sistema (WiFi, IP, etc.). Debe existir antes de llamar a esp_wifi_init. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --- 3. Driver WiFi ------------------------------------------------
     * WIFI_INIT_CONFIG_DEFAULT() rellena la estructura cfg con los valores
     * por defecto de Espressif (buffers, tareas internas, potencia TX...).
     * esp_wifi_init arranca las tareas internas del stack WiFi. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* --- 4. Modo Station + start ---------------------------------------
     * El modo STA es necesario para que exista la interfaz WIFI_IF_STA,
     * que es de donde se lee la MAC. Sin set_mode + start, la interfaz
     * no está activa y esp_wifi_get_mac devolvería error. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* --- 5. Lectura e impresión de MAC ---------------------------------
     * La MAC se almacena en un array de 6 bytes (formato OUI estándar).
     * Se imprime en formato XX:XX:XX:XX:XX:XX con %02X para asegurar
     * dos dígitos hexadecimales en mayúsculas por byte. */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* --- 6. Bucle idle -------------------------------------------------
     * vTaskDelay cede el control al scheduler de FreeRTOS durante 1 s.
     * Mantener un bucle en app_main es obligatorio en ESP-IDF: si
     * app_main retorna, la tarea se elimina y el sistema queda sin tarea
     * principal activa. */
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}