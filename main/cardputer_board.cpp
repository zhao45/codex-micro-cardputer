#include "cardputer_board.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include <stdlib.h>

static const char *TAG = "cardputer_board";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;
static int s_filtered_battery_mv = -1;
static int s_displayed_percentage = -1;

static int compare_ints(const void *left, const void *right)
{
    const int a = *static_cast<const int *>(left);
    const int b = *static_cast<const int *>(right);
    return (a > b) - (a < b);
}

static int voltage_to_percentage(int battery_mv)
{
    // Cardputer uses a single-cell LiPo. Keep the endpoints conservative and
    // let filtering handle the charger/load ripple between measurements.
    constexpr int empty_mv = 3300;
    constexpr int full_mv = 4150;
    if (battery_mv <= empty_mv) return 0;
    if (battery_mv >= full_mv) return 100;
    return (battery_mv - empty_mv) * 100 / (full_mv - empty_mv);
}

extern "C" esp_err_t cardputer_board_initialize(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_adc), TAG,
                        "create battery ADC");

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_9,
                                                    &channel_config), TAG,
                        "configure GPIO10 battery ADC");

    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_9,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const esp_err_t calibration_status =
        adc_cali_create_scheme_curve_fitting(&calibration_config, &s_calibration);
    if (calibration_status != ESP_OK) {
        s_calibration = nullptr;
        ESP_LOGW(TAG, "ADC calibration unavailable: %s",
                 esp_err_to_name(calibration_status));
    }
    ESP_LOGI(TAG, "Cardputer battery ADC initialized on GPIO10");
    return ESP_OK;
}

extern "C" bool cardputer_board_battery(uint8_t *percentage, bool *charging)
{
    if (s_adc == nullptr || percentage == nullptr || charging == nullptr) {
        return false;
    }
    constexpr size_t sample_count = 16;
    int samples_mv[sample_count];
    for (size_t index = 0; index < sample_count; ++index) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_9, &raw) != ESP_OK) {
            return false;
        }
        int adc_mv = 0;
        if (s_calibration != nullptr) {
            if (adc_cali_raw_to_voltage(s_calibration, raw, &adc_mv) != ESP_OK) {
                return false;
            }
        } else {
            adc_mv = raw * 3100 / 4095;
        }
        samples_mv[index] = adc_mv * 2;
        esp_rom_delay_us(200);
    }

    // Reject the noisiest quartile at each end, then average the middle half.
    qsort(samples_mv, sample_count, sizeof(samples_mv[0]), compare_ints);
    int sum_mv = 0;
    for (size_t index = 4; index < 12; ++index) sum_mv += samples_mv[index];
    const int sampled_mv = sum_mv / 8;

    // An IIR filter prevents USB charging ripple from immediately becoming a
    // large percentage jump. The first reading is initialized without a fake
    // 100% value.
    if (s_filtered_battery_mv < 0) {
        s_filtered_battery_mv = sampled_mv;
    } else {
        s_filtered_battery_mv = (s_filtered_battery_mv * 7 + sampled_mv + 4) / 8;
    }

    const int candidate = voltage_to_percentage(s_filtered_battery_mv);
    if (s_displayed_percentage < 0 || abs(candidate - s_displayed_percentage) >= 2) {
        s_displayed_percentage = candidate;
    }
    *percentage = static_cast<uint8_t>(s_displayed_percentage);
    // Standard Cardputer has no charge-status signal exposed to the MCU.
    *charging = false;
    return true;
}
