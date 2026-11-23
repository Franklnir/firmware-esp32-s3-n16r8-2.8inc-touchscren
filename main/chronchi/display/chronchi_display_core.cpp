#include "chronchi_display_internal.h"

ChronchiDisplay::ChronchiDisplay() = default;
ChronchiDisplay::~ChronchiDisplay() {
    if (chronchi_bottom_nav_) { lv_obj_del(chronchi_bottom_nav_); chronchi_bottom_nav_ = nullptr; }
    if (recent_apps_modal_) { lv_obj_del(recent_apps_modal_); recent_apps_modal_ = nullptr; }
    if (overlay_backdrop_) { lv_obj_del(overlay_backdrop_); overlay_backdrop_ = nullptr; }
    if (vol_overlay_card_) { lv_obj_del(vol_overlay_card_); vol_overlay_card_ = nullptr; }
    if (bright_overlay_card_) { lv_obj_del(bright_overlay_card_); bright_overlay_card_ = nullptr; }
    if (chronchi_navbar_) { lv_obj_del(chronchi_navbar_); chronchi_navbar_ = nullptr; }
    if (nav_icon_buf_) { free(nav_icon_buf_); nav_icon_buf_ = nullptr; }
    if (qr_img_buf_) { free(qr_img_buf_); qr_img_buf_ = nullptr; }
}

void ChronchiDisplay::init(int width, int height) {
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    } else {
        width_ = width;
        height_ = height;
    }
    ESP_LOGI(TAG, "init %dx%d (Borderless Chronchi Watchface)", width_, height_);

    // Load synchronized watchface theme and color from NVS
    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        current_layout_ = synced->chronchi_layout % NUM_THEMES;
        theme_color_ = synced->accent_color;
    } else {
        Settings settings("chronchi", true);
        current_layout_ = settings.GetInt("layout", 1) % NUM_THEMES;
        theme_color_ = settings.GetInt("color", PRESET_THEMES[current_layout_].text);
    }
    light_mode_ = (PRESET_THEMES[current_layout_].bg > 0x888888);

    Settings prefs_24h("nova", true);
    is_24h_format_ = (prefs_24h.GetInt("24hour", 1) != 0);

    ESP_LOGI(TAG, "Initial watchface: layout=%lu (%s), color=0x%06lX, 24h=%d",
             (unsigned long)current_layout_, PRESET_THEMES[current_layout_].name, (unsigned long)theme_color_, (int)is_24h_format_);

    lv_lock();

    // Allocate navigation icon buffer (48x48 RGB565)
    nav_icon_buf_ = (uint8_t*)heap_caps_malloc(48 * 48 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nav_icon_buf_) nav_icon_buf_ = (uint8_t*)malloc(48 * 48 * 2);
    if (nav_icon_buf_) {
        const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
        const auto* synced_bg = GetCurrentSyncedBg();
        uint32_t card_hex = (synced_bg) ? synced_bg->panel_color : th.card_bg;
        uint16_t initial_bg = hex_to_rgb565(card_hex);
        for (int i = 0; i < 48 * 48; i++) {
            nav_icon_buf_[i * 2] = initial_bg & 0xFF;
            nav_icon_buf_[i * 2 + 1] = (initial_bg >> 8) & 0xFF;
        }
        memset(&nav_icon_dsc_, 0, sizeof(nav_icon_dsc_));
        nav_icon_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        nav_icon_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        nav_icon_dsc_.header.w = 48;
        nav_icon_dsc_.header.h = 48;
        nav_icon_dsc_.header.stride = 48 * 2;
        nav_icon_dsc_.data_size = 48 * 48 * 2;
        nav_icon_dsc_.data = nav_icon_buf_;
    }

    // Allocate QR code image buffer (140x140 RGB565)
    qr_img_buf_ = (uint8_t*)heap_caps_malloc(140 * 140 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!qr_img_buf_) qr_img_buf_ = (uint8_t*)malloc(140 * 140 * 2);
    if (qr_img_buf_) {
        memset(qr_img_buf_, 0xFF, 140 * 140 * 2); // default all white
        memset(&qr_img_dsc_, 0, sizeof(qr_img_dsc_));
        qr_img_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        qr_img_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        qr_img_dsc_.header.w = 140;
        qr_img_dsc_.header.h = 140;
        qr_img_dsc_.header.stride = 140 * 2;
        qr_img_dsc_.data_size = 140 * 140 * 2;
        qr_img_dsc_.data = qr_img_buf_;
    }

    createHomeScreen();
    createQrScreen();
    createNavScreen();
    createNotifScreen();
    createWeatherScreen();
    createMusicScreen();
    createStatusScreen();
    createAlertScreen();
    createNavbar();
    createBottomNav();

    applyColorMode();
    updateResolution();
    showHome();

    lv_unlock();
}

