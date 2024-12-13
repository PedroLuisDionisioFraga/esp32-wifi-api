idf_component_register(SRCS "wifi_api.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_wifi nvs_flash)
