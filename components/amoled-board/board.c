#include "board.h"
#include "board_priv.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define I2C_SPEED_HZ 400000
#define I2C_TIMEOUT_MS 50

// TCA9554 IO expander
#define EXPANDER_ADDR 0x20
#define EXPANDER_REG_INPUT 0x00
#define EXPANDER_REG_OUTPUT 0x01
#define EXPANDER_REG_CONFIG 0x03  // bit = 1 -> input, 0 -> output
#define EXIO_LCD_RESET (1 << 0)
#define EXIO_LCD_POWER (1 << 1)  // DSI_PWR_EN on the schematic
#define EXIO_TOUCH_RESET (1 << 2)
#define EXIO_PWR_BUTTON (1 << 4)  // high while pressed
#define EXIO_DISPLAY_LINES (EXIO_LCD_RESET | EXIO_LCD_POWER | EXIO_TOUCH_RESET)

// Touch controllers. Both report touches with the same register layout.
#define TOUCH_ADDR_FT3168 0x38
#define TOUCH_ADDR_CST820 0x15
#define TOUCH_REG_FINGER_COUNT 0x02  // followed by XH, XL, YH, YL
#define TOUCH_DETECT_TIMEOUT_MS 600

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_expander;  // NULL if the expander did not answer
static i2c_master_dev_handle_t s_touch;
static uint8_t s_touch_addr;  // 0 = no touch controller found

static bool probe(uint8_t addr)
{
    return i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS) == ESP_OK;
}

static esp_err_t add_device(uint8_t addr, i2c_master_dev_handle_t *dev)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &config, dev);
}

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t buf[] = {reg, value};
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, len, I2C_TIMEOUT_MS);
}

// Power-cycle and reset the display and touch controller through the expander.
static esp_err_t reset_display_and_touch(void)
{
    if (!probe(EXPANDER_ADDR)) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(add_device(EXPANDER_ADDR, &s_expander), TAG, "add expander");

    uint8_t output = 0xFF, config = 0xFF;
    esp_err_t err = read_regs(s_expander, EXPANDER_REG_OUTPUT, &output, 1);
    if (err == ESP_OK) err = read_regs(s_expander, EXPANDER_REG_CONFIG, &config, 1);
    if (err == ESP_OK) err = write_reg(s_expander, EXPANDER_REG_OUTPUT, output & ~EXIO_DISPLAY_LINES);
    if (err == ESP_OK) err = write_reg(s_expander, EXPANDER_REG_CONFIG, config & ~EXIO_DISPLAY_LINES);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (err == ESP_OK) err = write_reg(s_expander, EXPANDER_REG_OUTPUT, output | EXIO_DISPLAY_LINES);
    vTaskDelay(pdMS_TO_TICKS(100));
    return err;
}

// The touch controllers take a few hundred ms after reset to answer on I2C.
static uint8_t detect_touch_controller(void)
{
    for (int waited = 0; waited < TOUCH_DETECT_TIMEOUT_MS; waited += 20) {
        if (probe(TOUCH_ADDR_CST820)) return TOUCH_ADDR_CST820;
        if (probe(TOUCH_ADDR_FT3168)) return TOUCH_ADDR_FT3168;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return 0;
}

esp_err_t board_init(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "create I2C bus");

    const gpio_config_t boot_button = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&boot_button), TAG, "configure BOOT button");

    esp_err_t err = reset_display_and_touch();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "IO expander reset failed (%s); continuing without a hardware reset",
                 esp_err_to_name(err));
    }

    s_touch_addr = detect_touch_controller();
    if (s_touch_addr) {
        ESP_RETURN_ON_ERROR(add_device(s_touch_addr, &s_touch), TAG, "add touch controller");
    } else {
        ESP_LOGW(TAG, "No touch controller answered; assuming the original revision");
    }
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

board_revision_t board_revision(void)
{
    return s_touch_addr == TOUCH_ADDR_CST820 ? BOARD_REV_V2 : BOARD_REV_ORIGINAL;
}

void board_log_i2c_devices(void)
{
    char line[128];
    int len = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (probe(addr) && len < (int)sizeof(line) - 6) {
            len += snprintf(line + len, sizeof(line) - len, " 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "I2C devices:%s", len ? line : " none");
}

bool board_button_pressed(board_button_t button)
{
    if (button == BOARD_BUTTON_BOOT) {
        return gpio_get_level(BOARD_BOOT_BUTTON) == 0;
    }
    uint8_t inputs = 0;
    return s_expander && read_regs(s_expander, EXPANDER_REG_INPUT, &inputs, 1) == ESP_OK &&
           (inputs & EXIO_PWR_BUTTON);
}

bool board_touch_present(void)
{
    return s_touch_addr != 0;
}

bool board_touch_read(int *x, int *y)
{
    // The CST820 stops answering on I2C while it sleeps between touches. Probing
    // first keeps that expected silence from being logged as a bus error.
    if (!s_touch_addr || !probe(s_touch_addr)) {
        return false;
    }
    uint8_t data[5];
    if (read_regs(s_touch, TOUCH_REG_FINGER_COUNT, data, sizeof(data)) != ESP_OK) {
        return false;
    }
    if ((data[0] & 0x0F) == 0) {
        return false;
    }
    *x = ((data[1] & 0x0F) << 8) | data[2];
    *y = ((data[3] & 0x0F) << 8) | data[4];
    return *x < BOARD_LCD_WIDTH && *y < BOARD_LCD_HEIGHT;
}
