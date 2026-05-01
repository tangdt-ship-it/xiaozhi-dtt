#include "ps2_gamepad.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_display.h"
#include "status_led.h"

#define PS2_BTN_SELECT   0x0001
#define PS2_BTN_L3       0x0002
#define PS2_BTN_R3       0x0004
#define PS2_BTN_START    0x0008
#define PS2_BTN_UP       0x0010
#define PS2_BTN_RIGHT    0x0020
#define PS2_BTN_DOWN     0x0040
#define PS2_BTN_LEFT     0x0080
#define PS2_BTN_L2       0x0100
#define PS2_BTN_R2       0x0200
#define PS2_BTN_L1       0x0400
#define PS2_BTN_R1       0x0800
#define PS2_BTN_TRIANGLE 0x1000
#define PS2_BTN_CIRCLE   0x2000
#define PS2_BTN_CROSS    0x4000
#define PS2_BTN_SQUARE   0x8000

typedef struct {
    uint16_t mask;
    const char *name;
} ps2_button_name_t;

static const char *TAG = "ps2";

static const ps2_button_name_t s_buttons[] = {
    {PS2_BTN_UP, "UP"},
    {PS2_BTN_RIGHT, "RIGHT"},
    {PS2_BTN_DOWN, "DOWN"},
    {PS2_BTN_LEFT, "LEFT"},
    {PS2_BTN_START, "START"},
    {PS2_BTN_SELECT, "SELECT"},
    {PS2_BTN_L1, "L1"},
    {PS2_BTN_R1, "R1"},
    {PS2_BTN_L2, "L2"},
    {PS2_BTN_R2, "R2"},
    {PS2_BTN_L3, "L3"},
    {PS2_BTN_R3, "R3"},
    {PS2_BTN_TRIANGLE, "TRIANGLE"},
    {PS2_BTN_CIRCLE, "CIRCLE"},
    {PS2_BTN_CROSS, "CROSS"},
    {PS2_BTN_SQUARE, "SQUARE"},
};

static TaskHandle_t s_ps2_task_handle;
static uint16_t s_last_buttons;
static const char *s_last_display = "";

static uint8_t ps2_transfer_byte(uint8_t out)
{
    uint8_t in = 0;

    for (int bit = 0; bit < 8; bit++) {
        gpio_set_level(BOARD_PS2_CMD_GPIO, (out >> bit) & 0x01);
        esp_rom_delay_us(BOARD_PS2_CLOCK_DELAY_US);

        gpio_set_level(BOARD_PS2_CLK_GPIO, 0);
        esp_rom_delay_us(BOARD_PS2_CLOCK_DELAY_US);

        if (gpio_get_level(BOARD_PS2_DAT_GPIO)) {
            in |= (1U << bit);
        }

        gpio_set_level(BOARD_PS2_CLK_GPIO, 1);
        esp_rom_delay_us(BOARD_PS2_CLOCK_DELAY_US);
    }

    gpio_set_level(BOARD_PS2_CMD_GPIO, 1);
    return in;
}

static void ps2_transaction(const uint8_t *tx, uint8_t *rx, size_t length)
{
    gpio_set_level(BOARD_PS2_ATT_GPIO, 0);
    esp_rom_delay_us(BOARD_PS2_CLOCK_DELAY_US);

    for (size_t i = 0; i < length; i++) {
        rx[i] = ps2_transfer_byte(tx[i]);
    }

    gpio_set_level(BOARD_PS2_ATT_GPIO, 1);
    esp_rom_delay_us(BOARD_PS2_CLOCK_DELAY_US);
}

