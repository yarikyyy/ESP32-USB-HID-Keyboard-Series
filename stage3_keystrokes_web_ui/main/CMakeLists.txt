idf_component_register(
    SRCS "tusb_hid_example.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_http_server esp_wifi nvs_flash esp_eth
    )
