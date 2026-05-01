#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <cstring>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/ledc.h>
#include <cstdlib>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "DttEsp32Board"

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

struct Ps2ButtonName {
    uint16_t mask;
    const char* name;
};

static const Ps2ButtonName kPs2Buttons[] = {
    {PS2_BTN_UP, "PS2: UP"},
    {PS2_BTN_RIGHT, "PS2: RIGHT"},
    {PS2_BTN_DOWN, "PS2: DOWN"},
    {PS2_BTN_LEFT, "PS2: LEFT"},
    {PS2_BTN_START, "PS2: START"},
    {PS2_BTN_SELECT, "PS2: SELECT"},
    {PS2_BTN_L1, "PS2: L1"},
    {PS2_BTN_R1, "PS2: R1"},
    {PS2_BTN_L2, "PS2: L2"},
    {PS2_BTN_R2, "PS2: R2"},
    {PS2_BTN_L3, "PS2: L3"},
    {PS2_BTN_R3, "PS2: R3"},
    {PS2_BTN_TRIANGLE, "PS2: TRIANGLE"},
    {PS2_BTN_CIRCLE, "PS2: CIRCLE/O"},
    {PS2_BTN_CROSS, "PS2: CROSS/X"},
    {PS2_BTN_SQUARE, "PS2: SQUARE"},
};

class DttEsp32Board : public WifiBoard {
private:
    enum ExternalLedEffect {
        kExternalLedOff,
        kExternalLedSteady,
        kExternalLedBlink,
        kExternalLedBreath,
    };

    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    TaskHandle_t ps2_task_handle_ = nullptr;
    TaskHandle_t led_effect_task_handle_ = nullptr;
    portMUX_TYPE ps2_mux_ = portMUX_INITIALIZER_UNLOCKED;
    portMUX_TYPE led_mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t last_ps2_buttons_ = 0;
    const char* last_ps2_display_ = "";
    bool manual_led_state_ = false;
    ExternalLedEffect external_led_effect_ = kExternalLedOff;
    int external_led_brightness_ = 100;
    int external_led_blink_interval_ms_ = 500;
    bool ps2_linked_ = false;
    int ps2_read_failures_ = 0;
    int64_t last_ps2_input_us_ = 0;
    int64_t last_ps2_status_log_us_ = 0;
    uint8_t last_ps2_rx1_ = 0;
    uint8_t last_ps2_rx2_ = 0;
    uint16_t last_ps2_raw_ = 0xFFFF;

    uint8_t Ps2TransferByte(uint8_t out) {
        uint8_t in = 0;

        for (int bit = 0; bit < 8; bit++) {
            gpio_set_level(PS2_CMD_GPIO, (out >> bit) & 0x01);
            esp_rom_delay_us(PS2_CLOCK_DELAY_US);

            gpio_set_level(PS2_CLK_GPIO, 0);
            esp_rom_delay_us(PS2_CLOCK_DELAY_US);

            if (gpio_get_level(PS2_DAT_GPIO)) {
                in |= (1U << bit);
            }

            gpio_set_level(PS2_CLK_GPIO, 1);
            esp_rom_delay_us(PS2_CLOCK_DELAY_US);
        }

        gpio_set_level(PS2_CMD_GPIO, 1);
        return in;
    }

    void Ps2Transaction(const uint8_t* tx, uint8_t* rx, size_t length) {
        portENTER_CRITICAL(&ps2_mux_);
        gpio_set_level(PS2_ATT_GPIO, 0);
        esp_rom_delay_us(PS2_CLOCK_DELAY_US);

        for (size_t i = 0; i < length; i++) {
            rx[i] = Ps2TransferByte(tx[i]);
            esp_rom_delay_us(PS2_CLOCK_DELAY_US);
        }

        gpio_set_level(PS2_ATT_GPIO, 1);
        esp_rom_delay_us(PS2_CLOCK_DELAY_US);
        portEXIT_CRITICAL(&ps2_mux_);
    }

