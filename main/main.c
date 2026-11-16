// Circle with a Line on the Waveshare ESP32-S3-Touch-AMOLED-1.8: connects the game to
// the board's touch controller, display, side buttons and speaker.
//
// Buttons:  BOOT, short press   next puzzle
//           PWR, short press    previous puzzle
//           either, held        clear the line

#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game.h"
#include "puzzle_cache.h"
#include "sound.h"

static const char *TAG = "circle";

#define CO5300_COLUMN_OFFSET 16  // the V2 panel starts 16 columns into the controller's memory
#define LONG_PRESS_MS 600

_Static_assert(GAME_SCREEN_WIDTH == BOARD_LCD_WIDTH && GAME_SCREEN_HEIGHT == BOARD_LCD_HEIGHT,
               "the game is laid out for this board's screen");

typedef enum { PRESS_NONE, PRESS_SHORT, PRESS_LONG } press_t;

typedef struct {
    board_button_t button;
    bool down;       // debounced state
    bool last_read;  // previous raw reading
    bool long_sent;  // the long press has already been reported for this hold
    uint32_t down_since_ms;
} button_state_t;

// Reports a short press when the button is released, or a long press as soon
// as it has been held for LONG_PRESS_MS.
static press_t poll_button(button_state_t *b, uint32_t now_ms)
{
    const bool read = board_button_pressed(b->button);
    const bool stable = (read == b->last_read);  // two equal readings in a row
    b->last_read = read;
    if (!stable) {
        return PRESS_NONE;
    }

    if (read && !b->down) {
        b->down = true;
        b->long_sent = false;
        b->down_since_ms = now_ms;
    } else if (!read && b->down) {
        b->down = false;
        return b->long_sent ? PRESS_NONE : PRESS_SHORT;
    } else if (b->down && !b->long_sent && now_ms - b->down_since_ms >= LONG_PRESS_MS) {
        b->long_sent = true;
        return PRESS_LONG;
    }
    return PRESS_NONE;
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    const bool is_v2 = board_revision() == BOARD_REV_V2;
    ESP_LOGI(TAG, "Board revision: %s", is_v2 ? "V2 (CO5300 + CST820)" : "original (SH8601 + FT3168)");
    if (!board_touch_present()) {
        ESP_LOGW(TAG, "No touch controller found; the game cannot be played");
    }

    ESP_ERROR_CHECK(display_init(is_v2 ? CO5300_COLUMN_OFFSET : 0));
    sound_init();
    puzzle_cache_start();
    game_set_puzzle_source(puzzle_cache_take);
    const int64_t begin = esp_timer_get_time();
    game_init(esp_random());
    const int64_t drawn = esp_timer_get_time();
    game_set_sound_handler(sound_play);
    display_flush();
    ESP_LOGI(TAG, "First screen: drawn in %lld ms, sent to the panel in %lld ms", (drawn - begin) / 1000,
             (esp_timer_get_time() - drawn) / 1000);
    display_set_brightness(200);

    button_state_t boot = {.button = BOARD_BUTTON_BOOT}, pwr = {.button = BOARD_BUTTON_PWR};
    int logged_level = -1;
    bool logged_solved = false;
    while (true) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        const press_t boot_press = poll_button(&boot, now_ms), pwr_press = poll_button(&pwr, now_ms);
        if (boot_press == PRESS_LONG || pwr_press == PRESS_LONG) {
            ESP_LOGI(TAG, "%s held: clear the line", boot_press == PRESS_LONG ? "BOOT" : "PWR");
            game_reset_line();
        } else if (boot_press == PRESS_SHORT) {
            ESP_LOGI(TAG, "BOOT pressed: next puzzle");
            game_next_level();
        } else if (pwr_press == PRESS_SHORT) {
            ESP_LOGI(TAG, "PWR pressed: previous puzzle");
            game_previous_level();
        }

        game_touch_t touch = {0};
        touch.pressed = board_touch_read(&touch.x, &touch.y);
        game_update(&touch, now_ms);
        display_flush();

        if (game_level() != logged_level) {
            logged_level = game_level();
            ESP_LOGI(TAG, "Puzzle %d", logged_level + 1);
        }
        if (game_is_solved() != logged_solved) {
            logged_solved = game_is_solved();
            if (logged_solved) {
                ESP_LOGI(TAG, "Puzzle %d solved", logged_level + 1);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
