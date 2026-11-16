idf_component_register(
    SRCS "audio.c" "board.c" "display.c" "gfx.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi)
