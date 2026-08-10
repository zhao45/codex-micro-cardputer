#include "cardputer_usb_codex.h"

#include <atomic>
#include <string.h>

#include "codex_control_transport.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tusb.h"

static const char *TAG = "cardputer_usb_codex";
static constexpr size_t USB_HID_PACKET_SIZE = 64;

typedef enum {
    USB_EVENT_MOUNTED,
    USB_EVENT_UNMOUNTED,
    USB_EVENT_REPORT,
} usb_event_type_t;

typedef struct {
    usb_event_type_t type;
    uint16_t length;
    uint8_t data[USB_HID_PACKET_SIZE];
} usb_event_t;

static codex_control_t *s_control;
static const codex_transport_profile_t *s_profile;
static QueueHandle_t s_event_queue;
static SemaphoreHandle_t s_send_mutex;
static std::atomic_bool s_connected(false);
static std::atomic_bool s_host_active(false);

static void queue_simple_event(usb_event_type_t type)
{
    if (s_event_queue == nullptr) return;
    const usb_event_t event = {.type = type, .length = 0, .data = {}};
    xQueueSend(s_event_queue, &event, 0);
}

static void usb_event_task(void *)
{
    usb_event_t event;
    while (true) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        switch (event.type) {
        case USB_EVENT_MOUNTED:
            s_connected.store(true);
            s_host_active.store(false);
            codex_control_transport_connected(s_control);
            ESP_LOGI(TAG, "composite USB mounted");
            break;
        case USB_EVENT_UNMOUNTED:
            s_connected.store(false);
            s_host_active.store(false);
            codex_control_transport_disconnected(s_control);
            ESP_LOGI(TAG, "composite USB unavailable; BLE fallback enabled");
            break;
        case USB_EVENT_REPORT: {
            const codex_status_t status =
                codex_control_receive_report(s_control, event.data, event.length);
            if (status == CODEX_STATUS_OK) {
                if (!s_host_active.exchange(true)) {
                    ESP_LOGI(TAG, "Codex host detected on USB; USB control has priority");
                }
            } else {
                ESP_LOGW(TAG, "discard USB HID output report: %s",
                         codex_status_to_string(status));
            }
            break;
        }
        }
    }
}

static codex_status_t send_reports(uint8_t report_id, uint8_t report_map_index,
                                   const uint8_t *reports, size_t report_count,
                                   size_t report_size, uint32_t fragment_delay_ms,
                                   void *)
{
    if (reports == nullptr || report_count == 0 || report_map_index != 0 ||
        s_profile == nullptr || report_size != s_profile->report_size) {
        return CODEX_STATUS_INVALID_ARGUMENT;
    }
    if (!s_connected.load()) return CODEX_STATUS_NOT_CONNECTED;
    if (xSemaphoreTake(s_send_mutex, portMAX_DELAY) != pdTRUE) {
        return CODEX_STATUS_TRANSPORT_ERROR;
    }

    codex_status_t result = CODEX_STATUS_OK;
    for (size_t index = 0; index < report_count; ++index) {
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100);
        while (s_connected.load() && !tud_hid_ready() &&
               static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!s_connected.load() || !tud_hid_ready() ||
            !tud_hid_report(report_id, reports + index * report_size, report_size)) {
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

static codex_status_t set_battery(uint8_t, void *)
{
    // USB has no separate Battery Service; the value remains available through
    // the existing Codex protocol status response.
    return CODEX_STATUS_OK;
}

static const codex_transport_ops_t s_transport_ops = {
    .send_reports = send_reports,
    .set_battery = set_battery,
};

extern "C" esp_err_t cardputer_usb_codex_start(codex_control_t *control)
{
    ESP_RETURN_ON_FALSE(control != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "control is required");
    ESP_RETURN_ON_FALSE(s_control == nullptr, ESP_ERR_INVALID_STATE, TAG,
                        "USB Codex transport already started");
    s_control = control;
    s_profile = codex_control_transport_profile();
    ESP_RETURN_ON_FALSE(s_profile->report_size + 1 <= USB_HID_PACKET_SIZE,
                        ESP_ERR_INVALID_SIZE, TAG, "Codex HID report is too large");

    s_event_queue = xQueueCreate(12, sizeof(usb_event_t));
    s_send_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_event_queue != nullptr && s_send_mutex != nullptr,
                        ESP_ERR_NO_MEM, TAG, "allocate USB transport resources");
    ESP_RETURN_ON_FALSE(xTaskCreate(usb_event_task, "usb_codex_events", 4096,
                                    nullptr, 6, nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create USB event task");

    const codex_status_t bind_status =
        codex_control_bind_transport(control, &s_transport_ops, nullptr);
    ESP_RETURN_ON_FALSE(bind_status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                        "bind USB Codex transport: %s",
                        codex_status_to_string(bind_status));
    ESP_LOGI(TAG, "USB vendor HID ready VID=%04X PID=%04X report=%u",
             s_profile->vendor_id, s_profile->product_id, s_profile->report_id);
    return ESP_OK;
}

extern "C" bool cardputer_usb_codex_is_connected(void)
{
    return s_connected.load();
}

extern "C" bool cardputer_usb_codex_host_active(void)
{
    return s_host_active.load();
}

extern "C" void tud_mount_cb(void)
{
    queue_simple_event(USB_EVENT_MOUNTED);
}

extern "C" void tud_umount_cb(void)
{
    queue_simple_event(USB_EVENT_UNMOUNTED);
}

extern "C" void tud_suspend_cb(bool)
{
    queue_simple_event(USB_EVENT_UNMOUNTED);
}

extern "C" void tud_resume_cb(void)
{
    queue_simple_event(USB_EVENT_MOUNTED);
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t report_id,
                                            hid_report_type_t report_type,
                                            uint8_t *buffer, uint16_t requested_length)
{
    if (s_profile == nullptr || report_id != s_profile->report_id ||
        report_type != HID_REPORT_TYPE_INPUT ||
        requested_length < s_profile->report_size) {
        return 0;
    }
    memset(buffer, 0, s_profile->report_size);
    return static_cast<uint16_t>(s_profile->report_size);
}

extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t report_id,
                                        hid_report_type_t report_type,
                                        const uint8_t *buffer, uint16_t length)
{
    if (s_event_queue == nullptr || buffer == nullptr ||
        report_type != HID_REPORT_TYPE_OUTPUT) {
        return;
    }
    usb_event_t event = {.type = USB_EVENT_REPORT, .length = 0, .data = {}};
    if (report_id != 0) {
        event.data[event.length++] = report_id;
    }
    const size_t available = sizeof(event.data) - event.length;
    const size_t copy_length = length < available ? length : available;
    memcpy(event.data + event.length, buffer, copy_length);
    event.length += static_cast<uint16_t>(copy_length);
    xQueueSend(s_event_queue, &event, 0);
}
