#include "codex_ble_espidf.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "codex_control_transport.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "codex_ble";

static codex_control_t *s_control;
static const codex_transport_profile_t *s_profile;
static esp_hidd_dev_t *s_hid_device;
static SemaphoreHandle_t s_send_mutex;
static atomic_bool s_connected;
static atomic_bool s_adv_data_ready;
static atomic_bool s_scan_response_ready;
static atomic_bool s_hid_ready;

static esp_hid_raw_report_map_t s_report_maps[1];
static esp_hid_device_config_t s_hid_config;

static uint8_t s_hid_service_uuid[] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t s_advertising_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .appearance = ESP_HID_APPEARANCE_GENERIC,
    .service_uuid_len = sizeof(s_hid_service_uuid),
    .p_service_uuid = s_hid_service_uuid,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

static esp_ble_adv_data_t s_scan_response_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0012,
};

static esp_ble_adv_params_t s_advertising_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising_if_ready(void)
{
    if (!atomic_load(&s_adv_data_ready) || !atomic_load(&s_scan_response_ready) ||
        !atomic_load(&s_hid_ready) || atomic_load(&s_connected)) {
        return;
    }
    const esp_err_t status = esp_ble_gap_start_advertising(&s_advertising_params);
    if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(status));
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *params)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        atomic_store(&s_adv_data_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        atomic_store(&s_scan_response_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (params->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "advertising as %s", s_profile->device_name);
        } else {
            ESP_LOGE(TAG, "advertising failed, status=%d", params->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "pairing %s", params->ble_security.auth_cmpl.success ? "complete" : "failed");
        if (!params->ble_security.auth_cmpl.success) {
            ESP_LOGW(TAG, "pairing failure reason=0x%02x",
                     params->ble_security.auth_cmpl.fail_reason);
        }
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(params->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGD(TAG, "bonding key exchanged, type=%d", params->ble_security.ble_key.key_type);
        break;
    default:
        break;
    }
}

static esp_err_t configure_gap(void)
{
    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG,
                        "register GAP callback");

    esp_ble_auth_req_t auth_request = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io_capability = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t response_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
                                                       &auth_request, sizeof(auth_request)),
                        TAG, "set authentication mode");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_capability,
                                                       sizeof(io_capability)),
                        TAG, "set IO capability");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size,
                                                       sizeof(key_size)),
                        TAG, "set key size");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key,
                                                       sizeof(init_key)),
                        TAG, "set initiator key mask");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &response_key,
                                                       sizeof(response_key)),
                        TAG, "set response key mask");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(s_profile->device_name), TAG,
                        "set device name");
    ESP_RETURN_ON_ERROR(esp_ble_gap_config_adv_data(&s_advertising_data), TAG,
                        "configure advertising data");
    return esp_ble_gap_config_adv_data(&s_scan_response_data);
}

static esp_err_t initialize_bluetooth(void)
{
    const esp_err_t release_status = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (release_status != ESP_OK && release_status != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(release_status, TAG, "release Classic BT memory");
    }

    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&controller_config), TAG,
                        "initialize controller");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG,
                        "enable BLE controller");

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en = false;
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&bluedroid_config), TAG,
                        "initialize Bluedroid");
    return esp_bluedroid_enable();
}

static codex_status_t send_reports(uint8_t report_id, uint8_t report_map_index,
                                   const uint8_t *reports, size_t report_count,
                                   size_t report_size, uint32_t fragment_delay_ms,
                                   void *context)
{
    (void)context;
    if (reports == NULL || report_count == 0 || report_size != s_profile->report_size) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    if (s_hid_device == NULL) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }
    if (!atomic_load(&s_connected)) {
        return CODEX_STATUS_NOT_CONNECTED;
    }
    if (xSemaphoreTake(s_send_mutex, portMAX_DELAY) != pdTRUE) {
        return CODEX_STATUS_TRANSPORT_ERROR;
    }

    codex_status_t result = CODEX_STATUS_OK;
    for (size_t index = 0; index < report_count; ++index) {
        const uint8_t *report = reports + index * report_size;
        const esp_err_t status = esp_hidd_dev_input_set(s_hid_device, report_map_index,
                                                        report_id, (uint8_t *)report,
                                                        report_size);
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "send HID report failed: %s", esp_err_to_name(status));
            result = CODEX_STATUS_TRANSPORT_ERROR;
            break;
        }
        if (index + 1 < report_count && fragment_delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(fragment_delay_ms));
        }
    }
    xSemaphoreGive(s_send_mutex);
    return result;
}

