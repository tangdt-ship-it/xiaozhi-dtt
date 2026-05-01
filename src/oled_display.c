#include "oled_display.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"

#define OLED_PAGE_COUNT (BOARD_OLED_HEIGHT / 8)
#define OLED_BUFFER_SIZE (BOARD_OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_LINE_CHARS 21

static const char *TAG = "oled";

static bool s_initialized;
static uint8_t s_buffer[OLED_BUFFER_SIZE];
static char s_led_state[OLED_LINE_CHARS + 1] = "BOOTING";
static char s_network_state[OLED_LINE_CHARS + 1] = "OFFLINE";
static char s_app_state[OLED_LINE_CHARS + 1] = "BOOT";

static esp_err_t oled_write_bytes(const uint8_t *data, size_t size)
{
    return i2c_master_write_to_device(
        BOARD_OLED_I2C_PORT,
        BOARD_OLED_I2C_ADDRESS,
        data,
        size,
        pdMS_TO_TICKS(100));
}

static esp_err_t oled_cmd(uint8_t command)
{
    uint8_t data[] = {0x00, command};
    return oled_write_bytes(data, sizeof(data));
}

static esp_err_t oled_data(const uint8_t *data, size_t size)
{
    uint8_t packet[17] = {0x40};

    while (size > 0) {
        size_t chunk = size > 16 ? 16 : size;
        memcpy(&packet[1], data, chunk);
        ESP_RETURN_ON_ERROR(oled_write_bytes(packet, chunk + 1), TAG, "OLED data write failed");
        data += chunk;
        size -= chunk;
    }

    return ESP_OK;
}

static void font5x7(char c, uint8_t out[5])
{
    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    switch (c) {
    case '0': memcpy(out, (uint8_t[]){0x3E, 0x51, 0x49, 0x45, 0x3E}, 5); break;
    case '1': memcpy(out, (uint8_t[]){0x00, 0x42, 0x7F, 0x40, 0x00}, 5); break;
    case '2': memcpy(out, (uint8_t[]){0x42, 0x61, 0x51, 0x49, 0x46}, 5); break;
    case '3': memcpy(out, (uint8_t[]){0x21, 0x41, 0x45, 0x4B, 0x31}, 5); break;
    case '4': memcpy(out, (uint8_t[]){0x18, 0x14, 0x12, 0x7F, 0x10}, 5); break;
    case '5': memcpy(out, (uint8_t[]){0x27, 0x45, 0x45, 0x45, 0x39}, 5); break;
    case '6': memcpy(out, (uint8_t[]){0x3C, 0x4A, 0x49, 0x49, 0x30}, 5); break;
    case '7': memcpy(out, (uint8_t[]){0x01, 0x71, 0x09, 0x05, 0x03}, 5); break;
    case '8': memcpy(out, (uint8_t[]){0x36, 0x49, 0x49, 0x49, 0x36}, 5); break;
    case '9': memcpy(out, (uint8_t[]){0x06, 0x49, 0x49, 0x29, 0x1E}, 5); break;
    case 'A': memcpy(out, (uint8_t[]){0x7E, 0x11, 0x11, 0x11, 0x7E}, 5); break;
    case 'B': memcpy(out, (uint8_t[]){0x7F, 0x49, 0x49, 0x49, 0x36}, 5); break;
    case 'C': memcpy(out, (uint8_t[]){0x3E, 0x41, 0x41, 0x41, 0x22}, 5); break;
    case 'D': memcpy(out, (uint8_t[]){0x7F, 0x41, 0x41, 0x22, 0x1C}, 5); break;
    case 'E': memcpy(out, (uint8_t[]){0x7F, 0x49, 0x49, 0x49, 0x41}, 5); break;
    case 'F': memcpy(out, (uint8_t[]){0x7F, 0x09, 0x09, 0x09, 0x01}, 5); break;
    case 'G': memcpy(out, (uint8_t[]){0x3E, 0x41, 0x49, 0x49, 0x7A}, 5); break;
    case 'H': memcpy(out, (uint8_t[]){0x7F, 0x08, 0x08, 0x08, 0x7F}, 5); break;
    case 'I': memcpy(out, (uint8_t[]){0x00, 0x41, 0x7F, 0x41, 0x00}, 5); break;
    case 'J': memcpy(out, (uint8_t[]){0x20, 0x40, 0x41, 0x3F, 0x01}, 5); break;
    case 'K': memcpy(out, (uint8_t[]){0x7F, 0x08, 0x14, 0x22, 0x41}, 5); break;
    case 'L': memcpy(out, (uint8_t[]){0x7F, 0x40, 0x40, 0x40, 0x40}, 5); break;
    case 'M': memcpy(out, (uint8_t[]){0x7F, 0x02, 0x0C, 0x02, 0x7F}, 5); break;
    case 'N': memcpy(out, (uint8_t[]){0x7F, 0x04, 0x08, 0x10, 0x7F}, 5); break;
    case 'O': memcpy(out, (uint8_t[]){0x3E, 0x41, 0x41, 0x41, 0x3E}, 5); break;
    case 'P': memcpy(out, (uint8_t[]){0x7F, 0x09, 0x09, 0x09, 0x06}, 5); break;
    case 'Q': memcpy(out, (uint8_t[]){0x3E, 0x41, 0x51, 0x21, 0x5E}, 5); break;
    case 'R': memcpy(out, (uint8_t[]){0x7F, 0x09, 0x19, 0x29, 0x46}, 5); break;
    case 'S': memcpy(out, (uint8_t[]){0x46, 0x49, 0x49, 0x49, 0x31}, 5); break;
    case 'T': memcpy(out, (uint8_t[]){0x01, 0x01, 0x7F, 0x01, 0x01}, 5); break;
    case 'U': memcpy(out, (uint8_t[]){0x3F, 0x40, 0x40, 0x40, 0x3F}, 5); break;
    case 'V': memcpy(out, (uint8_t[]){0x1F, 0x20, 0x40, 0x20, 0x1F}, 5); break;
    case 'W': memcpy(out, (uint8_t[]){0x3F, 0x40, 0x38, 0x40, 0x3F}, 5); break;
    case 'X': memcpy(out, (uint8_t[]){0x63, 0x14, 0x08, 0x14, 0x63}, 5); break;
    case 'Y': memcpy(out, (uint8_t[]){0x07, 0x08, 0x70, 0x08, 0x07}, 5); break;
    case 'Z': memcpy(out, (uint8_t[]){0x61, 0x51, 0x49, 0x45, 0x43}, 5); break;
    case ':': memcpy(out, (uint8_t[]){0x00, 0x36, 0x36, 0x00, 0x00}, 5); break;
    case '-': memcpy(out, (uint8_t[]){0x08, 0x08, 0x08, 0x08, 0x08}, 5); break;
    case '.': memcpy(out, (uint8_t[]){0x00, 0x60, 0x60, 0x00, 0x00}, 5); break;
    case '/': memcpy(out, (uint8_t[]){0x20, 0x10, 0x08, 0x04, 0x02}, 5); break;
    case ' ': memset(out, 0x00, 5); break;
    default: memcpy(out, (uint8_t[]){0x02, 0x01, 0x51, 0x09, 0x06}, 5); break;
    }
}

