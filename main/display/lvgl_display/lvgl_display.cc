#include <esp_err.h>
#include <esp_log.h>
#include <material_symbols.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "dynamic_glyph_cache.h"
#include "jpg/image_to_jpeg.h"
#include "lvgl_display.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

#define TAG "Display"

LvglDisplay::LvglDisplay() {
    dynamic_glyph_cache_ = std::make_unique<DynamicGlyphCache>();
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback =
            [](void* arg) {
                LvglDisplay* display = static_cast<LvglDisplay*>(arg);
                DisplayLockGuard lock(display);
                lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

bool LvglDisplay::SetTextFont(std::shared_ptr<LvglFont> text_font) {
    if (text_font == nullptr || text_font->font() == nullptr) {
        return false;
    }

    DisplayLockGuard lock(this);
    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");
    if (light_theme == nullptr && dark_theme == nullptr) {
        return false;
    }

    // LVGL styles keep raw lv_font_t pointers. Keep the previous font owners alive until the
    // current theme has rebound every style to the new font.
    auto previous_light_font = light_theme != nullptr ? light_theme->text_font() : nullptr;
    auto previous_dark_font = dark_theme != nullptr ? dark_theme->text_font() : nullptr;
    if (light_theme != nullptr) {
        light_theme->set_text_font(text_font);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_text_font(text_font);
    }
    if (current_theme_ != nullptr) {
        SetTheme(current_theme_);
    }
    previous_light_font.reset();
    previous_dark_font.reset();
    return true;
}

bool LvglDisplay::AddTextGlyphs(const std::vector<TextGlyph>& glyphs, uint8_t bpp) {
    if (dynamic_glyph_cache_ == nullptr) {
        return false;
    }
    if (glyphs.empty()) {
        if (!TextGlyphStorageUsesPsram()) {
            ClearTextGlyphs();
        }
        return false;
    }
    if (bpp != 1 && bpp != 4) {
        return false;
    }

    DisplayLockGuard lock(this);
    auto theme = dynamic_cast<LvglTheme*>(current_theme_);
    if (theme == nullptr || theme->text_font() == nullptr) {
        return false;
    }

    auto fallback = dynamic_glyph_cache_->EnsureFont(theme->text_font()->font(), bpp);
    if (fallback == nullptr) {
        return false;
    }
    theme->text_font()->SetFallback(fallback);
    return dynamic_glyph_cache_->AddGlyphs(glyphs);
}

void LvglDisplay::ClearTextGlyphs() {
    if (dynamic_glyph_cache_ == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    dynamic_glyph_cache_->Clear();
}

LvglDisplay::~LvglDisplay() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
    }
    if (notification_label_ != nullptr) {
        lv_obj_del(notification_label_);
    }
    if (status_label_ != nullptr) {
        lv_obj_del(status_label_);
    }
    if (mute_label_ != nullptr) {
        lv_obj_del(mute_label_);
    }
    if (battery_label_ != nullptr) {
        lv_obj_del(battery_label_);
    }
    if (battery_pct_label_ != nullptr) {
        lv_obj_del(battery_pct_label_);
    }
    if (bluetooth_label_ != nullptr) {
        lv_obj_del(bluetooth_label_);
    }
    if (low_battery_popup_ != nullptr) {
        lv_obj_del(low_battery_popup_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void LvglDisplay::SetStatus(const char* status) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetStatus('%s') called before SetupUI() - message will be lost!", status);
    }
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetStatus('%s') failed: status_label_ is nullptr (SetupUI() was called but "
                     "label not created)",
                     status);
        }
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    last_status_update_time_ = std::chrono::system_clock::now();
}

void LvglDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void LvglDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "ShowNotification('%s') called before SetupUI() - message will be lost!",
                 notification);
    }
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "ShowNotification('%s') failed: notification_label_ is nullptr (SetupUI() was "
                     "called but label not created)",
                     notification);
        }
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void LvglDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    // 1. Bluetooth BLE status icon
    if (bluetooth_label_ != nullptr) {
        DisplayLockGuard lock(this);
        bool ble_conn = board.GetBleConnected();
        lv_label_set_text(bluetooth_label_, MATERIAL_SYMBOLS_BLUETOOTH);
        lv_obj_set_style_text_color(bluetooth_label_, ble_conn ? lv_color_hex(0x38BDF8) : lv_color_hex(0x64748B), 0);
    }

    // 2. Mute / Speaker icon based on volume level
    if (mute_label_ != nullptr) {
        DisplayLockGuard lock(this);
        int vol = codec ? codec->output_volume() : 100;
        if (vol == 0) {
            muted_ = true;
            lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_OFF);
            lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x94A3B8), 0);
        } else {
            muted_ = false;
            lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_UP);
            lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x06B6D4), 0);
        }
    }

    // 3. Wi-Fi network icon & reconnecting status animation
    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    if (network_label_ != nullptr) {
        DisplayLockGuard lock(this);
        static uint8_t wifi_blink = 0;
        wifi_blink++;
        if (wifi_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                int8_t rssi = ap_info.rssi;
                if (rssi >= -65) {
                    // Strong signal (3 bars): Green
                    lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI);
                    lv_obj_set_style_text_color(network_label_, lv_color_hex(0x22C55E), 0);
                } else if (rssi >= -78) {
                    // Medium signal (2 bars): Amber/Yellow
                    lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI_2_BAR);
                    lv_obj_set_style_text_color(network_label_, lv_color_hex(0xEAB308), 0);
                } else {
                    // Weak signal (1 bar): Red/Coral
                    lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI_1_BAR);
                    lv_obj_set_style_text_color(network_label_, lv_color_hex(0xEF4444), 0);
                }
            } else {
                lv_label_set_text(network_label_, MATERIAL_SYMBOLS_WIFI);
                lv_obj_set_style_text_color(network_label_, lv_color_hex(0x22C55E), 0);
            }
        } else {
            // Reconnecting amber alternating animation
            lv_label_set_text(network_label_, (wifi_blink % 2 == 0) ? MATERIAL_SYMBOLS_REFRESH : MATERIAL_SYMBOLS_WIFI_OFF);
            lv_obj_set_style_text_color(network_label_, lv_color_hex(0xF59E0B), 0);
        }
    }

    // 4. Update Time & Auto-Poke SNTP when Wi-Fi is connected
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    if (tm->tm_year < 125 && wifi_connected) {
        // NTP not synced yet: Poke SNTP to sync immediately!
        static uint32_t last_sntp_poke = 0;
        uint32_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_sntp_poke >= 2000) {
            last_sntp_poke = now_ms;
            if (!esp_sntp_enabled()) {
                esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
                esp_sntp_setservername(0, "id.pool.ntp.org");
                esp_sntp_setservername(1, "time.google.com");
                esp_sntp_setservername(2, "time.cloudflare.com");
                esp_sntp_setservername(3, "pool.ntp.org");
                esp_sntp_init();
            } else {
                esp_sntp_restart();
            }
            ESP_LOGI(TAG, "XiaoZhi: SNTP poke for instant time sync...");
        }
    }

    if (app.GetDeviceState() == kDeviceStateIdle) {
        if (tm->tm_year >= 125) {
            Settings nova_prefs("nova", true);
            bool is_24h = nova_prefs.GetBool("24hour", true);
            char time_str[32];
            if (is_24h) {
                snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
            } else {
                int h = tm->tm_hour;
                const char* ampm = (h >= 12) ? "PM" : "AM";
                h = (h == 0) ? 12 : (h > 12) ? h - 12 : h;
                snprintf(time_str, sizeof(time_str), "%d:%02d %s", h, tm->tm_min, ampm);
            }
            SetStatus(time_str);
        } else {
            SetStatus(Lang::Strings::STANDBY);
        }
    }

    // 5. Update Battery Icon & Percentage according to bat_style
    esp_pm_lock_acquire(pm_lock_);
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT;
        } else {
            const char* levels[] = {
                MATERIAL_SYMBOLS_BATTERY_ANDROID_0,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6,
                MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
            };
            int level_index = battery_level <= 0
                                  ? 0
                                  : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
            icon = levels[level_index];
        }

        Settings prefs("nova", true);
        int bat_style = prefs.GetInt("bat_style", 1); // 0: icon only, 1: icon + %, 2: % only

        bool is_light_bg = false;
        auto theme = dynamic_cast<LvglTheme*>(current_theme_);
        if (theme != nullptr) {
            uint32_t rgb = lv_color_to_int(theme->background_color());
            uint8_t r = (rgb >> 16) & 0xFF;
            uint8_t g = (rgb >> 8) & 0xFF;
            uint8_t b = rgb & 0xFF;
            int lum = (int)(0.299f * r + 0.587f * g + 0.114f * b);
            if (lum > 140) {
                is_light_bg = true;
            }
        }

        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr) {
            if (bat_style == 2) {
                lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
                battery_icon_ = icon;
                lv_label_set_text(battery_label_, battery_icon_);
                if (charging) {
                    lv_obj_set_style_text_color(battery_label_, is_light_bg ? lv_color_hex(0xD97706) : lv_color_hex(0xF59E0B), 0);
                } else if (battery_level <= 20) {
                    lv_obj_set_style_text_color(battery_label_, is_light_bg ? lv_color_hex(0xDC2626) : lv_color_hex(0xFF8195), 0);
                } else {
                    lv_obj_set_style_text_color(battery_label_, is_light_bg ? lv_color_hex(0x475569) : lv_color_hex(0x7DE0B5), 0);
                }
            }
        }
        if (battery_pct_label_ != nullptr) {
            if (bat_style == 0) {
                lv_obj_add_flag(battery_pct_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(battery_pct_label_, LV_OBJ_FLAG_HIDDEN);
                char pct_str[16];
                snprintf(pct_str, sizeof(pct_str), "%d%%", battery_level);
                lv_label_set_text(battery_pct_label_, pct_str);
                lv_obj_set_style_text_color(battery_pct_label_, is_light_bg ? lv_color_hex(0x475569) : lv_color_white(), 0);
            }
        }

        // Low battery popup disabled
        if (low_battery_popup_ != nullptr && !update_all) {
            if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    esp_pm_lock_release(pm_lock_);
}

void LvglDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {}

void LvglDisplay::SetPowerSaveMode(bool on) {
    if (on) {
        SetChatMessage("system", "");
        SetEmotion("sleepy");
    } else {
        SetChatMessage("system", "");
        SetEmotion("neutral");
    }
}

bool LvglDisplay::SnapshotToJpeg(std::string& jpeg_data, int quality) {
#if CONFIG_LV_USE_SNAPSHOT
    DisplayLockGuard lock(this);

    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* draw_buffer = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    if (draw_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to take snapshot, draw_buffer is nullptr");
        return false;
    }

    // swap bytes
    uint16_t* data = (uint16_t*)draw_buffer->data;
    size_t pixel_count = draw_buffer->data_size / 2;
    for (size_t i = 0; i < pixel_count; i++) {
        data[i] = __builtin_bswap16(data[i]);
    }

    // Clear output string and use callback version to avoid pre-allocating large memory blocks
    jpeg_data.clear();

    // Use callback-based JPEG encoder to further save memory
    bool ret =
        image_to_jpeg_cb((uint8_t*)draw_buffer->data, draw_buffer->data_size, draw_buffer->header.w,
                         draw_buffer->header.h, V4L2_PIX_FMT_RGB565, quality,
                         [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                             std::string* output = static_cast<std::string*>(arg);
                             if (data && len > 0) {
                                 output->append(static_cast<const char*>(data), len);
                             }
                             return len;
                         },
                         &jpeg_data);
    if (!ret) {
        ESP_LOGE(TAG, "Failed to convert image to JPEG");
    }

    lv_draw_buf_destroy(draw_buffer);
    return ret;
#else
    ESP_LOGE(TAG, "LV_USE_SNAPSHOT is not enabled");
    return false;
#endif
}
