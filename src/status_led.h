#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    STATUS_LED_STATE_OFF = 0,
    STATUS_LED_STATE_BOOTING,
    STATUS_LED_STATE_READY,
    STATUS_LED_STATE_ERROR,
} status_led_state_t;

esp_err_t status_led_init(void);
void status_led_set_state(status_led_state_t state);
void status_led_set_on(bool on);
void status_led_toggle(void);
bool status_led_is_on(void);