    void Ps2SendCommand(const uint8_t* tx, size_t length) {
        uint8_t rx[9] = {};
        Ps2Transaction(tx, rx, length);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    void Ps2EnterAnalogMode() {
        static const uint8_t enter_config[] = {0x01, 0x43, 0x00, 0x01, 0x00};
        static const uint8_t set_analog[] = {0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t exit_config[] = {0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};

        Ps2SendCommand(enter_config, sizeof(enter_config));
        Ps2SendCommand(set_analog, sizeof(set_analog));
        Ps2SendCommand(exit_config, sizeof(exit_config));
    }

    bool Ps2Read(uint16_t* buttons, uint8_t sticks[4]) {
        static const uint8_t tx_analog[] = {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t tx_digital[] = {0x01, 0x42, 0x00, 0x00, 0x00};
        uint8_t rx[sizeof(tx_analog)] = {};

        Ps2Transaction(tx_analog, rx, sizeof(tx_analog));
        last_ps2_rx1_ = rx[1];
        last_ps2_rx2_ = rx[2];
        last_ps2_raw_ = ((uint16_t)rx[4] << 8) | rx[3];

        if (rx[1] == 0xFF || rx[2] != 0x5A) {
            memset(rx, 0, sizeof(rx));
            Ps2Transaction(tx_digital, rx, sizeof(tx_digital));
            last_ps2_rx1_ = rx[1];
            last_ps2_rx2_ = rx[2];
            last_ps2_raw_ = ((uint16_t)rx[4] << 8) | rx[3];
            if (rx[1] == 0xFF || rx[2] != 0x5A) {
                return false;
            }
        }

        *buttons = (uint16_t)(~(((uint16_t)rx[4] << 8) | rx[3]));
        if (rx[1] == 0x73 || rx[1] == 0x79) {
            sticks[0] = rx[7];
            sticks[1] = rx[8];
            sticks[2] = rx[5];
            sticks[3] = rx[6];
        } else {
            sticks[0] = 128;
            sticks[1] = 128;
            sticks[2] = 128;
            sticks[3] = 128;
        }
        return true;
    }

    const char* Ps2FirstPressedButton(uint16_t buttons) {
        for (const auto& button : kPs2Buttons) {
            if (buttons & button.mask) {
                return button.name;
            }
        }
        return nullptr;
    }

    const char* Ps2StickName(const uint8_t sticks[4]) {
        int lx = (int)sticks[0] - 128;
        int ly = (int)sticks[1] - 128;
        int rx = (int)sticks[2] - 128;
        int ry = (int)sticks[3] - 128;

        if (std::abs(lx) > PS2_STICK_DEADZONE) {
            return lx < 0 ? "PS2: LSTICK LEFT" : "PS2: LSTICK RIGHT";
        }
        if (std::abs(ly) > PS2_STICK_DEADZONE) {
            return ly < 0 ? "PS2: LSTICK UP" : "PS2: LSTICK DOWN";
        }
        if (std::abs(rx) > PS2_STICK_DEADZONE) {
            return rx < 0 ? "PS2: RSTICK LEFT" : "PS2: RSTICK RIGHT";
        }
        if (std::abs(ry) > PS2_STICK_DEADZONE) {
            return ry < 0 ? "PS2: RSTICK UP" : "PS2: RSTICK DOWN";
        }

        return nullptr;
    }

    int ClampInt(int value, int min_value, int max_value) {
        if (value < min_value) {
            return min_value;
        }
        if (value > max_value) {
            return max_value;
        }
        return value;
    }

    const char* ExternalLedEffectName(ExternalLedEffect effect) {
        switch (effect) {
            case kExternalLedOff:
                return "off";
            case kExternalLedSteady:
                return "steady";
            case kExternalLedBlink:
                return "blink";
            case kExternalLedBreath:
                return "breath";
            default:
                return "unknown";
        }
    }

    uint32_t ExternalLedDuty(int brightness) {
        brightness = ClampInt(brightness, 0, 100);
        if (brightness >= 100) {
            return EXTERNAL_LED_LEDC_MAX_DUTY;
        }
        return (uint32_t)(brightness * EXTERNAL_LED_LEDC_MAX_DUTY / 100);
    }

    void WriteExternalLedDuty(int brightness) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(EXTERNAL_LED_LEDC_MODE,
            EXTERNAL_LED_LEDC_CHANNEL, ExternalLedDuty(brightness)));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(EXTERNAL_LED_LEDC_MODE,
            EXTERNAL_LED_LEDC_CHANNEL));
    }

