#include "chronchi_display_internal.h"

// ---- DYNAMIC THEME & COLOR APPLICATION ----
void ChronchiDisplay::applyColorMode() {
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    lv_color_t bg_color       = lv_color_hex(th.bg);
    lv_color_t card_bg_col    = lv_color_hex(th.card_bg);
    lv_color_t card_bdr_col   = lv_color_hex(th.card_border);
    lv_color_t text_color     = lv_color_hex(th.text);
    lv_color_t dim_color      = lv_color_hex(th.dim);
    lv_color_t accent_color   = lv_color_hex(th.accent);
    lv_color_t success_color  = lv_color_hex(0x22C55E);
    lv_color_t notif_line_col = lv_color_hex(0x334155);
    lv_color_t notif_msg_col  = lv_color_hex(0xCBD5E1);

    const auto* synced_bg = GetCurrentSyncedBg();
    if (synced_bg) {
        bg_color     = lv_color_hex(synced_bg->bg_color);
        card_bg_col  = lv_color_hex(synced_bg->panel_color);
        card_bdr_col = lv_color_hex(synced_bg->border_color);
    }

    if (chronchi_navbar_ && lv_obj_is_valid(chronchi_navbar_)) {
        lv_obj_set_style_bg_color(chronchi_navbar_, card_bg_col, 0);
        lv_obj_set_style_border_color(chronchi_navbar_, card_bdr_col, 0);
    }
    if (chronchi_bottom_nav_ && lv_obj_is_valid(chronchi_bottom_nav_)) {
        lv_obj_set_style_bg_color(chronchi_bottom_nav_, card_bg_col, 0);
        lv_obj_set_style_border_color(chronchi_bottom_nav_, card_bdr_col, 0);
    }

    auto style_card = [](lv_obj_t* card, lv_color_t bg, lv_color_t border) {
        if (card && lv_obj_is_valid(card)) {
            lv_obj_set_style_bg_color(card, bg, 0);
            lv_obj_set_style_border_color(card, border, 0);
        }
    };

    // Style Navbar Theme Palette Button with active stabilo color
    if (navbar_th_lbl_ && lv_obj_is_valid(navbar_th_lbl_)) {
        lv_obj_set_style_text_color(navbar_th_lbl_, accent_color, 0);
    }
    if (navbar_th_btn_ && lv_obj_is_valid(navbar_th_btn_)) {
        lv_obj_set_style_border_color(navbar_th_btn_, accent_color, 0);
        lv_obj_set_style_border_width(navbar_th_btn_, 1, 0);
    }

    // 1. Home Screen (Simple Minimalist Chronos Watchface)
    if (home_screen_) lv_obj_set_style_bg_color(home_screen_, bg_color, 0);
    if (home_time_card_) style_card(home_time_card_, card_bg_col, card_bdr_col);
    if (home_time_label_) lv_obj_set_style_text_color(home_time_label_, text_color, 0);
    if (home_date_card_) lv_obj_set_style_bg_color(home_date_card_, accent_color, 0);
    if (home_date_label_) lv_obj_set_style_text_color(home_date_label_, dim_color, 0);

    // Navbar Bluetooth Button & Label styling
    if (navbar_bt_label_ && lv_obj_is_valid(navbar_bt_label_)) {
        lv_obj_set_style_text_color(navbar_bt_label_, last_bt_connected_ ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    }
    if (navbar_bt_btn_ && lv_obj_is_valid(navbar_bt_btn_)) {
        lv_obj_set_style_bg_color(navbar_bt_btn_, last_bt_connected_ ? lv_color_hex(0x0F2E4A) : lv_color_hex(0x27272A), 0);
    }

    auto style_card_borderless = [](lv_obj_t* card, lv_color_t bg) {
        if (card && lv_obj_is_valid(card)) {
            lv_obj_set_style_bg_color(card, bg, 0);
            lv_obj_set_style_border_width(card, 0, 0);
        }
    };

    // 2. Nav Screen (Borderless)
    if (nav_screen_) lv_obj_set_style_bg_color(nav_screen_, bg_color, 0);
    style_card_borderless(nav_header_card_, card_bg_col);
    if (nav_header_label_) lv_obj_set_style_text_color(nav_header_label_, lv_color_hex(0x3B82F6), 0);
    if (nav_time_label_) lv_obj_set_style_text_color(nav_time_label_, text_color, 0);

    style_card_borderless(nav_main_card_, card_bg_col);
    if (nav_dist_label_) lv_obj_set_style_text_color(nav_dist_label_, accent_color, 0);
    if (nav_title_label_) lv_obj_set_style_text_color(nav_title_label_, dim_color, 0);
    if (nav_street_label_) lv_obj_set_style_text_color(nav_street_label_, text_color, 0);

    style_card_borderless(nav_stats_card_, card_bg_col);
    if (nav_dur_label_) lv_obj_set_style_text_color(nav_dur_label_, text_color, 0);
    if (nav_eta_label_) lv_obj_set_style_text_color(nav_eta_label_, success_color, 0);
    if (nav_speed_label_) lv_obj_set_style_text_color(nav_speed_label_, accent_color, 0);
    setNavIcon(last_maneuver_.empty() ? "STRAIGHT" : last_maneuver_.c_str());

    // 3. Notif Screen
    if (notif_screen_) lv_obj_set_style_bg_color(notif_screen_, bg_color, 0);
    style_card(notif_header_badge_, card_bg_col, accent_color);
    if (notif_app_label_) lv_obj_set_style_text_color(notif_app_label_, accent_color, 0);
    if (notif_time_label_) lv_obj_set_style_text_color(notif_time_label_, dim_color, 0);
    style_card(notif_card_, card_bg_col, card_bdr_col);
    if (notif_line_) lv_obj_set_style_bg_color(notif_line_, notif_line_col, 0);
    if (notif_title_label_) lv_obj_set_style_text_color(notif_title_label_, text_color, 0);
    if (notif_msg_label_) lv_obj_set_style_text_color(notif_msg_label_, notif_msg_col, 0);
    if (notif_icon_label_) lv_obj_set_style_text_color(notif_icon_label_, dim_color, 0);

    // 4. Weather Screen
    if (weather_screen_) lv_obj_set_style_bg_color(weather_screen_, bg_color, 0);
    style_card(weather_header_card_, card_bg_col, lv_color_hex(0x0284C7));
    if (weather_header_label_) lv_obj_set_style_text_color(weather_header_label_, lv_color_hex(0x38BDF8), 0);
    style_card(weather_main_card_, card_bg_col, card_bdr_col);
    if (weather_temp_label_) lv_obj_set_style_text_color(weather_temp_label_, lv_color_hex(0xF59E0B), 0);
    if (weather_cond_label_) lv_obj_set_style_text_color(weather_cond_label_, text_color, 0);
    if (weather_range_label_) lv_obj_set_style_text_color(weather_range_label_, dim_color, 0);
    style_card(weather_stats_card_, card_bg_col, card_bdr_col);
    if (weather_humid_label_) lv_obj_set_style_text_color(weather_humid_label_, text_color, 0);
    if (weather_press_label_) lv_obj_set_style_text_color(weather_press_label_, dim_color, 0);
    style_card(weather_loc_card_, card_bg_col, card_bdr_col);
    if (weather_loc_label_) lv_obj_set_style_text_color(weather_loc_label_, lv_color_hex(0x00E5FF), 0);

    // 5. Music Screen (Borderless)
    if (music_screen_) lv_obj_set_style_bg_color(music_screen_, bg_color, 0);
    style_card_borderless(music_header_card_, card_bg_col);
    if (music_header_label_) lv_obj_set_style_text_color(music_header_label_, lv_color_hex(0x1DB954), 0);
    if (music_state_label_) lv_obj_set_style_text_color(music_state_label_, success_color, 0);
    style_card_borderless(music_card_, card_bg_col);
    if (music_title_label_) lv_obj_set_style_text_color(music_title_label_, text_color, 0);
    if (music_artist_label_) lv_obj_set_style_text_color(music_artist_label_, accent_color, 0);
    if (music_recom_label_) lv_obj_set_style_text_color(music_recom_label_, lv_color_hex(0xFBBF24), 0);
    if (music_bar_) {
        lv_obj_set_style_bg_color(music_bar_, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_width(music_bar_, 0, 0);
    }
    if (music_bar_fill_) {
        lv_obj_set_style_bg_color(music_bar_fill_, lv_color_hex(0x22C55E), 0);
    }

    // 6. Status Screen (Borderless)
    if (status_screen_) lv_obj_set_style_bg_color(status_screen_, bg_color, 0);
    style_card_borderless(status_header_card_, card_bg_col);
    if (status_header_label_) lv_obj_set_style_text_color(status_header_label_, lv_color_hex(0x00E5FF), 0);
    style_card_borderless(status_bat_card_, card_bg_col);
    if (status_phone_bat_title_) lv_obj_set_style_text_color(status_phone_bat_title_, dim_color, 0);
    if (status_phone_bat_label_) lv_obj_set_style_text_color(status_phone_bat_label_, success_color, 0);
    if (status_bat_bar_) {
        lv_obj_set_style_bg_color(status_bat_bar_, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_width(status_bat_bar_, 0, 0);
    }
    if (status_bat_bar_fill_) {
        lv_obj_set_style_bg_color(status_bat_bar_fill_, success_color, 0);
    }
    style_card_borderless(status_net_card_, card_bg_col);
    if (status_network_label_) lv_obj_set_style_text_color(status_network_label_, accent_color, 0);
    if (status_signal_label_) lv_obj_set_style_text_color(status_signal_label_, dim_color, 0);
    if (status_net_hint_label_) lv_obj_set_style_text_color(status_net_hint_label_, dim_color, 0);
    if (status_find_phone_btn_) {
        lv_obj_set_style_bg_color(status_find_phone_btn_, find_phone_active_ ? lv_color_hex(0xDC2626) : card_bg_col, 0);
        lv_obj_set_style_border_width(status_find_phone_btn_, 0, 0);
    }
    style_card_borderless(status_bt_card_, card_bg_col);
    if (status_bt_label_) lv_obj_set_style_text_color(status_bt_label_, last_bt_connected_ ? success_color : accent_color, 0);

    // 7. Alert Screen
    if (alert_screen_) lv_obj_set_style_bg_color(alert_screen_, bg_color, 0);
    style_card(alert_card_, light_mode_ ? lv_color_hex(0xFEE2E2) : lv_color_hex(0x3B0707), lv_color_hex(0xEF4444));
    if (alert_icon_label_) lv_obj_set_style_text_color(alert_icon_label_, lv_color_hex(0xFACC15), 0);
    if (alert_title_label_) lv_obj_set_style_text_color(alert_title_label_, lv_color_hex(0xEF4444), 0);
    if (alert_msg_label_) lv_obj_set_style_text_color(alert_msg_label_, light_mode_ ? lv_color_hex(0x7F1D1D) : lv_color_hex(0xFCA5A5), 0);
    if (is_camera_mode_) {
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0x0284C7), 0);
    } else {
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    }
    if (alert_btn_label_) lv_obj_set_style_text_color(alert_btn_label_, lv_color_hex(0xFFFFFF), 0);

    // 8. QR Pairing Screen
    if (qr_screen_) lv_obj_set_style_bg_color(qr_screen_, bg_color, 0);
    style_card(qr_header_card_, card_bg_col, accent_color);
    if (qr_header_label_) lv_obj_set_style_text_color(qr_header_label_, accent_color, 0);
    if (qr_clock_btn_) {
        lv_obj_set_style_bg_color(qr_clock_btn_, card_bg_col, 0);
        lv_obj_set_style_border_color(qr_clock_btn_, card_bdr_col, 0);
        lv_obj_set_style_border_width(qr_clock_btn_, 1, 0);
    }
}

void ChronchiDisplay::setWatchfaceTheme(uint32_t color, uint32_t layout) {
    ESP_LOGI(TAG, "Watchface customized via Chronos: color=0x%06lX, layout=%lu",
             (unsigned long)color, (unsigned long)layout);

    current_layout_ = layout % NUM_THEMES;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = (color != 0) ? color : th.text;
    light_mode_ = (th.bg > 0x888888);

    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);

    // Global theme synchronization across XiaoZhi, Chronchi, and Nova OS
    SyncThemeByLayout(current_layout_);

    lv_lock();
    applyColorMode();
    lv_unlock();
}

