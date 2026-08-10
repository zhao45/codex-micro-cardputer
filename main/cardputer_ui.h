#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codex_control.h"
#include "esp_err.h"

typedef enum {
    CARDPUTER_UI_STATUS = 0,
    CARDPUTER_UI_KEY_MAP,
} cardputer_ui_page_t;

typedef enum {
    CARDPUTER_TRANSPORT_NONE = 0,
    CARDPUTER_TRANSPORT_BLE,
    CARDPUTER_TRANSPORT_USB,
} cardputer_ui_transport_t;

typedef struct {
    bool known;
    uint32_t color_rgb;
    float brightness;
    char effect[CODEX_EFFECT_NAME_SIZE];
} cardputer_ui_agent_t;

typedef struct {
    cardputer_ui_page_t page;
    bool ble_connected;
    bool usb_connected;
    bool battery_valid;
    cardputer_ui_transport_t active_transport;
    uint8_t battery_percentage;
    uint8_t selected_agent;
    cardputer_ui_agent_t agents[CODEX_AGENT_COUNT];
} cardputer_ui_state_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cardputer_ui_start(void);
void cardputer_ui_update(const cardputer_ui_state_t *state);

#ifdef __cplusplus
}
#endif
