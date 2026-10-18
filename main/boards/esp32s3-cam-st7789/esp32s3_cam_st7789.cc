#include "application.h"
#include "button.h"
#include "chronchi/chronchi_alert.h"
#include "chronchi/chronchi_ble.h"
#include "chronchi/chronchi_display.h"
#include "chronchi/mode_selector.h"
#include "youtube_audio_poller.h"
#include "codecs/no_audio_codec.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "display/oled_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "esp32_camera.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "system_reset.h"
#include "wifi_board.h"
#include "boards/common/backlight.h"
#include "assets/lang_config.h"
#include "jpeg_decoder.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "esp_lcd_panel_sh1106.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>

#undef lv_lock
#undef lv_unlock
#define lv_lock() lvgl_port_lock(0)
#define lv_unlock() lvgl_port_unlock()

extern "C" void esp_brownout_disable(void);

#define TAG "Esp32S3CamSt7789"

class Esp32S3CamLed : public Led {
private:
    std::mutex mutex_;
    led_strip_handle_t led_strip_ = nullptr;
    uint8_t r_ = 0, g_ = 0, b_ = 0;
    int blink_counter_ = 0;
    int blink_interval_ms_ = 0;
    esp_timer_handle_t blink_timer_ = nullptr;

    const uint8_t DEFAULT_BRIGHTNESS = 8;
    const uint8_t HIGH_BRIGHTNESS = 20;

    void OnBlinkTimer() {
        std::lock_guard<std::mutex> lock(mutex_);
        blink_counter_--;
        if (blink_counter_ & 1) {
            led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
            led_strip_refresh(led_strip_);
        } else {
            led_strip_clear(led_strip_);
            if (blink_counter_ == 0 && blink_timer_ != nullptr) {
                esp_timer_stop(blink_timer_);
            }
        }
    }