void ChronchiDisplay::toggleColorMode() {
    current_layout_ = (current_layout_ + 1) % NUM_THEMES;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = th.text;
    light_mode_ = (th.bg > 0x888888);

    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);

    // Global theme synchronization across XiaoZhi, Chronchi, and Nova OS
    SyncThemeByLayout(current_layout_);

    ESP_LOGI(TAG, "Watchface theme toggled to: %lu (%s) - Color: 0x%06lX",
             (unsigned long)current_layout_, th.name, (unsigned long)theme_color_);

    lv_lock();
    applyColorMode();

    if (home_status_label_ && lv_obj_is_valid(home_status_label_) && !google_maps_active_) {
        char toast[64];
        snprintf(toast, sizeof(toast), "🎨 %s", th.name);
        lv_label_set_text(home_status_label_, toast);
    }
    lv_unlock();
}

void ChronchiDisplay::setColorMode(bool light) {
    current_layout_ = light ? 1 : 0;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = th.text;
    light_mode_ = (th.bg > 0x888888);
    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);
    ESP_LOGI(TAG, "Color mode set to: %lu (%s)", (unsigned long)current_layout_, th.name);
    lv_lock();
    applyColorMode();
    lv_unlock();
}

void ChronchiDisplay::cycleTheme() {
    toggleColorMode();
}

void ChronchiDisplay::setTheme(int theme) {
    setWatchfaceTheme(theme_color_, theme);
}

void ChronchiDisplay::setThemeColor(uint32_t rgb) {
    setWatchfaceTheme(rgb, current_layout_);
}

void ChronchiDisplay::updateHomeWeather(const ChronchiWeather& weather) {
    lv_lock();
    if (home_status_label_ && lv_obj_is_valid(home_status_label_) && !google_maps_active_) {
        char sbuf[64];
        snprintf(sbuf, sizeof(sbuf), "%.0f°C  %s", weather.temperatureC, weather.condition.c_str());
        lv_label_set_text(home_status_label_, sbuf);
    }
    lv_unlock();
}