// ---- UNIFIED CHRONCHI TOP NAVBAR (Displayed on every screen via lv_layer_top) ----
void ChronchiDisplay::createNavbar() {
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    chronchi_navbar_ = lv_obj_create(top_layer);
    lv_obj_set_size(chronchi_navbar_, width_, 34);
    lv_obj_align(chronchi_navbar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(chronchi_navbar_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(chronchi_navbar_, LV_OPA_90, 0);
    lv_obj_set_style_border_side(chronchi_navbar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(chronchi_navbar_, 1, 0);
    lv_obj_set_style_border_color(chronchi_navbar_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_radius(chronchi_navbar_, 0, 0);
    lv_obj_set_style_pad_all(chronchi_navbar_, 0, 0);
    lv_obj_set_style_pad_left(chronchi_navbar_, 4, 0);
    lv_obj_set_style_pad_right(chronchi_navbar_, 6, 0);
    lv_obj_set_flex_flow(chronchi_navbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chronchi_navbar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chronchi_navbar_, LV_OBJ_FLAG_SCROLLABLE);

    // Left group: XiaoZhi AI button + Brightness button
    lv_obj_t* left_box = lv_obj_create(chronchi_navbar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 3, 0);
    lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Rotation Button (Cyan circular rotation arrow - rotates 90 degrees)
    navbar_xz_btn_ = lv_button_create(left_box);
    lv_obj_set_size(navbar_xz_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_xz_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_xz_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_xz_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_xz_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_xz_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_xz_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_xz_btn_, 0, 0);
    lv_obj_t* rot_icon = lv_label_create(navbar_xz_btn_);
    lv_label_set_text(rot_icon, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(rot_icon, lv_color_hex(0x06B6D4), 0);
    lv_obj_center(rot_icon);
    lv_obj_add_event_cb(navbar_xz_btn_, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().RotateDisplay();
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Icon Button (Amber sun)
    navbar_br_btn_ = lv_button_create(left_box);
    lv_obj_set_size(navbar_br_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_br_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_br_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_br_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_br_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_br_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_br_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_br_btn_, 0, 0);
    lv_obj_t* br_lbl = lv_label_create(navbar_br_btn_);
    lv_label_set_text(br_lbl, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(br_lbl, lv_color_hex(0xF59E0B), 0);
    lv_obj_center(br_lbl);
    lv_obj_add_event_cb(navbar_br_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showBrightnessOverlay();
    }, LV_EVENT_CLICKED, this);

    // Center: Theme Color / Palette Button (Tap -> cycle watchface colors)
    navbar_th_btn_ = lv_button_create(chronchi_navbar_);
    lv_obj_set_size(navbar_th_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_th_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_th_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_th_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_th_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_th_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_th_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_th_btn_, 0, 0);
    navbar_th_lbl_ = lv_label_create(navbar_th_btn_);
    lv_label_set_text(navbar_th_lbl_, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(navbar_th_lbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_th_lbl_, lv_color_hex(0xFFE600), 0);
    lv_obj_center(navbar_th_lbl_);
    lv_obj_add_event_cb(navbar_th_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->cycleTheme();
    }, LV_EVENT_CLICKED, this);

    // Right group: Bluetooth -> Speaker/Volume -> Wi-Fi -> Battery Status
    lv_obj_t* right_box = lv_obj_create(chronchi_navbar_);
    lv_obj_set_size(right_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_box, 0, 0);
    lv_obj_set_style_pad_all(right_box, 0, 0);
    lv_obj_set_flex_flow(right_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_box, 3, 0);
    lv_obj_clear_flag(right_box, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Bluetooth Button in navbar (Tap -> QR pairing or status)
    navbar_bt_btn_ = lv_button_create(right_box);
    lv_obj_set_size(navbar_bt_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_bt_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_bt_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_bt_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_bt_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_bt_btn_, 0, 0);
    navbar_bt_label_ = lv_label_create(navbar_bt_btn_);
    lv_label_set_text(navbar_bt_label_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(navbar_bt_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_bt_label_, last_bt_connected_ ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    lv_obj_center(navbar_bt_label_);
    lv_obj_add_event_cb(navbar_bt_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            if (!disp->isBtConnected()) {
                disp->showQrPairing();
            } else {
                disp->setScreenState(STATUS);
            }
        }
    }, LV_EVENT_CLICKED, this);

    // 2. Speaker / Volume Button (Tap -> cycle volume)
    navbar_vol_btn_ = lv_button_create(right_box);
    lv_obj_set_size(navbar_vol_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_vol_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_vol_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_vol_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_vol_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_vol_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_vol_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_vol_btn_, 0, 0);
    navbar_vol_label_ = lv_label_create(navbar_vol_btn_);
    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 75;
    lv_label_set_text(navbar_vol_label_, cur_vol == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(navbar_vol_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_vol_label_, cur_vol == 0 ? lv_color_hex(0x71717A) : lv_color_hex(0x06B6D4), 0);
    lv_obj_center(navbar_vol_label_);
    lv_obj_add_event_cb(navbar_vol_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showVolumeOverlay();
    }, LV_EVENT_CLICKED, this);

    // 3. Wi-Fi Button hidden in Chronchi mode as requested
    navbar_wifi_btn_ = nullptr;
    navbar_wifi_label_ = nullptr;

    // 4. Battery Status (Formatted based on bat_style)
    navbar_batt_label_ = lv_label_create(right_box);
    Settings s("nova", true);
    int bat_style = s.GetInt("bat_style", 1);
    if (bat_style == 0) {
        lv_label_set_text(navbar_batt_label_, LV_SYMBOL_BATTERY_FULL);
    } else if (bat_style == 2) {
        lv_label_set_text(navbar_batt_label_, "100%");
    } else {
        lv_label_set_text(navbar_batt_label_, "100% " LV_SYMBOL_BATTERY_FULL);
    }
    lv_obj_set_style_text_font(navbar_batt_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_batt_label_, lv_color_hex(0x34D399), 0);
}

void ChronchiDisplay::updateResolution() {
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    }

    bool is_landscape = (width_ > height_);

    if (chronchi_navbar_) {
        lv_obj_set_width(chronchi_navbar_, width_);
        lv_obj_set_height(chronchi_navbar_, is_landscape ? 28 : 34);
    }
    if (chronchi_bottom_nav_) {
        lv_obj_set_width(chronchi_bottom_nav_, width_);
        lv_obj_set_height(chronchi_bottom_nav_, is_landscape ? 26 : 30);
        lv_obj_align(chronchi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (overlay_backdrop_) {
        lv_obj_set_size(overlay_backdrop_, width_, height_);
    }
    if (vol_overlay_card_) {
        lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, is_landscape ? 30 : 36);
    }
    if (bright_overlay_card_) {
        lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, is_landscape ? 30 : 36);
    }

    lv_obj_t* screens[] = {home_screen_, qr_screen_, nav_screen_, notif_screen_, weather_screen_, music_screen_, status_screen_, alert_screen_};
    for (lv_obj_t* scr : screens) {
        if (!scr) continue;
        lv_obj_set_size(scr, width_, height_);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_bottom(scr, 0, 0);
        lv_obj_scroll_to_y(scr, 0, LV_ANIM_OFF);
    }

    // 1. Home Screen
    if (home_time_card_) {
        lv_obj_set_size(home_time_card_, is_landscape ? 280 : 224, is_landscape ? 116 : 136);
        lv_obj_align(home_time_card_, LV_ALIGN_CENTER, 0, is_landscape ? -2 : -4);
    }
    if (home_date_card_) {
        lv_obj_set_width(home_date_card_, is_landscape ? 200 : 120);
        lv_obj_align(home_date_card_, LV_ALIGN_CENTER, 0, is_landscape ? 6 : 8);
    }

    // 2. Nav Screen (Google Maps Navigation)
    if (is_landscape) {
        if (nav_header_card_) {
            lv_obj_set_size(nav_header_card_, 304, 24);
            lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (nav_header_label_) lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 10, 0);
        if (nav_time_label_) lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -10, 0);

        if (nav_main_card_) {
            lv_obj_set_size(nav_main_card_, 156, 148);
            lv_obj_align(nav_main_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (nav_icon_img_) lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 6, 8);
        if (nav_dist_label_) lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 52, 6);
        if (nav_title_label_) {
            lv_obj_set_width(nav_title_label_, 98);
            lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 52, 32);
        }
        if (nav_street_label_) {
            lv_obj_set_width(nav_street_label_, 144);
            lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (nav_stats_card_) {
            lv_obj_set_size(nav_stats_card_, 142, 148);
            lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_LEFT, 170, 58);
        }
        if (nav_dur_label_) lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 10, 16);
        if (nav_eta_label_) lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_LEFT, 10, 58);
        if (nav_speed_label_) lv_obj_align(nav_speed_label_, LV_ALIGN_TOP_LEFT, 10, 100);
    } else {
        if (nav_header_card_) {
            lv_obj_set_size(nav_header_card_, 220, 26);
            lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 38);
        }
        if (nav_header_label_) lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 12, 0);
        if (nav_time_label_) lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -12, 0);

        if (nav_main_card_) {
            lv_obj_set_size(nav_main_card_, 220, 118);
            lv_obj_align(nav_main_card_, LV_ALIGN_TOP_MID, 0, 70);
        }
        if (nav_icon_img_) lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 10, 10);
        if (nav_dist_label_) lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 68, 8);
        if (nav_title_label_) {
            lv_obj_set_width(nav_title_label_, 142);
            lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 68, 36);
        }
        if (nav_street_label_) {
            lv_obj_set_width(nav_street_label_, 200);
            lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (nav_stats_card_) {
            lv_obj_set_size(nav_stats_card_, 220, 66);
            lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_MID, 0, 196);
        }
        if (nav_dur_label_) lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 12, 8);
        if (nav_eta_label_) lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_RIGHT, -12, 8);
        if (nav_speed_label_) lv_obj_align(nav_speed_label_, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    }

    // 3. Weather Screen
    if (is_landscape) {
        if (weather_header_card_) {
            lv_obj_set_size(weather_header_card_, 304, 24);
            lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (weather_main_card_) {
            lv_obj_set_size(weather_main_card_, 152, 148);
            lv_obj_align(weather_main_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (weather_temp_label_) lv_obj_align(weather_temp_label_, LV_ALIGN_TOP_LEFT, 12, 12);
        if (weather_cond_label_) {
            lv_obj_set_width(weather_cond_label_, 78);
            lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -8, 14);
        }
        if (weather_range_label_) {
            lv_obj_set_width(weather_range_label_, 136);
            lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_MID, 0, -12);
        }

        if (weather_stats_card_) {
            lv_obj_set_size(weather_stats_card_, 150, 88);
            lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_LEFT, 162, 58);
        }
        if (weather_humid_label_) lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 10, 14);
        if (weather_press_label_) lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 10, 48);

        if (weather_loc_card_) {
            lv_obj_set_size(weather_loc_card_, 150, 52);
            lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_LEFT, 162, 152);
        }
        if (weather_loc_label_) {
            lv_obj_set_width(weather_loc_label_, 140);
            lv_obj_center(weather_loc_label_);
        }
    } else {
        if (weather_header_card_) {
            lv_obj_set_size(weather_header_card_, 224, 26);
            lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (weather_main_card_) {
            lv_obj_set_size(weather_main_card_, 224, 80);
            lv_obj_align(weather_main_card_, LV_ALIGN_TOP_MID, 0, 68);
        }
        if (weather_temp_label_) lv_obj_align(weather_temp_label_, LV_ALIGN_LEFT_MID, 16, 0);
        if (weather_cond_label_) {
            lv_obj_set_width(weather_cond_label_, 110);
            lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -14, 14);
        }
        if (weather_range_label_) {
            lv_obj_set_width(weather_range_label_, 110);
            lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_RIGHT, -14, -14);
        }

        if (weather_stats_card_) {
            lv_obj_set_size(weather_stats_card_, 224, 72);
            lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_MID, 0, 154);
        }
        if (weather_humid_label_) lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 12, 12);
        if (weather_press_label_) lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 12, 42);

        if (weather_loc_card_) {
            lv_obj_set_size(weather_loc_card_, 224, 30);
            lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_MID, 0, 232);
        }
        if (weather_loc_label_) {
            lv_obj_set_width(weather_loc_label_, 200);
            lv_obj_center(weather_loc_label_);
        }
    }

    // 4. Music Screen
    if (is_landscape) {
        if (music_header_card_) {
            lv_obj_set_size(music_header_card_, 304, 24);
            lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (music_card_) {
            lv_obj_set_size(music_card_, 154, 148);
            lv_obj_align(music_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (music_title_label_) {
            lv_obj_set_width(music_title_label_, 138);
            lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 8, 8);
        }
        if (music_artist_label_) {
            lv_obj_set_width(music_artist_label_, 138);
            lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 8, 34);
        }
        if (music_recom_label_) {
            lv_obj_set_width(music_recom_label_, 138);
            lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 8, 60);
        }
        if (music_bar_) {
            lv_obj_set_width(music_bar_, 138);
            lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (music_controls_row_) {
            lv_obj_set_size(music_controls_row_, 142, 42);
            lv_obj_align(music_controls_row_, LV_ALIGN_TOP_LEFT, 170, 68);
        }
        if (music_prev_btn_) lv_obj_set_size(music_prev_btn_, 38, 38);
        if (music_play_btn_) lv_obj_set_size(music_play_btn_, 56, 38);
        if (music_next_btn_) lv_obj_set_size(music_next_btn_, 38, 38);

        if (music_vol_row_) {
            lv_obj_set_size(music_vol_row_, 142, 38);
            lv_obj_align(music_vol_row_, LV_ALIGN_TOP_LEFT, 170, 126);
        }
        if (music_vol_down_btn_) lv_obj_set_size(music_vol_down_btn_, 67, 34);
        if (music_vol_up_btn_) lv_obj_set_size(music_vol_up_btn_, 67, 34);
    } else {
        if (music_header_card_) {
            lv_obj_set_size(music_header_card_, 220, 26);
            lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 38);
        }
        if (music_card_) {
            lv_obj_set_size(music_card_, 220, 98);
            lv_obj_align(music_card_, LV_ALIGN_TOP_MID, 0, 70);
        }
        if (music_title_label_) {
            lv_obj_set_width(music_title_label_, 200);
            lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 10, 8);
        }
        if (music_artist_label_) {
            lv_obj_set_width(music_artist_label_, 200);
            lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 10, 30);
        }
        if (music_recom_label_) {
            lv_obj_set_width(music_recom_label_, 200);
            lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 10, 52);
        }
        if (music_bar_) {
            lv_obj_set_width(music_bar_, 200);
            lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (music_controls_row_) {
            lv_obj_set_size(music_controls_row_, 220, 40);
            lv_obj_align(music_controls_row_, LV_ALIGN_TOP_MID, 0, 176);
        }
        if (music_prev_btn_) lv_obj_set_size(music_prev_btn_, 60, 38);
        if (music_play_btn_) lv_obj_set_size(music_play_btn_, 86, 38);
        if (music_next_btn_) lv_obj_set_size(music_next_btn_, 60, 38);

        if (music_vol_row_) {
            lv_obj_set_size(music_vol_row_, 220, 34);
            lv_obj_align(music_vol_row_, LV_ALIGN_TOP_MID, 0, 224);
        }
        if (music_vol_down_btn_) lv_obj_set_size(music_vol_down_btn_, 105, 32);
        if (music_vol_up_btn_) lv_obj_set_size(music_vol_up_btn_, 105, 32);
    }

    // 5. Status Screen
    if (is_landscape) {
        if (status_header_card_) {
            lv_obj_set_size(status_header_card_, 304, 24);
            lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (status_bat_card_) {
            lv_obj_set_size(status_bat_card_, 150, 66);
            lv_obj_align(status_bat_card_, LV_ALIGN_TOP_LEFT, 8, 56);
        }
        if (status_phone_bat_title_) lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 8, 8);
        if (status_phone_bat_label_) lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -8, 8);
        if (status_bat_bar_) {
            lv_obj_set_width(status_bat_bar_, 134);
            lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (status_net_card_) {
            lv_obj_set_size(status_net_card_, 150, 82);
            lv_obj_align(status_net_card_, LV_ALIGN_TOP_LEFT, 8, 126);
        }
        if (status_network_label_) lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 8, 6);
        if (status_signal_label_) lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 8, 26);
        if (status_net_hint_label_) lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 8, 46);

        if (status_find_phone_btn_) {
            lv_obj_set_size(status_find_phone_btn_, 150, 66);
            lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_LEFT, 162, 56);
        }
        if (status_bt_card_) {
            lv_obj_set_size(status_bt_card_, 150, 82);
            lv_obj_align(status_bt_card_, LV_ALIGN_TOP_LEFT, 162, 126);
        }
    } else {
        if (status_header_card_) {
            lv_obj_set_size(status_header_card_, 224, 26);
            lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (status_bat_card_) {
            lv_obj_set_size(status_bat_card_, 224, 62);
            lv_obj_align(status_bat_card_, LV_ALIGN_TOP_MID, 0, 66);
        }
        if (status_phone_bat_title_) lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 12, 10);
        if (status_phone_bat_label_) lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -12, 10);
        if (status_bat_bar_) {
            lv_obj_set_width(status_bat_bar_, 200);
            lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (status_net_card_) {
            lv_obj_set_size(status_net_card_, 224, 66);
            lv_obj_align(status_net_card_, LV_ALIGN_TOP_MID, 0, 132);
        }
        if (status_network_label_) lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 12, 8);
        if (status_signal_label_) lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 12, 28);
        if (status_net_hint_label_) lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 12, 48);

        if (status_find_phone_btn_) {
            lv_obj_set_size(status_find_phone_btn_, 224, 36);
            lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_MID, 0, 202);
        }
        if (status_bt_card_) {
            lv_obj_set_size(status_bt_card_, 224, 32);
            lv_obj_align(status_bt_card_, LV_ALIGN_TOP_MID, 0, 242);
        }
    }

    // 6. Notification Screen
    if (is_landscape) {
        if (notif_header_badge_) {
            lv_obj_set_size(notif_header_badge_, 304, 24);
            lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (notif_card_) {
            lv_obj_set_size(notif_card_, 304, 148);
            lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 58);
        }
        if (notif_title_label_) lv_obj_set_width(notif_title_label_, 284);
        if (notif_line_) lv_obj_set_width(notif_line_, 284);
        if (notif_msg_label_) lv_obj_set_width(notif_msg_label_, 284);
        if (notif_icon_label_) lv_obj_align(notif_icon_label_, LV_ALIGN_BOTTOM_MID, 0, -4);
    } else {
        if (notif_header_badge_) {
            lv_obj_set_size(notif_header_badge_, 216, 28);
            lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (notif_card_) {
            lv_obj_set_size(notif_card_, 216, 170);
            lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 68);
        }
        if (notif_title_label_) lv_obj_set_width(notif_title_label_, 196);
        if (notif_line_) lv_obj_set_width(notif_line_, 196);
        if (notif_msg_label_) lv_obj_set_width(notif_msg_label_, 196);
        if (notif_icon_label_) lv_obj_align(notif_icon_label_, LV_ALIGN_TOP_MID, 0, 248);
    }

    // 7. Alert Screen
    if (alert_card_) {
        lv_obj_set_size(alert_card_, is_landscape ? 280 : 216, is_landscape ? 150 : 180);
        lv_obj_align(alert_card_, LV_ALIGN_CENTER, 0, 0);
    }
    if (alert_msg_label_) lv_obj_set_width(alert_msg_label_, is_landscape ? 254 : 190);
    if (alert_btn_) lv_obj_set_width(alert_btn_, is_landscape ? 254 : 190);

    // 8. QR Screen
    if (is_landscape) {
        if (qr_card_) {
            lv_obj_set_size(qr_card_, 142, 142);
            lv_obj_align(qr_card_, LV_ALIGN_TOP_LEFT, 14, 32);
        }
        if (qr_header_card_) {
            lv_obj_set_size(qr_header_card_, 142, 26);
            lv_obj_align(qr_header_card_, LV_ALIGN_TOP_LEFT, 170, 32);
        }
        if (qr_info_card_) {
            lv_obj_set_size(qr_info_card_, 142, 110);
            lv_obj_align(qr_info_card_, LV_ALIGN_TOP_LEFT, 170, 64);
        }
        if (qr_name_label_) lv_obj_set_width(qr_name_label_, 138);
        if (qr_mac_label_) lv_obj_set_width(qr_mac_label_, 138);
        if (qr_clock_btn_) lv_obj_add_flag(qr_clock_btn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (qr_header_card_) {
            lv_obj_set_size(qr_header_card_, 220, 24);
            lv_obj_align(qr_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (qr_card_) {
            lv_obj_set_size(qr_card_, 142, 142);
            lv_obj_align(qr_card_, LV_ALIGN_TOP_MID, 0, 63);
        }
        if (qr_info_card_) {
            lv_obj_set_size(qr_info_card_, 224, 36);
            lv_obj_align(qr_info_card_, LV_ALIGN_TOP_MID, 0, 207);
        }
        if (qr_name_label_) lv_obj_set_width(qr_name_label_, 220);
        if (qr_mac_label_) lv_obj_set_width(qr_mac_label_, 220);
        if (qr_clock_btn_) lv_obj_clear_flag(qr_clock_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    if (recent_apps_modal_) {
        ShowRecentAppsModal();
    }
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_screen_active());
}

// ---- UNIFIED CHRONCHI BOTTOM NAVIGATION BAR (Layaknya HP: Back, Home, Recent Apps) ----
void ChronchiDisplay::createBottomNav() {
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    chronchi_bottom_nav_ = lv_obj_create(top_layer);
    lv_obj_set_size(chronchi_bottom_nav_, width_, 30);
    lv_obj_align(chronchi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chronchi_bottom_nav_, lv_color_hex(0x0A0F1D), 0);
    lv_obj_set_style_bg_opa(chronchi_bottom_nav_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chronchi_bottom_nav_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_side(chronchi_bottom_nav_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(chronchi_bottom_nav_, 1, 0);
    lv_obj_set_style_radius(chronchi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_all(chronchi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_left(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_right(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_column(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_flex_flow(chronchi_bottom_nav_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chronchi_bottom_nav_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chronchi_bottom_nav_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Back Button (◀)
    lv_obj_t* back_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(back_btn, 26);
    lv_obj_set_flex_grow(back_btn, 1);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(back_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            if (disp->IsRecentAppsVisible()) {
                disp->CloseRecentAppsModal();
                return;
            }
            if (disp->getScreenState() != HOME) {
                disp->setScreenState(HOME);
                return;
            }
        }
        Board::GetInstance().SwitchToMode(2); // Kembali ke Nova Mini OS
    }, LV_EVENT_CLICKED, this);

    // 2. Home Button (●)
    lv_obj_t* home_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(home_btn, 26);
    lv_obj_set_flex_grow(home_btn, 1);
    lv_obj_set_style_radius(home_btn, 6, 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(home_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(home_btn, 0, 0);
    lv_obj_set_style_pad_all(home_btn, 0, 0);
    lv_obj_set_style_shadow_width(home_btn, 0, 0);
    lv_obj_t* home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, MATERIAL_SYMBOLS_HOME);
    lv_obj_set_style_text_font(home_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(home_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(home_lbl);
    lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().SwitchToMode(2); // Langsung ke Beranda Nova Mini OS
    }, LV_EVENT_CLICKED, nullptr);

    // 3. Recent Apps Button (▢)
    lv_obj_t* recent_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(recent_btn, 26);
    lv_obj_set_flex_grow(recent_btn, 1);
    lv_obj_set_style_radius(recent_btn, 6, 0);
    lv_obj_set_style_bg_opa(recent_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(recent_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(recent_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(recent_btn, 0, 0);
    lv_obj_set_style_pad_all(recent_btn, 0, 0);
    lv_obj_set_style_shadow_width(recent_btn, 0, 0);

    lv_obj_t* sq_shape = lv_obj_create(recent_btn);
    lv_obj_set_size(sq_shape, 14, 14);
    lv_obj_center(sq_shape);
    lv_obj_clear_flag(sq_shape, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(sq_shape, 3, 0);
    lv_obj_set_style_bg_opa(sq_shape, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sq_shape, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_border_width(sq_shape, 2, 0);
    lv_obj_set_style_pad_all(sq_shape, 0, 0);
    lv_obj_set_style_shadow_width(sq_shape, 0, 0);

    lv_obj_add_event_cb(recent_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            disp->ShowRecentAppsModal();
        }
    }, LV_EVENT_CLICKED, this);
}

void ChronchiDisplay::hideOverlays() {
    if (overlay_backdrop_) lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_) lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_) lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
}

void ChronchiDisplay::showVolumeOverlay() {
    lv_lock();
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) {
        lv_unlock();
        return;
    }

    if (!overlay_backdrop_) {
        overlay_backdrop_ = lv_obj_create(top_layer);
        lv_obj_set_size(overlay_backdrop_, width_, height_);
        lv_obj_set_pos(overlay_backdrop_, 0, 0);
        lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
        lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
        lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
        lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay_backdrop_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(overlay_backdrop_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->hideOverlays();
        }, LV_EVENT_CLICKED, this);
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x141E33;
    uint32_t accent_hex = synced ? synced->accent_color : 0x00E5FF;

    if (!vol_overlay_card_) {
        vol_overlay_card_ = lv_obj_create(top_layer);
        lv_obj_set_size(vol_overlay_card_, 120, 195);
        lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, (width_ > height_) ? 30 : 36);
        lv_obj_set_style_radius(vol_overlay_card_, 14, 0);
        lv_obj_set_style_bg_opa(vol_overlay_card_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(vol_overlay_card_, 2, 0);
        lv_obj_set_style_pad_all(vol_overlay_card_, 8, 0);
        lv_obj_set_style_pad_row(vol_overlay_card_, 4, 0);
        lv_obj_set_flex_flow(vol_overlay_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(vol_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(vol_overlay_card_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(vol_overlay_card_, LV_OBJ_FLAG_SCROLLABLE);

        // Header: Icon + Value
        lv_obj_t* vol_hdr = lv_obj_create(vol_overlay_card_);
        lv_obj_set_size(vol_hdr, 104, 26);
        lv_obj_set_style_bg_opa(vol_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(vol_hdr, 0, 0);
        lv_obj_set_style_pad_all(vol_hdr, 0, 0);
        lv_obj_set_flex_flow(vol_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(vol_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(vol_hdr, 6, 0);

        vol_overlay_icon_ = lv_label_create(vol_hdr);
        lv_label_set_text(vol_overlay_icon_, MATERIAL_SYMBOLS_VOLUME_UP);
        lv_obj_set_style_text_font(vol_overlay_icon_, &font_material_symbols_20_4, 0);

        auto codec = Board::GetInstance().GetAudioCodec();
        int cur_vol = codec ? codec->output_volume() : 75;

        vol_overlay_label_ = lv_label_create(vol_hdr);
        lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
        lv_obj_set_style_text_font(vol_overlay_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vol_overlay_label_, lv_color_white(), 0);

        // Large Vertical Slider: 46px wide x 120px tall
        vol_overlay_slider_ = lv_slider_create(vol_overlay_card_);
        lv_obj_set_size(vol_overlay_slider_, 46, 120);
        lv_slider_set_range(vol_overlay_slider_, 0, 100);
        lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
        lv_obj_set_style_radius(vol_overlay_slider_, 10, 0);
        lv_obj_set_style_radius(vol_overlay_slider_, 10, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(vol_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(vol_overlay_slider_, 0, LV_PART_KNOB);

        lv_obj_add_event_cb(vol_overlay_slider_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto c = Board::GetInstance().GetAudioCodec();
            if (c) c->SetOutputVolume(val);
            Settings s("audio", true);
            s.SetInt("output_volume", val);
            s.SetInt("volume", val);
            Settings nova_s("nova", true);
            nova_s.SetInt("mute", val == 0 ? 1 : 0);
            if (disp && disp->vol_overlay_label_) {
                lv_label_set_text_fmt(disp->vol_overlay_label_, "%d%%", val);
            }
            if (disp && disp->vol_overlay_icon_) {
                lv_label_set_text(disp->vol_overlay_icon_,
                    val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
            }
            if (disp && disp->navbar_vol_label_) {
                lv_label_set_text(disp->navbar_vol_label_, val == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(disp->navbar_vol_label_, val == 0 ? lv_color_hex(0x71717A) : lv_color_hex(0x06B6D4), 0);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* vol_hint = lv_label_create(vol_overlay_card_);
        lv_label_set_text(vol_hint, "▲ Geser Ke Atas/Bawah ▼");
        lv_obj_set_style_text_color(vol_hint, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(vol_hint, &lv_font_montserrat_14, 0);
    }

    // Refresh dynamic theme colors
    lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(border_hex), 0);
    if (vol_overlay_slider_) {
        lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(accent_hex), LV_PART_INDICATOR);
    }
    if (vol_overlay_icon_) {
        lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(accent_hex), 0);
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 75;
    if (vol_overlay_slider_) lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    if (vol_overlay_label_) lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (vol_overlay_icon_) {
        lv_label_set_text(vol_overlay_icon_, cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    }

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(vol_overlay_card_);
    lv_unlock();
}

void ChronchiDisplay::showBrightnessOverlay() {
    lv_lock();
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) {
        lv_unlock();
        return;
    }

    if (!overlay_backdrop_) {
        overlay_backdrop_ = lv_obj_create(top_layer);
        lv_obj_set_size(overlay_backdrop_, width_, height_);
        lv_obj_set_pos(overlay_backdrop_, 0, 0);
        lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
        lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
        lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
        lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay_backdrop_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(overlay_backdrop_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->hideOverlays();
        }, LV_EVENT_CLICKED, this);
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x141E33;

    if (!bright_overlay_card_) {
        bright_overlay_card_ = lv_obj_create(top_layer);
        lv_obj_set_size(bright_overlay_card_, 120, 195);
        lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, (width_ > height_) ? 30 : 36);
        lv_obj_set_style_radius(bright_overlay_card_, 14, 0);
        lv_obj_set_style_bg_opa(bright_overlay_card_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(bright_overlay_card_, 2, 0);
        lv_obj_set_style_pad_all(bright_overlay_card_, 8, 0);
        lv_obj_set_style_pad_row(bright_overlay_card_, 4, 0);
        lv_obj_set_flex_flow(bright_overlay_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(bright_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(bright_overlay_card_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(bright_overlay_card_, LV_OBJ_FLAG_SCROLLABLE);

        // Header: Icon + Value
        lv_obj_t* br_hdr = lv_obj_create(bright_overlay_card_);
        lv_obj_set_size(br_hdr, 104, 26);
        lv_obj_set_style_bg_opa(br_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(br_hdr, 0, 0);
        lv_obj_set_style_pad_all(br_hdr, 0, 0);
        lv_obj_set_flex_flow(br_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(br_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(br_hdr, 6, 0);

        lv_obj_t* br_icon = lv_label_create(br_hdr);
        lv_label_set_text(br_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
        lv_obj_set_style_text_font(br_icon, &font_material_symbols_20_4, 0);
        lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

        Settings disp_set("display", true);
        int cur_br = disp_set.GetInt("brightness", 70);

        bright_overlay_label_ = lv_label_create(br_hdr);
        lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);
        lv_obj_set_style_text_font(bright_overlay_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(bright_overlay_label_, lv_color_white(), 0);

        // Large Vertical Slider: 46px wide x 120px tall
        bright_overlay_slider_ = lv_slider_create(bright_overlay_card_);
        lv_obj_set_size(bright_overlay_slider_, 46, 120);
        lv_slider_set_range(bright_overlay_slider_, 10, 100);
        lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
        lv_obj_set_style_radius(bright_overlay_slider_, 10, 0);
        lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bright_overlay_slider_, 10, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bright_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(bright_overlay_slider_, 0, LV_PART_KNOB);

        lv_obj_add_event_cb(bright_overlay_slider_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto bl = Board::GetInstance().GetBacklight();
            if (bl) bl->SetBrightness(val);
            Settings s("display", true);
            s.SetInt("brightness", val);
            Settings nova_s("nova", true);
            nova_s.SetInt("bright", (val * 255) / 100);
            if (disp && disp->bright_overlay_label_) {
                lv_label_set_text_fmt(disp->bright_overlay_label_, "%d%%", val);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* br_hint = lv_label_create(bright_overlay_card_);
        lv_label_set_text(br_hint, "▲ Geser Ke Atas/Bawah ▼");
        lv_obj_set_style_text_color(br_hint, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(br_hint, &lv_font_montserrat_14, 0);
    }

    // Refresh dynamic theme colors
    lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(border_hex), 0);
    if (bright_overlay_slider_) {
        lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(panel2_hex), 0);
    }

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);
    if (bright_overlay_slider_) lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    if (bright_overlay_label_) lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(bright_overlay_card_);
    lv_unlock();
}

void ChronchiDisplay::CloseRecentAppsModal() {
    if (recent_apps_modal_ != nullptr) {
        lv_obj_del(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
}

void ChronchiDisplay::ShowRecentAppsModal() {
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x0D1728;
    uint32_t accent_hex = synced ? synced->accent_color : 0x06B6D4;

    // 1. Full-screen dimmed backdrop
    recent_apps_modal_ = lv_obj_create(top_layer);
    lv_obj_set_pos(recent_apps_modal_, 0, 0);
    lv_obj_set_size(recent_apps_modal_, width_, height_);
    lv_obj_clear_flag(recent_apps_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(recent_apps_modal_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(recent_apps_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(recent_apps_modal_, 0, 0);
    lv_obj_set_style_radius(recent_apps_modal_, 0, 0);
    lv_obj_set_style_pad_all(recent_apps_modal_, 0, 0);

    // 2. Centered Floating Modal Card
    int card_w = (width_ > 240) ? 280 : 226;
    int card_h = (height_ < 260) ? (height_ - 16) : 252;
    lv_obj_t* card = lv_obj_create(recent_apps_modal_);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header Title
    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, "APLIKASI TERBUKA");
    lv_obj_set_pos(title_lbl, 8, 4);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(accent_hex), 0);

    lv_obj_t* sub_lbl = lv_label_create(card);
    lv_label_set_text(sub_lbl, "Ketuk untuk beralih mode instan");
    lv_obj_set_pos(sub_lbl, 8, 22);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x64748B), 0);

    // Close button (X)
    lv_obj_t* close_btn = lv_button_create(card);
    lv_obj_set_pos(close_btn, card_w - 34, 2);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->CloseRecentAppsModal();
    }, LV_EVENT_CLICKED, this);

    // Scrollable container for App Cards
    int list_h = (card_h < 250) ? (card_h - 78) : 160;
    lv_obj_t* list = lv_obj_create(card);
    lv_obj_set_pos(list, 4, 38);
    lv_obj_set_size(list, card_w - 20, list_h);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    struct AppCardItem {
        const char* name;
        const char* desc;
        const char* icon;
        lv_color_t color;
        int action; // 0: xz, 1: watch (close), 2: nova os
    };

    const AppCardItem items[] = {
        {"Nova Mini OS", "Beranda & Dashboard HP", MATERIAL_SYMBOLS_HOME, lv_color_hex(0x06B6D4), 2},
        {"Xiaozhi AI", "Asisten Suara & Musik LLM", MATERIAL_SYMBOLS_ROBOT_2, lv_color_hex(0x22C55E), 0},
        {"Chronchi Watch", "Smartwatch & Jam [Aktif]", MATERIAL_SYMBOLS_WATCH, lv_color_hex(0xF59E0B), 1}
    };

    const int total_items = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < total_items; ++i) {
        lv_obj_t* app_card = lv_button_create(list);
        lv_obj_set_pos(app_card, 0, i * 44);
        lv_obj_set_size(app_card, card_w - 26, 38);
        lv_obj_set_style_bg_color(app_card, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_border_color(app_card, lv_color_hex(border_hex), 0);
        lv_obj_set_style_border_width(app_card, 1, 0);
        lv_obj_set_style_radius(app_card, 10, 0);
        lv_obj_set_style_pad_all(app_card, 0, 0);

        // App Icon
        lv_obj_t* icon_lbl = lv_label_create(app_card);
        lv_label_set_text(icon_lbl, items[i].icon);
        lv_obj_set_style_text_font(icon_lbl, &font_material_symbols_20_4, 0);
        lv_obj_set_style_text_color(icon_lbl, items[i].color, 0);
        lv_obj_set_pos(icon_lbl, 8, 9);

        // Titles
        lv_obj_t* c_title = lv_label_create(app_card);
        lv_label_set_text(c_title, items[i].name);
        lv_obj_set_pos(c_title, 36, 4);
        lv_obj_set_size(c_title, card_w - 92, 17);
        lv_label_set_long_mode(c_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_title, lv_color_white(), 0);

        lv_obj_t* c_desc = lv_label_create(app_card);
        lv_label_set_text(c_desc, items[i].desc);
        lv_obj_set_pos(c_desc, 36, 20);
        lv_obj_set_size(c_desc, card_w - 92, 14);
        lv_label_set_long_mode(c_desc, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_desc, lv_color_hex(0x64748B), 0);

        // Right arrow
        lv_obj_t* arr = lv_label_create(app_card);
        lv_label_set_text(arr, ">");
        lv_obj_set_pos(arr, card_w - 48, 10);
        lv_obj_set_style_text_color(arr, items[i].color, 0);

        lv_obj_set_user_data(app_card, (void*)(intptr_t)items[i].action);
        lv_obj_add_event_cb(app_card, [](lv_event_t* ev) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(ev));
            if (!disp) return;
            int act = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
            disp->CloseRecentAppsModal();
            if (act == 0) {
                Board::GetInstance().SwitchToMode(0);
            } else if (act == 1) {
                // Already in Watch
            } else if (act == 2) {
                Board::GetInstance().SwitchToMode(2);
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Bottom Button: KEMBALI KE BERANDA NOVA
    lv_obj_t* home_btn_bar = lv_button_create(card);
    lv_obj_set_pos(home_btn_bar, 4, card_h - 40);
    lv_obj_set_size(home_btn_bar, card_w - 22, 30);
    lv_obj_set_style_bg_color(home_btn_bar, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(home_btn_bar, lv_color_hex(accent_hex), 0);
    lv_obj_set_style_border_width(home_btn_bar, 1, 0);
    lv_obj_set_style_radius(home_btn_bar, 8, 0);
    lv_obj_set_style_pad_all(home_btn_bar, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(home_btn_bar);
    lv_label_set_text(btn_lbl, "KEMBALI KE BERANDA NOVA");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(home_btn_bar, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            disp->CloseRecentAppsModal();
            Board::GetInstance().SwitchToMode(2);
        }
    }, LV_EVENT_CLICKED, this);
}
