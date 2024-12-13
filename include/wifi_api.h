/**
 * @file wifi_api.h
 * @author Pedro Luis Dionísio Fraga (pedrodfraga@hotmail.com)
 *
 * @brief Wi-Fi API
 *
 * @version 0.1
 * @date 2024-11-16
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef WIFI_API_H
#define WIFI_API_H

#include <esp_err.h>

/**
 * @brief Configure Wi-Fi with the given SSID and password.
 *
 * Initializes the Wi-Fi station, sets up the event handler, and connects to the
 * specified network.
 *
 * @param[in] ssid The SSID of the Wi-Fi network.
 * @param[in] password The password for the Wi-Fi network.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t wifi_api_configure(const char *ssid, const char *password);

/**
 * @brief Disconnect from the Wi-Fi network.
 *
 * Unregisters the event handlers and cleans up resources.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t wifi_api_disconnect();

/**
 * @brief Alter the Wi-Fi STA configuration dynamically.
 *
 * Updates the SSID and password for the STA mode and reconnects.
 *
 * @param[in] new_ssid The new SSID for the Wi-Fi network.
 * @param[in] new_password The new password for the Wi-Fi network.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t wifi_api_alter_sta(const char *new_ssid, const char *new_password);

/**
 * @brief Start a Wi-Fi scan.
 *
 * This function prints the authentication mode of the Wi-Fi network based on
 * the `authmode` parameter.
 *
 * @note It must be called after a successful Wi-Fi connection, i.e., after
 * `esp_wifi_start()` and `esp_wifi_connect()`.
 */
void wifi_api_scan();

#endif // WIFI_API_H
