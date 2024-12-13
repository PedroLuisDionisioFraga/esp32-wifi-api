/**
 * @file wifi_api.c
 * @author Pedro Luis Dionísio Fraga (pedrodfraga@hotmail.com)
 *
 * @brief Wi-Fi API implementation based of `protocol_examples_common` from
 * ESP-IDF
 *
 * @version 0.1
 * @date 2024-11-16
 *
 * @copyright Copyright (c) 2024
 *
 */

#include "wifi_api.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/semphr.h>
#include <nvs_flash.h>
#include <string.h>

/**
 * @brief Tag for logging.
 */
static const char *TAG = "WIFI_API";

/**
 * @brief Description for the Wi-Fi station interface.
 */
static const char *NETIF_DESC_STA = "STA";

/**
 * @brief Maximum number of retry attempts for Wi-Fi connection.
 */
static const uint8_t MAX_RETRY = 10;

/**
 * @brief Wi-Fi station network interface.
 */
static esp_netif_t *s_sta_netif = NULL;

/**
 * @brief Semaphore to signal IP acquisition.
 */
static SemaphoreHandle_t s_ip_semaphore = NULL;

/**
 * @brief Number of retry attempts for Wi-Fi connection.
 */
static int s_retry_num = 0;

/**
 * @brief Event handler instance for any Wi-Fi event.
 */
static esp_event_handler_instance_t instance_any_id = NULL;

/**
 * @brief Event handler instance for IP acquisition event.
 */
static esp_event_handler_instance_t instance_got_ip = NULL;

void wifi_api_scan()
{
  ESP_LOGI(TAG, "Starting Wi-Fi scan...");

  uint16_t number = 10;
  wifi_ap_record_t ap_info[10];
  uint16_t ap_count = 0;
  memset(ap_info, 0, sizeof(ap_info));

  wifi_scan_config_t scan_config = {
    .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = true};
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  ESP_LOGI(TAG, "Max AP number ap_info can hold = %u", number);

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));

  ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u",
           ap_count, number);
  for (int i = 0; i < number; i++)
  {
    ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
    ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
    ESP_LOGI(TAG, "Authmode \t%s",
             ap_info[i].authmode == WIFI_AUTH_OPEN
               ? "WIFI_AUTH_OPEN"
               : (ap_info[i].authmode == WIFI_AUTH_WEP
                    ? "WIFI_AUTH_WEP"
                    : (ap_info[i].authmode == WIFI_AUTH_WPA_PSK
                         ? "WIFI_AUTH_WPA_PSK"
                         : (ap_info[i].authmode == WIFI_AUTH_WPA2_PSK
                              ? "WIFI_AUTH_WPA2_PSK"
                              : (ap_info[i].authmode == WIFI_AUTH_WPA_WPA2_PSK
                                   ? "WIFI_AUTH_WPA_WPA2_PSK"
                                   : "WIFI_AUTH_UNKNOWN")))));
    ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
  }
}

/**
 * @brief Event handler for Wi-Fi and IP events.
 *
 * This function handles various Wi-Fi and IP events such as station start,
 * disconnection, and IP acquisition. It attempts to reconnect on
 * disconnection and signals when an IP address is obtained.
 *
 * @param arg User-defined argument (not used).
 * @param event_base Base ID of the event.
 * @param event_id ID of the event.
 * @param event_data Event-specific data.
 */
static void wifi_api_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
  switch (event_id)
  {
    case WIFI_EVENT_STA_START:
    {
      esp_wifi_connect();
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED:
    {
      if (s_retry_num < MAX_RETRY)
      {
        esp_wifi_connect();
        s_retry_num++;
        ESP_LOGI(TAG, "Retry to connect to the AP");
      }
      else
      {
        // Making available to `xSemaphoreTake` in `wifi_api_configure`, i.e.,
        // allows the application to continue execution below the
        // `xSemaphoreTake()` call
        xSemaphoreGive(s_ip_semaphore);
        ESP_LOGI(TAG, "Connect to the AP fail");
      }
      break;
    }
    case IP_EVENT_STA_GOT_IP:
    {
      s_retry_num = 0;
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      xSemaphoreGive(s_ip_semaphore);
      break;
    }
    default:
      break;
  }
}