    void ShowLedStatus(const char* prefix = "LED") {
        if (display_ == nullptr) {
            return;
        }

        ExternalLedEffect effect;
        int brightness;
        int interval_ms;
        portENTER_CRITICAL(&led_mux_);
        effect = external_led_effect_;
        brightness = external_led_brightness_;
        interval_ms = external_led_blink_interval_ms_;
        portEXIT_CRITICAL(&led_mux_);

        char message[40];
        if (effect == kExternalLedBlink) {
            snprintf(message, sizeof(message), "%s %s %d%% %dms", prefix, ExternalLedEffectName(effect), brightness, interval_ms);
        } else {
            snprintf(message, sizeof(message), "%s %s %d%%", prefix, ExternalLedEffectName(effect), brightness);
        }
        display_->ShowNotification(message, 1500);
    }

    void SetExternalLedEffect(ExternalLedEffect effect, bool notify = true) {
        portENTER_CRITICAL(&led_mux_);
        external_led_effect_ = effect;
        manual_led_state_ = effect != kExternalLedOff;
        portEXIT_CRITICAL(&led_mux_);

        if (effect == kExternalLedOff) {
            WriteExternalLedDuty(0);
        } else if (effect == kExternalLedSteady) {
            WriteExternalLedDuty(external_led_brightness_);
        }

        ESP_LOGI(TAG, "External LED effect: %s brightness=%d blink=%dms",
            ExternalLedEffectName(effect), external_led_brightness_, external_led_blink_interval_ms_);
        if (notify) {
            ShowLedStatus("LED");
        }
    }

    void SetManualLed(bool on) {
        SetExternalLedEffect(on ? kExternalLedSteady : kExternalLedOff);
    }

    void SetExternalLedBrightness(int brightness, bool notify = true) {
        portENTER_CRITICAL(&led_mux_);
        external_led_brightness_ = ClampInt(brightness, 0, 100);
        auto effect = external_led_effect_;
        portEXIT_CRITICAL(&led_mux_);

        if (effect == kExternalLedSteady) {
            WriteExternalLedDuty(external_led_brightness_);
        }
        ESP_LOGI(TAG, "External LED brightness: %d%%", external_led_brightness_);
        if (notify) {
            ShowLedStatus("LED");
        }
    }

    void AdjustExternalLedBrightness(int delta) {
        SetExternalLedBrightness(external_led_brightness_ + delta);
    }

    void SetExternalLedBlinkInterval(int interval_ms, bool notify = true) {
        portENTER_CRITICAL(&led_mux_);
        external_led_blink_interval_ms_ = ClampInt(interval_ms, 80, 2000);
        portEXIT_CRITICAL(&led_mux_);

        ESP_LOGI(TAG, "External LED blink interval: %dms", external_led_blink_interval_ms_);
        if (notify) {
            ShowLedStatus("LED");
        }
    }

    void AdjustExternalLedBlinkInterval(int delta_ms) {
        SetExternalLedBlinkInterval(external_led_blink_interval_ms_ + delta_ms);
    }

    bool SetExternalLedEffectByName(const std::string& effect) {
        if (effect == "on" || effect == "steady" || effect == "bat" || effect == "bật") {
            SetExternalLedEffect(kExternalLedSteady);
            return true;
        }
        if (effect == "off" || effect == "tat" || effect == "tắt") {
            SetExternalLedEffect(kExternalLedOff);
            return true;
        }
        if (effect == "blink" || effect == "chop" || effect == "chớp" || effect == "nhap nhay" || effect == "nhấp nháy") {
            SetExternalLedEffect(kExternalLedBlink);
            return true;
        }
        if (effect == "breath" || effect == "breathe" || effect == "tho" || effect == "thở") {
            SetExternalLedEffect(kExternalLedBreath);
            return true;
        }
        return false;
    }

