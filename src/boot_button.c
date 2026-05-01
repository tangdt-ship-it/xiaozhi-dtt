#include "boot_button.h"

#include <stdbool.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_display.h"
#include "status_led.h"

static const char *TAG = "boot_button";

static TaskHandle_t s_button_task_handle;

static bool boot_button_is_pressed(void)
{
    return gpio_get_level(BOARD_BOOT_BUTTON_GPIO) == BOARD_BOOT_BUTTON_ACTIVE_LEVEL;
}

static void boot_button_task(void *arg)
{
    (void)arg;

    bool last_pressed = boot_button_is_pressed();

    while (true) {
        bool pressed = boot_button_is_pressed();

        if (pressed != last_pressed) {
            vTaskDelay(pdMS_TO_TICKS(BOARD_BOOT_BUTTON_DEBOUNCE_MS));
            pressed = boot_button_is_pressed();

            if (pressed != last_pressed) {
                last_pressed = pressed;

                if (pressed) {
                    status_led_toggle();
                    oled_display_set_app_state(status_led_is_on() ? "LED ON" : "LED OFF");
                    ESP_LOGI(TAG, "BOOT button pressed, LED is %s", status_led_is_on() ? "ON" : "OFF");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t boot_button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "button gpio config failed");

    if (s_button_task_handle == NULL) {
        BaseType_t created = xTaskCreate(
            boot_button_task,
            "boot_button",
            2048,
            NULL,
            4,
            &s_button_task_handle);

        if (created != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "BOOT button initialized on GPIO%d", BOARD_BOOT_BUTTON_GPIO);
    return ESP_OK;
}
