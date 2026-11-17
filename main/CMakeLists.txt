idf_component_register(
    SRCS "main.c" "game.c" "puzzle.c" "levels.c" "sound.c" "synth.c" "generator.c" "puzzle_cache.c" "puzzle_bank.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES amoled-board esp_timer esp_hw_support nvs_flash)
