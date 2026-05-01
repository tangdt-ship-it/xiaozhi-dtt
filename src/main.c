#include "board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "network.h"
#include "status_led.h"
#include "xiaozhi_app.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting Xiaozhi chatbox firmware");

    esp_err_t ret = board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = network_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(ret));
        status_led_set_state(STATUS_LED_STATE_ERROR);
        return;
    }

    ret = network_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network start skipped or failed: %s", esp_err_to_name(ret));
    }

    ret = xiaozhi_app_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Application start failed: %s", esp_err_to_name(ret));
        status_led_set_state(STATUS_LED_STATE_ERROR);
    }
}