    void StartContinuousBlink(int interval_ms) {
        if (led_strip_ == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (blink_timer_ != nullptr) {
            esp_timer_stop(blink_timer_);
            blink_counter_ = -2; // infinite
            blink_interval_ms_ = interval_ms;
            esp_timer_start_periodic(blink_timer_, interval_ms * 1000);
        }
    }

    void TurnOn() {
        if (led_strip_ == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (blink_timer_ != nullptr) {
            esp_timer_stop(blink_timer_);
        }
        led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
        led_strip_refresh(led_strip_);
    }

    void TurnOff() {
        if (led_strip_ == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (blink_timer_ != nullptr) {
            esp_timer_stop(blink_timer_);
        }
        led_strip_clear(led_strip_);
    }

    void SetColor(uint8_t r, uint8_t g, uint8_t b) {
        r_ = r; g_ = g; b_ = b;
    }

public:
    Esp32S3CamLed(gpio_num_t gpio) {
        if (gpio == GPIO_NUM_NC) return;

        led_strip_config_t strip_config = {};
        strip_config.strip_gpio_num = gpio;
        strip_config.max_leds = 1;
        strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
        strip_config.led_model = LED_MODEL_WS2812;

        led_strip_rmt_config_t rmt_config = {};
        rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

        if (led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_) != ESP_OK) {
            led_strip_ = nullptr;
            return;
        }
        led_strip_clear(led_strip_);

        esp_timer_create_args_t blink_timer_args = {
            .callback = [](void *arg) {
                auto led = static_cast<Esp32S3CamLed*>(arg);
                led->OnBlinkTimer();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "cam_led_timer",
            .skip_unhandled_events = false,
        };
        esp_timer_create(&blink_timer_args, &blink_timer_);
    }

    ~Esp32S3CamLed() {
        if (blink_timer_ != nullptr) {
            esp_timer_stop(blink_timer_);
            esp_timer_delete(blink_timer_);
        }
        if (led_strip_ != nullptr) {
            led_strip_del(led_strip_);
        }
    }

    void OnStateChanged() override {
        auto& app = Application::GetInstance();
        auto device_state = app.GetDeviceState();
        switch (device_state) {
            case kDeviceStateStarting:
            case kDeviceStateConnecting:
            case kDeviceStateWifiConfiguring:
                // Belum konek WiFi -> Hijau kedap-kedip
                SetColor(0, DEFAULT_BRIGHTNESS, 0);
                StartContinuousBlink(500);
                break;

            case kDeviceStateListening:
            case kDeviceStateAudioTesting:
                // Listening -> Merah menyala
                if (app.IsVoiceDetected()) {
                    SetColor(HIGH_BRIGHTNESS, 0, 0);
                } else {
                    SetColor(DEFAULT_BRIGHTNESS, 0, 0);
                }
                TurnOn();
                break;

            case kDeviceStateSpeaking:
            case kDeviceStateNotifying:
                // Speaking -> Biru menyala
                SetColor(0, 0, DEFAULT_BRIGHTNESS);
                TurnOn();
                break;

            case kDeviceStateIdle:
            case kDeviceStateActivating:
            default:
                // Konek WiFi / Standby / Idle -> Mati total (hemat baterai)
                TurnOff();
                break;
        }
    }
};

class SoftwareBacklight : public Backlight {
private:
    Display* display_ = nullptr;
    std::unique_ptr<PwmBacklight> pwm_;
public:
    SoftwareBacklight(Display* display, int bl_pin) : display_(display) {
        if (bl_pin != (int)GPIO_NUM_NC) {
            pwm_ = std::make_unique<PwmBacklight>((gpio_num_t)bl_pin, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        }
    }
    void SetBrightnessImpl(uint8_t brightness) override {
        if (pwm_ != nullptr) {
            pwm_->SetBrightnessImpl(brightness);
        }
    }
};

class Esp32S3CamSt7789 : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button wake_up_button_;
    Button action_button_;
    Display* display_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    int rotation_ = 0; // 0, 90, 180, 270
    int display_width_ = 240;
    int display_height_ = 240;
    Esp32Camera* camera_ = nullptr;
    bool chronchi_mode_ = false;
    volatile bool mode_switch_requested_ = false;
    volatile bool ble_connected_flag_ = false;
    volatile bool ble_notif_flag_ = false;
    volatile bool ble_nav_flag_ = false;
    volatile bool ble_weather_flag_ = false;
    volatile bool ble_status_flag_ = false;
    volatile bool ble_music_flag_ = false;
    volatile bool ble_find_watch_flag_ = false;
    volatile bool ble_find_watch_active_ = false;
    volatile bool ble_camera_flag_ = false;
    volatile bool ble_camera_ready_ = false;
    ChronchiNotification last_notif_;
    ChronchiNavigation last_nav_;
    ChronchiWeather last_weather_;
    ChronchiPhoneStatus last_phone_status_;
    ChronchiMusic last_music_;
    ChronchiBle* chronchi_ble_ = nullptr;
    ChronchiDisplay* chronchi_display_ = nullptr;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t adc_cali_handle_ = nullptr;
    std::string display_preset_ = "headless";
    int backlight_pin_ = (int)GPIO_NUM_NC;
    std::unique_ptr<SoftwareBacklight> software_backlight_;

    void InitializeDisplay() {
        Settings settings("display", true);
        int cur_brightness = settings.GetInt("brightness", -1);
        if (cur_brightness == -1 || cur_brightness == 40 || cur_brightness == 20 || cur_brightness == 60) {
            settings.SetInt("brightness", 70);
        }
        std::string cur_theme = settings.GetString("theme", "dark");
        if (cur_theme.empty()) {
            settings.SetString("theme", "dark");
        }
        display_preset_ = settings.GetString("preset", "st7789_240x240_8p");
        backlight_pin_ = settings.GetInt("bl_pin", (int)DISPLAY_BACKLIGHT_PIN);

        // Hapus preset fiktif 1.69" 7-pin dan normalisasi ke 8-pin 240x240
        if (display_preset_.empty() || display_preset_ == "st7789_240x280_7p") {
            display_preset_ = "st7789_240x240_8p";
        }

        if (display_preset_ == "headless") {
            ESP_LOGI(TAG, "Initializing Headless mode (NoDisplay)");
            display_ = new NoDisplay();
            return;
        }

        // OLED SSD1306 & SH1106 128x64 I2C support
        if (display_preset_ == "oled_128x64" || display_preset_ == "oled_sh1106_128x64") {
            int sda_pin = settings.GetInt("oled_sda", 20);
            int scl_pin = settings.GetInt("oled_scl", 19);
            ESP_LOGI(TAG, "Initializing OLED (%s) on SDA=%d, SCL=%d", display_preset_.c_str(), sda_pin, scl_pin);

            i2c_master_bus_config_t i2c_bus_cfg = {
                .i2c_port = I2C_NUM_0,
                .sda_io_num = (gpio_num_t)sda_pin,
                .scl_io_num = (gpio_num_t)scl_pin,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7,
                .intr_priority = 0,
                .trans_queue_depth = 0,
                .flags = {
                    .enable_internal_pullup = 1,
                },
            };
            i2c_master_bus_handle_t oled_i2c_bus = nullptr;
            esp_err_t err = i2c_new_master_bus(&i2c_bus_cfg, &oled_i2c_bus);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to init OLED I2C bus (err 0x%x)", err);
                display_ = new NoDisplay();
                return;
            }

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
            esp_lcd_panel_io_handle_t panel_io = nullptr;
            ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(oled_i2c_bus, &io_config, &panel_io));

            esp_lcd_panel_dev_config_t panel_config = {};
            panel_config.reset_gpio_num = GPIO_NUM_NC;
            panel_config.bits_per_pixel = 1;

            esp_lcd_panel_handle_t panel = nullptr;
            if (display_preset_ == "oled_sh1106_128x64") {
                ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io, &panel_config, &panel));
            } else {
                esp_lcd_panel_ssd1306_config_t ssd1306_config = {
                    .height = 64,
                };
                panel_config.vendor_config = &ssd1306_config;
                ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io, &panel_config, &panel));
            }

            ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
            if (esp_lcd_panel_init(panel) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize OLED panel");
                display_ = new NoDisplay();
                return;
            }
            ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

            display_width_ = 128;
            display_height_ = 64;
            display_ = new OledDisplay(panel_io, panel, 128, 64, false, false);
            ESP_LOGI(TAG, "OLED (%s) successfully initialized!", display_preset_.c_str());
            return;
        }

        // ST7789 Presets - Default: 240x240 7-Pin (Tanpa CS)
        int width = 240;
        int height = 240;
        int offset_x = 0;
        int offset_y = 0;
        int cs_pin = (int)GPIO_NUM_NC;
        bool mirror_x = DISPLAY_MIRROR_X;
        bool mirror_y = DISPLAY_MIRROR_Y;
        bool swap_xy = DISPLAY_SWAP_XY;

        if (display_preset_ == "st7789_240x240_7p") {
            width = 240;
            height = 240;
            offset_x = 0;
            offset_y = 0;
            cs_pin = (int)GPIO_NUM_NC;
        } else if (display_preset_ == "st7789_240x240_8p") {
            width = 240;
            height = 240;
            offset_x = 0;
            offset_y = 0;
            cs_pin = 45;
        } else if (display_preset_ == "st7789_240x280_8p") {
            width = 240;
            height = 280;
            offset_x = 0;
            offset_y = 20;
            cs_pin = 45;
        } else if (display_preset_ == "st7789_172x320_8p") {
            width = 172;
            height = 320;
            offset_x = 34;
            offset_y = 0;
            cs_pin = 45;
        } else if (display_preset_ == "st7789_240x320_8p") {
            width = 240;
            height = 320;
            offset_x = 0;
            offset_y = 0;
            cs_pin = 45;
        }

        int scl_pin = settings.GetInt("scl_pin", (int)DISPLAY_CLK_PIN);
        int sda_pin = settings.GetInt("sda_pin", (int)DISPLAY_MOSI_PIN);
        int dc_pin  = settings.GetInt("dc_pin",  (int)DISPLAY_DC_PIN);
        int rst_pin = settings.GetInt("rst_pin", (int)DISPLAY_RST_PIN);

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = (gpio_num_t)sda_pin;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = (gpio_num_t)scl_pin;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = 320 * 240 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = (gpio_num_t)cs_pin;
        io_config.dc_gpio_num = (gpio_num_t)dc_pin;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        panel_io_ = panel_io;

        // Maximize drive capability for immune high-speed SPI signal
        gpio_set_drive_capability((gpio_num_t)scl_pin, GPIO_DRIVE_CAP_3);
        gpio_set_drive_capability((gpio_num_t)sda_pin, GPIO_DRIVE_CAP_3);
        gpio_set_drive_capability((gpio_num_t)dc_pin, GPIO_DRIVE_CAP_3);
        if (cs_pin != (int)GPIO_NUM_NC && cs_pin >= 0) {
            gpio_set_drive_capability((gpio_num_t)cs_pin, GPIO_DRIVE_CAP_3);
        }
        if (rst_pin != (int)GPIO_NUM_NC && rst_pin >= 0) {
            gpio_set_pull_mode((gpio_num_t)rst_pin, GPIO_PULLUP_ONLY);
            gpio_set_drive_capability((gpio_num_t)rst_pin, GPIO_DRIVE_CAP_3);
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = (gpio_num_t)rst_pin;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);

        // Pre-clear entire ST7789 RAM (240x320) with black
        std::vector<uint16_t> black_buf(240, 0x0000);
        for (int y = 0; y < 320; y++) {
            esp_lcd_panel_draw_bitmap(panel, 0, y, 240, y + 1, black_buf.data());
        }

        // Load rotation from NVS
        rotation_ = settings.GetInt("rotation", 0);
        if (rotation_ != 0 && rotation_ != 90 && rotation_ != 180 && rotation_ != 270) rotation_ = 0;
        if (rotation_ == 180) {
            mirror_x = !mirror_x;
            mirror_y = !mirror_y;
        }

        esp_lcd_panel_swap_xy(panel, swap_xy);
        esp_lcd_panel_mirror(panel, mirror_x, mirror_y);

        panel_ = panel;
        panel_io_ = panel_io;

        display_width_ = width;
        display_height_ = height;
        display_ = new SpiLcdDisplay(panel_io, panel, width, height,
                                     offset_x, offset_y, mirror_x,
                                     mirror_y, swap_xy);
        display_->SetIsSt7789(true);

        // Turn on backlight immediately once display is ready
        auto backlight = GetBacklight();
        if (backlight != nullptr) {
            backlight->RestoreBrightness();
        }

        ESP_LOGI(TAG, "ST7789 initialized (%dx%d, SCL=%d, SDA=%d, DC=%d, RST=%d, CS=%d)",
                 width, height, scl_pin, sda_pin, dc_pin, rst_pin, cs_pin);
    }
    void InitializeCamera() {
        Settings settings("camera", false);
        std::string cam_preset = settings.GetString("preset", "auto");
        if (cam_preset == "disabled") {
            ESP_LOGI(TAG, "Camera is disabled by user preset");
            camera_ = nullptr;
            return;
        }

        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = (cam_preset == "gc2145") ? 10000000 : 20000000;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;  // 320x240 for streaming (lightweight)
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_LATEST;

        camera_ = new Esp32Camera(config);
        esp_log_level_set("cam_hal", ESP_LOG_ERROR);
    }

    void InitializeButtons() {
        ESP_LOGI(TAG, "InitializeButtons: BOOT=GPIO%d, VOL_UP=GPIO%d, VOL_DOWN=GPIO%d",
                 (int)BOOT_BUTTON_GPIO, (int)VOLUME_UP_BUTTON_GPIO, (int)VOLUME_DOWN_BUTTON_GPIO);

        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                ESP_LOGI(TAG, "YouTube is playing, stopping playback");
                YouTubeAudioPoller::GetInstance().Abort();
                return;
            }
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                ESP_LOGI(TAG, "AI is speaking, interrupting speech");
                app.AbortSpeaking(kAbortReasonNone);
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "BOOT button double click: playing test song");
            YouTubeAudioPoller::GetInstance().PlayQuery("remix");
        });

        boot_button_.OnMultipleClick([this]() {
            ESP_LOGI(TAG, "BOOT button 3x click");
        }, 3);

        boot_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "BOOT button long press: entering Wi-Fi config mode");
            EnterWifiConfigMode();
        });

        volume_up_button_.OnClick([this]() {
            if (camera_ && camera_->IsStreaming()) {
                auto lcd = dynamic_cast<LcdDisplay*>(GetDisplay());
                if (lcd) {
                    int level = lcd->ZoomIn();
                    ESP_LOGI(TAG, "Zoom IN clicked (GPIO 14) -> %dx", level);
                    lcd->ShowNotification("Zoom: " + std::to_string(level) + "x");
                }
                return;
            }

            auto codec = GetAudioCodec();
            if (codec) {
                int vol = codec->output_volume();
                vol = std::min(100, vol + 10);
                codec->SetOutputVolume(vol);
                ESP_LOGI(TAG, "Volume UP clicked -> volume: %d%%", vol);
                if (display_) {
                    display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(vol / 10));
                }
            }
        });

        volume_up_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            if (codec) {
                codec->SetOutputVolume(100);
                ESP_LOGI(TAG, "Volume UP long press -> MAX VOLUME (100%)");
                if (display_) {
                    display_->ShowNotification(Lang::Strings::MAX_VOLUME);
                }
            }
        });

        volume_up_button_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "Volume UP double click: rotating display 180 deg");
            RotateDisplay();
        });

        volume_down_button_.OnClick([this]() {
            if (camera_ && camera_->IsStreaming()) {
                auto lcd = dynamic_cast<LcdDisplay*>(GetDisplay());
                if (lcd) {
                    int level = lcd->ZoomOut();
                    ESP_LOGI(TAG, "Zoom OUT clicked -> %dx", level);
                    lcd->ShowNotification("Zoom: " + std::to_string(level) + "x");
                }
                return;
            }

            auto codec = GetAudioCodec();
            if (codec) {
                int vol = codec->output_volume();
                vol = std::max(0, vol - 10);
                codec->SetOutputVolume(vol);
                ESP_LOGI(TAG, "Volume DOWN clicked -> volume: %d%%", vol);
                if (display_) {
                    display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(vol / 10));
                }
            }
        });

        volume_down_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            if (codec) {
                codec->SetOutputVolume(0);
                ESP_LOGI(TAG, "Volume DOWN long press -> MUTED (0%)");
                if (display_) {
                    display_->ShowNotification(Lang::Strings::MUTED);
                }
            }
        });

        volume_down_button_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "Volume DOWN double click");
        });

        wake_up_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                ESP_LOGI(TAG, "YouTube is playing, stopping playback");
                YouTubeAudioPoller::GetInstance().Abort();
                return;
            }
            app.ToggleChatState();
        });

        action_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                ESP_LOGI(TAG, "YouTube is playing, stopping playback");
                YouTubeAudioPoller::GetInstance().Abort();
                return;
            }
            app.ToggleChatState();
        });

        action_button_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "Double click: rotating display");
            RotateDisplay();
        });

        // Ensure starting volume is safe on battery to prevent brownout reset
        auto codec = GetAudioCodec();
        if (codec != nullptr && codec->output_volume() > 75) {
            ESP_LOGI(TAG, "Clamping initial volume from %d%% to 75%% to prevent battery brownout", codec->output_volume());
            codec->SetOutputVolume(75);
        }
    }

    void InitializeBatteryAdc() {
        gpio_reset_pin((gpio_num_t)BATTERY_ADC_GPIO);
        gpio_set_pull_mode((gpio_num_t)BATTERY_ADC_GPIO, GPIO_FLOATING);

        adc_oneshot_unit_init_cfg_t init_cfg = {};
        init_cfg.unit_id = ADC_UNIT_1;
        if (adc_oneshot_new_unit(&init_cfg, &adc_handle_) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to init ADC unit");
            return;
        }

        adc_oneshot_chan_cfg_t chan_cfg = {};
        chan_cfg.bitwidth = ADC_BITWIDTH_12;
        chan_cfg.atten = ADC_ATTEN_DB_12;
        if (adc_oneshot_config_channel(adc_handle_, BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to config ADC channel");
            return;
        }

        adc_cali_curve_fitting_config_t cali_config = {};
        cali_config.unit_id = ADC_UNIT_1;
        cali_config.chan = BATTERY_ADC_CHANNEL;
        cali_config.atten = ADC_ATTEN_DB_12;
        cali_config.bitwidth = ADC_BITWIDTH_12;
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create ADC calibration");
        }
        ESP_LOGI(TAG, "Battery ADC initialized on GPIO %d", BATTERY_ADC_GPIO);
    }

