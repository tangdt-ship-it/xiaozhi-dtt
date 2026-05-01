#include "board.h"

#include "esp_check.h"
#include "esp_log.h"
#include "oled_display.h"
#include "ps2_gamepad.h"
#include "status_led.h"

static const char *TAG = "board";

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "Initializing board peripherals");
    ESP_RETURN_ON_ERROR(oled_display_init(), TAG, "OLED init failed");
    oled_display_set_app_state("BOOT");
    oled_display_set_network_state("OFFLINE");

    ESP_RETURN_ON_ERROR(status_led_init(), TAG, "status LED init failed");
    ESP_RETURN_ON_ERROR(ps2_gamepad_init(), TAG, "PS2 gamepad init failed");
    oled_display_set_app_state("PS2 READY");
    return ESP_OK;
}
