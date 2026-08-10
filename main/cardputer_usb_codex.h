#pragma once

#include <stdbool.h>

#include "codex_control.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cardputer_usb_codex_start(codex_control_t *control);
bool cardputer_usb_codex_is_connected(void);
bool cardputer_usb_codex_host_active(void);

#ifdef __cplusplus
}
#endif