public:
    Esp32S3CamSt7789()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
          wake_up_button_(WAKE_UP_BUTTON_GPIO, false, 2000),
          action_button_(ACTION_BUTTON_GPIO) {
        esp_brownout_disable();
        {
            Settings d_set("display", true);
            if (d_set.GetInt("v3_sync", 0) == 0) {
                d_set.SetInt("v3_sync", 1);
                std::string cur_preset = d_set.GetString("preset", "");
                if (cur_preset.empty() || cur_preset == "st7789_240x280_7p") {
                    d_set.SetString("preset", "st7789_240x240_8p");
                    d_set.SetInt("cs_pin", 45);
                    d_set.SetInt("scl_pin", 19);
                    d_set.SetInt("sda_pin", 20);
                    d_set.SetInt("rst_pin", 21);
                    d_set.SetInt("dc_pin", 47);
                    d_set.SetInt("bl_pin", 38);
                    d_set.SetInt("offset_x", 0);
                    d_set.SetInt("offset_y", 0);
                }
            }

            Settings a_set("audio", true);
            if (a_set.GetString("profile", "").empty()) {
                a_set.SetString("profile", "simplex_inmp441_max98");
                a_set.SetInt("output_volume", 100);
                a_set.SetInt("mic_ws", 1);
                a_set.SetInt("mic_sck", 2);
                a_set.SetInt("mic_din", 42);
                a_set.SetInt("spk_bclk", 40);
                a_set.SetInt("spk_ws", 41);
                a_set.SetInt("spk_dout", 39);
            }
        }
        InitializeDisplay();

        // Quick boot check (non-blocking)
        DeviceMode mode = ModeSelector::CheckBoot(BOOT_BUTTON_GPIO);

        if (mode == DeviceMode::CHRONCHI) {
            ESP_LOGI("Board", "=== CHRONCHI MODE (boot) ===");
            chronchi_mode_ = true;
            InitializeBatteryAdc();
            auto backlight = GetBacklight();
            if (backlight != nullptr) {
                backlight->RestoreBrightness();
            }
            return;
        }

        ESP_LOGI("Board", "=== XIAOZHI MODE ===");
        chronchi_mode_ = false;
        InitializeButtons();
        InitializeBatteryAdc();
        InitializeCamera();

        // Set up camera streaming callback to display (color LCD like ST7789 only)
        if (camera_ && dynamic_cast<LcdDisplay*>(display_) != nullptr) {
            camera_->SetStreamCallback([this](uint8_t* data, size_t len, uint16_t w, uint16_t h) {
                auto display = dynamic_cast<LcdDisplay*>(GetDisplay());
                if (!display || !data || len == 0) return;

                if (w == 640 && h == 480) {
                    // VGA RGB565 (640x480) -> Fast 2:1 downsample to 320x240 with exact byte-swap colors
                    uint16_t target_w = 320;
                    uint16_t target_h = 240;
                    size_t out_size = target_w * target_h * 2;
                    uint8_t* buf = (uint8_t*)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (buf) {
                        uint16_t* src = (uint16_t*)data;
                        uint16_t* dst = (uint16_t*)buf;
                        for (int y = 0; y < target_h; y++) {
                            uint16_t* src_row = src + (y * 2) * 640;
                            uint16_t* dst_row = dst + y * target_w;
                            for (int x = 0; x < target_w; x++) {
                                dst_row[x] = __builtin_bswap16(src_row[x * 2]);
                            }
                        }
                        display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                            buf, out_size, target_w, target_h, target_w * 2, LV_COLOR_FORMAT_RGB565));
                    }
                } else if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
                    // JPEG stream frame (fallback)
                    esp_jpeg_image_cfg_t jpeg_cfg = {};
                    jpeg_cfg.indata = data;
                    jpeg_cfg.indata_size = len;

                    esp_jpeg_image_scale_t scale = (w > 360) ? JPEG_IMAGE_SCALE_1_2 : JPEG_IMAGE_SCALE_0;
                    int divisor = (w > 360) ? 2 : 1;

                    uint16_t out_w = (w + divisor - 1) / divisor;
                    uint16_t out_h = (h + divisor - 1) / divisor;
                    size_t out_size = out_w * out_h * 2;

                    uint8_t* buf = (uint8_t*)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (buf) {
                        jpeg_cfg.outbuf = buf;
                        jpeg_cfg.outbuf_size = out_size;
                        jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
                        jpeg_cfg.out_scale = scale;
                        jpeg_cfg.flags.swap_color_bytes = 0; // 0 = native little-endian for LVGL RGB565

                        esp_jpeg_image_output_t out_img = {};
                        if (esp_jpeg_decode(&jpeg_cfg, &out_img) == ESP_OK) {
                            display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                                buf, out_img.output_len, out_img.width, out_img.height,
                                out_img.width * 2, LV_COLOR_FORMAT_RGB565));
                        } else {
                            heap_caps_free(buf);
                        }
                    }
                } else {
                    // Raw RGB565 stream frame (QVGA 320x240)
                    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (buf) {
                        uint16_t* src = (uint16_t*)data;
                        uint16_t* dst = (uint16_t*)buf;
                        size_t pixel_count = w * h;
                        for (size_t i = 0; i < pixel_count; i++) {
                            dst[i] = __builtin_bswap16(src[i]);
                        }
                        display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                            buf, len, w, h, w * 2, LV_COLOR_FORMAT_RGB565));
                    }
                }
            });

            // Set up capture animation callback (flash effect)
            camera_->SetCaptureCallback([this]() {
                auto display = dynamic_cast<LcdDisplay*>(GetDisplay());
                if (display) {
                    display->ShowCaptureFlash();
                }
            });
        }

        auto backlight = GetBacklight();
        if (backlight != nullptr) {
            backlight->RestoreBrightness();
        }
    }

    virtual Led* GetLed() override {
        static Esp32S3CamLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static AudioCodec* s_audio_codec = nullptr;
        if (s_audio_codec != nullptr) {
            return s_audio_codec;
        }

        Settings settings("audio", false);
        std::string profile = settings.GetString("profile", "simplex_inmp441_max98");

        if (profile == "weact_es8311_ns4150b" || profile == "duplex_es8311") {
            esp_brownout_disable();
            ESP_LOGI(TAG, "Initializing WeAct ES8311 + NS4150B Mono Audio Codec");

            int sda_pin = settings.GetInt("es8311_sda", 2);
            int scl_pin = settings.GetInt("es8311_scl", 1);

            auto probe_bus = [](int sda, int scl, i2c_master_bus_handle_t& bus, uint8_t& found_addr) -> bool {
                i2c_master_bus_config_t i2c_bus_cfg = {
                    .i2c_port = I2C_NUM_0,
                    .sda_io_num = (gpio_num_t)sda,
                    .scl_io_num = (gpio_num_t)scl,
                    .clk_source = I2C_CLK_SRC_DEFAULT,
                    .glitch_ignore_cnt = 7,
                    .intr_priority = 0,
                    .trans_queue_depth = 0,
                    .flags = {
                        .enable_internal_pullup = 1,
                    },
                };
                if (i2c_new_master_bus(&i2c_bus_cfg, &bus) != ESP_OK) {
                    return false;
                }
                if (i2c_master_probe(bus, 0x18, 100) == ESP_OK) {
                    found_addr = 0x18;
                    return true;
                }
                if (i2c_master_probe(bus, 0x19, 100) == ESP_OK) {
                    found_addr = 0x19;
                    return true;
                }
                return false;
            };

            i2c_master_bus_handle_t i2c_bus = nullptr;
            uint8_t es8311_addr = 0x18;
            bool found = probe_bus(sda_pin, scl_pin, i2c_bus, es8311_addr);

            if (!found) {
                if (i2c_bus != nullptr) {
                    i2c_del_master_bus(i2c_bus);
                    i2c_bus = nullptr;
                }
                ESP_LOGW(TAG, "ES8311 not responding on SDA=%d, SCL=%d. Testing swapped pins (SDA=%d, SCL=%d)...",
                         sda_pin, scl_pin, scl_pin, sda_pin);
                found = probe_bus(scl_pin, sda_pin, i2c_bus, es8311_addr);
                if (found) {
                    ESP_LOGI(TAG, "ES8311 detected with swapped pins! (SDA=%d, SCL=%d, addr=0x%02x)",
                             scl_pin, sda_pin, es8311_addr);
                }
            }

            if (found && i2c_bus != nullptr) {
                ESP_LOGI(TAG, "ES8311 successfully detected at 0x%02x, initializing codec...", es8311_addr);
                s_audio_codec = new Es8311AudioCodec(
                    i2c_bus,
                    I2C_NUM_0,
                    AUDIO_INPUT_SAMPLE_RATE,
                    AUDIO_INPUT_SAMPLE_RATE,
                    AUDIO_I2S_GPIO_MCLK,      // MCLK pin (GPIO 14, connect to WeAct MC pin)
                    AUDIO_I2S_SPK_GPIO_BCLK,  // GPIO 40
                    AUDIO_I2S_SPK_GPIO_LRCK,  // GPIO 41
                    AUDIO_I2S_SPK_GPIO_DOUT,  // GPIO 39
                    AUDIO_I2S_MIC_GPIO_DIN,   // GPIO 42
                    (gpio_num_t)settings.GetInt("es8311_pa", (int)GPIO_NUM_46), // PA pin (GPIO 46)
                    (uint8_t)(es8311_addr << 1),
                    true,                     // use_mclk = true (hardware MCLK from ESP32)
                    false                     // pa_inverted = false (Active High)
                );
                return s_audio_codec;
            } else {
                if (i2c_bus != nullptr) {
                    ESP_LOGW(TAG, "Scanning for any other I2C devices on bus...");
                    for (uint8_t a = 1; a < 127; a++) {
                        if (i2c_master_probe(i2c_bus, a, 20) == ESP_OK) {
                            ESP_LOGI(TAG, "  -> Found I2C device at 0x%02X", a);
                        }
                    }
                    i2c_del_master_bus(i2c_bus);
                    i2c_bus = nullptr;
                }
                ESP_LOGE(TAG, "================================================================");
                ESP_LOGE(TAG, "[!] WeAct ES8311 TIDAK MERESPONS di I2C (addr 0x18/0x19)!");
                ESP_LOGE(TAG, "    Periksa koneksi kabel modul WeAct:");
                ESP_LOGE(TAG, "    - Pin 1 (5V)   -> 5V ESP32");
                ESP_LOGE(TAG, "    - Pin 2 (GND)  -> GND ESP32");
                ESP_LOGE(TAG, "    - Pin 8 (SCL)  -> GPIO 1");
                ESP_LOGE(TAG, "    - Pin 9 (SDA)  -> GPIO 2");
                ESP_LOGE(TAG, "    - Pin 10 (PA)  -> 3.3V (enable amplifier)");
                ESP_LOGE(TAG, "    Sistem dialihkan ke Simplex audio agar ESP32 TIDAK CRASH/REBOOT.");
                ESP_LOGE(TAG, "================================================================");
            }
        }

        ESP_LOGI(TAG, "Initializing INMP441 + MAX98357A Simplex Audio");
        int spk_bclk = settings.GetInt("spk_bclk", (int)AUDIO_I2S_SPK_GPIO_BCLK);
        int spk_ws   = settings.GetInt("spk_ws",   (int)AUDIO_I2S_SPK_GPIO_LRCK);
        int spk_dout = settings.GetInt("spk_dout", (int)AUDIO_I2S_SPK_GPIO_DOUT);
        int mic_sck  = settings.GetInt("mic_sck",  (int)AUDIO_I2S_MIC_GPIO_SCK);
        int mic_ws   = settings.GetInt("mic_ws",   (int)AUDIO_I2S_MIC_GPIO_WS);
        int mic_din  = settings.GetInt("mic_din",  (int)AUDIO_I2S_MIC_GPIO_DIN);

        // In Simplex mode, MIC and SPK must have dedicated pins.
        if (mic_sck == spk_bclk || mic_ws == spk_ws) {
            ESP_LOGW(TAG, "Simplex audio pins conflicted with duplex (mic_sck=%d, spk_bclk=%d). Enforcing INMP441 pins: SCK=2, WS=1",
                     mic_sck, spk_bclk);
            mic_sck = (int)AUDIO_I2S_MIC_GPIO_SCK; // GPIO 2
            mic_ws  = (int)AUDIO_I2S_MIC_GPIO_WS;  // GPIO 1
        }

        ESP_LOGI(TAG, "Audio Pins: SPK(BCLK=%d, WS=%d, DOUT=%d), MIC(SCK=%d, WS=%d, DIN=%d)",
                 spk_bclk, spk_ws, spk_dout, mic_sck, mic_ws, mic_din);

        s_audio_codec = new NoAudioCodecSimplex(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                                (gpio_num_t)spk_bclk, (gpio_num_t)spk_ws,
                                                (gpio_num_t)spk_dout, (gpio_num_t)mic_sck,
                                                (gpio_num_t)mic_ws, (gpio_num_t)mic_din);
        return s_audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        if (display_preset_ == "oled_128x64" || display_preset_ == "oled_sh1106_128x64" || display_preset_ == "headless") {
            return nullptr;
        }
        if (!software_backlight_ && display_ != nullptr) {
            software_backlight_ = std::make_unique<SoftwareBacklight>(display_, backlight_pin_);
        }
        if (software_backlight_) {
            return software_backlight_.get();
        }
        if (backlight_pin_ != (int)GPIO_NUM_NC) {
            static PwmBacklight backlight((gpio_num_t)backlight_pin_, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual Camera* GetCamera() override { return camera_; }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (adc_handle_ == nullptr || adc_cali_handle_ == nullptr) {
            return false;
        }

        int adc_raw = 0;
        int voltage_mv = 0;

        // Read ADC multiple times and average (20 samples for high stability)
        for (int i = 0; i < 20; i++) {
            int raw = 0;
            adc_oneshot_read(adc_handle_, BATTERY_ADC_CHANNEL, &raw);
            adc_raw += raw;
        }
        adc_raw /= 20;

        // Load custom ratio if set in settings (ratio_m = ratio * 1000, default 2000 for 100k+100k)
        Settings b_set("battery", false);
        int ratio_m = b_set.GetInt("ratio_m", (int)(BATTERY_VOLTAGE_DIVIDER_RATIO * 1000));
        float divider_ratio = (float)ratio_m / 1000.0f;

        // Convert to voltage
        adc_cali_raw_to_voltage(adc_cali_handle_, adc_raw, &voltage_mv);

        // Apply voltage divider ratio
        int battery_mv = (int)(voltage_mv * divider_ratio);

        // Accurate Li-ion discharge curve (Piecewise linear interpolation)
        static const struct {
            int voltage_mv;
            int percentage;
        } kBatteryCurve[] = {
            { 4200, 100 },
            { 4060,  90 },
            { 3980,  80 },
            { 3920,  70 },
            { 3870,  60 },
            { 3820,  50 },
            { 3790,  40 },
            { 3770,  30 },
            { 3740,  20 },
            { 3680,  10 },
            { 3450,   5 },
            { 3300,   0 },
        };
        constexpr size_t kCurvePoints = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);

        int calculated_level = 0;
        if (battery_mv >= kBatteryCurve[0].voltage_mv) {
            calculated_level = 100;
        } else if (battery_mv <= kBatteryCurve[kCurvePoints - 1].voltage_mv) {
            calculated_level = 0;
        } else {
            for (size_t i = 0; i < kCurvePoints - 1; i++) {
                if (battery_mv <= kBatteryCurve[i].voltage_mv && battery_mv >= kBatteryCurve[i + 1].voltage_mv) {
                    int v_high = kBatteryCurve[i].voltage_mv;
                    int v_low = kBatteryCurve[i + 1].voltage_mv;
                    int p_high = kBatteryCurve[i].percentage;
                    int p_low = kBatteryCurve[i + 1].percentage;
                    calculated_level = p_low + (battery_mv - v_low) * (p_high - p_low) / (v_high - v_low);
                    break;
                }
            }
        }

        // Exponential Moving Average (EMA) to eliminate jumpy readings
        static int s_filtered_level = -1;
        if (s_filtered_level < 0) {
            Settings b_set("battery", false);
            int saved_soc = b_set.GetInt("last_soc", -1);
            if (saved_soc >= 0 && saved_soc <= 100 && calculated_level <= saved_soc && calculated_level >= saved_soc - 15) {
                s_filtered_level = saved_soc;
                ESP_LOGI(TAG, "Anti boot-sag applied: using saved %d%% (raw=%d%%, vbat=%dmV)", saved_soc, calculated_level, battery_mv);
            } else {
                s_filtered_level = calculated_level;
            }
        } else {
            s_filtered_level = (s_filtered_level * 7 + calculated_level * 3) / 10;
        }
        level = s_filtered_level;

        static uint32_t s_last_nvs_time = 0;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_last_nvs_time == 0 || (now_ms - s_last_nvs_time >= 60000)) {
            s_last_nvs_time = now_ms;
            Settings b_set("battery", true);
            b_set.SetInt("last_soc", s_filtered_level);
        }

        // ── Smart Software Charging Detection ──
        static bool s_is_charging = false;
        static int s_prev_vbat = 0;
        static int s_baseline_vbat = 0;
        static int s_charge_streak = 0;
        static int s_discharge_streak = 0;

        if (s_prev_vbat == 0) {
            s_prev_vbat = battery_mv;
            s_baseline_vbat = battery_mv;
            // Jangan langsung asumsikan sedang dicas saat boot/restart jika baterai penuh
            s_is_charging = false;
        }

        int v_delta = battery_mv - s_prev_vbat;
        int baseline_delta = battery_mv - s_baseline_vbat;

        if (!s_is_charging) {
            // Deteksi charger dicolok:
            // 1. Lonjakan tegangan mendadak (>= +45mV) dibanding baseline stabil
            // 2. ATAU tren kenaikan konsisten (+20mV) saat baterai belum penuh (battery_mv < 4180mV)
            if (baseline_delta >= 45 || (v_delta >= 20 && battery_mv < 4180)) {
                s_charge_streak++;
                if (s_charge_streak >= 2) {
                    s_is_charging = true;
                    s_charge_streak = 0;
                    ESP_LOGI(TAG, "⚡ [SMART CHARGING] Charger terdeteksi! (Vbat=%dmV, jump=+%dmV)", battery_mv, baseline_delta);
                }
            } else {
                s_charge_streak = 0;
                // Baseline perlahan mengikuti tegangan saat stabil / discharging
                if (battery_mv < s_baseline_vbat) {
                    s_baseline_vbat = (s_baseline_vbat * 8 + battery_mv * 2) / 10;
                } else if (baseline_delta < 25) {
                    s_baseline_vbat = (s_baseline_vbat * 9 + battery_mv) / 10;
                }
            }
        } else {
            // Deteksi charger dicabut / cas selesai:
            // 1. Drop instan (<= -20mV) karena arus cas mati (cukup 1 kali pembacaan karena cabut charger adalah event seketika)
            // 2. ATAU baterai sudah penuh (level >= 100% atau battery_mv >= 4180mV) dan tidak ada kenaikan voltase (v_delta <= 0)
            // 3. ATAU voltase terus turun/stagnan (v_delta <= 0) selama 3 cycle berturut-turut saat voltase tinggi
            if (v_delta <= -20) {
                s_is_charging = false;
                s_discharge_streak = 0;
                s_baseline_vbat = battery_mv;
                ESP_LOGI(TAG, "🔋 [SMART CHARGING] Charger dicabut! (Vbat=%dmV, drop=%dmV)", battery_mv, v_delta);
            } else if (v_delta <= 0) {
                s_discharge_streak++;
                if (s_discharge_streak >= 3 || (battery_mv >= 4170 && s_discharge_streak >= 2)) {
                    s_is_charging = false;
                    s_discharge_streak = 0;
                    s_baseline_vbat = battery_mv;
                    ESP_LOGI(TAG, "🔋 [SMART CHARGING] Pengisian selesai atau charger dilepas (Vbat=%dmV)", battery_mv);
                }
            } else {
                s_discharge_streak = 0;
            }
        }

        s_prev_vbat = battery_mv;

        charging = s_is_charging;
        discharging = !s_is_charging;

        ESP_LOGI(TAG, "Battery: ADC=%d, Vpin=%dmV, Vbat=%dmV, Level=%d%% (charging=%s)", 
                 adc_raw, voltage_mv, battery_mv, level, charging ? "YES ⚡" : "NO");
        return true;
    }

    bool IsChronchiMode() override { return chronchi_mode_; }

    void SwitchToMode(DeviceMode target_mode) {
        if (target_mode == DeviceMode::CHRONCHI) {
            ESP_LOGW(TAG, "SwitchToMode(CHRONCHI) ignored: Device locked to XiaoZhi Assistant mode");
            return;
        }
        const char* name = "XiaoZhi";

        ESP_LOGI(TAG, "Switching mode to %s...", name);
        Settings settings("chronchi", true);
        settings.SetInt("mode", (int32_t)target_mode);

        if (chronchi_display_) {
            ChronchiNotification n;
            n.app = "System";
            n.title = "Mode Switch";
            n.message = std::string("Beralih ke mode ") + name + "...";
            chronchi_display_->showNotification(n);
        } else if (display_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Switching to %s...", name);
            display_->ShowNotification(buf, 1500);
        }

        esp_timer_handle_t timer;
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                esp_restart();
            },
            .name = "restart_timer"
        };
        esp_timer_create(&timer_args, &timer);
        esp_timer_start_once(timer, 400 * 1000);
    }

    void RotateDisplay() {
        if (!panel_ || dynamic_cast<LvglDisplay*>(display_) == nullptr) return;

        rotation_ = (rotation_ == 180) ? 0 : 180;

        bool mirror_x = (rotation_ == 180) ? !DISPLAY_MIRROR_X : DISPLAY_MIRROR_X;
        bool mirror_y = (rotation_ == 180) ? !DISPLAY_MIRROR_Y : DISPLAY_MIRROR_Y;

        esp_lcd_panel_mirror(panel_, mirror_x, mirror_y);

        // ST7789 MADCTL register:
        // Bit 3 (0x08) = BGR element order (matches DISPLAY_RGB_ORDER)
        // Bit 6 (0x40) = MX (Column Address Order / Mirror X)
        // Bit 7 (0x80) = MY (Row Address Order / Mirror Y)
        uint8_t madctl = 0x08; // Preserve BGR color element order
        if (mirror_x) madctl |= 0x40;
        if (mirror_y) madctl |= 0x80;
        if (panel_io_) {
            esp_lcd_panel_io_tx_param(panel_io_, 0x36, &madctl, 1);
        }

        {
            DisplayLockGuard lock(display_);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(lv_display_get_default());
        }

        Settings settings("display", true);
        settings.SetInt("rotation", rotation_);
        ESP_LOGI(TAG, "Rotated to %d deg (MADCTL=0x%02X, mirror_x=%d, mirror_y=%d)",
                 rotation_, madctl, mirror_x, mirror_y);
    }

    void StartChronchi() {
        ESP_LOGI(TAG, "Initializing Chronchi mode...");
        vTaskDelay(pdMS_TO_TICKS(500));  // Wait for LVGL to be ready
        StartNetwork();

        ChronchiAlert::GetInstance().Initialize(VIBRATION_MOTOR_PIN, BUILTIN_LED_GPIO);

        chronchi_display_ = new ChronchiDisplay();
        chronchi_display_->init(display_width_, display_height_);
        chronchi_display_->showHome();

        // Update initial battery status immediately
        int init_batt_level = 0;
        bool init_chg = false, init_dis = false;
        if (GetBatteryLevel(init_batt_level, init_chg, init_dis)) {
            chronchi_display_->updateBattery(init_batt_level, init_chg);
        }

        chronchi_ble_ = new ChronchiBle();
        chronchi_ble_->begin("Chronchi");

        chronchi_ble_->setConnectionCallback([this](bool connected) {
            ESP_LOGI(TAG, "Chronchi BLE: %s", connected ? "Connected" : "Disconnected");
            ble_connected_flag_ = connected;
        });

        chronchi_ble_->setNotificationCallback([this](const ChronchiNotification& notif) {
            ESP_LOGI(TAG, "Notification: %s - %s", notif.app.c_str(), notif.title.c_str());
            last_notif_ = notif;
            ble_notif_flag_ = true;
            if (!chronchi_ble_->isQuietActive()) {
                ChronchiAlert::GetInstance().VibrateNotification();
            } else {
                ESP_LOGI(TAG, "Quiet hours active: notification vibration suppressed");
            }
        });

        chronchi_ble_->setNavigationCallback([this](const ChronchiNavigation& nav) {
            ESP_LOGI(TAG, "Navigation: %s - %s", nav.title.c_str(), nav.directions.c_str());
            last_nav_ = nav;
            ble_nav_flag_ = true;
        });

        chronchi_ble_->setWeatherCallback([this](const ChronchiWeather& weather) {
            ESP_LOGI(TAG, "Weather: %.0f°C %s", weather.temperatureC, weather.condition.c_str());
            last_weather_ = weather;
            ble_weather_flag_ = true;
        });

        chronchi_ble_->setPhoneStatusCallback([this](const ChronchiPhoneStatus& status) {
            ESP_LOGI(TAG, "Phone: %d%% %s %s", status.batteryLevel,
                     status.network.transport.c_str(), status.network.generation.c_str());
            last_phone_status_ = status;
            ble_status_flag_ = true;
        });

        chronchi_ble_->setRingerCallback([this](const std::string& caller, bool active) {
            ESP_LOGI(TAG, "Chronos Call: %s (active: %d)", caller.c_str(), active);
            if (active) {
                ChronchiAlert::GetInstance().StartCallAlert();
                ChronchiNotification call_notif;
                call_notif.icon = 0x01;
                call_notif.app = "Phone Call";
                call_notif.title = "Incoming Call";
                call_notif.message = caller.empty() ? "Incoming Call" : caller;
                time_t now = time(nullptr);
                struct tm tm_info;
                localtime_r(&now, &tm_info);
                char tbuf[16];
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
                call_notif.time = tbuf;
                last_notif_ = call_notif;
                ble_notif_flag_ = true;
            } else {
                ChronchiAlert::GetInstance().StopAlert();
            }
        });

        chronchi_ble_->setMusicCallback([this](const ChronchiMusic& music) {
            ESP_LOGI(TAG, "Chronos Music: %s - %s (%s)", music.artist.c_str(), music.title.c_str(), music.app.c_str());
            last_music_ = music;
            ble_music_flag_ = true;
        });

        chronchi_ble_->setWatchfaceCallback([this](uint32_t color, uint32_t layout) {
            ESP_LOGI(TAG, "Watchface customized via Chronos: color=0x%06lX layout=%lu",
                     (unsigned long)color, (unsigned long)layout);
            lv_lock();
            if (chronchi_display_) {
                chronchi_display_->setWatchfaceTheme(color, layout);
            }
            lv_unlock();
        });

        chronchi_ble_->setFindWatchCallback([this](bool active) {
            ESP_LOGI(TAG, "Chronos Find Watch: %d", active);
            ble_find_watch_active_ = active;
            ble_find_watch_flag_ = true;
            if (active) {
                ChronchiAlert::GetInstance().StartFindDeviceAlarm();
            } else {
                ChronchiAlert::GetInstance().StopAlert();
            }
        });

        chronchi_ble_->setCameraStateCallback([this](bool ready) {
            ESP_LOGI(TAG, "Chronos Camera Ready: %d", ready);
            ble_camera_ready_ = ready;
            ble_camera_flag_ = true;
        });

        chronchi_ble_->setTimeFormatCallback([this](bool is_24h) {
            ESP_LOGI(TAG, "Chronos Time Format 24h: %d", is_24h);
            lv_lock();
            if (chronchi_display_) {
                chronchi_display_->set24HourFormat(is_24h);
            }
            lv_unlock();
        });

        chronchi_ble_->setAlarmCallback([this](uint8_t index, const ChronchiAlarm& alarm) {
            ESP_LOGI(TAG, "Chronos Alarm #%d: %02d:%02d enabled=%d", index, alarm.hour, alarm.minute, alarm.enabled);
        });

        ESP_LOGI(TAG, "Chronchi started. BLE: Chronchi");

        // LVGL timer to check BLE flags and update display (runs in LVGL context)
        lv_timer_create(
            [](lv_timer_t* t) {
                auto* board = static_cast<Esp32S3CamSt7789*>(lv_timer_get_user_data(t));
                if (!board || !board->chronchi_display_)
                    return;

                // Always update BT status (green=connected, grey=disconnected)
                board->chronchi_display_->updateBtStatus(board->ble_connected_flag_);

                if (board->ble_notif_flag_) {
                    board->chronchi_display_->showNotification(board->last_notif_);
                    board->ble_notif_flag_ = false;
                }
                if (board->ble_nav_flag_) {
                    if (board->last_nav_.active) {
                        board->chronchi_display_->showNavigation(board->last_nav_);
                    } else {
                        board->chronchi_display_->showHome();
                    }
                    board->ble_nav_flag_ = false;
                }
                if (board->ble_weather_flag_) {
                    board->chronchi_display_->showWeather(board->last_weather_);
                    board->chronchi_display_->updateHomeWeather(board->last_weather_);
                    board->ble_weather_flag_ = false;
                }
                if (board->ble_status_flag_) {
                    board->chronchi_display_->showPhoneStatus(board->last_phone_status_);
                    board->ble_status_flag_ = false;
                }
                if (board->ble_music_flag_) {
                    board->chronchi_display_->showMusic(board->last_music_);
                    board->ble_music_flag_ = false;
                }
                if (board->ble_find_watch_flag_) {
                    board->chronchi_display_->showFindWatchAlert(board->ble_find_watch_active_);
                    board->ble_find_watch_flag_ = false;
                }
                if (board->ble_camera_flag_) {
                    board->chronchi_display_->showCameraPrompt(board->ble_camera_ready_);
                    board->ble_camera_flag_ = false;
                }
            },
            200, this);

        // LVGL timer for 3 buttons in Chronchi mode
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << (int)BOOT_BUTTON_GPIO);
        if constexpr ((int)VOLUME_UP_BUTTON_GPIO >= 0) {
            io_conf.pin_bit_mask |= (1ULL << (int)VOLUME_UP_BUTTON_GPIO);
        }
        if constexpr ((int)VOLUME_DOWN_BUTTON_GPIO >= 0) {
            io_conf.pin_bit_mask |= (1ULL << (int)VOLUME_DOWN_BUTTON_GPIO);
        }
        if constexpr ((int)WAKE_UP_BUTTON_GPIO >= 0) {
            if (WAKE_UP_BUTTON_GPIO != BOOT_BUTTON_GPIO) {
                io_conf.pin_bit_mask |= (1ULL << (int)WAKE_UP_BUTTON_GPIO);
            }
        }
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);

        struct SingleButtonTracker {
            int hold_count = 0;
            bool was_pressed = false;
            int click_count = 0;
            int release_wait = 0;
            bool long_press_fired = false;

            int Update(bool pressed, bool& out_long_press) {
                out_long_press = false;
                if (pressed) {
                    hold_count++;
                    was_pressed = true;
                    if (hold_count >= 30 && !long_press_fired) { // 30 * 50ms = 1.5s
                        long_press_fired = true;
                        out_long_press = true;
                    }
                } else {
                    if (was_pressed) {
                        if (long_press_fired) {
                            long_press_fired = false;
                            click_count = 0;
                        } else if (hold_count >= 1 && hold_count < 30) {
                            click_count++;
                            release_wait = 7; // wait 350ms (7 * 50ms)
                        }
                        hold_count = 0;
                        was_pressed = false;
                    } else if (click_count > 0) {
                        release_wait--;
                        if (release_wait <= 0) {
                            int final_clicks = click_count;
                            click_count = 0;
                            return final_clicks;
                        }
                    }
                }
                return 0;
            }
        };

        struct ChronchiMultiButtonState {
            SingleButtonTracker btn1; // BOOT (Tombol 1)
            SingleButtonTracker btn2; // Vol Up / Rotate (Tombol 2 - GPIO 14)
            SingleButtonTracker btn3; // Vol Down / Mode (Tombol 3 - GPIO 46)
            Esp32S3CamSt7789* board;
        };
        auto* btn_state = new ChronchiMultiButtonState();
        btn_state->board = this;

        lv_timer_create(
            [](lv_timer_t* t) {
                auto* state = static_cast<ChronchiMultiButtonState*>(lv_timer_get_user_data(t));

                // Tombol 1: BOOT
                bool btn1_pressed = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
                if constexpr ((int)WAKE_UP_BUTTON_GPIO >= 0) {
                    if (WAKE_UP_BUTTON_GPIO != BOOT_BUTTON_GPIO) {
                        btn1_pressed = btn1_pressed || (gpio_get_level(WAKE_UP_BUTTON_GPIO) == 0);
                    }
                }
                bool btn1_lp = false;
                int clicks1 = state->btn1.Update(btn1_pressed, btn1_lp);
                if ((clicks1 > 0 || btn1_lp) && ChronchiAlert::GetInstance().IsAlarmActive()) {
                    ESP_LOGI("Chronchi", "Alarm dimatikan via Tombol 1");
                    ChronchiAlert::GetInstance().StopAlert();
                    if (state->board->chronchi_display_) {
                        state->board->chronchi_display_->showHome();
                    }
                } else if (btn1_lp) {
                    ESP_LOGI("Chronchi", "Tombol 1 Long Press (1.5s): Finding Phone!");
                    if (state->board->chronchi_ble_) {
                        state->board->chronchi_ble_->findPhone(true);
                    }
                    if (state->board->chronchi_display_) {
                        ChronchiNotification fn;
                        fn.icon = 0xC0;
                        fn.app = "Find Phone";
                        fn.title = "Mencari Ponsel...";
                        fn.message = "Perintah cari ponsel telah dikirim via Bluetooth!";
                        time_t now = time(nullptr);
                        struct tm tm_info;
                        localtime_r(&now, &tm_info);
                        char tbuf[16];
                        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
                        fn.time = tbuf;
                        state->board->chronchi_display_->showNotification(fn);
                    }
                } else if (clicks1 >= 3) {
                    ESP_LOGI("Chronchi", "Tombol 1 (BOOT) 3x click: switching to XiaoZhi mode!");
                    state->board->SwitchToMode(DeviceMode::XIAOZHI);
                } else if (clicks1 == 2) {
                    ESP_LOGI("Chronchi", "Tombol 1 2x click: toggling dark/light color mode");
                    if (state->board->chronchi_display_) {
                        state->board->chronchi_display_->toggleColorMode();
                    }
                } else if (clicks1 == 1) {
                    ESP_LOGI("Chronchi", "Tombol 1 single click: cycling screen");
                    if (state->board->chronchi_display_) {
                        state->board->chronchi_display_->cycleScreen();
                    }
                }

                // Tombol 2: Vol Up (GPIO 14) -> Single Click: Next Music / Take Photo; Double Click: Rotate 180 deg; Long Press: Play/Pause Toggle
                if constexpr ((int)VOLUME_UP_BUTTON_GPIO >= 0) {
                    bool btn2_pressed = (gpio_get_level(VOLUME_UP_BUTTON_GPIO) == 0);
                    bool btn2_lp = false;
                    int clicks2 = state->btn2.Update(btn2_pressed, btn2_lp);
                    if ((clicks2 > 0 || btn2_lp) && ChronchiAlert::GetInstance().IsAlarmActive()) {
                        ESP_LOGI("Chronchi", "Alarm dimatikan via Tombol 2");
                        ChronchiAlert::GetInstance().StopAlert();
                        if (state->board->chronchi_display_) {
                            state->board->chronchi_display_->showHome();
                        }
                    } else if (btn2_lp && state->board->chronchi_ble_) {
                        ESP_LOGI("Chronchi", "Tombol 2 long press: Music Play/Pause toggle");
                        state->board->chronchi_ble_->musicControl(MUSIC_TOGGLE);
                    } else if (clicks2 >= 2) {
                        ESP_LOGI("Chronchi", "Tombol 2 double click: rotating display 180 deg");
                        state->board->RotateDisplay();
                    } else if (clicks2 == 1 && state->board->chronchi_ble_) {
                        if (state->board->chronchi_ble_->isCameraReady()) {
                            ESP_LOGI("Chronchi", "Tombol 2 single click: capture photo");
                            state->board->chronchi_ble_->capturePhoto();
                        } else {
                            ESP_LOGI("Chronchi", "Tombol 2 single click: music next");
                            state->board->chronchi_ble_->musicControl(MUSIC_NEXT);
                        }
                    }
                }

                // Tombol 3: Vol Down / Mode (GPIO 46) -> Single Click: Prev Music; Double Click: Switch to XiaoZhi; Long Press: Volume Mute/Down
                if constexpr ((int)VOLUME_DOWN_BUTTON_GPIO >= 0) {
                    bool btn3_pressed = (gpio_get_level(VOLUME_DOWN_BUTTON_GPIO) == 0);
                    bool btn3_lp = false;
                    int clicks3 = state->btn3.Update(btn3_pressed, btn3_lp);
                    if ((clicks3 > 0 || btn3_lp) && ChronchiAlert::GetInstance().IsAlarmActive()) {
                        ESP_LOGI("Chronchi", "Alarm dimatikan via Tombol 3");
                        ChronchiAlert::GetInstance().StopAlert();
                        if (state->board->chronchi_display_) {
                            state->board->chronchi_display_->showHome();
                        }
                    } else if (btn3_lp && state->board->chronchi_ble_) {
                        ESP_LOGI("Chronchi", "Tombol 3 long press: Music Volume Down / Mute");
                        state->board->chronchi_ble_->musicControl(VOLUME_DOWN);
                    } else if (clicks3 >= 2) {
                        ESP_LOGI("Chronchi", "Tombol 3 double click: switching to XiaoZhi mode");
                        state->board->SwitchToMode(DeviceMode::XIAOZHI);
                    } else if (clicks3 == 1 && state->board->chronchi_ble_) {
                        ESP_LOGI("Chronchi", "Tombol 3 single click: music previous");
                        state->board->chronchi_ble_->musicControl(MUSIC_PREVIOUS);
                    }
                }
            },
            50, btn_state);

        // LVGL timer for time update & alarm/health reminders check (runs in LVGL context)
        lv_timer_create(
            [](lv_timer_t* t) {
                auto* board = static_cast<Esp32S3CamSt7789*>(lv_timer_get_user_data(t));
                if (!board || !board->chronchi_display_) return;
                time_t now = time(NULL);
                struct tm* tm_info = localtime(&now);
                board->chronchi_display_->updateTime(tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

                // Check on the 0th second of each minute
                if (tm_info->tm_sec == 0 && board->chronchi_ble_) {
                    // 1. Alarms with repeat mask matching
                    for (uint8_t i = 0; i < 8; i++) {
                        ChronchiAlarm al = board->chronchi_ble_->getAlarm(i);
                        if (al.enabled && al.hour == tm_info->tm_hour && al.minute == tm_info->tm_min) {
                            bool repeat_match = false;
                            if (al.repeat == 0 || al.repeat == 0x80 || al.repeat == 0x7F) {
                                repeat_match = true;
                            } else {
                                int day = tm_info->tm_wday; // 0=Sun..6=Sat
                                if (day == 0) {
                                    repeat_match = (al.repeat & (1 << 6)) != 0;
                                } else {
                                    repeat_match = (al.repeat & (1 << (day - 1))) != 0;
                                }
                            }
                            if (repeat_match) {
                                ESP_LOGI("Chronchi", "Alarm fired! #%d %02d:%02d", i, al.hour, al.minute);
                                board->chronchi_display_->showAlarmAlert(al.hour, al.minute);
                                ChronchiAlert::GetInstance().StartFindDeviceAlarm();
                                break;
                            }
                        }
                    }

                    // 2. Sedentary Reminder
                    static int last_sedentary_min = -1;
                    ChronchiReminder sed = board->chronchi_ble_->getSedentaryReminder();
                    if (sed.enabled && sed.intervalMinutes > 0) {
                        int cur_min = tm_info->tm_hour * 60 + tm_info->tm_min;
                        int s_min = sed.startHour * 60 + sed.startMinute;
                        int e_min = sed.endHour * 60 + sed.endMinute;
                        bool in_window = (s_min <= e_min) ? (cur_min >= s_min && cur_min <= e_min) : (cur_min >= s_min || cur_min <= e_min);
                        if (in_window) {
                            if (last_sedentary_min < 0 || (cur_min - last_sedentary_min) >= sed.intervalMinutes) {
                                last_sedentary_min = cur_min;
                                ESP_LOGI("Chronchi", "Sedentary reminder fired!");
                                board->chronchi_display_->showReminderAlert("Waktunya Bergerak!", "Anda sudah cukup lama duduk.\nYuk berdiri dan regangkan tubuh!");
                                ChronchiAlert::GetInstance().VibrateNotification();
                            }
                        }
                    }

                    // 3. Water Hydration Reminder
                    static int last_water_min = -1;
                    ChronchiReminder wtr = board->chronchi_ble_->getWaterReminder();
                    if (wtr.enabled && wtr.intervalMinutes > 0) {
                        int cur_min = tm_info->tm_hour * 60 + tm_info->tm_min;
                        int s_min = wtr.startHour * 60 + wtr.startMinute;
                        int e_min = wtr.endHour * 60 + wtr.endMinute;
                        bool in_window = (s_min <= e_min) ? (cur_min >= s_min && cur_min <= e_min) : (cur_min >= s_min || cur_min <= e_min);
                        if (in_window) {
                            if (last_water_min < 0 || (cur_min - last_water_min) >= wtr.intervalMinutes) {
                                last_water_min = cur_min;
                                ESP_LOGI("Chronchi", "Water reminder fired!");
                                board->chronchi_display_->showReminderAlert("Waktunya Minum Air!", "Tetap terhidrasi sepanjang hari.\nMinum segelas air putih.");
                                ChronchiAlert::GetInstance().VibrateNotification();
                            }
                        }
                    }
                }
            },
            1000, this);

        // LVGL timer for battery update (runs in LVGL context)
        lv_timer_create(
            [](lv_timer_t* t) {
                auto* board = static_cast<Esp32S3CamSt7789*>(lv_timer_get_user_data(t));
                int level = 0;
                bool chg = false;
                bool dis = false;
                if (board->GetBatteryLevel(level, chg, dis)) {
                    if (board->chronchi_display_)
                        board->chronchi_display_->updateBattery(level, chg);
                    if (board->chronchi_ble_)
                        board->chronchi_ble_->setBattery(level, chg);
                }
            },
            30000, this);
    }

    void ChronchiLoop() {
        if (!chronchi_mode_ || !chronchi_ble_)
            return;

        chronchi_ble_->loop();

        // Mode switch handled by LVGL timer in StartChronchi()

        // Check navigation state
        static uint32_t last_crc = 0;
        ChronchiNavigation nav = chronchi_ble_->getNavigation();
        if (nav.active && nav.iconCRC != last_crc) {
            last_crc = nav.iconCRC;
            if (chronchi_display_ && nav.hasIcon)
                chronchi_display_->setNavigationIcon(nav.icon, 288);
        }
        if (!nav.active)
            last_crc = 0;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
};

DECLARE_BOARD(Esp32S3CamSt7789);
