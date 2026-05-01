#include "mpu6050_tilt.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_display.h"
#include "status_led.h"

#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_WHO_AM_I 0x75
#define RAD_TO_DEG 57.2957795f

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} accel_raw_t;

typedef struct {
    float x;
    float y;
    float z;
} angle_set_t;

static const char *TAG = "mpu6050";

static TaskHandle_t s_mpu_task_handle;
static angle_set_t s_zero_angle;
static angle_set_t s_filtered_angle;
static bool s_filter_ready;
static bool s_led_limit_active;

static esp_err_t mpu6050_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_write_to_device(
        BOARD_OLED_I2C_PORT,
        BOARD_MPU6050_I2C_ADDRESS,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_read_regs(uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_write_read_device(
        BOARD_OLED_I2C_PORT,
        BOARD_MPU6050_I2C_ADDRESS,
        &reg,
        1,
        data,
        size,
        pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_read_accel(accel_raw_t *accel)
{
    uint8_t data[6] = {0};
    ESP_RETURN_ON_ERROR(mpu6050_read_regs(MPU6050_REG_ACCEL_XOUT_H, data, sizeof(data)), TAG, "read accel failed");

    accel->x = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    accel->y = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
    accel->z = (int16_t)(((uint16_t)data[4] << 8) | data[5]);

    return ESP_OK;
}

static angle_set_t mpu6050_compute_axis_angles(const accel_raw_t *accel)
{
    float ax = (float)accel->x;
    float ay = (float)accel->y;
    float az = (float)accel->z;

    angle_set_t angle = {
        .x = atan2f(ax, sqrtf((ay * ay) + (az * az))) * RAD_TO_DEG,
        .y = atan2f(ay, sqrtf((ax * ax) + (az * az))) * RAD_TO_DEG,
        .z = atan2f(az, sqrtf((ax * ax) + (ay * ay))) * RAD_TO_DEG,
    };

    return angle;
}

static float normalize_delta(float delta)
{
    while (delta > 180.0f) {
        delta -= 360.0f;
    }
    while (delta < -180.0f) {
        delta += 360.0f;
    }

    return delta;
}

static angle_set_t mpu6050_angle_delta(angle_set_t angle)
{
    angle_set_t delta = {
        .x = normalize_delta(angle.x - s_zero_angle.x),
        .y = normalize_delta(angle.y - s_zero_angle.y),
        .z = normalize_delta(angle.z - s_zero_angle.z),
    };

    return delta;
}

static angle_set_t mpu6050_filter(angle_set_t delta)
{
    if (!s_filter_ready) {
        s_filtered_angle = delta;
        s_filter_ready = true;
        return s_filtered_angle;
    }

    const float alpha = 1.0f / (float)(1U << BOARD_MPU6050_FILTER_SHIFT);
    s_filtered_angle.x += (delta.x - s_filtered_angle.x) * alpha;
    s_filtered_angle.y += (delta.y - s_filtered_angle.y) * alpha;
    s_filtered_angle.z += (delta.z - s_filtered_angle.z) * alpha;

    return s_filtered_angle;
}

static float max_abs3(float a, float b, float c)
{
    float max = fabsf(a);
    if (fabsf(b) > max) {
        max = fabsf(b);
    }
    if (fabsf(c) > max) {
        max = fabsf(c);
    }

    return max;
}

static void mpu6050_update_outputs(angle_set_t delta)
{
    float max_abs = max_abs3(delta.x, delta.y, delta.z);

    if (!s_led_limit_active && max_abs >= BOARD_MPU6050_LED_ON_THRESHOLD_DEG) {
        s_led_limit_active = true;
    } else if (s_led_limit_active && max_abs <= BOARD_MPU6050_LED_OFF_THRESHOLD_DEG) {
        s_led_limit_active = false;
    }

    status_led_set_on(s_led_limit_active);

    char line[22];
    snprintf(line, sizeof(line), "X:%d Y:%d Z:%d", (int)delta.x, (int)delta.y, (int)delta.z);
    oled_display_set_app_state(line);
}

static void mpu6050_calibrate_zero(void)
{
    angle_set_t sum = {0};
    int valid_samples = 0;

    for (int i = 0; i < BOARD_MPU6050_STARTUP_SAMPLES; i++) {
        accel_raw_t accel = {0};
        if (mpu6050_read_accel(&accel) == ESP_OK) {
            angle_set_t angle = mpu6050_compute_axis_angles(&accel);
            sum.x += angle.x;
            sum.y += angle.y;
            sum.z += angle.z;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (valid_samples > 0) {
        s_zero_angle.x = sum.x / valid_samples;
        s_zero_angle.y = sum.y / valid_samples;
        s_zero_angle.z = sum.z / valid_samples;
    } else {
        s_zero_angle = (angle_set_t){0};
    }

    s_filtered_angle = (angle_set_t){0};
    s_filter_ready = false;
    s_led_limit_active = false;

    ESP_LOGI(TAG, "Zero angle X %.1f Y %.1f Z %.1f", s_zero_angle.x, s_zero_angle.y, s_zero_angle.z);
}

static void mpu6050_task(void *arg)
{
    (void)arg;

    oled_display_set_app_state("MPU CAL");
    mpu6050_calibrate_zero();

    while (true) {
        accel_raw_t accel = {0};
        esp_err_t ret = mpu6050_read_accel(&accel);

        if (ret == ESP_OK) {
            angle_set_t angle = mpu6050_compute_axis_angles(&accel);
            angle_set_t delta = mpu6050_filter(mpu6050_angle_delta(angle));
            mpu6050_update_outputs(delta);
        } else {
            status_led_set_on(false);
            oled_display_set_app_state("MPU ERR");
            ESP_LOGW(TAG, "MPU6050 read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_MPU6050_POLL_INTERVAL_MS));
    }
}

esp_err_t mpu6050_tilt_init(void)
{
    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(mpu6050_read_regs(MPU6050_REG_WHO_AM_I, &who_am_i, 1), TAG, "WHO_AM_I read failed");
    ESP_LOGI(TAG, "WHO_AM_I: 0x%02X", who_am_i);

    ESP_RETURN_ON_ERROR(mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00), TAG, "wake failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00), TAG, "accel range config failed");

    if (s_mpu_task_handle == NULL) {
        BaseType_t created = xTaskCreate(
            mpu6050_task,
            "mpu6050_tilt",
            4096,
            NULL,
            5,
            &s_mpu_task_handle);

        if (created != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(
        TAG,
        "MPU6050 initialized at I2C address 0x%02X on SDA GPIO%d SCL GPIO%d",
        BOARD_MPU6050_I2C_ADDRESS,
        BOARD_OLED_I2C_SDA_GPIO,
        BOARD_OLED_I2C_SCL_GPIO);

    return ESP_OK;
}
