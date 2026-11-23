#include "chronchi_display_internal.h"

// ---- Screen management ----
void ChronchiDisplay::hideAllScreens() {
    if (home_screen_ && lv_screen_active() == home_screen_) lv_screen_load(lv_obj_get_screen(home_screen_));
}

void ChronchiDisplay::showScreen(lv_obj_t* screen) {
    if (screen) lv_screen_load(screen);
}

void ChronchiDisplay::setScreenState(ScreenState s) {
    prev_state_ = state_;
    state_ = s;
    switch (s) {
        case HOME: showScreen(home_screen_); break;
        case NAVIGATION: showScreen(nav_screen_); break;
        case NOTIFICATION: showScreen(notif_screen_); break;
        case WEATHER: showScreen(weather_screen_); break;
        case MUSIC: showScreen(music_screen_); break;
        case STATUS: showScreen(status_screen_); break;
        case ALERT: showScreen(alert_screen_); break;
        case QR_PAIRING: showScreen(qr_screen_); break;
    }
}

void ChronchiDisplay::showHome() {
    lv_lock();
    if (home_screen_) {
        lv_screen_load(home_screen_);
        ESP_LOGI(TAG, "Loaded Chronchi home watchface (Theme: %s)", PRESET_THEMES[current_layout_ % NUM_THEMES].name);
    }
    state_ = HOME;
    lv_unlock();
}

void ChronchiDisplay::showNavigation(const ChronchiNavigation& nav) {
    lv_lock();
    google_maps_active_ = nav.active;

    if (!nav.active) {
        if (state_ == NAVIGATION) {
            showHome();
        }
        if (home_status_label_ && lv_obj_is_valid(home_status_label_)) {
            lv_label_set_text(home_status_label_, "⚡ Chronos Smartwatch");
        }
        lv_unlock();
        return;
    }

    if (nav_title_label_) {
        if (!nav.title.empty()) {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), "📍 %s", nav.title.c_str());
            lv_label_set_text(nav_title_label_, tbuf);
        } else {
            lv_label_set_text(nav_title_label_, "📍 Google Maps");
        }
    }
    if (nav_street_label_) {
        lv_label_set_text(nav_street_label_, nav.directions.empty() ? "Ikuti rute jalan..." : nav.directions.c_str());
    }
    if (nav_dist_label_) {
        lv_label_set_text(nav_dist_label_, nav.distance.empty() ? "-- m" : nav.distance.c_str());
    }
    if (nav_dur_label_) {
        char buf[32];
        if (!nav.duration.empty()) {
            snprintf(buf, sizeof(buf), "⏱ %s", nav.duration.c_str());
            lv_label_set_text(nav_dur_label_, buf);
        } else {
            lv_label_set_text(nav_dur_label_, "⏱ -- min");
        }
    }
    if (nav_eta_label_) {
        char buf[32];
        if (!nav.eta.empty()) {
            if (strstr(nav.eta.c_str(), "ETA") || strstr(nav.eta.c_str(), "eta")) {
                snprintf(buf, sizeof(buf), "🏁 %s", nav.eta.c_str());
            } else {
                snprintf(buf, sizeof(buf), "🏁 ETA %s", nav.eta.c_str());
            }
            lv_label_set_text(nav_eta_label_, buf);
        } else {
            lv_label_set_text(nav_eta_label_, "🏁 ETA --:--");
        }
    }
    if (nav_speed_label_) {
        char buf[32];
        if (!nav.speed.empty()) {
            if (strstr(nav.speed.c_str(), "km") || strstr(nav.speed.c_str(), "m/h")) {
                snprintf(buf, sizeof(buf), "⚡ %s", nav.speed.c_str());
            } else {
                snprintf(buf, sizeof(buf), "⚡ %s km/h", nav.speed.c_str());
            }
            lv_label_set_text(nav_speed_label_, buf);
        } else {
            lv_label_set_text(nav_speed_label_, "⚡ -- km/h");
        }
    }

    // Accurate Turn Icon: Use 1bpp BLE icon if provided, otherwise parse maneuver/directions
    if (nav.hasIcon) {
        setNavigationIcon(const_cast<uint8_t*>(nav.icon), 288);
    } else if (!nav.maneuver.empty()) {
        setNavIcon(nav.maneuver.c_str());
    } else if (!nav.directions.empty()) {
        std::string d = nav.directions;
        for (auto& c : d) c = (char)tolower((unsigned char)c);
        if (d.find("slight left") != std::string::npos || d.find("serong kiri") != std::string::npos || d.find("agak ke kiri") != std::string::npos) {
            setNavIcon("SLIGHT_LEFT");
        } else if (d.find("slight right") != std::string::npos || d.find("serong kanan") != std::string::npos || d.find("agak ke kanan") != std::string::npos) {
            setNavIcon("SLIGHT_RIGHT");
        } else if (d.find("putar balik") != std::string::npos || d.find("u-turn") != std::string::npos) {
            setNavIcon("UTURN");
        } else if (d.find("kiri") != std::string::npos || d.find("left") != std::string::npos) {
            setNavIcon("LEFT");
        } else if (d.find("kanan") != std::string::npos || d.find("right") != std::string::npos) {
            setNavIcon("RIGHT");
        } else if (d.find("bundaran") != std::string::npos || d.find("roundabout") != std::string::npos) {
            setNavIcon("ROUNDABOUT");
        } else if (d.find("sampai") != std::string::npos || d.find("arrive") != std::string::npos || d.find("tujuan") != std::string::npos || d.find("tiba") != std::string::npos) {
            setNavIcon("ARRIVE");
        } else {
            setNavIcon("STRAIGHT");
        }
    } else {
        setNavIcon("STRAIGHT");
    }

    // Keep home status glance updated in real time as well
    if (home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        char gbuf[64];
        if (!nav.distance.empty()) {
            snprintf(gbuf, sizeof(gbuf), "🧭 %s - %s", nav.distance.c_str(), nav.directions.empty() ? "Lurus" : nav.directions.c_str());
        } else {
            snprintf(gbuf, sizeof(gbuf), "🧭 %s", nav.directions.empty() ? "Google Maps Aktif" : nav.directions.c_str());
        }
        lv_label_set_text(home_status_label_, gbuf);
    }

    if (state_ != NAVIGATION) setScreenState(NAVIGATION);
    lv_unlock();
}

