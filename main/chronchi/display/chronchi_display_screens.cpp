#include "chronchi_display_internal.h"

// ---- BORDERLESS AUTHENTIC CHRONCHI WATCHFACE ----
void ChronchiDisplay::createHomeScreen() {
    home_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(home_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(home_screen_, width_, height_);
    lv_obj_set_style_bg_opa(home_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_screen_, 0, 0);
    lv_obj_set_style_radius(home_screen_, 0, 0);
    lv_obj_set_style_pad_all(home_screen_, 0, 0);
    lv_obj_clear_flag(home_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Watchface Minimalis Chronos:
    // Tampilan simple, bersih, elegan: Jam:Menit:Detik + Hari, Tanggal Bulan Tahun
    home_time_card_ = create_card(home_screen_, 224, 136, 16);
    lv_obj_align(home_time_card_, LV_ALIGN_CENTER, 0, -4);

    home_time_label_ = lv_label_create(home_time_card_);
    lv_label_set_text(home_time_label_, "00:00:00");
    lv_obj_set_style_text_font(home_time_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(home_time_label_, LV_ALIGN_CENTER, 0, -20);

    // Garis aksen tipis elegan pembatas jam dan tanggal
    home_date_card_ = lv_obj_create(home_time_card_);
    lv_obj_set_size(home_date_card_, 120, 2);
    lv_obj_align(home_date_card_, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_opa(home_date_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_date_card_, 0, 0);
    lv_obj_clear_flag(home_date_card_, LV_OBJ_FLAG_SCROLLABLE);

    home_date_label_ = lv_label_create(home_time_card_);
    lv_label_set_text(home_date_label_, "--, -- --- ----");
    lv_obj_set_style_text_font(home_date_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(home_date_label_, LV_ALIGN_CENTER, 0, 26);

    // Layar / kartu jam bersih dari single click agar gestur double-tap layar utama responsif
    lv_obj_add_flag(home_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    lv_obj_add_flag(home_time_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_time_card_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Bersihkan pointer widget yang dihilangkan agar tidak ada sisa elemen
    home_chronchi_card_ = nullptr;
    home_chronchi_label_ = nullptr;
    home_bt_card_ = nullptr;
    home_bt_label_ = nullptr;
    home_batt_card_ = nullptr;
    home_batt_label_ = nullptr;
    home_batt_bar_ = nullptr;
    home_batt_bar_fill_ = nullptr;
    home_status_card_ = nullptr;
    home_status_label_ = nullptr;
}

// ---- QR PAIRING / BARCODE SCREEN ----
void ChronchiDisplay::createQrScreen() {
    qr_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(qr_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(qr_screen_, width_, height_);
    lv_obj_set_style_bg_opa(qr_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_screen_, 0, 0);
    lv_obj_set_style_radius(qr_screen_, 0, 0);
    lv_obj_set_style_pad_all(qr_screen_, 0, 0);
    lv_obj_clear_flag(qr_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Header Capsule (below the 34px top navbar)
    qr_header_card_ = create_card(qr_screen_, 220, 24, 12);
    lv_obj_align(qr_header_card_, LV_ALIGN_TOP_MID, 0, 36);
    qr_header_label_ = lv_label_create(qr_header_card_);
    lv_label_set_text(qr_header_label_, LV_SYMBOL_BLUETOOTH " Scan QR Chronos");
    lv_obj_set_style_text_font(qr_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(qr_header_label_);

    // 2. White Card Container for QR Code / Barcode (High contrast for easy camera scan)
    qr_card_ = lv_obj_create(qr_screen_);
    lv_obj_set_size(qr_card_, 142, 142);
    lv_obj_align(qr_card_, LV_ALIGN_TOP_MID, 0, 63);
    lv_obj_set_style_radius(qr_card_, 10, 0);
    lv_obj_set_style_bg_color(qr_card_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_card_, 2, 0);
    lv_obj_set_style_border_color(qr_card_, lv_color_hex(0x06B6D4), 0);
    lv_obj_set_style_pad_all(qr_card_, 4, 0);
    lv_obj_clear_flag(qr_card_, LV_OBJ_FLAG_SCROLLABLE);

    qr_code_obj_ = lv_image_create(qr_card_);
    if (qr_img_buf_) {
        lv_image_set_src(qr_code_obj_, &qr_img_dsc_);
    }
    lv_obj_center(qr_code_obj_);
    renderQrCode(qr_data_.c_str());

    // 3. Info Container (Device Name & MAC Address)
    qr_info_card_ = lv_obj_create(qr_screen_);
    lv_obj_set_size(qr_info_card_, 224, 36);
    lv_obj_align(qr_info_card_, LV_ALIGN_TOP_MID, 0, 207);
    lv_obj_set_style_bg_opa(qr_info_card_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qr_info_card_, 0, 0);
    lv_obj_set_style_pad_all(qr_info_card_, 0, 0);
    lv_obj_set_flex_flow(qr_info_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_info_card_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(qr_info_card_, 1, 0);
    lv_obj_clear_flag(qr_info_card_, LV_OBJ_FLAG_SCROLLABLE);

    qr_name_label_ = lv_label_create(qr_info_card_);
    char n_buf[64];
    snprintf(n_buf, sizeof(n_buf), "Nama BLE: %s", ble_device_name_.c_str());
    lv_label_set_text(qr_name_label_, n_buf);
    lv_obj_set_style_text_font(qr_name_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qr_name_label_, lv_color_hex(0x38BDF8), 0);

    qr_mac_label_ = lv_label_create(qr_info_card_);
    char m_buf[64];
    snprintf(m_buf, sizeof(m_buf), "MAC: %s", ble_mac_address_.c_str());
    lv_label_set_text(qr_mac_label_, m_buf);
    lv_obj_set_style_text_font(qr_mac_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qr_mac_label_, lv_color_hex(0x94A3B8), 0);

    // 4. Quick Clock Button (strictly above bottom navbar at y=284)
    qr_clock_btn_ = lv_button_create(qr_screen_);
    lv_obj_set_size(qr_clock_btn_, 140, 26);
    lv_obj_align(qr_clock_btn_, LV_ALIGN_TOP_MID, 0, 246);
    lv_obj_set_style_radius(qr_clock_btn_, 13, 0);
    lv_obj_set_style_bg_color(qr_clock_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(qr_clock_btn_, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(qr_clock_btn_);
    lv_label_set_text(btn_lbl, "🕒 Lihat Jam");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(qr_clock_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showHome();
    }, LV_EVENT_CLICKED, this);
}

void ChronchiDisplay::renderQrCode(const char* text) {
    if (!qr_img_buf_ || !text || strlen(text) == 0) return;

    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    bool ok = qrcodegen_encodeText(text, tempBuffer, qrcode,
                                   qrcodegen_Ecc_LOW, 1, 10,
                                   qrcodegen_Mask_AUTO, true);
    if (!ok) {
        ESP_LOGE(TAG, "Failed to encode QR code for: %s", text);
        return;
    }

    int qr_size = qrcodegen_getSize(qrcode);
    int target_w = 140;
    int target_h = 140;

    // Fill background with pure white (0xFFFF in RGB565)
    uint16_t* pixels = (uint16_t*)qr_img_buf_;
    for (int i = 0; i < target_w * target_h; i++) {
        pixels[i] = 0xFFFF;
    }

    // Border / Quiet zone: 2 modules
    int border = 2;
    int total_modules = qr_size + border * 2;
    int scale = target_w / total_modules;
    if (scale < 1) scale = 1;
    int offset_x = (target_w - (total_modules * scale)) / 2 + border * scale;
    int offset_y = (target_h - (total_modules * scale)) / 2 + border * scale;

    // Draw black modules (0x0000 in RGB565)
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                int px_start = offset_x + x * scale;
                int py_start = offset_y + y * scale;
                for (int dy = 0; dy < scale; dy++) {
                    int py = py_start + dy;
                    if (py < 0 || py >= target_h) continue;
                    for (int dx = 0; dx < scale; dx++) {
                        int px = px_start + dx;
                        if (px < 0 || px >= target_w) continue;
                        pixels[py * target_w + px] = 0x0000;
                    }
                }
            }
        }
    }

    if (qr_code_obj_) {
        lv_image_set_src(qr_code_obj_, &qr_img_dsc_);
        lv_obj_invalidate(qr_code_obj_);
    }
}

void ChronchiDisplay::showQrPairing() {
    lv_lock();
    if (qr_screen_) {
        setScreenState(QR_PAIRING);
        ESP_LOGI(TAG, "Showing Chronchi QR Code / Barcode Pairing Screen");
    }
    lv_unlock();
}

void ChronchiDisplay::setBleInfo(const char* name, const char* mac, const char* qr_data) {
    if (name && strlen(name) > 0) ble_device_name_ = name;
    if (mac && strlen(mac) > 0) ble_mac_address_ = mac;
    if (qr_data && strlen(qr_data) > 0) qr_data_ = qr_data;

    lv_lock();
    if (qr_name_label_ && lv_obj_is_valid(qr_name_label_)) {
        char n_buf[64];
        snprintf(n_buf, sizeof(n_buf), "Nama BLE: %s", ble_device_name_.c_str());
        lv_label_set_text(qr_name_label_, n_buf);
    }
    if (qr_mac_label_ && lv_obj_is_valid(qr_mac_label_)) {
        char m_buf[64];
        snprintf(m_buf, sizeof(m_buf), "MAC: %s", ble_mac_address_.c_str());
        lv_label_set_text(qr_mac_label_, m_buf);
    }
    renderQrCode(qr_data_.c_str());
    lv_unlock();
}

// ---- NAVIGATION SCREEN ----
void ChronchiDisplay::createNavScreen() {
    nav_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(nav_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(nav_screen_, width_, height_);
    lv_obj_set_style_bg_opa(nav_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_screen_, 0, 0);
    lv_obj_set_style_radius(nav_screen_, 0, 0);
    lv_obj_set_style_pad_all(nav_screen_, 0, 0);
    lv_obj_clear_flag(nav_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(nav_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nav_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // 1. Header Capsule (below the 34px top navbar)
    nav_header_card_ = create_card(nav_screen_, 220, 26, 13);
    lv_obj_set_style_border_width(nav_header_card_, 0, 0);
    lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 38);
    nav_header_label_ = lv_label_create(nav_header_card_);
    lv_label_set_text(nav_header_label_, "🧭 GOOGLE MAPS");
    lv_obj_set_style_text_font(nav_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 12, 0);

    nav_time_label_ = lv_label_create(nav_header_card_);
    lv_label_set_text(nav_time_label_, "00:00");
    lv_obj_set_style_text_font(nav_time_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    // 2. Main Route & Maneuver Card (y=70, h=118, w=220)
    nav_main_card_ = create_card(nav_screen_, 220, 118, 14);
    lv_obj_set_style_border_width(nav_main_card_, 0, 0);
    lv_obj_align(nav_main_card_, LV_ALIGN_TOP_MID, 0, 70);

    if (nav_icon_buf_) {
        nav_icon_img_ = lv_image_create(nav_main_card_);
        lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
        lv_image_set_scale(nav_icon_img_, 240); // 45x45 px crisp turn arrow
        lv_obj_set_style_bg_opa(nav_icon_img_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(nav_icon_img_, 0, 0);
        lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 10, 10);
    }

    nav_dist_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_dist_label_, "-- m");
    lv_obj_set_style_text_font(nav_dist_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 68, 8);

    nav_title_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_title_label_, "📍 Rute Navigasi");
    lv_obj_set_style_text_font(nav_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(nav_title_label_, 142);
    lv_obj_set_style_text_align(nav_title_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(nav_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 68, 36);

    nav_street_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_street_label_, "Siap bernavigasi...");
    lv_obj_set_style_text_font(nav_street_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(nav_street_label_, 200);
    lv_obj_set_style_text_align(nav_street_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(nav_street_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);

    // 3. Dashboard Stats Card (y=196, h=66, w=220)
    nav_stats_card_ = create_card(nav_screen_, 220, 66, 12);
    lv_obj_set_style_border_width(nav_stats_card_, 0, 0);
    lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_MID, 0, 196);

    nav_dur_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_dur_label_, "⏱ -- min");
    lv_obj_set_style_text_font(nav_dur_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 12, 8);

    nav_eta_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_eta_label_, "🏁 ETA: --:--");
    lv_obj_set_style_text_font(nav_eta_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_RIGHT, -12, 8);

    nav_speed_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_speed_label_, "⚡ Kecepatan: -- km/h");
    lv_obj_set_style_text_font(nav_speed_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_speed_label_, LV_ALIGN_BOTTOM_LEFT, 12, -8);
}

// ---- NOTIFICATION SCREEN ----
void ChronchiDisplay::createNotifScreen() {
    notif_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(notif_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(notif_screen_, width_, height_);
    lv_obj_set_style_bg_opa(notif_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notif_screen_, 0, 0);
    lv_obj_set_style_radius(notif_screen_, 0, 0);
    lv_obj_set_style_pad_all(notif_screen_, 0, 0);
    lv_obj_clear_flag(notif_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // App Header Badge Capsule (below the 34px top navbar)
    notif_header_badge_ = create_card(notif_screen_, 216, 28, 12);
    lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 36);

    notif_app_label_ = lv_label_create(notif_header_badge_);
    lv_label_set_text(notif_app_label_, "💬 NOTIFIKASI");
    lv_obj_set_style_text_font(notif_app_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_app_label_, LV_ALIGN_LEFT_MID, 8, 0);

    notif_time_label_ = lv_label_create(notif_header_badge_);
    lv_label_set_text(notif_time_label_, "Baru saja");
    lv_obj_set_style_text_font(notif_time_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_time_label_, LV_ALIGN_RIGHT_MID, -8, 0);

    // Main Notification Card (ends at y=238)
    notif_card_ = create_card(notif_screen_, 216, 170, 14);
    lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 68);

    notif_title_label_ = lv_label_create(notif_card_);
    lv_label_set_text(notif_title_label_, "Pengirim");
    lv_obj_set_style_text_font(notif_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(notif_title_label_, 196);
    lv_label_set_long_mode(notif_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(notif_title_label_, LV_ALIGN_TOP_LEFT, 8, 8);

    notif_line_ = lv_obj_create(notif_card_);
    lv_obj_set_size(notif_line_, 196, 2);
    lv_obj_align(notif_line_, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_obj_set_style_bg_opa(notif_line_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notif_line_, 0, 0);
    lv_obj_clear_flag(notif_line_, LV_OBJ_FLAG_SCROLLABLE);

    notif_msg_label_ = lv_label_create(notif_card_);
    lv_label_set_text(notif_msg_label_, "Pesan notifikasi...");
    lv_obj_set_style_text_font(notif_msg_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(notif_msg_label_, 196);
    lv_label_set_long_mode(notif_msg_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(notif_msg_label_, LV_ALIGN_TOP_LEFT, 8, 38);

    // Bottom Dismiss hint (placed safely at y=248, ends at 268)
    notif_icon_label_ = lv_label_create(notif_screen_);
    lv_label_set_text(notif_icon_label_, LV_SYMBOL_BELL " Tutup otomatis dalam 5s");
    lv_obj_set_style_text_font(notif_icon_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_icon_label_, LV_ALIGN_TOP_MID, 0, 248);
}

// ---- WEATHER SCREEN ----
void ChronchiDisplay::createWeatherScreen() {
    weather_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(weather_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(weather_screen_, width_, height_);
    lv_obj_set_style_bg_opa(weather_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weather_screen_, 0, 0);
    lv_obj_set_style_radius(weather_screen_, 0, 0);
    lv_obj_set_style_pad_all(weather_screen_, 0, 0);
    lv_obj_clear_flag(weather_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Weather App Header
    weather_header_card_ = create_card(weather_screen_, 220, 26, 13);
    lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 38);
    weather_header_label_ = lv_label_create(weather_header_card_);
    lv_label_set_text(weather_header_label_, "⛅ CUACA HARI INI");
    lv_obj_set_style_text_font(weather_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(weather_header_label_);

    // Weather Main Hero Card (y=70, h=82, w=220)
    weather_main_card_ = create_card(weather_screen_, 220, 82, 14);
    lv_obj_align(weather_main_card_, LV_ALIGN_TOP_MID, 0, 70);

    weather_temp_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_temp_label_, "--°C");
    lv_obj_set_style_text_font(weather_temp_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(weather_temp_label_, LV_ALIGN_LEFT_MID, 16, 0);

    weather_cond_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_cond_label_, "Memuat...");
    lv_obj_set_style_text_font(weather_cond_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_cond_label_, 110);
    lv_obj_set_style_text_align(weather_cond_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(weather_cond_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -14, 14);

    weather_range_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_range_label_, "H: --°  L: --°");
    lv_obj_set_style_text_font(weather_range_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_range_label_, 110);
    lv_obj_set_style_text_align(weather_range_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_RIGHT, -14, -14);

    // Weather Stats Card (y=154, h=72, w=224)
    weather_stats_card_ = create_card(weather_screen_, 224, 72, 12);
    lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_MID, 0, 154);

    weather_humid_label_ = lv_label_create(weather_stats_card_);
    lv_label_set_text(weather_humid_label_, "💧 Kelembapan: --%");
    lv_obj_set_style_text_font(weather_humid_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 12, 12);

    weather_press_label_ = lv_label_create(weather_stats_card_);
    lv_label_set_text(weather_press_label_, "⏱️ Tekanan: -- hPa");
    lv_obj_set_style_text_font(weather_press_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 12, 42);

    // Weather Location (y=232, h=30, w=224)
    weather_loc_card_ = create_card(weather_screen_, 224, 30, 10);
    lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_MID, 0, 232);
    weather_loc_label_ = lv_label_create(weather_loc_card_);
    lv_label_set_text(weather_loc_label_, "📍 Lokasi: --");
    lv_obj_set_style_text_font(weather_loc_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_loc_label_, 200);
    lv_label_set_long_mode(weather_loc_label_, LV_LABEL_LONG_DOT);
    lv_obj_center(weather_loc_label_);

    lv_obj_add_flag(weather_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weather_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);
}

// ---- MUSIC SCREEN (Chronos Spotify / YT Music) ----
void ChronchiDisplay::createMusicScreen() {
    music_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(music_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(music_screen_, width_, height_);
    lv_obj_set_style_bg_opa(music_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_screen_, 0, 0);
    lv_obj_set_style_radius(music_screen_, 0, 0);
    lv_obj_set_style_pad_all(music_screen_, 0, 0);
    lv_obj_clear_flag(music_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(music_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(music_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Music App Header (below the 34px top navbar)
    music_header_card_ = create_card(music_screen_, 220, 26, 13);
    lv_obj_set_style_border_width(music_header_card_, 0, 0);
    lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 38);

    music_header_label_ = lv_label_create(music_header_card_);
    lv_label_set_text(music_header_label_, "🎵 PEMUTAR MUSIK");
    lv_obj_set_style_text_font(music_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(music_header_label_, LV_ALIGN_LEFT_MID, 12, 0);

    music_state_label_ = lv_label_create(music_header_card_);
    lv_label_set_text(music_state_label_, "⏸ Jeda");
    lv_obj_set_style_text_font(music_state_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(music_state_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    // Main Player Card (y=70, h=98, w=220)
    music_card_ = create_card(music_screen_, 220, 98, 14);
    lv_obj_set_style_border_width(music_card_, 0, 0);
    lv_obj_align(music_card_, LV_ALIGN_TOP_MID, 0, 70);

    // 1. Judul Lagu (Continuous Scrolling Marquee Text)
    music_title_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_title_label_, "🎵 Judul: Tidak Ada Media        ");
    lv_obj_set_style_text_font(music_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_title_label_, 200);
    lv_label_set_long_mode(music_title_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 10, 8);

    // 2. Nama Penyanyi / Artis (Continuous Scrolling Marquee Text)
    music_artist_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_artist_label_, "👤 Artis: Putar di Ponsel Anda        ");
    lv_obj_set_style_text_font(music_artist_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_artist_label_, 200);
    lv_label_set_long_mode(music_artist_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 10, 30);

    // 3. Teks Rekomendasi (Continuous Scrolling Marquee Text)
    music_recom_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_recom_label_, "✨ Bluetooth Audio Connected        ");
    lv_obj_set_style_text_font(music_recom_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_recom_label_, 200);
    lv_label_set_long_mode(music_recom_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 10, 52);

    // Playback Progress Bar (Container + Fill)
    music_bar_ = create_card(music_card_, 200, 6, 3);
    lv_obj_set_style_border_width(music_bar_, 0, 0);
    lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -8);
    music_bar_fill_ = lv_obj_create(music_bar_);
    lv_obj_set_size(music_bar_fill_, 70, 6);
    lv_obj_set_style_radius(music_bar_fill_, 3, 0);
    lv_obj_set_style_bg_opa(music_bar_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_bar_fill_, 0, 0);
    lv_obj_set_style_pad_all(music_bar_fill_, 0, 0);
    lv_obj_align(music_bar_fill_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(music_bar_fill_, LV_OBJ_FLAG_SCROLLABLE);

    // Playback Controls Row: [⏮ Prev] [ ▶ Play / Pause ] [ Next ⏭ ] (y=176, h=40, w=220)
    music_controls_row_ = lv_obj_create(music_screen_);
    lv_obj_set_size(music_controls_row_, 220, 40);
    lv_obj_align(music_controls_row_, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_set_style_bg_opa(music_controls_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_controls_row_, 0, 0);
    lv_obj_set_style_pad_all(music_controls_row_, 0, 0);
    lv_obj_set_flex_flow(music_controls_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_controls_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(music_controls_row_, LV_OBJ_FLAG_SCROLLABLE);

    // Previous Button (⏮)
    music_prev_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_prev_btn_, 60, 38);
    lv_obj_set_style_radius(music_prev_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_prev_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(music_prev_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_prev_btn_, 0, 0);
    lv_obj_t* prev_lbl = lv_label_create(music_prev_btn_);
    lv_label_set_text(prev_lbl, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(prev_lbl, lv_color_white(), 0);
    lv_obj_center(prev_lbl);
    lv_obj_add_event_cb(music_prev_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9D02); // MUSIC_PREVIOUS
    }, LV_EVENT_CLICKED, this);

    // Play / Pause Button (⏯)
    music_play_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_play_btn_, 86, 38);
    lv_obj_set_style_radius(music_play_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_play_btn_, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(music_play_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_play_btn_, 0, 0);
    music_play_label_ = lv_label_create(music_play_btn_);
    lv_label_set_text(music_play_label_, LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(music_play_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(music_play_label_, lv_color_white(), 0);
    lv_obj_center(music_play_label_);
    lv_obj_add_event_cb(music_play_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9900); // MUSIC_TOGGLE
    }, LV_EVENT_CLICKED, this);

    // Next Button (⏭)
    music_next_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_next_btn_, 60, 38);
    lv_obj_set_style_radius(music_next_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_next_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(music_next_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_next_btn_, 0, 0);
    lv_obj_t* next_lbl = lv_label_create(music_next_btn_);
    lv_label_set_text(next_lbl, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(next_lbl, lv_color_white(), 0);
    lv_obj_center(next_lbl);
    lv_obj_add_event_cb(music_next_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9D03); // MUSIC_NEXT
    }, LV_EVENT_CLICKED, this);

    // Volume Row: [🔉 Vol -] [🔊 Vol +] (y=224, h=34, w=220)
    music_vol_row_ = lv_obj_create(music_screen_);
    lv_obj_set_size(music_vol_row_, 220, 34);
    lv_obj_align(music_vol_row_, LV_ALIGN_TOP_MID, 0, 224);
    lv_obj_set_style_bg_opa(music_vol_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_vol_row_, 0, 0);
    lv_obj_set_style_pad_all(music_vol_row_, 0, 0);
    lv_obj_set_flex_flow(music_vol_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_vol_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(music_vol_row_, LV_OBJ_FLAG_SCROLLABLE);

    music_vol_down_btn_ = lv_button_create(music_vol_row_);
    lv_obj_set_size(music_vol_down_btn_, 105, 32);
    lv_obj_set_style_radius(music_vol_down_btn_, 8, 0);
    lv_obj_set_style_bg_color(music_vol_down_btn_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(music_vol_down_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_vol_down_btn_, 0, 0);
    lv_obj_t* vdown_lbl = lv_label_create(music_vol_down_btn_);
    lv_label_set_text(vdown_lbl, LV_SYMBOL_VOLUME_MID " Vol −");
    lv_obj_set_style_text_font(vdown_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vdown_lbl, lv_color_hex(0xE4E4E7), 0);
    lv_obj_center(vdown_lbl);
    lv_obj_add_event_cb(music_vol_down_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x99A2); // VOLUME_DOWN
    }, LV_EVENT_CLICKED, this);

    music_vol_up_btn_ = lv_button_create(music_vol_row_);
    lv_obj_set_size(music_vol_up_btn_, 105, 32);
    lv_obj_set_style_radius(music_vol_up_btn_, 8, 0);
    lv_obj_set_style_bg_color(music_vol_up_btn_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(music_vol_up_btn_, 0, 0);
    lv_obj_set_style_border_color(music_vol_up_btn_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(music_vol_up_btn_, 0, 0);
    lv_obj_t* vup_lbl = lv_label_create(music_vol_up_btn_);
    lv_label_set_text(vup_lbl, LV_SYMBOL_VOLUME_MAX " Vol +");
    lv_obj_set_style_text_font(vup_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vup_lbl, lv_color_hex(0xE4E4E7), 0);
    lv_obj_center(vup_lbl);
    lv_obj_add_event_cb(music_vol_up_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x99A1); // VOLUME_UP
    }, LV_EVENT_CLICKED, this);

    music_back_btn_ = nullptr;
    music_hint_card_ = nullptr;
    music_hint_label_ = nullptr;
}

// ---- STATUS SCREEN (Phone & Network) ----
void ChronchiDisplay::createStatusScreen() {
    status_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(status_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(status_screen_, width_, height_);
    lv_obj_set_style_bg_opa(status_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_screen_, 0, 0);
    lv_obj_set_style_radius(status_screen_, 0, 0);
    lv_obj_set_style_pad_all(status_screen_, 0, 0);
    lv_obj_clear_flag(status_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(status_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(status_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Status Header (y=36, h=26, w=224)
    status_header_card_ = create_card(status_screen_, 224, 26, 13);
    lv_obj_set_style_border_width(status_header_card_, 0, 0);
    lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 36);
    status_header_label_ = lv_label_create(status_header_card_);
    lv_label_set_text(status_header_label_, "📱 STATUS PONSEL");
    lv_obj_set_style_text_font(status_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(status_header_label_);

    // Phone Battery Card (y=66, h=62, w=224)
    status_bat_card_ = create_card(status_screen_, 224, 62, 14);
    lv_obj_set_style_border_width(status_bat_card_, 0, 0);
    lv_obj_align(status_bat_card_, LV_ALIGN_TOP_MID, 0, 66);

    status_phone_bat_title_ = lv_label_create(status_bat_card_);
    lv_label_set_text(status_phone_bat_title_, "🔋 Baterai");
    lv_obj_set_style_text_font(status_phone_bat_title_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 12, 10);

    status_phone_bat_label_ = lv_label_create(status_bat_card_);
    lv_label_set_text(status_phone_bat_label_, "--%");
    lv_obj_set_style_text_font(status_phone_bat_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -12, 10);

    status_bat_bar_ = create_card(status_bat_card_, 200, 8, 4);
    lv_obj_set_style_border_width(status_bat_bar_, 0, 0);
    lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
    status_bat_bar_fill_ = lv_obj_create(status_bat_bar_);
    lv_obj_set_size(status_bat_bar_fill_, 100, 8);
    lv_obj_set_style_radius(status_bat_bar_fill_, 4, 0);
    lv_obj_set_style_bg_opa(status_bat_bar_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_bat_bar_fill_, 0, 0);
    lv_obj_set_style_pad_all(status_bat_bar_fill_, 0, 0);
    lv_obj_align(status_bat_bar_fill_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(status_bat_bar_fill_, LV_OBJ_FLAG_SCROLLABLE);

    // Network & Signal Card (y=132, h=66, w=224) - Clean separate rows
    status_net_card_ = create_card(status_screen_, 224, 66, 14);
    lv_obj_set_style_border_width(status_net_card_, 0, 0);
    lv_obj_align(status_net_card_, LV_ALIGN_TOP_MID, 0, 132);

    status_network_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_network_label_, "📶 Jaringan: Seluler");
    lv_obj_set_style_text_font(status_network_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 12, 8);

    status_signal_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_signal_label_, "📶 Sinyal: Aktif");
    lv_obj_set_style_text_font(status_signal_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 12, 28);

    status_net_hint_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_net_hint_label_, "⚡ Koneksi: BLE Chronos");
    lv_obj_set_style_text_font(status_net_hint_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 12, 48);

    // Find Phone Button (Ring Phone) (y=202, h=36, w=224)
    status_find_phone_btn_ = lv_button_create(status_screen_);
    lv_obj_set_size(status_find_phone_btn_, 224, 36);
    lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_MID, 0, 202);
    lv_obj_set_style_radius(status_find_phone_btn_, 12, 0);
    lv_obj_set_style_bg_color(status_find_phone_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(status_find_phone_btn_, 0, 0);
    lv_obj_set_style_pad_all(status_find_phone_btn_, 0, 0);
    status_find_phone_lbl_ = lv_label_create(status_find_phone_btn_);
    lv_label_set_text(status_find_phone_lbl_, "🔔 CARI PONSEL SAYA");
    lv_obj_set_style_text_font(status_find_phone_lbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_find_phone_lbl_, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(status_find_phone_lbl_);
    lv_obj_add_event_cb(status_find_phone_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (!disp) return;
        disp->find_phone_active_ = !disp->find_phone_active_;
        if (disp->find_phone_cb_) {
            disp->find_phone_cb_(disp->find_phone_active_);
        }
        if (disp->find_phone_active_) {
            bool is_landscape = (disp->width_ > disp->height_);
            lv_label_set_text(disp->status_find_phone_lbl_, is_landscape ? "🔔 STOP" : "🔔 STOP BUNYI");
            lv_obj_set_style_bg_color(disp->status_find_phone_btn_, lv_color_hex(0xDC2626), 0);
            lv_obj_set_style_text_color(disp->status_find_phone_lbl_, lv_color_white(), 0);
        } else {
            bool is_landscape = (disp->width_ > disp->height_);
            lv_label_set_text(disp->status_find_phone_lbl_, is_landscape ? "🔔 CARI PONSEL" : "🔔 CARI PONSEL SAYA");
            lv_obj_set_style_bg_color(disp->status_find_phone_btn_, lv_color_hex(0x27272A), 0);
            lv_obj_set_style_text_color(disp->status_find_phone_lbl_, lv_color_hex(0x38BDF8), 0);
        }
    }, LV_EVENT_CLICKED, this);

    // Bluetooth Info Pill (y=242, h=32, w=224)
    status_bt_card_ = create_card(status_screen_, 224, 32, 10);
    lv_obj_set_style_border_width(status_bt_card_, 0, 0);
    lv_obj_align(status_bt_card_, LV_ALIGN_TOP_MID, 0, 242);
    status_bt_label_ = lv_label_create(status_bt_card_);
    lv_label_set_text(status_bt_label_, last_bt_connected_ ? "⚡ BLE Chronos Terhubung" : "🔗 Pindai QR BLE");
    lv_obj_set_style_text_font(status_bt_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(status_bt_label_);
    lv_obj_add_event_cb(status_bt_card_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && !disp->isBtConnected()) {
            disp->showQrPairing();
        }
    }, LV_EVENT_CLICKED, this);
}

// ---- ALERT SCREEN (Find Watch, Alarm, Camera trigger) ----
void ChronchiDisplay::createAlertScreen() {
    alert_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(alert_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(alert_screen_, width_, height_);
    lv_obj_set_style_bg_opa(alert_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alert_screen_, 0, 0);
    lv_obj_set_style_radius(alert_screen_, 0, 0);
    lv_obj_set_style_pad_all(alert_screen_, 0, 0);
    lv_obj_clear_flag(alert_screen_, LV_OBJ_FLAG_SCROLLABLE);

    alert_card_ = create_card(alert_screen_, 216, 165, 16);
    lv_obj_align(alert_card_, LV_ALIGN_TOP_MID, 0, 38);

    alert_icon_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL "  " LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(alert_icon_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(alert_icon_label_, LV_ALIGN_TOP_MID, 0, 14);

    alert_title_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_title_label_, "PERINGATAN");
    lv_obj_set_style_text_font(alert_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(alert_title_label_, LV_ALIGN_TOP_MID, 0, 44);

    alert_msg_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_msg_label_, "");
    lv_obj_set_style_text_font(alert_msg_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(alert_msg_label_, 190);
    lv_label_set_long_mode(alert_msg_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(alert_msg_label_, LV_ALIGN_TOP_MID, 0, 75);

    alert_btn_ = lv_button_create(alert_screen_);
    lv_obj_set_size(alert_btn_, 190, 38);
    lv_obj_set_style_radius(alert_btn_, 10, 0);
    lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_pad_all(alert_btn_, 0, 0);
    lv_obj_align(alert_btn_, LV_ALIGN_TOP_MID, 0, 212);

    alert_btn_label_ = lv_label_create(alert_btn_);
    lv_label_set_text(alert_btn_label_, "✕ MATIKAN / STOP");
    lv_obj_set_style_text_font(alert_btn_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(alert_btn_label_, lv_color_white(), 0);
    lv_obj_center(alert_btn_label_);

    lv_obj_add_event_cb(alert_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (!disp) return;
        if (disp->is_camera_mode_) {
            if (disp->camera_capture_cb_) {
                disp->camera_capture_cb_();
            }
            if (disp->alert_btn_label_) {
                lv_label_set_text(disp->alert_btn_label_, "✓ MEMOTRET...");
            }
        } else {
            ChronchiAlert::GetInstance().StopAlert();
            disp->showHome();
        }
    }, LV_EVENT_CLICKED, this);
}
