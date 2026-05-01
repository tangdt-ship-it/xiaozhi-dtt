#pragma once

#include "esp_err.h"

esp_err_t oled_display_init(void);
void oled_display_set_led_state(const char *state);
void oled_display_set_network_state(const char *state);
void oled_display_set_app_state(const char *state);