void ChronchiDisplay::showNotification(const ChronchiNotification& notif) {
    lv_lock();

    // Check if this notification is from Google Maps or Navigation
    std::string app_low = notif.app;
    for (auto& c : app_low) c = (char)tolower((unsigned char)c);
    std::string title_low = notif.title;
    for (auto& c : title_low) c = (char)tolower((unsigned char)c);
    std::string msg_low = notif.message;
    for (auto& c : msg_low) c = (char)tolower((unsigned char)c);

    bool is_maps = (app_low.find("map") != std::string::npos ||
                    app_low.find("navigasi") != std::string::npos ||
                    title_low.find("google maps") != std::string::npos ||
                    title_low.find("maps") != std::string::npos ||
                    title_low.find("navigasi") != std::string::npos);

    // If it's a Google Maps navigation notification, route to showNavigation for persistent real-time view!
    if (is_maps) {
        lv_unlock();
        ChronchiNavigation nav;
        nav.active = true;
        nav.title = "Google Maps";
        if (title_low.find(" m") != std::string::npos || title_low.find(" km") != std::string::npos) {
            nav.distance = notif.title;
            nav.directions = notif.message;
        } else if (msg_low.find(" m") != std::string::npos || msg_low.find(" km") != std::string::npos) {
            nav.distance = notif.message;
            nav.directions = notif.title;
        } else {
            nav.title = notif.title.empty() ? "Google Maps" : notif.title;
            nav.directions = notif.message;
        }
        showNavigation(nav);
        return;
    }

    // USER REQUIREMENT: Jika Google Maps sedang aktif atau berada di layar Navigasi, jangan tampilkan notifikasi popup dulu
    if (google_maps_active_ || state_ == NAVIGATION) {
        ESP_LOGI("Chronchi", "Google Maps aktif: notifikasi ditahan/diabaikan agar rute navigasi tidak terganggu");
        lv_unlock();
        return;
    }

    if (notif_app_label_) {
        char abuf[64];
        snprintf(abuf, sizeof(abuf), "💬 %s", notif.app.empty() ? "NOTIFIKASI" : notif.app.c_str());
        lv_label_set_text(notif_app_label_, abuf);
    }
    if (notif_time_label_) lv_label_set_text(notif_time_label_, notif.time.empty() ? "Baru saja" : notif.time.c_str());
    if (notif_title_label_) lv_label_set_text(notif_title_label_, notif.title.c_str());
    if (notif_msg_label_) lv_label_set_text(notif_msg_label_, notif.message.c_str());

    // App-specific header color badge (WhatsApp, Telegram, Instagram, Spotify, etc.)
    if (notif_header_badge_) {
        lv_color_t badge_bg = lv_color_hex(0x1E293B);
        lv_color_t badge_border = lv_color_hex(0x3B82F6);
        lv_color_t badge_text = lv_color_hex(0x60A5FA);

        if (strstr(notif.app.c_str(), "WhatsApp") || strstr(notif.app.c_str(), "WA")) {
            badge_bg = lv_color_hex(0x064E3B);
            badge_border = lv_color_hex(0x10B981);
            badge_text = lv_color_hex(0x34D399);
        } else if (strstr(notif.app.c_str(), "Telegram")) {
            badge_bg = lv_color_hex(0x0C4A6E);
            badge_border = lv_color_hex(0x0284C7);
            badge_text = lv_color_hex(0x38BDF8);
        } else if (strstr(notif.app.c_str(), "Instagram")) {
            badge_bg = lv_color_hex(0x4A044E);
            badge_border = lv_color_hex(0xC026D3);
            badge_text = lv_color_hex(0xF472B6);
        } else if (strstr(notif.app.c_str(), "Spotify")) {
            badge_bg = lv_color_hex(0x052E16);
            badge_border = lv_color_hex(0x15803D);
            badge_text = lv_color_hex(0x22C55E);
        } else if (strstr(notif.app.c_str(), "YouTube")) {
            badge_bg = lv_color_hex(0x450A0A);
            badge_border = lv_color_hex(0xDC2626);
            badge_text = lv_color_hex(0xF87171);
        }

        lv_obj_set_style_bg_color(notif_header_badge_, badge_bg, 0);
        lv_obj_set_style_border_color(notif_header_badge_, badge_border, 0);
        if (notif_app_label_) lv_obj_set_style_text_color(notif_app_label_, badge_text, 0);
    }

    prev_state_ = state_;
    setScreenState(NOTIFICATION);

    // Auto-dismiss (4 detik jika navigasi Maps aktif agar cepat kembali ke peta, atau 6 detik jika di home)
    if (notif_timer_) {
        lv_timer_delete(notif_timer_);
        notif_timer_ = nullptr;
    }
    uint32_t dismiss_ms = google_maps_active_ ? 4000 : 6000;
    notif_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_timer_get_user_data(t));
        if (disp) {
            disp->notif_timer_ = nullptr;
            if (disp->state_ == NOTIFICATION) {
                if (disp->google_maps_active_) {
                    disp->setScreenState(NAVIGATION);
                } else {
                    disp->showHome();
                }
            }
        }
        lv_timer_delete(t);
    }, dismiss_ms, this);
    lv_timer_set_repeat_count(notif_timer_, 1);
    lv_unlock();
}

