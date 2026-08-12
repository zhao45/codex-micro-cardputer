#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cardputer_key_mask_t;

enum {
    CARDPUTER_KEY_TAB = 14,
    CARDPUTER_KEY_R = 18,
    CARDPUTER_KEY_Y = 20,
    CARDPUTER_KEY_P = 24,
    CARDPUTER_KEY_F = 33,
    CARDPUTER_KEY_UP = 39,
    CARDPUTER_KEY_ENTER = 41,
    CARDPUTER_KEY_C = 47,
    CARDPUTER_KEY_N = 50,
    CARDPUTER_KEY_M = 51,
    CARDPUTER_KEY_LEFT = 52,
    CARDPUTER_KEY_DOWN = 53,
    CARDPUTER_KEY_RIGHT = 54,
    CARDPUTER_KEY_SPACE = 55,
};

esp_err_t cardputer_keyboard_initialize(void);
cardputer_key_mask_t cardputer_keyboard_scan(void);

static inline cardputer_key_mask_t cardputer_key_bit(uint8_t row, uint8_t column)
{
    return 1ULL << (row * 14U + column);
}

#ifdef __cplusplus
}
#endif
