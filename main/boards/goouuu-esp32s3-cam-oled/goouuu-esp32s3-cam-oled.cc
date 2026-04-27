#include "wifi_board.h"
#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "esp32_camera.h"
#include "led/single_led.h"

#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_camera.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdio>

#define TAG "GOOUUU_ESP32S3_CAM_OLED"

class GoouuuEsp32S3CamOledBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Button boot_button_;
    Display* display_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    bool camera_health_mode_ = false;
    esp_timer_handle_t camera_health_timer_ = nullptr;

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

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
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

        // Double click to toggle camera health mode on OLED:
        // shows capture status + average brightness periodically.
        boot_button_.OnDoubleClick([this]() {
            ToggleCameraHealthMode();
        });
    }

    void InitializeCameraHealthTimer() {
        esp_timer_create_args_t args = {
            .callback = [](void* arg) {
                auto* self = static_cast<GoouuuEsp32S3CamOledBoard*>(arg);
                self->RunCameraHealthCheck();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "cam_health_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &camera_health_timer_));
    }

    void ToggleCameraHealthMode() {
        camera_health_mode_ = !camera_health_mode_;
        if (camera_health_mode_) {
            ESP_ERROR_CHECK(esp_timer_start_periodic(camera_health_timer_, 2 * 1000 * 1000));
            GetDisplay()->ShowNotification("CAM HEALTH ON", 1200);
        } else {
            esp_timer_stop(camera_health_timer_);
            GetDisplay()->ShowNotification("CAM HEALTH OFF", 1200);
        }
    }

    void RunCameraHealthCheck() {
        if (!camera_health_mode_) {
            return;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (fb == nullptr) {
            GetDisplay()->ShowNotification("CAM FAIL", 1500);
            return;
        }

        int avg_luma = -1;
        if (fb->format == PIXFORMAT_RGB565 && fb->buf != nullptr) {
            // Sample pixels to reduce CPU load.
            const uint16_t* pixels = reinterpret_cast<const uint16_t*>(fb->buf);
            const size_t pixel_count = fb->len / 2;
            const size_t step = 8;
            uint64_t sum = 0;
            size_t cnt = 0;
            for (size_t i = 0; i < pixel_count; i += step) {
                uint16_t p = pixels[i];
                int r = ((p >> 11) & 0x1F) << 3;
                int g = ((p >> 5) & 0x3F) << 2;
                int b = (p & 0x1F) << 3;
                int y = (77 * r + 150 * g + 29 * b) >> 8;
                sum += static_cast<uint32_t>(y);
                cnt++;
            }
            if (cnt > 0) {
                avg_luma = static_cast<int>(sum / cnt);
            }
        }

        esp_camera_fb_return(fb);

        char text[32] = {0};
        if (avg_luma >= 0) {
            snprintf(text, sizeof(text), "CAM OK L:%d", avg_luma);
        } else {
            snprintf(text, sizeof(text), "CAM OK");
        }
        GetDisplay()->ShowNotification(text, 1200);
    }

    void InitializeCamera() {
        camera_config_t camera_config = {
            .pin_pwdn = CAMERA_PIN_PWDN,
            .pin_reset = CAMERA_PIN_RESET,
            .pin_xclk = CAMERA_PIN_XCLK,
            .pin_sccb_sda = CAMERA_PIN_SIOD,
            .pin_sccb_scl = CAMERA_PIN_SIOC,
            .pin_d7 = CAMERA_PIN_D7,
            .pin_d6 = CAMERA_PIN_D6,
            .pin_d5 = CAMERA_PIN_D5,
            .pin_d4 = CAMERA_PIN_D4,
            .pin_d3 = CAMERA_PIN_D3,
            .pin_d2 = CAMERA_PIN_D2,
            .pin_d1 = CAMERA_PIN_D1,
            .pin_d0 = CAMERA_PIN_D0,
            .pin_vsync = CAMERA_PIN_VSYNC,
            .pin_href = CAMERA_PIN_HREF,
            .pin_pclk = CAMERA_PIN_PCLK,
            .xclk_freq_hz = XCLK_FREQ_HZ,
            .ledc_timer = LEDC_TIMER_0,
            .ledc_channel = LEDC_CHANNEL_0,
            .pixel_format = PIXFORMAT_RGB565,
            .frame_size = FRAMESIZE_VGA,
            .jpeg_quality = 10,
            .fb_count = 1,
            .fb_location = CAMERA_FB_IN_PSRAM,
            .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
            .sccb_i2c_port = (i2c_port_t)0,
        };

        camera_ = new Esp32Camera(camera_config);
        if (camera_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create camera");
            return;
        }
        camera_->SetVFlip(true);

        sensor_t* sensor = esp_camera_sensor_get();
        if (sensor == nullptr) {
            ESP_LOGW(TAG, "sensor is null, skip camera tuning");
            return;
        }

        // Improve edge details for object recognition use-cases.
        sensor->set_contrast(sensor, 1);
        sensor->set_brightness(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_quality(sensor, 10);
        sensor->set_framesize(sensor, FRAMESIZE_VGA);
        ESP_LOGI(TAG, "Camera tuned for sharper image (VGA, contrast=1, quality=10)");
    }

public:
    GoouuuEsp32S3CamOledBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeCameraHealthTimer();
        InitializeButtons();
        InitializeCamera();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(GoouuuEsp32S3CamOledBoard);
