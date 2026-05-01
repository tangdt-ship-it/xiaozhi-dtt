#include "xiaozhi_app.h"

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "oled_display.h"

static const char *TAG = "xiaozhi_app";

static TaskHandle_t s_app_task_handle;

static void xiaozhi_app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "%s v%s started", APP_NAME, APP_VERSION);
    ESP_LOGI(TAG, "Xiaozhi server URL: %s", XIAOZHI_SERVER_URL);

    if (network_is_connected()) {
        ESP_LOGI(TAG, "Network is ready; Xiaozhi protocol layer can start here");
    } else {
        ESP_LOGW(TAG, "Network is offline; running in development mode");
    }

    while (true) {
        /*
         * Future extension points:
         * - Audio input/output drivers
         * - Wake word detection
         * - WebSocket/MQTT transport
         * - Xiaozhi message protocol
         * - Device control via MCP tools
         */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t xiaozhi_app_start(void)
{
    if (s_app_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(
        xiaozhi_app_task,
        "xiaozhi_app",
        XIAOZHI_APP_TASK_STACK_SIZE,
        NULL,
        XIAOZHI_APP_TASK_PRIORITY,
        &s_app_task_handle);

    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
