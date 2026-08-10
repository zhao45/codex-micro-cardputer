#include "cardputer_keyboard.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "cardputer_keys";

static constexpr uint8_t TCA8418_ADDRESS = 0x34;
static constexpr uint8_t REG_CFG = 0x01;
static constexpr uint8_t REG_INT_STAT = 0x02;
static constexpr uint8_t REG_KEY_LCK_EC = 0x03;
static constexpr uint8_t REG_KEY_EVENT_A = 0x04;
static constexpr uint8_t REG_KP_GPIO_1 = 0x1d;
static constexpr uint8_t REG_KP_GPIO_2 = 0x1e;
static constexpr uint8_t REG_KP_GPIO_3 = 0x1f;
static constexpr uint8_t REG_DEBOUNCE_DIS_1 = 0x29;
static constexpr uint8_t REG_DEBOUNCE_DIS_2 = 0x2a;
static constexpr uint8_t REG_DEBOUNCE_DIS_3 = 0x2b;
static constexpr uint8_t CFG_KE_IEN = 1U << 0;
static constexpr uint8_t INT_K_INT = 1U << 0;
static constexpr uint8_t EVENT_COUNT_MASK = 0x0f;
static constexpr uint8_t EVENT_PRESSED = 0x80;
static constexpr uint8_t EVENT_CODE_MASK = 0x7f;

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_keyboard;
static cardputer_key_mask_t s_keys;

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t message[] = {reg, value};
    return i2c_master_transmit(s_keyboard, message, sizeof(message), 20);
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_keyboard, &reg, sizeof(reg), value, 1, 20);
}

static esp_err_t discard_pending_events(void)
{
    uint8_t event_count;
    ESP_RETURN_ON_ERROR(read_register(REG_KEY_LCK_EC, &event_count), TAG,
                        "read initial event count");
    event_count &= EVENT_COUNT_MASK;
    while (event_count-- > 0) {
        uint8_t ignored;
        ESP_RETURN_ON_ERROR(read_register(REG_KEY_EVENT_A, &ignored), TAG,
                            "discard initial key event");
    }
    return write_register(REG_INT_STAT, INT_K_INT);
}

extern "C" esp_err_t cardputer_keyboard_initialize(void)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = GPIO_NUM_8;
    bus_config.scl_io_num = GPIO_NUM_9;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG,
                        "create keyboard I2C bus");

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = TCA8418_ADDRESS;
    device_config.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_keyboard), TAG,
                        "add TCA8418 keyboard");

    // Cardputer ADV connects seven rows and eight columns to the TCA8418.
    ESP_RETURN_ON_ERROR(write_register(REG_CFG, 0), TAG, "disable keyboard events");
    ESP_RETURN_ON_ERROR(write_register(REG_KP_GPIO_1, 0x7f), TAG, "enable matrix rows");
    ESP_RETURN_ON_ERROR(write_register(REG_KP_GPIO_2, 0xff), TAG, "enable matrix columns");
    ESP_RETURN_ON_ERROR(write_register(REG_KP_GPIO_3, 0x00), TAG, "disable unused matrix pins");
    ESP_RETURN_ON_ERROR(write_register(REG_DEBOUNCE_DIS_1, 0), TAG, "enable row debounce");
    ESP_RETURN_ON_ERROR(write_register(REG_DEBOUNCE_DIS_2, 0), TAG, "enable column debounce");
    ESP_RETURN_ON_ERROR(write_register(REG_DEBOUNCE_DIS_3, 0), TAG, "enable matrix debounce");
    ESP_RETURN_ON_ERROR(discard_pending_events(), TAG, "clear keyboard FIFO");
    ESP_RETURN_ON_ERROR(write_register(REG_CFG, CFG_KE_IEN), TAG,
                        "enable TCA8418 key events");
    s_keys = 0;
    ESP_LOGI(TAG, "Cardputer ADV TCA8418 keyboard ready");
    return ESP_OK;
}

extern "C" cardputer_key_mask_t cardputer_keyboard_scan(void)
{
    if (s_keyboard == nullptr) return 0;

    uint8_t event_count;
    esp_err_t status = read_register(REG_KEY_LCK_EC, &event_count);
    if (status != ESP_OK) {
        ESP_LOGW(TAG, "read event count failed: %s", esp_err_to_name(status));
        return s_keys;
    }

    event_count &= EVENT_COUNT_MASK;
    while (event_count-- > 0) {
        uint8_t event;
        status = read_register(REG_KEY_EVENT_A, &event);
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "read key event failed: %s", esp_err_to_name(status));
            break;
        }

        const uint8_t code = event & EVENT_CODE_MASK;
        if (code == 0) continue;
        const uint8_t raw = code - 1U;
        const uint8_t raw_row = raw / 10U;
        const uint8_t raw_column = raw % 10U;
        if (raw_row >= 7 || raw_column >= 8) continue;

        // TCA8418 wiring is interleaved; this is M5Stack's 4x14 logical layout.
        const uint8_t column = raw_row * 2U + (raw_column > 3U ? 1U : 0U);
        const uint8_t row = (raw_column + 4U) % 4U;
        const cardputer_key_mask_t key = cardputer_key_bit(row, column);
        if ((event & EVENT_PRESSED) != 0) {
            s_keys |= key;
        } else {
            s_keys &= ~key;
        }
    }

    status = write_register(REG_INT_STAT, INT_K_INT);
    if (status != ESP_OK) {
        ESP_LOGW(TAG, "clear key interrupt failed: %s", esp_err_to_name(status));
    }
    return s_keys;
}
