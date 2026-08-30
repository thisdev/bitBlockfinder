idf_component_register(
    SRCS "bitblockfinder.c" "chain.c" "fetch.c" "net.c" "clock.c"
         "ui.c" "backlight.c" "shot.c"
         "schrift/comic_14.c" "schrift/comic_20.c"
         "schrift/comic_28.c" "schrift/comic_40.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_client esp-tls nvs_flash json esp_netif
             esp_timer esp_driver_gpio esp_http_server
)