/**
 * @brief Shut down Wi-Fi and release all resources.
 *
 * Deinitializes the Wi-Fi interface, destroys semaphores, and clears Wi-Fi
 * drivers.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
static void wifi_api_shutdown()
{
  ESP_LOGI(TAG, "Shutting down Wi-Fi...");
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_deinit());
  ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_sta_netif));
  esp_netif_destroy(s_sta_netif);
  s_sta_netif = NULL;
}

/**
 * @brief Initialize the Non-Volatile Storage (NVS) flash.
 *
 * This function initializes the NVS flash, which is required for storing
 * persistent data such as Wi-Fi credentials. It checks for errors during
 * initialization and handles them appropriately.
 */
static void initialize_nvs()
{
  ESP_ERROR_CHECK(nvs_flash_init());
}

// TODO: Implement error handling for all ESP_ERROR_CHECK calls
// TODO: Improve the wifi callback functions in
// `esp_event_handler_instance_register`
esp_err_t wifi_api_configure(const char *ssid, const char *password)
{
  initialize_nvs();

  ESP_LOGI(TAG, "Configuring Wi-Fi...");
  s_ip_semaphore = xSemaphoreCreateBinary();
  if (!s_ip_semaphore)
  {
    ESP_LOGE(TAG, "Failed to create semaphore");
    return ESP_FAIL;
  }

  // --------------------------------------------------------------------

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // --------------------------------------------------------------------

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_netif_inherent_config_t nif_cfg = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
  nif_cfg.if_desc = NETIF_DESC_STA;
  s_sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &nif_cfg);
  esp_wifi_set_default_wifi_sta_handlers();

  // --------------------------------------------------------------------

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  // --------------------------------------------------------------------

  wifi_config_t wc = {
    .sta =
      {
        .ssid = "",
        .password = "",
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      },
  };
  strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
  strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password));

  // --------------------------------------------------------------------

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
    WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_api_event_handler, NULL,
    &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
    IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_api_event_handler, NULL,
    &instance_got_ip));

  // --------------------------------------------------------------------

  ESP_LOGI(TAG, "Connecting to %s...", wc.sta.ssid);
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_connect());

  // --------------------------------------------------------------------

  // Wait for IP acquisition until success or timeout
  xSemaphoreTake(s_ip_semaphore, portMAX_DELAY);

  if (s_retry_num > MAX_RETRY)
    return ESP_FAIL;

  // --------------------------------------------------------------------
  ESP_ERROR_CHECK(esp_register_shutdown_handler(&wifi_api_shutdown));

  return ESP_OK;
}

esp_err_t wifi_api_disconnect()
{
  ESP_LOGI(TAG, "Disconnecting Wi-Fi...");
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
    WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
    IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));

  if (s_ip_semaphore)
    vSemaphoreDelete(s_ip_semaphore);

  return esp_wifi_disconnect();
}

esp_err_t wifi_api_alter_sta(const char *new_ssid, const char *new_password)
{
  ESP_LOGI(TAG, "Updating STA configuration...");

  wifi_config_t wc;
  esp_wifi_get_config(ESP_IF_WIFI_STA, &wc);

  strncpy((char *)wc.sta.ssid, new_ssid, sizeof(wc.sta.ssid) - 1);
  strncpy((char *)wc.sta.password, new_password, sizeof(wc.sta.password) - 1);

  esp_wifi_set_config(ESP_IF_WIFI_STA, &wc);
  esp_wifi_disconnect();
  esp_wifi_connect();

  return ESP_OK;
}
