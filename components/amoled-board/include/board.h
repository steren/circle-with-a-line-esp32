// Waveshare ESP32-S3-Touch-AMOLED-1.8: pin map, power/reset sequencing,
// hardware revision detection and touch input.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define BOARD_LCD_WIDTH 368
#define BOARD_LCD_HEIGHT 448

// Display (QSPI)
#define BOARD_LCD_CS 12
#define BOARD_LCD_SCLK 11
#define BOARD_LCD_SDIO0 4
#define BOARD_LCD_SDIO1 5
#define BOARD_LCD_SDIO2 6
#define BOARD_LCD_SDIO3 7

// I2C bus shared by the IO expander, touch controller, PMU, RTC, IMU and codec
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

// Audio codec (ES8311, I2S) and speaker amplifier
#define BOARD_I2S_MCLK 16
#define BOARD_I2S_BCLK 9
#define BOARD_I2S_WS 45
#define BOARD_I2S_DOUT 8  // to the codec
#define BOARD_I2S_DIN 10  // from the codec
#define BOARD_AMP_ENABLE 46

#define BOARD_BOOT_BUTTON 0  // active low

// The board ships in two revisions that share one pin map.
typedef enum {
    BOARD_REV_ORIGINAL,  // SH8601 display driver, FT3168 touch (I2C 0x38)
    BOARD_REV_V2,        // CO5300 display driver, CST820 touch (I2C 0x15)
} board_revision_t;

// Brings up I2C, power-cycles the display and touch controller through the IO
// expander, and detects the hardware revision.
esp_err_t board_init(void);

board_revision_t board_revision(void);

// Logs the address of every device that answers on the I2C bus.
void board_log_i2c_devices(void);

// The two side buttons. PWR is read through the IO expander; holding it for
// about six seconds switches the board off (that is done by the power chip).
typedef enum { BOARD_BUTTON_BOOT, BOARD_BUTTON_PWR } board_button_t;

bool board_button_pressed(board_button_t button);

bool board_touch_present(void);

// Returns true and the touch position while a finger is on the screen.
bool board_touch_read(int *x, int *y);
