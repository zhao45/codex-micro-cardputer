#include "cardputer_usb_mic.h"

#include <limits.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "usb_device_uac.h"

static const char *TAG = "cardputer_usb_mic";

static constexpr uint8_t ES8311_ADDRESS = 0x18;
static constexpr uint32_t MIC_SAMPLE_RATE = 16000;
static constexpr int MIC_SOFTWARE_GAIN = 8;

static i2c_master_dev_handle_t s_codec;
static i2s_chan_handle_t s_i2s_rx;
static bool s_reported_read_error;

static esp_err_t codec_write(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(s_codec, command, sizeof(command), 20);
}

static esp_err_t initialize_codec(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(I2C_NUM_0, &bus), TAG,
                        "get shared Cardputer I2C bus");

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = ES8311_ADDRESS;
    device_config.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &s_codec), TAG,
                        "add ES8311 codec");

    // Cardputer ADV microphone setup from M5Stack's M5Unified board support.
    const uint8_t registers[][2] = {
        {0x00, 0x80}, // reset / clock state machine on
        {0x01, 0xba}, // derive MCLK from BCLK
        {0x02, 0x18}, // clock pre-divider
        {0x0d, 0x01}, // power up analog circuitry
        {0x0e, 0x02}, // enable analog PGA and ADC modulator
        {0x14, 0x1a}, // differential microphone input, +30 dB low-noise PGA
        {0x17, 0xbf}, // ADC digital volume: 0 dB
        {0x1c, 0x6a}, // bypass EQ and cancel digital DC offset
    };
    for (const auto &entry : registers) {
        ESP_RETURN_ON_ERROR(codec_write(entry[0], entry[1]), TAG,
                            "configure ES8311 register 0x%02x", entry[0]);
    }
    return ESP_OK;
}

static esp_err_t initialize_i2s(void)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 160;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, nullptr, &s_i2s_rx), TAG,
                        "create microphone I2S RX channel");

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_41,
            .ws = GPIO_NUM_43,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_46,
            .invert_flags = {},
        },
    };
    // ES8311 places its mono ADC stream in the right I2S slot.
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &standard_config), TAG,
                        "configure microphone I2S mode");
    return i2s_channel_enable(s_i2s_rx);
}

static void amplify_pcm16(uint8_t *buffer, size_t length)
{
    int16_t *samples = reinterpret_cast<int16_t *>(buffer);
    const size_t sample_count = length / sizeof(int16_t);
    for (size_t index = 0; index < sample_count; ++index) {
        int32_t sample = static_cast<int32_t>(samples[index]) * MIC_SOFTWARE_GAIN;
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        samples[index] = static_cast<int16_t>(sample);
    }
}

static esp_err_t read_microphone(uint8_t *buffer, size_t length,
                                 size_t *bytes_read, void *)
{
    size_t received = 0;
    const esp_err_t status = i2s_channel_read(s_i2s_rx, buffer, length, &received, 20);
    if (status != ESP_OK && status != ESP_ERR_TIMEOUT) {
        if (!s_reported_read_error) {
            ESP_LOGE(TAG, "I2S microphone read failed: %s", esp_err_to_name(status));
            s_reported_read_error = true;
        }
        received = 0;
    }
    if (received < length) memset(buffer + received, 0, length - received);
    amplify_pcm16(buffer, received);
    *bytes_read = length;
    return ESP_OK;
}

static void stream_state(bool microphone_active, bool, void *)
{
    ESP_LOGI(TAG, "Windows USB microphone stream %s",
             microphone_active ? "active" : "idle");
}

extern "C" esp_err_t cardputer_usb_mic_start(void)
{
    ESP_RETURN_ON_ERROR(initialize_i2s(), TAG, "initialize microphone I2S");
    ESP_RETURN_ON_ERROR(initialize_codec(), TAG, "initialize ES8311 microphone");

    uac_device_config_t config = {};
    config.input_cb = read_microphone;
    config.stream_state_cb = stream_state;
#if CONFIG_USB_DEVICE_UAC_AS_PART
    config.spk_itf_num = -1;
    config.mic_itf_num = 1;
#endif
    ESP_RETURN_ON_ERROR(uac_device_init(&config), TAG, "start USB UAC microphone");
    ESP_LOGI(TAG, "Windows UAC microphone ready: mono 16 kHz 16-bit");
    return ESP_OK;
}
