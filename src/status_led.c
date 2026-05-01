#include "status_led.h"

#include <stdbool.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "oled_display.h"

static const char *TAG = "status_led";

static bool s_led_on;

static void status_led_write(bool on)
{
    const int level = on ? BOARD_STATUS_LED_ACTIVE_LEVEL : !BOARD_STATUS_LED_ACTIVE_LEVEL;
    gpio_set_level(BOARD_STATUS_LED_GPIO, level);
}

esp_err_t status_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio config failed");
    status_led_set_on(false);
    return ESP_OK;
}

void status_led_set_state(status_led_state_t state)
{
    status_led_set_on(state == STATUS_LED_STATE_READY);
}

void status_led_set_on(bool on)
{
    s_led_on = on;
    status_led_write(s_led_on);
    oled_display_set_led_state(s_led_on ? "ON" : "OFF");
}

void status_led_toggle(void)
{
    status_led_set_on(!s_led_on);
}

bool status_led_is_on(void)
{
    return s_led_on;
}
