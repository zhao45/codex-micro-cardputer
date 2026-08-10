#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cardputer_board_initialize(void);
bool cardputer_board_battery(uint8_t *percentage, bool *charging);

#ifdef __cplusplus
}
#endif
