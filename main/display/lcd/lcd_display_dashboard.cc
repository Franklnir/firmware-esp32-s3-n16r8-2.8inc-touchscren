#include "lcd_display_internal.h"

void LcdDisplay::CreateIdleDashboard() {
    auto screen = lv_screen_active();
    (void)screen;
    // Disabled as requested: XiaoZhi maintains clean standard idle view with centered emote
    idle_dashboard_ = nullptr;
    idle_dashboard_visible_ = false;
    return;

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    /* Main Idle Dashboard Container */
    idle_dashboard_ = lv_obj_create(screen);
    lv_obj_set_size(idle_dashboard_, LV_HOR_RES, LV_VER_RES - 34 - 36);
    lv_obj_set_pos(idle_dashboard_, 0, 34);
    lv_obj_set_style_bg_opa(idle_dashboard_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_dashboard_, 0, 0);
    lv_obj_set_style_radius(idle_dashboard_, 0, 0);
    lv_obj_set_style_pad_all(idle_dashboard_, 0, 0);
    lv_obj_set_style_pad_top(idle_dashboard_, 4, 0);
    lv_obj_set_flex_flow(idle_dashboard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(idle_dashboard_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(idle_dashboard_, 6, 0);
    lv_obj_clear_flag(idle_dashboard_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Hero Digital Clock Card (Futuristic Obsidian / Cyan Glass)
    lv_obj_t* time_box = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(time_box, 216, 68);
    lv_obj_set_style_radius(time_box, 14, 0);
    lv_obj_set_style_bg_color(time_box, lv_color_hex(0x090D16), 0);
    lv_obj_set_style_bg_opa(time_box, LV_OPA_90, 0);
    lv_obj_set_style_border_width(time_box, 1, 0);
    lv_obj_set_style_border_color(time_box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_pad_all(time_box, 0, 0);
    lv_obj_set_flex_flow(time_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(time_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(time_box, 0, 0);
    lv_obj_clear_flag(time_box, LV_OBJ_FLAG_SCROLLABLE);

    idle_time_label_ = lv_label_create(time_box);
    lv_obj_set_style_text_font(idle_time_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idle_time_label_, lv_color_hex(0x38BDF8),
                                0);  // Vibrant Electric Sky
    lv_label_set_text(idle_time_label_, "00:00");

    idle_sec_label_ = lv_label_create(time_box);
    lv_obj_set_style_text_font(idle_sec_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(idle_sec_label_, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(idle_sec_label_, "· 00 detik ·");

    // 2. Day & Date Glassmorphic Card (Hari & Tanggal)
    idle_date_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_date_card_, 216, 34);
    lv_obj_set_style_radius(idle_date_card_, 10, 0);
    lv_obj_set_style_bg_color(idle_date_card_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(idle_date_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_date_card_, 1, 0);
    lv_obj_set_style_border_color(idle_date_card_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(idle_date_card_, 0, 0);
    lv_obj_set_flex_flow(idle_date_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_date_card_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(idle_date_card_, 6, 0);
    lv_obj_clear_flag(idle_date_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cal_icon = lv_label_create(idle_date_card_);
    lv_obj_set_style_text_font(cal_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(cal_icon, MATERIAL_SYMBOLS_CALENDAR_MONTH);
    lv_obj_set_style_text_color(cal_icon, lv_color_hex(0x38BDF8), 0);

    idle_date_label_ = lv_label_create(idle_date_card_);
    lv_obj_set_style_text_font(idle_date_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_date_label_, lv_color_hex(0xF1F5F9), 0);
    lv_label_set_text(idle_date_label_, "SABTU, 10 OKT 2026");

    // 3. Modern Weather & Climate Card (Suhu & Cuaca)
    idle_weather_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_weather_card_, 216, 66);
    lv_obj_set_style_radius(idle_weather_card_, 12, 0);
    lv_obj_set_style_bg_color(idle_weather_card_, lv_color_hex(0x0C192C), 0);
    lv_obj_set_style_bg_opa(idle_weather_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_weather_card_, 1, 0);
    lv_obj_set_style_border_color(idle_weather_card_, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_pad_hor(idle_weather_card_, 10, 0);
    lv_obj_set_style_pad_ver(idle_weather_card_, 4, 0);
    lv_obj_set_flex_flow(idle_weather_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_weather_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(idle_weather_card_, LV_OBJ_FLAG_SCROLLABLE);

    // Left: Thermostat Icon & Temperature
    lv_obj_t* w_left = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_left, 86, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_left, 0, 0);
    lv_obj_set_style_pad_all(w_left, 0, 0);
    lv_obj_set_flex_flow(w_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w_left, 4, 0);
    lv_obj_clear_flag(w_left, LV_OBJ_FLAG_SCROLLABLE);

    idle_weather_icon_ = lv_label_create(w_left);
    lv_obj_set_style_text_font(idle_weather_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0x38BDF8), 0);
    lv_label_set_text(idle_weather_icon_, MATERIAL_SYMBOLS_DEVICE_THERMOSTAT);

    idle_temp_label_ = lv_label_create(w_left);
    lv_obj_set_style_text_font(idle_temp_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idle_temp_label_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(idle_temp_label_, "28°C");

    // Divider Line
    lv_obj_t* w_div = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_div, 1, 38);
    lv_obj_set_style_bg_color(w_div, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(w_div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w_div, 0, 0);
    lv_obj_set_style_pad_all(w_div, 0, 0);
    lv_obj_clear_flag(w_div, LV_OBJ_FLAG_SCROLLABLE);

    // Right: Condition & Location (with Marquee scrolling)
    lv_obj_t* w_right = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_right, 102, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_right, 0, 0);
    lv_obj_set_style_pad_all(w_right, 0, 0);
    lv_obj_set_flex_flow(w_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w_right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(w_right, 2, 0);
    lv_obj_clear_flag(w_right, LV_OBJ_FLAG_SCROLLABLE);

    idle_cond_label_ = lv_label_create(w_right);
    lv_obj_set_style_text_font(idle_cond_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_cond_label_, lv_color_hex(0xFDE047), 0);  // Warm Sun Yellow
    lv_obj_set_width(idle_cond_label_, 100);
    lv_label_set_long_mode(idle_cond_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(idle_cond_label_, "Cerah Berawan");

    idle_loc_label_ = lv_label_create(w_right);
    lv_obj_set_style_text_font(idle_loc_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_loc_label_, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_width(idle_loc_label_, 100);
    lv_label_set_long_mode(idle_loc_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(idle_loc_label_, "Jakarta • 72% RH");

    // 4. Quick Action Mic Pill (Tap to talk! with Marquee running text)
    idle_hint_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_hint_card_, 216, 36);
    lv_obj_set_style_radius(idle_hint_card_, 18, 0);
    lv_obj_set_style_bg_color(idle_hint_card_, lv_color_hex(0x1E1B4B), 0);
    lv_obj_set_style_bg_opa(idle_hint_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_hint_card_, 1, 0);
    lv_obj_set_style_border_color(idle_hint_card_, lv_color_hex(0x4F46E5), 0);
    lv_obj_set_style_pad_hor(idle_hint_card_, 10, 0);
    lv_obj_set_style_pad_ver(idle_hint_card_, 0, 0);
    lv_obj_set_flex_flow(idle_hint_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_hint_card_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(idle_hint_card_, 6, 0);
    lv_obj_clear_flag(idle_hint_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* mic_icon = lv_label_create(idle_hint_card_);
    lv_obj_set_style_text_font(mic_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(mic_icon, MATERIAL_SYMBOLS_MIC);
    lv_obj_set_style_text_color(mic_icon, lv_color_hex(0xA5B4FC), 0);

    lv_obj_t* hint_text = lv_label_create(idle_hint_card_);
    lv_obj_set_style_text_font(hint_text, text_font, 0);
    lv_obj_set_style_text_color(hint_text, lv_color_hex(0xE0E7FF), 0);
    lv_obj_set_width(hint_text, 172);
    lv_label_set_long_mode(hint_text, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(hint_text,
                      "Ketuk 2x Layar Untuk Bicara   •   Sentuh Disini Untuk Bicara   •   ");

    lv_obj_add_flag(idle_hint_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        idle_hint_card_,
        [](lv_event_t* e) {
            (void)e;
            Application::GetInstance().Schedule(
                []() { Application::GetInstance().ToggleChatState(); });
        },
        LV_EVENT_CLICKED, nullptr);

    // 5. Subtle Footer
    idle_footer_label_ = lv_label_create(idle_dashboard_);
    lv_obj_set_style_text_font(idle_footer_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_footer_label_, lv_color_hex(0x64748B), 0);
    lv_label_set_text(idle_footer_label_, "• XiaoZhi AI  ·  Standby •");

    if (ui_theme_id_ != 2) {
        lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        idle_dashboard_visible_ = false;
    } else {
        idle_dashboard_visible_ = true;
    }
    UpdateIdleDashboard();
}

void LcdDisplay::UpdateIdleDashboard() {
    DisplayLockGuard lock(this);
    if (!idle_dashboard_)
        return;

    if (ui_theme_id_ != 2) {
        // Theme 1: Emote robot tetap di tengah layar seperti biasa
        if (idle_dashboard_visible_) {
            idle_dashboard_visible_ = false;
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ && lv_obj_has_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN) &&
            !music_visualizer_active_ &&
            (preview_image_ == nullptr || lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN))) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    bool should_show_idle =
        (state == kDeviceStateIdle) && !music_visualizer_active_ &&
        (preview_image_ == nullptr || lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN));

    if (should_show_idle) {
        if (!idle_dashboard_visible_) {
            idle_dashboard_visible_ = true;
            lv_obj_remove_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_)
                lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (content_)
                lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
            if (ui_theme_id_ != 2 && bottom_bar_)
                lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }

        // Update clock and date every second
        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm* tm = localtime_r(&now, &tm_buf);
        if (!tm)
            return;

        // Auto-poke SNTP if Wi-Fi connected and time not synced yet!
        esp_netif_ip_info_t ip_info;
        esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        bool wifi_conn = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK &&
                          ip_info.ip.addr != 0);
        if (tm->tm_year < 125 && wifi_conn) {
            static uint32_t last_idle_sntp = 0;
            uint32_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_idle_sntp >= 4000) {
                last_idle_sntp = now_ms;
                if (!esp_sntp_enabled()) {
                    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    esp_sntp_setservername(0, "id.pool.ntp.org");
                    esp_sntp_setservername(1, "pool.ntp.org");
                    esp_sntp_setservername(2, "time.google.com");
                    esp_sntp_init();
                } else {
                    esp_sntp_restart();
                }
            }
        }

        Settings nova_prefs("nova", true);
        bool is_24h = nova_prefs.GetBool("24hour", true);

        char time_str[32];
        if (tm->tm_year >= 125) {
            if (is_24h) {
                snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
            } else {
                int h = tm->tm_hour;
                h = (h == 0) ? 12 : (h > 12) ? h - 12 : h;
                snprintf(time_str, sizeof(time_str), "%d:%02d", h, tm->tm_min);
            }
        } else {
            snprintf(time_str, sizeof(time_str), "--:--");
        }
        if (idle_time_label_) {
            lv_label_set_text(idle_time_label_, time_str);
        }

        if (idle_sec_label_) {
            char sec_str[32];
            if (tm->tm_year >= 125) {
                if (!is_24h) {
                    const char* ampm = (tm->tm_hour >= 12) ? "PM" : "AM";
                    snprintf(sec_str, sizeof(sec_str), "· %02d detik (%s) ·", tm->tm_sec, ampm);
                } else {
                    snprintf(sec_str, sizeof(sec_str), "· %02d detik ·", tm->tm_sec);
                }
            } else {
                snprintf(sec_str, sizeof(sec_str),
                         wifi_conn ? "· Sinkronisasi Waktu... ·" : "· Hubungkan Wi-Fi ·");
            }
            lv_label_set_text(idle_sec_label_, sec_str);
        }

        if (idle_date_label_) {
            static const char* kDays[] = {"MINGGU", "SENIN", "SELASA", "RABU",
                                          "KAMIS",  "JUMAT", "SABTU"};
            static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
                                            "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};
            char date_str[64];
            if (tm->tm_year >= 125) {
                int wday = std::clamp(tm->tm_wday, 0, 6);
                int mon = std::clamp(tm->tm_mon, 0, 11);
                snprintf(date_str, sizeof(date_str), "%s, %d %s %d", kDays[wday], tm->tm_mday,
                         kMonths[mon], tm->tm_year + 1900);
            } else {
                snprintf(date_str, sizeof(date_str),
                         wifi_conn ? "Menyinkronkan Tanggal..." : "Waktu Belum Sinkron");
            }
            lv_label_set_text(idle_date_label_, date_str);
        }

        // Update Weather & Temperature
        Settings weather_settings("weather", true);
        int temp = weather_settings.GetInt("temp", 28);
        std::string cond = weather_settings.GetString("condition", "Cerah Berawan");
        std::string city = weather_settings.GetString("city", "Jakarta");
        int humid = weather_settings.GetInt("humidity", 72);

        if (idle_temp_label_) {
            char temp_str[16];
            snprintf(temp_str, sizeof(temp_str), "%d°C", temp);
            lv_label_set_text(idle_temp_label_, temp_str);
        }

        if (idle_weather_icon_) {
            lv_label_set_text(idle_weather_icon_, MATERIAL_SYMBOLS_DEVICE_THERMOSTAT);
            if (temp >= 33) {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0xEF4444),
                                            0);  // Hot Red
            } else if (temp <= 22) {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0x38BDF8),
                                            0);  // Cool Cyan
            } else {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0xF59E0B),
                                            0);  // Warm Amber
            }
        }

        if (idle_cond_label_) {
            lv_label_set_text(idle_cond_label_, cond.c_str());
        }

        if (idle_loc_label_) {
            char loc_str[48];
            snprintf(loc_str, sizeof(loc_str), "%s • %d%% RH", city.c_str(), humid);
            lv_label_set_text(idle_loc_label_, loc_str);
        }
    } else {
        if (idle_dashboard_visible_) {
            idle_dashboard_visible_ = false;
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_)
                lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (content_)
                lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
            if (ui_theme_id_ != 2 && bottom_bar_)
                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ── Touchscreen Wi-Fi Scanner & Virtual Keyboard ──
