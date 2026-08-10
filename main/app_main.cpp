#include "cardputer_board.h"
#include "cardputer_keyboard.h"
#include "cardputer_usb_codex.h"
#include "cardputer_usb_mic.h"
#include "cardputer_ui.h"
#include "codex_ble_espidf.h"
#include "codex_control.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "cardputer_codex";
static const char *TARGET_HOST = "Windows 11";
static codex_control_t *s_codex_ble;
static codex_control_t *s_codex_usb;
static SemaphoreHandle_t s_state_mutex;
static cardputer_ui_state_t s_ui;
static bool s_mic_action_active;
static codex_control_t *s_mic_action_control;

typedef enum {
    CODEX_TRANSPORT_BLE = 0,
    CODEX_TRANSPORT_USB,
} codex_transport_kind_t;

static esp_err_t initialize_nvs(void)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        status = nvs_flash_init();
    }
    return status;
}

static void publish_ui(void)
{
    cardputer_ui_state_t copy;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    copy = s_ui;
    xSemaphoreGive(s_state_mutex);
    cardputer_ui_update(&copy);
}

static cardputer_ui_transport_t active_transport(void)
{
    const bool usb_connected = cardputer_usb_codex_is_connected();
    const bool ble_connected = codex_ble_espidf_is_connected();
    if (usb_connected && (!ble_connected || cardputer_usb_codex_host_active())) {
        return CARDPUTER_TRANSPORT_USB;
    }
    if (ble_connected) return CARDPUTER_TRANSPORT_BLE;
    if (usb_connected) return CARDPUTER_TRANSPORT_USB;
    return CARDPUTER_TRANSPORT_NONE;
}

static codex_control_t *active_control(void)
{
    return active_transport() == CARDPUTER_TRANSPORT_USB ? s_codex_usb : s_codex_ble;
}

static codex_status_t send_action_to(codex_control_t *control, codex_action_t action,
                                     codex_action_phase_t phase)
{
    const codex_status_t status = codex_control_send_action(control, action, phase);
    if (status != CODEX_STATUS_OK) {
        ESP_LOGW(TAG, "action %d phase %d not sent: %s", (int)action, (int)phase,
                 codex_status_to_string(status));
    }
    return status;
}

static void click_action(codex_action_t action)
{
    codex_control_t *control = active_control();
    if (send_action_to(control, action, CODEX_ACTION_PRESS) == CODEX_STATUS_OK) {
        vTaskDelay(pdMS_TO_TICKS(30));
        send_action_to(control, action, CODEX_ACTION_RELEASE);
    }
}

