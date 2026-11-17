// Circle with a Line on the Waveshare ESP32-S3-Touch-AMOLED-1.8: connects the game to
// the board's touch controller, display, side buttons and speaker.
//
// Buttons:  BOOT, short press        next puzzle
//           PWR, short press         previous puzzle
//           either, held for 6 s     power off / on

#include "audio.h"
#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game.h"
#include "levels.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "puzzle_cache.h"
#include "sound.h"

static const char *TAG = "circle";

#define CO5300_COLUMN_OFFSET 16  // the V2 panel starts 16 columns into the controller's memory
#define LONG_PRESS_MS 6000       // holding either button for 6 s switches the board off or on
#define BRIGHTNESS_ACTIVE 200
#define BRIGHTNESS_DIM 25
#define FADE_IN_MS 800           // how long a new puzzle takes to fade in from black
#define IDLE_DIM_MS 30000        // dim the screen after 30 s without touch or button input

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

static nvs_handle_t s_nvs;
static bool s_nvs_open;

static int load_saved_level(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK || nvs_open("witness", NVS_READWRITE, &s_nvs) != ESP_OK) {
        return 0;
    }
    s_nvs_open = true;
    int32_t saved = 0;
    if (nvs_get_i32(s_nvs, "level", &saved) == ESP_OK && saved >= 0 && saved < level_count) {
        return (int)saved;
    }
    return 0;
}

static void save_level(int level)
{
    if (s_nvs_open) {
        nvs_set_i32(s_nvs, "level", level);
        nvs_commit(s_nvs);
    }
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
    const int initial_level = load_saved_level();
    const int64_t begin = esp_timer_get_time();
    game_init(esp_random());
    if (initial_level > 0) {
        game_load_level(initial_level);
    }
    const int64_t drawn = esp_timer_get_time();
    game_set_sound_handler(sound_play);
    display_flush();
    ESP_LOGI(TAG, "First screen: drawn in %lld ms, sent to the panel in %lld ms", (drawn - begin) / 1000,
             (esp_timer_get_time() - drawn) / 1000);
    display_set_brightness(0);

    button_state_t boot = {.button = BOARD_BUTTON_BOOT}, pwr = {.button = BOARD_BUTTON_PWR};
    uint32_t last_active_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t level_since_ms = last_active_ms;
    int brightness = 0;  // starts at 0 so the first puzzle fades in from black
    int logged_level = game_level();
    uint32_t logged_puzzle = game_puzzle_id();
    bool logged_solved = false;
    bool powered_off = false;
    ESP_LOGI(TAG, "Puzzle %d", logged_level + 1);

    while (true) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        const press_t boot_press = poll_button(&boot, now_ms), pwr_press = poll_button(&pwr, now_ms);
        if (boot_press == PRESS_LONG || pwr_press == PRESS_LONG) {
            powered_off = !powered_off;
            ESP_LOGI(TAG, "%s held for 6 s: power %s", boot_press == PRESS_LONG ? "BOOT" : "PWR",
                     powered_off ? "off" : "on");
            if (powered_off) {
                sound_play(GAME_SOUND_LIFT);
                audio_set_power(false);
                display_set_power(false);
                brightness = 0;
            } else {
                display_set_power(true);
                audio_set_power(true);
                sound_play(GAME_SOUND_LEVEL);
                last_active_ms = now_ms;
                level_since_ms = now_ms;
                brightness = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (powered_off) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (boot_press == PRESS_SHORT) {
            ESP_LOGI(TAG, "BOOT pressed: next puzzle");
            game_next_level();
        } else if (pwr_press == PRESS_SHORT) {
            ESP_LOGI(TAG, "PWR pressed: previous puzzle");
            game_previous_level();
        }

        game_touch_t touch = {0};
        touch.pressed = board_touch_read(&touch.x, &touch.y);
        if (touch.pressed || boot.down || pwr.down || boot_press != PRESS_NONE || pwr_press != PRESS_NONE) {
            last_active_ms = now_ms;
        }

        game_update(&touch, now_ms);

        // When a new puzzle opens, blank the panel before flushing the new
        // framebuffer so it gradually fades in from black like a screen turning on.
        if (game_puzzle_id() != logged_puzzle) {
            logged_puzzle = game_puzzle_id();
            if (game_level() != logged_level) {
                logged_level = game_level();
                save_level(logged_level);
            }
            ESP_LOGI(TAG, "Puzzle %d", logged_level + 1);
            level_since_ms = now_ms;
            brightness = 0;
            display_set_brightness(0);
        }
        display_flush();

        const uint32_t level_age_ms = now_ms - level_since_ms;
        int target_brightness =
            (now_ms - last_active_ms >= IDLE_DIM_MS) ? BRIGHTNESS_DIM : BRIGHTNESS_ACTIVE;
        const int fade_out = game_screen_fade();
        if (fade_out < 255) {
            // Smoothly fade the solved screen down to black before the next puzzle opens.
            const int out_b = target_brightness * fade_out / 255;
            if (brightness != out_b) {
                brightness = out_b;
                display_set_brightness((uint8_t)brightness);
            }
        } else if (level_age_ms < FADE_IN_MS) {
            // Quadratic gamma fade-in from 0 to target_brightness over FADE_IN_MS.
            const int fade = (int)((uint64_t)target_brightness * level_age_ms * level_age_ms /
                                   ((uint64_t)FADE_IN_MS * FADE_IN_MS));
            if (brightness != fade) {
                brightness = fade;
                display_set_brightness((uint8_t)brightness);
            }
        } else if (brightness != target_brightness) {
            if (brightness < target_brightness) {
                brightness += 15;
                if (brightness > target_brightness) brightness = target_brightness;
            } else {
                brightness -= 2;
                if (brightness < target_brightness) brightness = target_brightness;
            }
            display_set_brightness((uint8_t)brightness);
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
