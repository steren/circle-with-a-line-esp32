#include "display.h"

#include <string.h>

#include "board.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"

static const char *TAG = "display";

#define WIDTH BOARD_LCD_WIDTH
#define HEIGHT BOARD_LCD_HEIGHT

#define LCD_HOST SPI2_HOST
#define LCD_CLOCK_HZ (40 * 1000 * 1000)

// Every transfer is an 8-bit instruction and a 24-bit address (the register
// number in the middle byte) on one data line, followed by the payload.
#define OPCODE_WRITE_REG 0x02     // payload on one line
#define OPCODE_WRITE_PIXELS 0x32  // payload on all four lines

// Registers (MIPI DCS plus a few vendor ones, shared by the CO5300 and SH8601)
#define REG_SLEEP_OUT 0x11
#define REG_DISPLAY_ON 0x29
#define REG_COLUMN_ADDR 0x2A
#define REG_ROW_ADDR 0x2B
#define REG_MEMORY_WRITE 0x2C
#define REG_PIXEL_FORMAT 0x3A
#define REG_BRIGHTNESS 0x51
#define REG_CTRL_DISPLAY 0x53
#define REG_BRIGHTNESS_HBM 0x63
#define REG_SPI_MODE 0xC4
#define REG_COMMAND_PAGE 0xFE

// Pixels are copied from the framebuffer to the panel through a DMA-capable
// buffer holding this many full-width lines.
#define FLUSH_LINES 16
#define FLUSH_PIXELS (WIDTH * FLUSH_LINES)

static spi_device_handle_t s_spi;
static uint16_t *s_framebuffer;  // drawn into through gfx.h
static uint16_t *s_flush_buffer;
static int s_column_offset;

static void write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .flags = len ? SPI_TRANS_USE_TXDATA : 0,  // no data phase for bare commands
        .cmd = OPCODE_WRITE_REG,
        .addr = (uint32_t)reg << 8,
        .length = len * 8,
    };
    assert(len <= sizeof(t.tx_data));
    memcpy(t.tx_data, data, len);
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void write_reg8(uint8_t reg, uint8_t value)
{
    write_reg(reg, &value, 1);
}

static void set_window(int x1, int y1, int x2, int y2)
{
    x1 += s_column_offset;
    x2 += s_column_offset;
    write_reg(REG_COLUMN_ADDR, (uint8_t[]){x1 >> 8, x1, x2 >> 8, x2}, 4);
    write_reg(REG_ROW_ADDR, (uint8_t[]){y1 >> 8, y1, y2 >> 8, y2}, 4);
}

esp_err_t display_init(int column_offset)
{
    s_column_offset = column_offset;

    // 330 KB: too big for internal RAM, so it lives in the 8 MB PSRAM.
    s_framebuffer = heap_caps_calloc(WIDTH * HEIGHT, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_framebuffer, ESP_ERR_NO_MEM, TAG, "no PSRAM for the framebuffer");
    s_flush_buffer = heap_caps_malloc(FLUSH_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_flush_buffer, ESP_ERR_NO_MEM, TAG, "no memory for the flush buffer");

    const spi_bus_config_t bus_config = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .data0_io_num = BOARD_LCD_SDIO0,
        .data1_io_num = BOARD_LCD_SDIO1,
        .data2_io_num = BOARD_LCD_SDIO2,
        .data3_io_num = BOARD_LCD_SDIO3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = FLUSH_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "init SPI bus");

    const spi_device_interface_config_t device_config = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = 0,
        .clock_speed_hz = LCD_CLOCK_HZ,
        .spics_io_num = BOARD_LCD_CS,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &device_config, &s_spi), TAG, "add SPI device");
    // Nothing else is on this bus, so keep it acquired to make transfers cheaper.
    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(s_spi, portMAX_DELAY), TAG, "acquire SPI bus");

    write_reg(REG_SLEEP_OUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    write_reg8(REG_COMMAND_PAGE, 0x00);    // standard command set
    write_reg8(REG_SPI_MODE, 0x80);        // accept pixel data on four lines
    write_reg8(REG_PIXEL_FORMAT, 0x55);    // 16 bits per pixel (RGB565)
    write_reg8(REG_CTRL_DISPLAY, 0x20);    // enable brightness control
    write_reg8(REG_BRIGHTNESS_HBM, 0xFF);
    write_reg8(REG_BRIGHTNESS, 0x00);

    // Clear the panel's memory before switching it on, so no garbage is shown.
    gfx_init(s_framebuffer, WIDTH, HEIGHT);
    gfx_fill_rect(0, 0, WIDTH, HEIGHT, 0);
    display_flush();
    write_reg(REG_DISPLAY_ON, NULL, 0);
    return ESP_OK;
}

void display_set_brightness(uint8_t level)
{
    write_reg8(REG_BRIGHTNESS, level);
}

void display_flush(void)
{
    int x1, y1, x2, y2;
    if (!gfx_take_dirty(&x1, &y1, &x2, &y2)) {
        return;
    }
    // These controllers only take windows that start on an even pixel and have
    // an even width and height, so grow the dirty box to fit.
    x1 &= ~1;
    y1 &= ~1;
    x2 |= 1;
    y2 |= 1;
    const int w = x2 - x1 + 1;
    const int max_lines = (FLUSH_PIXELS / w) & ~1;

    for (int y = y1; y <= y2;) {
        const int lines = (y2 - y + 1 < max_lines) ? y2 - y + 1 : max_lines;
        for (int row = 0; row < lines; row++) {
            memcpy(&s_flush_buffer[row * w], &s_framebuffer[(y + row) * WIDTH + x1], w * sizeof(uint16_t));
        }
        set_window(x1, y, x2, y + lines - 1);
        spi_transaction_t t = {
            .flags = SPI_TRANS_MODE_QIO,
            .cmd = OPCODE_WRITE_PIXELS,
            .addr = REG_MEMORY_WRITE << 8,
            .length = w * lines * sizeof(uint16_t) * 8,
            .tx_buffer = s_flush_buffer,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        y += lines;
    }
}