static void draw_text(uint8_t page, uint8_t column, const char *text)
{
    while (*text != '\0' && column + 6 <= BOARD_OLED_WIDTH && page < OLED_PAGE_COUNT) {
        uint8_t glyph[5];
        font5x7(*text++, glyph);

        for (int i = 0; i < 5; i++) {
            s_buffer[page * BOARD_OLED_WIDTH + column++] = glyph[i];
        }
        s_buffer[page * BOARD_OLED_WIDTH + column++] = 0x00;
    }
}

static esp_err_t oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGE_COUNT; page++) {
        ESP_RETURN_ON_ERROR(oled_cmd((uint8_t)(0xB0 + page)), TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_cmd(0x00), TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_cmd(0x10), TAG, "set high column failed");
        ESP_RETURN_ON_ERROR(oled_data(&s_buffer[page * BOARD_OLED_WIDTH], BOARD_OLED_WIDTH), TAG, "flush failed");
    }

    return ESP_OK;
}

static void copy_state(char *destination, const char *source)
{
    if (source == NULL) {
        source = "";
    }

    snprintf(destination, OLED_LINE_CHARS + 1, "%s", source);
}

static void oled_refresh(void)
{
    if (!s_initialized) {
        return;
    }

    memset(s_buffer, 0x00, sizeof(s_buffer));

    draw_text(0, 0, "XIAOZHI DTT");

    char line[32];
    snprintf(line, sizeof(line), "LED:%s", s_led_state);
    draw_text(1, 0, line);

    snprintf(line, sizeof(line), "NET:%s", s_network_state);
    draw_text(2, 0, line);

    snprintf(line, sizeof(line), "APP:%s", s_app_state);
    draw_text(3, 0, line);

    esp_err_t ret = oled_flush();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED refresh failed: %s", esp_err_to_name(ret));
    }
}

esp_err_t oled_display_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_OLED_I2C_SDA_GPIO,
        .scl_io_num = BOARD_OLED_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_OLED_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_OLED_I2C_PORT, &config), TAG, "I2C param config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(BOARD_OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0), TAG, "I2C install failed");

    const uint8_t init_sequence[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x1F, 0xA4, 0xD3,
        0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x02, 0xDB,
        0x40, 0x8D, 0x14, 0xAF,
    };

    for (size_t i = 0; i < sizeof(init_sequence); i++) {
        ESP_RETURN_ON_ERROR(oled_cmd(init_sequence[i]), TAG, "OLED init command failed");
    }

    s_initialized = true;
    oled_refresh();
    ESP_LOGI(TAG, "OLED initialized on SDA GPIO%d, SCL GPIO%d", BOARD_OLED_I2C_SDA_GPIO, BOARD_OLED_I2C_SCL_GPIO);
    return ESP_OK;
}

void oled_display_set_led_state(const char *state)
{
    copy_state(s_led_state, state);
    oled_refresh();
}

void oled_display_set_network_state(const char *state)
{
    copy_state(s_network_state, state);
    oled_refresh();
}

void oled_display_set_app_state(const char *state)
{
    copy_state(s_app_state, state);
    oled_refresh();
}