    std::string AnswerCustomKnowledge(const std::string& question) {
        if (question.find("Sếp Tắng") != std::string::npos ||
            question.find("Thầy Tắng") != std::string::npos ||
            question.find("Thầy Tấn") != std::string::npos ||
            question.find("Thầy Tắn") != std::string::npos ||
            question.find("anh Tắng") != std::string::npos ||
            question.find("giới thiệu về Thầy Tắng") != std::string::npos ||
            question.find("giới thiệu về Thầy Tấn") != std::string::npos ||
            question.find("giới thiệu về Thầy Tắn") != std::string::npos ||
            question.find("sep tang") != std::string::npos ||
            question.find("thay tang") != std::string::npos ||
            question.find("thay tan") != std::string::npos ||
            question.find("thay tang la ai") != std::string::npos ||
            question.find("thay tan la ai") != std::string::npos ||
            question.find("thay tanh la ai") != std::string::npos ||
            question.find("thay tangg la ai") != std::string::npos ||
            question.find("thay tan la ai") != std::string::npos ||
            question.find("thay tans la ai") != std::string::npos ||
            question.find("thay tắn") != std::string::npos ||
            question.find("thầy tắn") != std::string::npos ||
            question.find("thầy tấn") != std::string::npos ||
            question.find("thầy tăng") != std::string::npos ||
            question.find("gioi thieu ve thay tang") != std::string::npos ||
            question.find("gioi thieu ve thay tan") != std::string::npos ||
            question.find("gioi thieu thay tang") != std::string::npos ||
            question.find("gioi thieu thay tan") != std::string::npos ||
            question.find("anh tang") != std::string::npos) {
            return "Thầy Tắng là giảng viên nghề Điện công nghiệp, Khoa Điện Trường Cao đẳng Kỹ thuật Công Nghệ Bà Rịa Vũng Tàu, Sếp Tắng sinh ngày 1 tháng 5 năm 1986.";
        }
        if (question.find("bạn là ai") != std::string::npos ||
            question.find("mày là ai") != std::string::npos ||
            question.find("du là ai") != std::string::npos ||
            question.find("ban la ai") != std::string::npos ||
            question.find("may la ai") != std::string::npos ||
            question.find("du la ai") != std::string::npos) {
            return "Mình là trợ lý AI của thầy Tắn.";
        }
        if (question.find("đèn dùng chân nào") != std::string::npos ||
            question.find("den dung chan nao") != std::string::npos ||
            question.find("GPIO") != std::string::npos ||
            question.find("gpio") != std::string::npos) {
            return "Đèn ngoài đang dùng GPIO8.";
        }
        if (question.find("Từ đánh thức") != std::string::npos ||
            question.find("từ đánh thức") != std::string::npos ||
            question.find("tu danh thuc") != std::string::npos ||
            question.find("wake word") != std::string::npos) {
            return "Hello.";
        }
        if (question.find("tên thiết bị") != std::string::npos ||
            question.find("ten thiet bi") != std::string::npos) {
            return "Tên thiết bị là DTT AI.";
        }
        return "Tôi chưa có nội dung ghi nhớ phù hợp cho câu hỏi này.";
    }

    void ToggleExternalLedEffect() {
        SetExternalLedEffect(manual_led_state_ ? kExternalLedOff : kExternalLedSteady);
    }

    void InitializeExternalLed() {
        ledc_timer_config_t ledc_timer = {};
        ledc_timer.duty_resolution = EXTERNAL_LED_LEDC_RESOLUTION;
        ledc_timer.freq_hz = EXTERNAL_LED_PWM_FREQ_HZ;
        ledc_timer.speed_mode = EXTERNAL_LED_LEDC_MODE;
        ledc_timer.timer_num = EXTERNAL_LED_LEDC_TIMER;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_channel = {};
        ledc_channel.channel = EXTERNAL_LED_LEDC_CHANNEL;
        ledc_channel.duty = 0;
        ledc_channel.gpio_num = EXTERNAL_LED_GPIO;
        ledc_channel.speed_mode = EXTERNAL_LED_LEDC_MODE;
        ledc_channel.hpoint = 0;
        ledc_channel.timer_sel = EXTERNAL_LED_LEDC_TIMER;
        ledc_channel.flags.output_invert = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        BaseType_t created = xTaskCreate([](void* arg) {
            auto board = static_cast<DttEsp32Board*>(arg);
            board->ExternalLedEffectTask();
        }, "external_led", 2048, this, 5, &led_effect_task_handle_);

        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create external LED task");
        }

