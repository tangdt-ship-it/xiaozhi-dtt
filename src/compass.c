#include "compass.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_config.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_display.h"
#include "status_led.h"

static const char *TAG = "compass";

static TaskHandle_t s_compass_task_handle;
static int16_t s_zero_offset_deci_deg;
static int32_t s_filtered_deci_deg;
static bool s_filter_ready;
static bool s_led_limit_active;

static esp_err_t compass_write_command(char command)
{
    int written = uart_write_bytes(BOARD_COMPASS_UART_PORT, &command, 1);
    if (written != 1) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t compass_read_angle(int16_t *angle_deci_deg)
{
    uart_flush_input(BOARD_COMPASS_UART_PORT);
    ESP_RETURN_ON_ERROR(compass_write_command('z'), TAG, "send read command failed");
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t data[2] = {0};
    int read = uart_read_bytes(
        BOARD_COMPASS_UART_PORT,
        data,
        sizeof(data),
        pdMS_TO_TICKS(50));

    if (read != sizeof(data)) {
        return ESP_ERR_TIMEOUT;
    }

    *angle_deci_deg = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ESP_OK;
}

static int16_t compass_apply_filter(int16_t raw_deci_deg)
{
    int16_t corrected_deci_deg = raw_deci_deg - s_zero_offset_deci_deg;

    if (abs(corrected_deci_deg) <= BOARD_COMPASS_DEADBAND_DECI_DEG) {
        corrected_deci_deg = 0;
    }

    if (!s_filter_ready) {
        s_filtered_deci_deg = corrected_deci_deg;
        s_filter_ready = true;
    } else {
        s_filtered_deci_deg += ((int32_t)corrected_deci_deg - s_filtered_deci_deg) >> BOARD_COMPASS_FILTER_SHIFT;
    }

    return (int16_t)s_filtered_deci_deg;
}

static void compass_update_outputs(int16_t raw_deci_deg)
{
    int16_t angle_deci_deg = compass_apply_filter(raw_deci_deg);
    int angle_abs_deci_deg = abs(angle_deci_deg);

    if (!s_led_limit_active && angle_abs_deci_deg >= BOARD_COMPASS_LED_ON_THRESHOLD_DECI_DEG) {
        s_led_limit_active = true;
    } else if (s_led_limit_active && angle_abs_deci_deg <= BOARD_COMPASS_LED_OFF_THRESHOLD_DECI_DEG) {
        s_led_limit_active = false;
    }

    status_led_set_on(s_led_limit_active);

    char line[22];
    snprintf(line, sizeof(line), "ANG:%d.%d", angle_deci_deg / 10, abs(angle_deci_deg % 10));
    oled_display_set_app_state(line);
}

static void compass_calibrate_offset(void)
{
    int32_t sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < BOARD_COMPASS_STARTUP_SAMPLES; i++) {
        int16_t angle_deci_deg = 0;
        if (compass_read_angle(&angle_deci_deg) == ESP_OK) {
            sum += angle_deci_deg;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (valid_samples > 0) {
        s_zero_offset_deci_deg = (int16_t)(sum / valid_samples);
    } else {
        s_zero_offset_deci_deg = 0;
    }

    s_filtered_deci_deg = 0;
    s_filter_ready = false;
    s_led_limit_active = false;

    ESP_LOGI(TAG, "Compass zero offset: %d.%d deg", s_zero_offset_deci_deg / 10, abs(s_zero_offset_deci_deg % 10));
}

static void compass_task(void *arg)
{
    (void)arg;

#if BOARD_COMPASS_ZERO_ON_STARTUP
    ESP_LOGI(TAG, "Zeroing compass at current heading");
    if (compass_write_command('a') != ESP_OK) {
        ESP_LOGW(TAG, "Compass zero command failed");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    uart_flush_input(BOARD_COMPASS_UART_PORT);
#endif

    compass_calibrate_offset();

    while (true) {
        int16_t angle_deci_deg = 0;
        esp_err_t ret = compass_read_angle(&angle_deci_deg);

        if (ret == ESP_OK) {
            compass_update_outputs(angle_deci_deg);
        } else {
            status_led_set_on(false);
            oled_display_set_app_state("COMPASS ERR");
            ESP_LOGW(TAG, "Compass read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_COMPASS_POLL_INTERVAL_MS));
    }
}

esp_err_t compass_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = BOARD_COMPASS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(BOARD_COMPASS_UART_PORT, 256, 0, 0, NULL, 0), TAG, "UART install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(BOARD_COMPASS_UART_PORT, &uart_config), TAG, "UART config failed");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(
            BOARD_COMPASS_UART_PORT,
            BOARD_COMPASS_UART_TX_GPIO,
            BOARD_COMPASS_UART_RX_GPIO,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE),
        TAG,
        "UART pin config failed");

    if (s_compass_task_handle == NULL) {
        BaseType_t created = xTaskCreate(
            compass_task,
            "compass",
            3072,
            NULL,
            5,
            &s_compass_task_handle);

        if (created != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(
        TAG,
        "Compass initialized UART%d TX GPIO%d RX GPIO%d baud %d",
        BOARD_COMPASS_UART_PORT,
        BOARD_COMPASS_UART_TX_GPIO,
        BOARD_COMPASS_UART_RX_GPIO,
        BOARD_COMPASS_UART_BAUD_RATE);

    return ESP_OK;
}
