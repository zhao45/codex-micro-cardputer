#pragma once

#include <stdbool.h>

#include "codex_control.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the singleton ESP-IDF BLE HID adapter and binds it to control. */
esp_err_t codex_ble_espidf_start(codex_control_t *control);
bool codex_ble_espidf_is_connected(void);

#ifdef __cplusplus
}
#endif