void ChronchiDisplay::set24HourFormat(bool is_24h) {
    is_24h_format_ = is_24h;
    Settings prefs("nova", true);
    prefs.SetInt("24hour", is_24h ? 1 : 0);
}

void ChronchiDisplay::updateTime(int hour, int minute, int second) {
    lv_lock();
    char buf[16];
    if (is_24h_format_) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
    } else {
        int h = hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, minute, second);
    }
    if (home_time_label_ && lv_obj_is_valid(home_time_label_)) lv_label_set_text(home_time_label_, buf);

    if (nav_time_label_ && lv_obj_is_valid(nav_time_label_)) {
        char nbuf[16];
        snprintf(nbuf, sizeof(nbuf), "%02d:%02d", hour, minute);
        lv_label_set_text(nav_time_label_, nbuf);
    }

    // Update date: Hari, Tanggal Bulan Tahun
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char date_buf[64];
    const char* days[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    const char* months[] = {
        "Januari", "Februari", "Maret", "April", "Mei", "Juni",
        "Juli", "Agustus", "September", "Oktober", "November", "Desember"
    };
    int year = tm_info->tm_year + 1900;
    if (is_24h_format_) {
        snprintf(date_buf, sizeof(date_buf), "%s, %d %s %d",
                 days[tm_info->tm_wday % 7], tm_info->tm_mday, months[tm_info->tm_mon % 12], year);
    } else {
        snprintf(date_buf, sizeof(date_buf), "%s • %s, %d %s %d",
                 hour >= 12 ? "PM" : "AM", days[tm_info->tm_wday % 7], tm_info->tm_mday, months[tm_info->tm_mon % 12], year);
    }
    if (home_date_label_ && lv_obj_is_valid(home_date_label_)) lv_label_set_text(home_date_label_, date_buf);
    lv_unlock();
}