static void click_agent(uint8_t agent)
{
    codex_control_t *control = active_control();
    const codex_status_t status =
        codex_control_send_agent(control, agent, CODEX_ACTION_PRESS);
    if (status != CODEX_STATUS_OK) {
        ESP_LOGW(TAG, "agent %u not sent: %s", agent + 1, codex_status_to_string(status));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
    codex_control_send_agent(control, agent, CODEX_ACTION_RELEASE);
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_ui.selected_agent = agent;
    xSemaphoreGive(s_state_mutex);
    publish_ui();
}

static void codex_event(const codex_event_t *event, void *context)
{
    const codex_transport_kind_t kind =
        static_cast<codex_transport_kind_t>(reinterpret_cast<uintptr_t>(context));
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (event->type == CODEX_EVENT_CONNECTED) {
        if (kind == CODEX_TRANSPORT_USB) s_ui.usb_connected = true;
        else s_ui.ble_connected = true;
    } else if (event->type == CODEX_EVENT_DISCONNECTED) {
        if (kind == CODEX_TRANSPORT_USB) s_ui.usb_connected = false;
        else s_ui.ble_connected = false;
    } else if (event->type == CODEX_EVENT_HOST_STATUS) {
        for (size_t index = 0; index < event->agent_status_count; ++index) {
            const codex_agent_status_t &update = event->agent_statuses[index];
            if (update.id >= CODEX_AGENT_COUNT) continue;
            cardputer_ui_agent_t &agent = s_ui.agents[update.id];
            agent.known = true;
            if (update.fields & CODEX_AGENT_FIELD_COLOR) agent.color_rgb = update.color_rgb;
            if (update.fields & CODEX_AGENT_FIELD_BRIGHTNESS) agent.brightness = update.brightness;
            if (update.fields & CODEX_AGENT_FIELD_EFFECT) {
                memcpy(agent.effect, update.effect, sizeof(agent.effect));
                if (strcmp(agent.effect, "breath") == 0) s_ui.selected_agent = update.id;
            }
        }
    }
    xSemaphoreGive(s_state_mutex);
    publish_ui();
}

static esp_err_t create_codex(codex_transport_kind_t kind, codex_control_t **control)
{
    const codex_control_config_t config = {
        .firmware_version = "0.4.0-cardputer-adv-composite",
        .profile_index = 0,
        .layer_index = 1,
        .event_callback = codex_event,
        .event_context = reinterpret_cast<void *>(static_cast<uintptr_t>(kind)),
    };
    const codex_status_t status = codex_control_create(&config, control);
    ESP_RETURN_ON_FALSE(status == CODEX_STATUS_OK, ESP_FAIL, TAG,
                        "create Codex control: %s", codex_status_to_string(status));
    return ESP_OK;
}

static esp_err_t initialize_codex(void)
{
    ESP_RETURN_ON_ERROR(create_codex(CODEX_TRANSPORT_BLE, &s_codex_ble), TAG,
                        "create BLE control");
    ESP_RETURN_ON_ERROR(create_codex(CODEX_TRANSPORT_USB, &s_codex_usb), TAG,
                        "create USB control");
    ESP_RETURN_ON_ERROR(codex_ble_espidf_start(s_codex_ble), TAG,
                        "start BLE transport");
    return cardputer_usb_codex_start(s_codex_usb);
}

static bool pressed(cardputer_key_mask_t keys, uint8_t key)
{
    return (keys & (1ULL << key)) != 0;
}

static void controls_task(void *)
{
    cardputer_key_mask_t stable = 0;
    cardputer_key_mask_t previous_sample = 0;
    int64_t battery_ticks = 0;
    cardputer_ui_transport_t previous_transport = CARDPUTER_TRANSPORT_NONE;
    while (true) {
        const cardputer_key_mask_t sample = cardputer_keyboard_scan();
        if (sample == previous_sample && sample != stable) {
            const cardputer_key_mask_t down = sample & ~stable;
            const cardputer_key_mask_t up = stable & ~sample;
            stable = sample;

            if (pressed(down, CARDPUTER_KEY_SPACE)) {
                xSemaphoreTake(s_state_mutex, portMAX_DELAY);
                s_ui.page = s_ui.page == CARDPUTER_UI_STATUS ?
                            CARDPUTER_UI_KEY_MAP : CARDPUTER_UI_STATUS;
                xSemaphoreGive(s_state_mutex);
                publish_ui();
            }
            if (pressed(down, CARDPUTER_KEY_ENTER)) click_action(CODEX_ACTION_SEND);
            if (pressed(down, CARDPUTER_KEY_Y)) click_action(CODEX_ACTION_APPROVE);
            if (pressed(down, CARDPUTER_KEY_N)) click_action(CODEX_ACTION_DECLINE);
            if (pressed(down, CARDPUTER_KEY_F)) click_action(CODEX_ACTION_FAST);
            if (pressed(down, CARDPUTER_KEY_TAB)) click_action(CODEX_ACTION_FORK);

            if (pressed(down, CARDPUTER_KEY_M)) {
                codex_control_t *control = active_control();
                if (send_action_to(control, CODEX_ACTION_MIC,
                                   CODEX_ACTION_PRESS) == CODEX_STATUS_OK) {
                    s_mic_action_active = true;
                    s_mic_action_control = control;
                }
            }
            if (pressed(up, CARDPUTER_KEY_M)) {
                if (s_mic_action_active) {
                    send_action_to(s_mic_action_control, CODEX_ACTION_MIC,
                                   CODEX_ACTION_RELEASE);
                }
                s_mic_action_active = false;
                s_mic_action_control = nullptr;
            }
            for (uint8_t agent = 0; agent < CODEX_AGENT_COUNT; ++agent) {
                if (down & cardputer_key_bit(0, agent + 1)) click_agent(agent);
            }
        }
        previous_sample = sample;

        const cardputer_ui_transport_t transport = active_transport();
        if (transport != previous_transport) {
            previous_transport = transport;
            xSemaphoreTake(s_state_mutex, portMAX_DELAY);
            s_ui.active_transport = transport;
            s_ui.ble_connected = codex_ble_espidf_is_connected();
            s_ui.usb_connected = cardputer_usb_codex_is_connected();
            xSemaphoreGive(s_state_mutex);
            publish_ui();
        }

        if (++battery_ticks >= 500) {
            battery_ticks = 0;
            uint8_t percentage;
            bool charging;
            if (cardputer_board_battery(&percentage, &charging)) {
                codex_control_set_battery(s_codex_ble, percentage, charging);
                codex_control_set_battery(s_codex_usb, percentage, charging);
                xSemaphoreTake(s_state_mutex, portMAX_DELAY);
                s_ui.battery_percentage = percentage;
                s_ui.battery_valid = true;
                xSemaphoreGive(s_state_mutex);
                publish_ui();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(initialize_nvs());
    s_state_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_state_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
    memset(&s_ui, 0, sizeof(s_ui));
    for (auto &agent : s_ui.agents) {
        agent.color_rgb = 0x394B59;
        strcpy(agent.effect, "off");
    }

    ESP_ERROR_CHECK(cardputer_board_initialize());
    uint8_t initial_percentage;
    bool initial_charging;
    if (cardputer_board_battery(&initial_percentage, &initial_charging)) {
        s_ui.battery_percentage = initial_percentage;
        s_ui.battery_valid = true;
    }
    ESP_ERROR_CHECK(cardputer_keyboard_initialize());
    ESP_ERROR_CHECK(cardputer_ui_start());
    publish_ui();
    ESP_ERROR_CHECK(initialize_codex());
    ESP_ERROR_CHECK(xTaskCreate(controls_task, "cardputer_keys", 4096, nullptr, 5, nullptr) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Codex Micro (Cardputer ADV) ready");
    ESP_LOGI(TAG, "Target host: %s (USB preferred, BLE fallback)", TARGET_HOST);
    ESP_LOGI(TAG, "Enter Send | M Mic hold | Y Approve | N Decline | F Fast | Tab Fork");
    ESP_LOGI(TAG, "1-6 Agents | Space local page toggle");
    ESP_LOGI(TAG, "Starting Windows 11 composite USB microphone + Codex HID");
    ESP_ERROR_CHECK(cardputer_usb_mic_start());
    ESP_LOGI(TAG, "CODEX_MICRO_CARDPUTER_READY USB_COMPOSITE_READY BLE_FALLBACK_READY");
}