static codex_status_t set_battery(uint8_t percentage, void *context)
{
    (void)context;
    if (s_hid_device == NULL) {
        return CODEX_STATUS_NOT_INITIALIZED;
    }
    const esp_err_t status = esp_hidd_dev_battery_set(s_hid_device, percentage);
    return status == ESP_OK ? CODEX_STATUS_OK : CODEX_STATUS_TRANSPORT_ERROR;
}

static const codex_transport_ops_t s_transport_ops = {
    .send_reports = send_reports,
    .set_battery = set_battery,
};

static void hid_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id,
                              void *event_data)
{
    (void)handler_args;
    (void)event_base;
    const esp_hidd_event_t event = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *params = event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        atomic_store(&s_hid_ready, true);
        start_advertising_if_ready();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        atomic_store(&s_connected, true);
        codex_control_transport_connected(s_control);
        ESP_LOGI(TAG, "host connected");
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        if (params != NULL && params->output.report_id == s_profile->report_id) {
            const codex_status_t status = codex_control_receive_report(
                s_control, params->output.data, params->output.length);
            if (status != CODEX_STATUS_OK && status != CODEX_STATUS_NOT_CONNECTED) {
                ESP_LOGW(TAG, "process output report failed: %s",
                         codex_status_to_string(status));
            }
        }
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        atomic_store(&s_connected, false);
        codex_control_transport_disconnected(s_control);
        ESP_LOGI(TAG, "host disconnected, reason=%d",
                 params != NULL ? params->disconnect.reason : 0);
        start_advertising_if_ready();
        break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
        ESP_LOGD(TAG, "protocol mode=%u", params->protocol_mode.protocol_mode);
        break;
    default:
        break;
    }
}

esp_err_t codex_ble_espidf_start(codex_control_t *control)
{
    ESP_RETURN_ON_FALSE(control != NULL, ESP_ERR_INVALID_ARG, TAG, "control is required");
    ESP_RETURN_ON_FALSE(s_control == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "BLE transport already started");

    s_control = control;
    s_profile = codex_control_transport_profile();
    atomic_init(&s_connected, false);
    atomic_init(&s_adv_data_ready, false);
    atomic_init(&s_scan_response_ready, false);
    atomic_init(&s_hid_ready, false);

    s_report_maps[0].data = (uint8_t *)s_profile->report_map;
    s_report_maps[0].len = s_profile->report_map_size;
    s_hid_config = (esp_hid_device_config_t){
        .vendor_id = s_profile->vendor_id,
        .product_id = s_profile->product_id,
        .version = s_profile->device_version,
        .device_name = s_profile->device_name,
        .manufacturer_name = s_profile->manufacturer_name,
        .serial_number = NULL,
        .report_maps = s_report_maps,
        .report_maps_len = 1,
    };

    const codex_status_t bind_status =
        codex_control_bind_transport(control, &s_transport_ops, NULL);
    ESP_RETURN_ON_FALSE(bind_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                        "bind Codex control transport");

    s_send_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_send_mutex != NULL, ESP_ERR_NO_MEM, TAG, "create send mutex");
    ESP_RETURN_ON_ERROR(initialize_bluetooth(), TAG, "initialize Bluetooth");
    ESP_RETURN_ON_ERROR(configure_gap(), TAG, "configure GAP");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler), TAG,
                        "register GATTS callback");
    ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE,
                                          hid_event_handler, &s_hid_device),
                        TAG, "initialize HID device");

    ESP_LOGI(TAG, "vendor HID ready VID=%04X PID=%04X usage=FF00 report=%u",
             s_profile->vendor_id, s_profile->product_id, s_profile->report_id);
    return ESP_OK;
}

bool codex_ble_espidf_is_connected(void)
{
    return atomic_load(&s_connected);
}