        SetExternalLedEffect(kExternalLedOff, false);
    }

    void ExternalLedEffectTask() {
        bool blink_visible = false;
        int breath_level = 0;
        int breath_delta = 4;

        while (true) {
            ExternalLedEffect effect;
            int brightness;
            int interval_ms;
            portENTER_CRITICAL(&led_mux_);
            effect = external_led_effect_;
            brightness = external_led_brightness_;
            interval_ms = external_led_blink_interval_ms_;
            portEXIT_CRITICAL(&led_mux_);

            switch (effect) {
                case kExternalLedOff:
                    WriteExternalLedDuty(0);
                    vTaskDelay(pdMS_TO_TICKS(120));
                    break;
                case kExternalLedSteady:
                    WriteExternalLedDuty(brightness);
                    vTaskDelay(pdMS_TO_TICKS(120));
                    break;
                case kExternalLedBlink:
                    blink_visible = !blink_visible;
                    WriteExternalLedDuty(blink_visible ? brightness : 0);
                    vTaskDelay(pdMS_TO_TICKS(interval_ms));
                    break;
                case kExternalLedBreath:
                    breath_level += breath_delta;
                    if (breath_level >= brightness) {
                        breath_level = brightness;
                        breath_delta = -4;
                    } else if (breath_level <= 0) {
                        breath_level = 0;
                        breath_delta = 4;
                    }
                    WriteExternalLedDuty(breath_level);
                    vTaskDelay(pdMS_TO_TICKS(35));
                    break;
                default:
                    vTaskDelay(pdMS_TO_TICKS(120));
                    break;
            }
        }
    }

    void HandlePs2Input(uint16_t buttons, const uint8_t sticks[4]) {
        uint16_t pressed = buttons & ~last_ps2_buttons_;
        const char* button_name = Ps2FirstPressedButton(buttons);
        const char* display = button_name;
        if (display == nullptr) {
            display = Ps2StickName(sticks);
        }

        if (pressed & PS2_BTN_CIRCLE) {
            SetExternalLedEffect(kExternalLedSteady);
            display = "PS2: LED ON";
        } else if (pressed & PS2_BTN_TRIANGLE) {
            SetExternalLedEffect(kExternalLedOff);
            display = "PS2: LED OFF";
        } else if (pressed & PS2_BTN_SQUARE) {
            SetExternalLedEffect(external_led_effect_ == kExternalLedBlink ? kExternalLedSteady : kExternalLedBlink);
            display = "PS2: LED BLINK";
        } else if (pressed & PS2_BTN_CROSS) {
            SetExternalLedEffect(kExternalLedBreath);
            display = "PS2: LED BREATH";
        } else if (pressed & PS2_BTN_R1) {
            AdjustExternalLedBrightness(10);
            display = "PS2: BRIGHT +";
        } else if (pressed & PS2_BTN_L1) {
            AdjustExternalLedBrightness(-10);
            display = "PS2: BRIGHT -";
        } else if (pressed & PS2_BTN_R2) {
            AdjustExternalLedBlinkInterval(-100);
            display = "PS2: BLINK FAST";
        } else if (pressed & PS2_BTN_L2) {
            AdjustExternalLedBlinkInterval(100);
            display = "PS2: BLINK SLOW";
        } else if (pressed & PS2_BTN_START) {
            ToggleExternalLedEffect();
            display = "PS2: LED TOGGLE";
        } else if (pressed & PS2_BTN_SELECT) {
            ShowLedStatus("PS2");
            display = "PS2: LED STATUS";
        }

        if (display == nullptr) {
            display = "PS2 READY";
        }

        if (buttons != last_ps2_buttons_ || display != last_ps2_display_) {
            last_ps2_buttons_ = buttons;
            last_ps2_display_ = display;
            last_ps2_input_us_ = esp_timer_get_time();
            ESP_LOGI(TAG, "PS2 input: %s buttons=0x%04X raw=0x%04X rx1=0x%02X rx2=0x%02X",
                display, buttons, last_ps2_raw_, last_ps2_rx1_, last_ps2_rx2_);
            if (display_ != nullptr) {
                display_->ShowNotification(display, 1500);
            }
        }
    }

    void InitializePs2Gamepad() {
        ESP_LOGI(TAG, "Initializing PS2 gamepad");
        gpio_config_t output_conf = {
            .pin_bit_mask = (1ULL << PS2_CLK_GPIO) | (1ULL << PS2_CMD_GPIO) | (1ULL << PS2_ATT_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&output_conf));

        gpio_config_t input_conf = {
            .pin_bit_mask = 1ULL << PS2_DAT_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&input_conf));

        gpio_set_drive_capability(PS2_CLK_GPIO, GPIO_DRIVE_CAP_2);
        gpio_set_drive_capability(PS2_CMD_GPIO, GPIO_DRIVE_CAP_2);
        gpio_set_drive_capability(PS2_ATT_GPIO, GPIO_DRIVE_CAP_2);
        gpio_set_level(PS2_CLK_GPIO, 1);
        gpio_set_level(PS2_CMD_GPIO, 1);
        gpio_set_level(PS2_ATT_GPIO, 1);

        BaseType_t created = xTaskCreatePinnedToCore([](void* arg) {
            auto board = static_cast<DttEsp32Board*>(arg);
            board->Ps2Task();
        }, "ps2_gamepad", 4096, this, 10, &ps2_task_handle_, 1);

        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create PS2 task");
        }

        ESP_LOGI(TAG, "PS2 receiver pins CLK GPIO%d CMD GPIO%d ATT GPIO%d DAT GPIO%d",
            PS2_CLK_GPIO, PS2_CMD_GPIO, PS2_ATT_GPIO, PS2_DAT_GPIO);
    }

    void Ps2Task() {
        ESP_LOGI(TAG, "PS2 task started");
        if (display_ != nullptr) {
            display_->ShowNotification("PS2 INIT", 1500);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        for (int i = 0; i < 3; i++) {
            Ps2EnterAnalogMode();
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        ESP_LOGI(TAG, "PS2 analog init sent");
        if (display_ != nullptr) {
            display_->ShowNotification("PS2 READY", 1500);
        }

        while (true) {
            uint16_t buttons = 0;
            uint8_t sticks[4] = {128, 128, 128, 128};
            if (Ps2Read(&buttons, sticks)) {
                if (!ps2_linked_ && display_ != nullptr) {
                    display_->ShowNotification("PS2 READY", 1500);
                }
                ps2_linked_ = true;
                ps2_read_failures_ = 0;
                HandlePs2Input(buttons, sticks);
            } else {
                ps2_read_failures_++;
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_ps2_status_log_us_ > 5000000) {
                    last_ps2_status_log_us_ = now_us;
                    ESP_LOGW(TAG, "PS2 no link failures=%d rx1=0x%02X rx2=0x%02X raw=0x%04X dat=%d",
                        ps2_read_failures_, last_ps2_rx1_, last_ps2_rx2_, last_ps2_raw_, gpio_get_level(PS2_DAT_GPIO));
                }
                if (ps2_linked_ && ps2_read_failures_ >= 10) {
                    ps2_linked_ = false;
                    ESP_LOGW(TAG, "No PS2 controller response rx1=0x%02X rx2=0x%02X", last_ps2_rx1_, last_ps2_rx2_);
                }
                if (!ps2_linked_ && ps2_read_failures_ % 20 == 0) {
                    Ps2EnterAnalogMode();
                }
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            vTaskDelay(pdMS_TO_TICKS(PS2_POLL_INTERVAL_MS));
        }
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            EnterWifiConfigMode();
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.led.set_state",
            "Control the external LED on GPIO8. Use this when the user asks in Vietnamese or English to turn the light on/off, for example: bat den, tat den, turn on the LED, turn off the LED.",
            PropertyList({
                Property("on", kPropertyTypeBoolean)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                bool on = properties["on"].value<bool>();
                SetManualLed(on);
                const char* message = on ? "LED ON" : "LED OFF";
                ESP_LOGI(TAG, "MCP LED command: %s", message);
                if (display_ != nullptr) {
                    display_->ShowNotification(message, 1500);
                }
                return std::string(on ? "Da bat den" : "Da tat den");
            });

        mcp_server.AddTool("self.led.get_state",
            "Get current state of the external LED on GPIO8, including effect, brightness and blink speed.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddBoolToObject(root, "on", manual_led_state_);
                cJSON_AddStringToObject(root, "effect", ExternalLedEffectName(external_led_effect_));
                cJSON_AddNumberToObject(root, "brightness_percent", external_led_brightness_);
                cJSON_AddNumberToObject(root, "blink_interval_ms", external_led_blink_interval_ms_);
                return root;
            });

        mcp_server.AddTool("self.led.toggle",
            "Toggle the external LED on GPIO8. Use this when the user asks to change, toggle, or dao trang thai den.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SetManualLed(!manual_led_state_);
                const char* message = manual_led_state_ ? "LED ON" : "LED OFF";
                ESP_LOGI(TAG, "MCP LED toggle: %s", message);
                if (display_ != nullptr) {
                    display_->ShowNotification(message, 1500);
                }
                return std::string(manual_led_state_ ? "Da bat den" : "Da tat den");
            });

        mcp_server.AddTool("self.led.set_brightness",
            "Set external LED brightness on GPIO8 from 0 to 100 percent. Use this for Vietnamese commands like tang do sang den, giam do sang den, den sang hon, den toi di.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetExternalLedBrightness(properties["brightness"].value<int>());
                return std::string("Da chinh do sang den");
            });

        mcp_server.AddTool("self.led.adjust_brightness",
            "Increase or decrease external LED brightness. Positive delta makes the LED brighter, negative delta makes it dimmer.",
            PropertyList({
                Property("delta", kPropertyTypeInteger, -100, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                AdjustExternalLedBrightness(properties["delta"].value<int>());
                return std::string("Da dieu chinh do sang den");
            });

        mcp_server.AddTool("self.led.set_effect",
            "Set external LED effect. effect can be off, steady, blink, or breath. Use this for Vietnamese commands: bat den, tat den, cho den chop tat, nhap nhay, hieu ung tho.",
            PropertyList({
                Property("effect", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto effect = properties["effect"].value<std::string>();
                if (!SetExternalLedEffectByName(effect)) {
                    return std::string("Hieu ung khong hop le. Hay dung off, steady, blink hoac breath");
                }
                return std::string("Da doi hieu ung den");
            });

        mcp_server.AddTool("self.led.set_blink_interval",
            "Set external LED blink interval in milliseconds. Smaller value blinks faster, larger value blinks slower. Use this for tang toc chop, giam toc chop, chop nhanh hon, chop cham hon.",
            PropertyList({
                Property("interval_ms", kPropertyTypeInteger, 80, 2000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetExternalLedBlinkInterval(properties["interval_ms"].value<int>());
                SetExternalLedEffect(kExternalLedBlink);
                return std::string("Da chinh tan so chop tat");
            });

        mcp_server.AddTool("self.led.adjust_blink_speed",
            "Adjust external LED blink speed. Positive delta_ms makes blinking slower, negative delta_ms makes blinking faster.",
            PropertyList({
                Property("delta_ms", kPropertyTypeInteger, -1000, 1000)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                AdjustExternalLedBlinkInterval(properties["delta_ms"].value<int>());
                SetExternalLedEffect(kExternalLedBlink);
                return std::string("Da dieu chinh toc do chop tat");
            });

        mcp_server.AddTool("self.dtt.custom_knowledge",
            "Answer only the relevant saved DTT knowledge. Use this tool when the user asks: ten thiet bi, ban la ai, may la ai, du la ai, Sep Tang/Thay Tang/Thay Tan/Thay Tan/anh Tang la ai, gioi thieu ve Thay Tang/Thay Tan/Thay Tan, den dung chan nao, tu danh thuc/wake word la gi. Do not add unrelated saved facts. Mandatory exact answers: identity questions such as 'Bạn là ai', 'Mày là ai', 'Du là ai' must be answered exactly: 'Mình là trợ lý AI của thầy Tắn.' Teacher questions such as 'Thầy Tắng là ai', 'Thầy Tấn là ai', 'Thầy Tắn là ai', or 'Giới thiệu về Thầy Tấn/Tắn/Tắng' must be answered exactly: 'Thầy Tắng là giảng viên nghề Điện công nghiệp, Khoa Điện Trường Cao đẳng Kỹ thuật Công Nghệ Bà Rịa Vũng Tàu, Sếp Tắng sinh ngày 1 tháng 5 năm 1986.'",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto question = properties["question"].value<std::string>();
                auto answer = AnswerCustomKnowledge(question);
                ESP_LOGI(TAG, "Custom knowledge query: %s", question.c_str());
                if (display_ != nullptr) {
                    display_->ShowNotification("DTT KNOWLEDGE", 1200);
                }
                return answer;
            });

        mcp_server.AddTool("self.dtt.get_status",
            "Get DTT board status: LED state, PS2 receiver status, OLED, audio hardware, wake word, and pins.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                cJSON* root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "board", "DTT ESP32-S3 N16R8");
                cJSON_AddStringToObject(root, "device_name", "DTT AI");
                cJSON_AddStringToObject(root, "language", "vi-VN");
                cJSON_AddStringToObject(root, "wake_word", "Hello");
                cJSON_AddStringToObject(root, "wake_response", "Xin chao");
                cJSON_AddBoolToObject(root, "led_on", manual_led_state_);
                cJSON_AddStringToObject(root, "led_effect", ExternalLedEffectName(external_led_effect_));
                cJSON_AddNumberToObject(root, "led_brightness_percent", external_led_brightness_);
                cJSON_AddNumberToObject(root, "led_blink_interval_ms", external_led_blink_interval_ms_);
                cJSON_AddBoolToObject(root, "ps2_linked", ps2_linked_);
                cJSON_AddStringToObject(root, "last_ps2_display", last_ps2_display_);
                cJSON_AddNumberToObject(root, "seconds_since_last_ps2_input",
                    last_ps2_input_us_ > 0 ? (esp_timer_get_time() - last_ps2_input_us_) / 1000000 : -1);
                cJSON_AddStringToObject(root, "oled", "SSD1306 128x32 SDA GPIO47 SCL GPIO21");
                cJSON_AddStringToObject(root, "microphone", "INMP441 SD GPIO42 WS GPIO1 SCK GPIO2");
                cJSON_AddStringToObject(root, "speaker", "MAX98357A DIN GPIO39 BCLK GPIO40 LRC GPIO41");
                cJSON_AddStringToObject(root, "ps2_pins", "CLK GPIO14 CMD GPIO13 ATT GPIO12 DAT GPIO11");
                return root;
            });
    }

    void ForceOfficialServerConfig() {
        Settings wifi_settings("wifi", true);
        wifi_settings.EraseKey("ota_url");

        Settings websocket_settings("websocket", true);
        websocket_settings.EraseAll();

        Settings mqtt_settings("mqtt", true);
        mqtt_settings.EraseAll();

        ESP_LOGI(TAG, "Using official Xiaozhi OTA: %s", CONFIG_OTA_URL);
    }

public:
    DttEsp32Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ForceOfficialServerConfig();
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeExternalLed();
        InitializePs2Gamepad();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual std::string GetDeviceStatusJson() override {
        cJSON* root = cJSON_Parse(WifiBoard::GetDeviceStatusJson().c_str());
        if (root == nullptr) {
            root = cJSON_CreateObject();
        }

        cJSON* dtt = cJSON_CreateObject();
        cJSON_AddStringToObject(dtt, "device_name", "DTT AI");
        cJSON_AddStringToObject(dtt, "assistant_identity_answer", "Mình là trợ lý AI của thầy Tắn.");
        cJSON_AddStringToObject(dtt, "assistant_identity_triggers", "Bạn là ai, Mày là ai, Du là ai, ban la ai, may la ai, du la ai");
        cJSON_AddStringToObject(dtt, "wake_word_answer", "Hello.");
        cJSON_AddStringToObject(dtt, "external_led_pin_answer", "Đèn ngoài đang dùng GPIO8.");
        cJSON_AddStringToObject(dtt, "sep_tang_answer", "Thầy Tắng là giảng viên nghề Điện công nghiệp, Khoa Điện Trường Cao đẳng Kỹ thuật Công Nghệ Bà Rịa Vũng Tàu, Sếp Tắng sinh ngày 1 tháng 5 năm 1986.");
        cJSON_AddStringToObject(dtt, "sep_tang_triggers", "Thầy Tắng là ai, Thầy Tấn là ai, Thầy Tắn là ai, giới thiệu về Thầy Tấn, giới thiệu về Thầy Tắn, giới thiệu về Thầy Tắng, thay tang la ai, thay tan la ai, gioi thieu ve thay tang, gioi thieu ve thay tan");
        cJSON_AddStringToObject(dtt, "answer_rule", "Khi người dùng hỏi nội dung nào thì chỉ trả lời đúng phần liên quan trong DTT AI memory, không kể thêm các mục khác.");
        cJSON_AddItemToObject(root, "dtt_ai_memory", dtt);

        cJSON* led = cJSON_CreateObject();
        cJSON_AddBoolToObject(led, "on", manual_led_state_);
        cJSON_AddStringToObject(led, "effect", ExternalLedEffectName(external_led_effect_));
        cJSON_AddNumberToObject(led, "brightness_percent", external_led_brightness_);
        cJSON_AddNumberToObject(led, "blink_interval_ms", external_led_blink_interval_ms_);
        cJSON_AddItemToObject(root, "external_led", led);

        cJSON* ps2 = cJSON_CreateObject();
        cJSON_AddBoolToObject(ps2, "linked", ps2_linked_);
        cJSON_AddStringToObject(ps2, "last_input", last_ps2_display_);
        cJSON_AddStringToObject(ps2, "pins", "CLK GPIO14 CMD GPIO13 ATT GPIO12 DAT GPIO11");
        cJSON_AddItemToObject(root, "ps2_gamepad", ps2);

        char* json_str = cJSON_PrintUnformatted(root);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(root);
        return result;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            I2S_STD_SLOT_LEFT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN,
            I2S_STD_SLOT_LEFT
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(DttEsp32Board);