void ChronchiDisplay::updateBattery(uint8_t level, bool charging) {
    lv_lock();
    last_batt_level_ = level;
    last_batt_charging_ = charging;

    if (navbar_batt_label_ && lv_obj_is_valid(navbar_batt_label_)) {
        const char* icon = charging ? LV_SYMBOL_CHARGE :
                           (level > 80) ? LV_SYMBOL_BATTERY_FULL :
                           (level > 40) ? LV_SYMBOL_BATTERY_3 :
                           (level > 20) ? LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_1;
        Settings s("nova", true);
        int bat_style = s.GetInt("bat_style", 1);
        if (bat_style == 0) {
            lv_label_set_text(navbar_batt_label_, icon);
        } else if (bat_style == 2) {
            lv_label_set_text_fmt(navbar_batt_label_, "%d%%", (int)level);
        } else {
            lv_label_set_text_fmt(navbar_batt_label_, "%d%% %s", (int)level, icon);
        }
        lv_obj_set_style_text_color(navbar_batt_label_, charging ? lv_color_hex(0xF59E0B) : (level <= 20 ? lv_color_hex(0xEF4444) : lv_color_hex(0x34D399)), 0);
    }

    if (home_batt_label_ && lv_obj_is_valid(home_batt_label_)) {
        char bbuf[16];
        snprintf(bbuf, sizeof(bbuf), "%d%%%s", (int)level, charging ? " " LV_SYMBOL_CHARGE : "");
        lv_label_set_text(home_batt_label_, bbuf);
        lv_color_t bat_c = charging ? lv_color_hex(0xFACC15) :
                           (level > 50) ? lv_color_hex(0x22C55E) :
                           (level > 20) ? lv_color_hex(0xF59E0B) : lv_color_hex(0xEF4444);
        lv_obj_set_style_text_color(home_batt_label_, bat_c, 0);
        lv_obj_clear_flag(home_batt_label_, LV_OBJ_FLAG_HIDDEN);
    }

    if (home_batt_bar_fill_ && lv_obj_is_valid(home_batt_bar_fill_)) {
        int32_t fw = (24 * level) / 100;
        if (fw < 2 && level > 0) fw = 2;
        if (fw > 24) fw = 24;
        lv_obj_set_width(home_batt_bar_fill_, fw);

        lv_color_t bar_col = charging ? lv_color_hex(0x00E5FF) :
                             (level > 50) ? lv_color_hex(0x22C55E) :
                             (level > 20) ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_bg_color(home_batt_bar_fill_, bar_col, 0);
    }

    if (home_batt_card_ && lv_obj_is_valid(home_batt_card_)) {
        const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
        lv_color_t border_col = (level <= 20) ? lv_color_hex(0xEA580C) : lv_color_hex(th.card_border);
        lv_obj_set_style_border_color(home_batt_card_, border_col, 0);
    }
    lv_unlock();
}

