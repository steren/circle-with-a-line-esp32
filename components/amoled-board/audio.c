#include "audio.h"

#include "board.h"
#include "board_priv.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio";

#define CODEC_ADDR 0x18
#define CODEC_VOLUME 0xBF  // DAC volume register: 0xBF = 0 dB, 0.5 dB per step
#define I2C_TIMEOUT_MS 50

static i2s_chan_handle_t s_i2s;

// ES8311 set-up for playback: I2S slave, 16-bit samples, master clock of
// 256 x the sample rate on its MCLK pin. Values follow Espressif's es8311
// driver for a 4.096 MHz clock at 16 kHz.
static const uint8_t CODEC_INIT[][2] = {
    {0x00, 0x1F},  // reset
    {0x00, 0x00},
    {0x00, 0x80},  // power on, slave mode
    {0x01, 0x3F},  // enable all clocks, take the master clock from the MCLK pin
    {0x02, 0x00},  // clock pre-divider 1, pre-multiplier 1
    {0x03, 0x10},  // ADC oversampling
    {0x04, 0x10},  // DAC oversampling
    {0x05, 0x00},  // ADC and DAC clock dividers 1
    {0x06, 0x03},  // bit clock divider
    {0x07, 0x00},  // frame clock divider, high byte
    {0x08, 0xFF},  // frame clock divider, low byte
    {0x09, 0x0C},  // input to the DAC: I2S format, 16 bits
    {0x0A, 0x0C},  // output from the ADC: I2S format, 16 bits
    {0x0D, 0x01},  // power up the analogue circuitry
    {0x0E, 0x02},  // power up the input amplifier and ADC modulator
    {0x12, 0x00},  // power up the DAC
    {0x13, 0x10},  // enable the output driver
    {0x1C, 0x6A},  // ADC equaliser bypass, remove DC offset
    {0x37, 0x08},  // DAC equaliser bypass
    {0x32, CODEC_VOLUME},
    {0x31, 0x00},  // unmute the DAC: on this board it comes out of reset muted
};

esp_err_t audio_init(void)
{
    // I2S first: the codec needs its master clock running before it is configured.
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;   // play silence when nothing is written
    channel_config.dma_desc_num = 4;    // small buffers keep sounds in step with the screen
    channel_config.dma_frame_num = 128;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_i2s, NULL), TAG, "create I2S channel");

    const i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s, &std_config), TAG, "configure I2S");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s), TAG, "enable I2S");

    i2c_master_dev_handle_t codec;
    const i2c_device_config_t codec_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CODEC_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_bus(), &codec_config, &codec), TAG, "add codec");
    for (size_t i = 0; i < sizeof(CODEC_INIT) / sizeof(CODEC_INIT[0]); i++) {
        ESP_RETURN_ON_ERROR(i2c_master_transmit(codec, CODEC_INIT[i], 2, I2C_TIMEOUT_MS), TAG,
                            "write codec register 0x%02X", CODEC_INIT[i][0]);
    }
    i2c_master_bus_rm_device(codec);

    const gpio_config_t amp_enable = {
        .pin_bit_mask = 1ULL << BOARD_AMP_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&amp_enable), TAG, "configure amplifier pin");
    gpio_set_level(BOARD_AMP_ENABLE, 1);
    return ESP_OK;
}

void audio_set_power(bool on)
{
    gpio_set_level(BOARD_AMP_ENABLE, on ? 1 : 0);
}

void audio_write(const int16_t *samples, size_t count)
{
    if (!s_i2s) {
        return;
    }
    // The codec takes stereo frames; send each sample to both channels.
    int16_t stereo[2 * 64];
    while (count > 0) {
        const size_t n = count < 64 ? count : 64;
        for (size_t i = 0; i < n; i++) {
            stereo[2 * i] = stereo[2 * i + 1] = samples[i];
        }
        size_t written;
        i2s_channel_write(s_i2s, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        samples += n;
        count -= n;
    }
}