static void ps2_send_command(const uint8_t *tx, size_t length)
{
    uint8_t rx[9] = {0};
    ps2_transaction(tx, rx, length);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void ps2_enter_analog_mode(void)
{
    static const uint8_t enter_config[] = {0x01, 0x43, 0x00, 0x01, 0x00};
    static const uint8_t set_analog[] = {0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t exit_config[] = {0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};

    ps2_send_command(enter_config, sizeof(enter_config));
    ps2_send_command(set_analog, sizeof(set_analog));
    ps2_send_command(exit_config, sizeof(exit_config));
}

static bool ps2_read(uint16_t *buttons, uint8_t sticks[4])
{
    static const uint8_t tx[] = {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t rx[sizeof(tx)] = {0};

    ps2_transaction(tx, rx, sizeof(tx));

    if (rx[1] == 0xFF || rx[2] != 0x5A) {
        return false;
    }

    *buttons = (uint16_t)(~(((uint16_t)rx[4] << 8) | rx[3]));
    sticks[0] = rx[7]; /* Left X */
    sticks[1] = rx[8]; /* Left Y */
    sticks[2] = rx[5]; /* Right X */
    sticks[3] = rx[6]; /* Right Y */

    return true;
}

static const char *ps2_first_pressed_button(uint16_t buttons)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        if (buttons & s_buttons[i].mask) {
            return s_buttons[i].name;
        }
    }

    return NULL;
}

static const char *ps2_stick_name(const uint8_t sticks[4])
{
    int lx = (int)sticks[0] - 128;
    int ly = (int)sticks[1] - 128;
    int rx = (int)sticks[2] - 128;
    int ry = (int)sticks[3] - 128;

    if (abs(lx) > BOARD_PS2_STICK_DEADZONE) {
        return lx < 0 ? "LSTICK LEFT" : "LSTICK RIGHT";
    }
    if (abs(ly) > BOARD_PS2_STICK_DEADZONE) {
        return ly < 0 ? "LSTICK UP" : "LSTICK DOWN";
    }
    if (abs(rx) > BOARD_PS2_STICK_DEADZONE) {
        return rx < 0 ? "RSTICK LEFT" : "RSTICK RIGHT";
    }
    if (abs(ry) > BOARD_PS2_STICK_DEADZONE) {
        return ry < 0 ? "RSTICK UP" : "RSTICK DOWN";
    }

    return NULL;
}

static void ps2_update_outputs(uint16_t buttons, const uint8_t sticks[4])
{
    const char *display = ps2_first_pressed_button(buttons);
    bool active = display != NULL;

    if (display == NULL) {
        display = ps2_stick_name(sticks);
        active = display != NULL;
    }

    status_led_set_on(active);

    if (!active) {
        display = "PS2 READY";
    }

    if (buttons != s_last_buttons || display != s_last_display) {
        s_last_buttons = buttons;
        s_last_display = display;
        oled_display_set_app_state(display);
        ESP_LOGI(TAG, "Input: %s", display);
    }
}

static void ps2_task(void *arg)
{
    (void)arg;

    oled_display_set_app_state("PS2 INIT");
    ps2_enter_analog_mode();
    oled_display_set_app_state("PS2 READY");

    while (true) {
        uint16_t buttons = 0;
        uint8_t sticks[4] = {128, 128, 128, 128};

        if (ps2_read(&buttons, sticks)) {
            ps2_update_outputs(buttons, sticks);
        } else {
            status_led_set_on(false);
            oled_display_set_app_state("PS2 NO LINK");
            ESP_LOGW(TAG, "No PS2 controller response");
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_PS2_POLL_INTERVAL_MS));
    }
}

esp_err_t ps2_gamepad_init(void)
{
    gpio_config_t output_conf = {
        .pin_bit_mask = (1ULL << BOARD_PS2_CLK_GPIO) | (1ULL << BOARD_PS2_CMD_GPIO) | (1ULL << BOARD_PS2_ATT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config_t input_conf = {
        .pin_bit_mask = 1ULL << BOARD_PS2_DAT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&output_conf), TAG, "PS2 output GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_config(&input_conf), TAG, "PS2 data GPIO config failed");

    gpio_set_level(BOARD_PS2_CLK_GPIO, 1);
    gpio_set_level(BOARD_PS2_CMD_GPIO, 1);
    gpio_set_level(BOARD_PS2_ATT_GPIO, 1);

    if (s_ps2_task_handle == NULL) {
        BaseType_t created = xTaskCreate(
            ps2_task,
            "ps2_gamepad",
            4096,
            NULL,
            5,
            &s_ps2_task_handle);

        if (created != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(
        TAG,
        "PS2 receiver pins CLK GPIO%d CMD GPIO%d ATT GPIO%d DAT GPIO%d",
        BOARD_PS2_CLK_GPIO,
        BOARD_PS2_CMD_GPIO,
        BOARD_PS2_ATT_GPIO,
        BOARD_PS2_DAT_GPIO);

    return ESP_OK;
}