void ChronchiDisplay::updateWifi(bool connected) {
    lv_lock();
    if (navbar_wifi_label_ && lv_obj_is_valid(navbar_wifi_label_)) {
        static uint8_t wifi_tick = 0;
        wifi_tick++;
        if (connected) {
            lv_label_set_text(navbar_wifi_label_, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(navbar_wifi_label_, lv_color_hex(0x22C55E), 0);
        } else {
            lv_label_set_text(navbar_wifi_label_, (wifi_tick % 2 == 0) ? LV_SYMBOL_REFRESH : LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(navbar_wifi_label_, lv_color_hex(0xF59E0B), 0);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::updateBtStatus(bool connected) {
    lv_lock();
    last_bt_connected_ = connected;
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    lv_color_t success_color = light_mode_ ? lv_color_hex(0x16A34A) : lv_color_hex(0x22C55E);
    lv_color_t dim_color = lv_color_hex(th.dim);
    lv_color_t border_color = connected ? success_color : lv_color_hex(th.card_border);

    if (navbar_bt_label_ && lv_obj_is_valid(navbar_bt_label_)) {
        lv_obj_set_style_text_color(navbar_bt_label_, connected ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    }
    if (navbar_bt_btn_ && lv_obj_is_valid(navbar_bt_btn_)) {
        lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_TRANSP, 0);
    }

    if (home_bt_label_ && lv_obj_is_valid(home_bt_label_)) {
        lv_obj_set_style_text_color(home_bt_label_, connected ? success_color : dim_color, 0);
        lv_label_set_text(home_bt_label_, connected ? "Terhubung" : "Siap BLE");
    }
    if (home_bt_card_ && lv_obj_is_valid(home_bt_card_)) {
        lv_obj_set_style_border_color(home_bt_card_, border_color, 0);
    }

    if (connected) {
        if (state_ == QR_PAIRING) {
            setScreenState(HOME);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::setNavigationIcon(uint8_t* icon_data, int size) {
    if (!nav_icon_buf_ || !icon_data || size < 288) return;
    lv_lock();
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    uint16_t bg_color = hex_to_rgb565(th.card_bg);
    uint16_t fg_color = hex_to_rgb565(th.accent);
    if (fg_color == bg_color) {
        fg_color = light_mode_ ? 0x0290 : 0x07FF;
    }

    // Convert 1bpp 48x48 to RGB565 with transparent theme blending
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 48; x++) {
            int byte_idx = (y * 48 + x) / 8;
            int bit_pos = 7 - (x % 8);
            bool on = (icon_data[byte_idx] >> bit_pos) & 0x01;
            uint16_t color = on ? fg_color : bg_color;
            int offset = (y * 48 + x) * 2;
            nav_icon_buf_[offset] = color & 0xFF;
            nav_icon_buf_[offset + 1] = (color >> 8) & 0xFF;
        }
    }
    if (nav_icon_img_) lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
    lv_unlock();
}

void ChronchiDisplay::showWeather(const ChronchiWeather& weather) {
    lv_lock();
    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f°C", weather.temperatureC);
    if (weather_temp_label_) lv_label_set_text(weather_temp_label_, buf);
    if (weather_cond_label_) lv_label_set_text(weather_cond_label_, weather.condition.empty() ? "Cerah" : weather.condition.c_str());

    if (weather_range_label_) {
        if (weather.highC != 0 || weather.lowC != 0) {
            snprintf(buf, sizeof(buf), "H: %d°  L: %d°", weather.highC, weather.lowC);
        } else {
            snprintf(buf, sizeof(buf), "H: --°  L: --°");
        }
        lv_label_set_text(weather_range_label_, buf);
    }

    if (weather_humid_label_) {
        bool is_landscape = (width_ > height_);
        if (weather.uv > 0) {
            if (is_landscape) {
                snprintf(buf, sizeof(buf), "💧 %d%% • ☀️ UV %d", weather.humidity, weather.uv);
            } else {
                snprintf(buf, sizeof(buf), "💧 Lembap: %d%%  •  ☀️ UV: %d", weather.humidity, weather.uv);
            }
        } else {
            snprintf(buf, sizeof(buf), is_landscape ? "💧 Lembap: %d%%" : "💧 Kelembapan: %d%%", weather.humidity);
        }
        lv_label_set_text(weather_humid_label_, buf);
    }

    if (weather_press_label_) {
        bool is_landscape = (width_ > height_);
        if (weather.pressure > 0) {
            snprintf(buf, sizeof(buf), is_landscape ? "⏱️ %d hPa" : "⏱️ Tekanan: %d hPa", weather.pressure);
        } else {
            snprintf(buf, sizeof(buf), is_landscape ? "⏱️ 1012 hPa" : "⏱️ Tekanan: 1012 hPa");
        }
        lv_label_set_text(weather_press_label_, buf);
    }

    if (weather_loc_label_) {
        snprintf(buf, sizeof(buf), "📍 %s", weather.location.empty() ? "Prakiraan Cuaca" : weather.location.c_str());
        lv_label_set_text(weather_loc_label_, buf);
    }

    // Also update home status glance capsule if no navigation is active
    if (!google_maps_active_ && home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        char status_buf[64];
        if (!weather.condition.empty()) {
            snprintf(status_buf, sizeof(status_buf), "⛅ %.0f°C  %s", weather.temperatureC, weather.condition.c_str());
            lv_label_set_text(home_status_label_, status_buf);
        }
    }

    // Do NOT automatically switch away from HOME to WEATHER; let the user stay on the digital clock!
    lv_unlock();
}

void ChronchiDisplay::showPhoneStatus(const ChronchiPhoneStatus& status) {
    lv_lock();
    char buf[64];
    const char* chgIcon = status.charging ? " ⚡" : "";
    snprintf(buf, sizeof(buf), "%d%%%s", status.batteryLevel, chgIcon);
    if (status_phone_bat_label_) {
        lv_label_set_text(status_phone_bat_label_, buf);
        lv_color_t bat_col = status.batteryLevel > 50 ? lv_color_hex(0x10B981) :
                             status.batteryLevel > 20 ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_text_color(status_phone_bat_label_, bat_col, 0);
    }

    if (status_bat_bar_fill_ && status_bat_bar_) {
        int32_t bar_w = lv_obj_get_width(status_bat_bar_);
        if (bar_w <= 0) bar_w = (width_ > height_) ? 134 : 200;
        int32_t bw = (bar_w * status.batteryLevel) / 100;
        if (bw < 0) bw = 0;
        if (bw > bar_w) bw = bar_w;
        lv_obj_set_width(status_bat_bar_fill_, bw);
        lv_color_t bar_col = status.batteryLevel > 50 ? lv_color_hex(0x10B981) :
                             status.batteryLevel > 20 ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_bg_color(status_bat_bar_fill_, bar_col, 0);
    }

    bool is_landscape = (width_ > height_);
    if (!status.network.transport.empty()) {
        if (is_landscape) {
            snprintf(buf, sizeof(buf), "📶 %s %s",
                     status.network.transport.c_str(), status.network.generation.c_str());
        } else {
            snprintf(buf, sizeof(buf), "📶 Jaringan: %s %s",
                     status.network.transport.c_str(), status.network.generation.c_str());
        }
    } else {
        snprintf(buf, sizeof(buf), is_landscape ? "📶 Seluler" : "📶 Jaringan: Seluler");
    }
    if (status_network_label_) lv_label_set_text(status_network_label_, buf);

    if (status.network.signalLevel > 0) {
        const char* sigBars = (status.network.signalLevel >= 4) ? "●●●●" :
                              (status.network.signalLevel == 3) ? "●●●○" :
                              (status.network.signalLevel == 2) ? "●●○○" :
                              (status.network.signalLevel == 1) ? "●○○○" : "○○○○";
        snprintf(buf, sizeof(buf), is_landscape ? "Sinyal: %s" : "📶 Sinyal: %s", sigBars);
    } else {
        snprintf(buf, sizeof(buf), is_landscape ? "Sinyal: Aktif" : "📶 Sinyal: Aktif");
    }
    if (status_signal_label_) lv_label_set_text(status_signal_label_, buf);

    if (status_net_hint_label_) {
        lv_label_set_text(status_net_hint_label_, last_bt_connected_ ? (is_landscape ? "BLE: Aktif" : "⚡ Koneksi: BLE Chronos") :
                                                                       (is_landscape ? "BLE: Menunggu..." : "⚡ Menunggu Ponsel..."));
    }

    // Do NOT automatically switch away from HOME to STATUS; let the user stay on the digital clock!
    lv_unlock();
}

void ChronchiDisplay::showMusic(const ChronchiMusic& music) {
    lv_lock();
    if (music_state_label_) {
        bool playing = (music.state == 1);
        lv_label_set_text(music_state_label_, playing ? "▶ Putar" : "⏸ Jeda");
        lv_obj_set_style_text_color(music_state_label_,
            playing ? (light_mode_ ? lv_color_hex(0x16A34A) : lv_color_hex(0x22C55E)) :
                      lv_color_hex(0xF59E0B), 0);
    }
    if (music_title_label_) {
        std::string t = music.title.empty() ? "Tidak Ada Media" : music.title;
        char buf[160];
        snprintf(buf, sizeof(buf), "🎵 %s        ", t.c_str());
        lv_label_set_text(music_title_label_, buf);
    }
    if (music_artist_label_) {
        std::string a = music.artist.empty() ? "Putar musik di ponsel Anda" : music.artist;
        char buf[160];
        snprintf(buf, sizeof(buf), "👤 %s        ", a.c_str());
        lv_label_set_text(music_artist_label_, buf);
    }
    if (music_recom_label_) {
        std::string appName = music.app.empty() ? "Chronos Audio" : music.app;
        char buf[160];
        snprintf(buf, sizeof(buf), "✨ Direkomendasikan untuk mu • %s        ", appName.c_str());
        lv_label_set_text(music_recom_label_, buf);
    }
    if (music_bar_fill_) {
        int32_t mw = (music.state == 1) ? 140 : 40;
        lv_obj_set_width(music_bar_fill_, mw);
    }

    if (music_play_label_ && lv_obj_is_valid(music_play_label_)) {
        lv_label_set_text(music_play_label_, (music.state == 1) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    // Dynamic Home Screen status update: if music is active, show song on Home!
    if (!google_maps_active_ && home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        if (music.state == 1 && !music.title.empty()) {
            char mbuf[64];
            snprintf(mbuf, sizeof(mbuf), "🎵 %s", music.title.c_str());
            lv_label_set_text(home_status_label_, mbuf);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::showFindWatchAlert(bool active) {
    lv_lock();
    if (active) {
        is_camera_mode_ = false;
        if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL "  " LV_SYMBOL_WARNING);
        if (alert_title_label_) lv_label_set_text(alert_title_label_, "CARI JAM TANGAN!");
        if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Ponsel Anda sedang mencari jam ini.\nGetaran & suara aktif.");
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
        if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ MATIKAN ALARM");
        setScreenState(ALERT);
    } else {
        is_camera_mode_ = false;
        ChronchiAlert::GetInstance().StopAlert();
        if (state_ == ALERT) showHome();
    }
    lv_unlock();
}

void ChronchiDisplay::showCameraPrompt(bool ready) {
    lv_lock();
    if (ready) {
        is_camera_mode_ = true;
        if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_IMAGE " KAMERA");
        if (alert_title_label_) lv_label_set_text(alert_title_label_, "KAMERA AKTIF");
        if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Kamera ponsel Anda siap.\nTekan tombol di bawah untuk memotret foto!");
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0x0284C7), 0);
        if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "📸 JEPRET FOTO");
        setScreenState(ALERT);
    } else {
        is_camera_mode_ = false;
        if (state_ == ALERT) showHome();
    }
    lv_unlock();
}

void ChronchiDisplay::showAlarmAlert(int hour, int minute) {
    lv_lock();
    is_camera_mode_ = false;
    if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL " ALARM");
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "WAKTU: %02d:%02d", hour, minute);
    if (alert_title_label_) lv_label_set_text(alert_title_label_, tbuf);
    if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Alarm Berbunyi!\nWaktunya beraktivitas.");
    if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ MATIKAN ALARM");
    setScreenState(ALERT);
    lv_unlock();
}

void ChronchiDisplay::showReminderAlert(const char* title, const char* message) {
    lv_lock();
    is_camera_mode_ = false;
    if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL " PENGINGAT");
    if (alert_title_label_) lv_label_set_text(alert_title_label_, title ? title : "PENGINGAT");
    if (alert_msg_label_) lv_label_set_text(alert_msg_label_, message ? message : "Waktunya aktivitas!");
    if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ TUTUP");
    setScreenState(ALERT);
    lv_unlock();

    // Auto-dismiss after 8 seconds
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_timer_get_user_data(t));
        if (disp && disp->state_ == ALERT) {
            disp->showHome();
        }
        lv_timer_del(t);
    }, 8000, this);
    lv_timer_set_repeat_count(timer, 1);
}

void ChronchiDisplay::cycleScreen() {
    static uint32_t last_cycle = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_cycle < 350) return;
    last_cycle = now;

    lv_lock();
    if (state_ == HOME) {
        setScreenState(WEATHER);
    } else if (state_ == WEATHER) {
        setScreenState(MUSIC);
    } else if (state_ == MUSIC) {
        setScreenState(NAVIGATION);
    } else if (state_ == NAVIGATION) {
        setScreenState(STATUS);
    } else if (state_ == STATUS) {
        if (!last_bt_connected_) {
            setScreenState(QR_PAIRING);
        } else {
            showHome();
        }
    } else if (state_ == QR_PAIRING) {
        showHome();
    } else {
        showHome();
    }
    lv_unlock();
}
