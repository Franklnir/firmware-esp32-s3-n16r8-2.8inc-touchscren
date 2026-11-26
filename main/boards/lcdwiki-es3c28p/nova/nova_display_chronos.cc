#include "nova_display_internal.h"

static bool CheckAndExtractOtp(const std::string& app, const std::string& title, const std::string& message,
                               std::string& out_service, std::string& out_otp_code) {
    if (message.empty() && title.empty()) return false;

    std::string combined = app + " " + title + " " + message;
    std::string lower = combined;
    for (char& c : lower) {
        c = (char)tolower((unsigned char)c);
    }

    bool has_otp_kw = false;
    static const char* const kKeywords[] = {
        "otp", "token", "kode verifikasi", "verification code", "verifikasi",
        "kode masuk", "login code", "kode konfirmasi", "security code",
        "kode keamanan", "passcode", "kode rahasia", "one time password", "autentikasi"
    };
    for (const char* kw : kKeywords) {
        if (lower.find(kw) != std::string::npos) {
            has_otp_kw = true;
            break;
        }
    }
    if (!has_otp_kw) return false;

    // Service identification
    static const struct { const char* key; const char* name; } kServices[] = {
        {"bca", "BCA BANK"},
        {"mandiri", "BANK MANDIRI"},
        {"bri", "BANK BRI"},
        {"bni", "BANK BNI"},
        {"bsi", "BANK BSI"},
        {"cimb", "CIMB NIAGA"},
        {"permata", "PERMATA BANK"},
        {"jenius", "JENIUS"},
        {"jago", "BANK JAGO"},
        {"dana", "DANA"},
        {"ovo", "OVO"},
        {"gopay", "GOPAY"},
        {"gojek", "GOJEK"},
        {"grab", "GRAB"},
        {"shopee", "SHOPEE"},
        {"tokopedia", "TOKOPEDIA"},
        {"blibli", "BLIBLI"},
        {"lazada", "LAZADA"},
        {"google", "GOOGLE"},
        {"whatsapp", "WHATSAPP"},
        {"telegram", "TELEGRAM"},
        {"tiktok", "TIKTOK"},
        {"facebook", "FACEBOOK"},
        {"instagram", "INSTAGRAM"},
        {"microsoft", "MICROSOFT"},
        {"apple", "APPLE ID"}
    };

    out_service.clear();
    for (const auto& s : kServices) {
        if (lower.find(s.key) != std::string::npos) {
            out_service = s.name;
            break;
        }
    }
    if (out_service.empty()) {
        if (!title.empty() && title.length() <= 20) {
            out_service = title;
        } else if (!app.empty()) {
            out_service = app;
        } else {
            out_service = "SMS BANKING";
        }
    }

    // 1. Check Google format: G-123456
    size_t g_pos = message.find("G-");
    if (g_pos != std::string::npos && g_pos + 8 <= message.length()) {
        bool all_num = true;
        for (int i = 0; i < 6; ++i) {
            if (!isdigit((unsigned char)message[g_pos + 2 + i])) {
                all_num = false;
                break;
            }
        }
        if (all_num) {
            out_otp_code = message.substr(g_pos, 8);
            return true;
        }
    }

    // 2. Scan after prefixes
    static const char* const kPrefixes[] = {
        "otp:", "otp :", "otp adalah", "otp anda", "kode:", "kode :", "kode anda",
        "code:", "code :", "code is", "token:", "token :", "adalah", "is"
    };
    for (const char* pfx : kPrefixes) {
        size_t idx = lower.find(pfx);
        if (idx != std::string::npos) {
            size_t s = idx + strlen(pfx);
            while (s < message.length() && (message[s] == ' ' || message[s] == ':' || message[s] == '=')) {
                s++;
            }
            std::string cand;
            while (s < message.length() && (isdigit((unsigned char)message[s]) || message[s] == '-')) {
                cand += message[s++];
            }
            int digits = 0;
            for (char c : cand) if (isdigit((unsigned char)c)) digits++;
            if (digits >= 4 && digits <= 8) {
                out_otp_code = cand;
                return true;
            }
        }
    }

    // 3. Scan standalone candidates
    for (size_t i = 0; i < message.length(); ) {
        if (isdigit((unsigned char)message[i])) {
            size_t start = i;
            while (i < message.length() && (isdigit((unsigned char)message[i]) || message[i] == '-')) {
                i++;
            }
            std::string cand = message.substr(start, i - start);
            int digits = 0;
            for (char c : cand) if (isdigit((unsigned char)c)) digits++;

            if (digits >= 4 && digits <= 8) {
                bool is_currency = false;
                if (start >= 2) {
                    size_t pre_start = (start >= 5) ? start - 5 : 0;
                    std::string pre = message.substr(pre_start, start - pre_start);
                    for (char& c : pre) c = (char)tolower((unsigned char)c);
                    if (pre.find("rp") != std::string::npos || pre.find("idr") != std::string::npos) {
                        is_currency = true;
                    }
                }
                if (digits == 4 && (cand == "2024" || cand == "2025" || cand == "2026" || cand == "2027")) {
                    is_currency = true;
                }
                if (!is_currency) {
                    out_otp_code = cand;
                    return true;
                }
            }
        } else {
            i++;
        }
    }

    return false;
}

void NovaDisplay::ShowNotificationBanner(const std::string& app, const std::string& title, const std::string& message) {
    // 1. Check if this is an OTP / SMS Banking token
    std::string otp_service, otp_code;
    if (CheckAndExtractOtp(app, title, message, otp_service, otp_code)) {
        ESP_LOGI(TAG, "Smart OTP Notification detected: service='%s', code='%s'",
                 otp_service.c_str(), otp_code.c_str());
        ShowOtpHighlight(otp_service, otp_code, message);
        return;
    }
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (notif_banner_) {
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    if (notif_banner_timer_) {
        lv_timer_del(notif_banner_timer_);
        notif_banner_timer_ = nullptr;
    }

    int banner_w = width_ - 16;
    int banner_h = 52;

    notif_banner_ = lv_obj_create(top);
    lv_obj_set_pos(notif_banner_, 8, 4);
    lv_obj_set_size(notif_banner_, banner_w, banner_h);
    lv_obj_set_style_radius(notif_banner_, 10, 0);
    lv_obj_set_style_bg_color(notif_banner_, lv_color_hex(0x090D16), 0);
    lv_obj_set_style_bg_opa(notif_banner_, LV_OPA_90, 0);

    uint32_t border_color = 0x38BDF8;
    if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
    else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
    else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
    else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

    lv_obj_set_style_border_color(notif_banner_, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(notif_banner_, 2, 0);
    lv_obj_set_style_pad_all(notif_banner_, 4, 0);
    lv_obj_set_style_pad_left(notif_banner_, 10, 0);
    lv_obj_set_style_pad_right(notif_banner_, 10, 0);
    lv_obj_clear_flag(notif_banner_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_lbl = lv_label_create(notif_banner_);
    std::string h_str = app.empty() ? "NOTIFIKASI" : app;
    if (!title.empty()) h_str += " - " + title;
    lv_label_set_text(title_lbl, h_str.c_str());
    lv_obj_set_style_text_font(title_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(border_color), 0);
    lv_obj_set_pos(title_lbl, 0, 2);
    lv_obj_set_width(title_lbl, banner_w - 24);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);

    lv_obj_t* msg_lbl = lv_label_create(notif_banner_);
    lv_label_set_text(msg_lbl, message.empty() ? "(Pesan baru)" : message.c_str());
    lv_obj_set_style_text_font(msg_lbl, FONT_14, 0);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(msg_lbl, 0, 22);
    lv_obj_set_width(msg_lbl, banner_w - 24);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_move_foreground(notif_banner_);
    lv_obj_add_flag(notif_banner_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(notif_banner_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) {
            self->DismissNotificationBanner();
            self->NavigateTo(PAGE_NOTIF);
        }
    }, LV_EVENT_CLICKED, this);

    notif_banner_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
        if (self) {
            self->notif_banner_timer_ = nullptr;
            self->DismissNotificationBanner();
        }
    }, 6000, this);
    lv_timer_set_repeat_count(notif_banner_timer_, 1);

    lvgl_port_unlock();
}

void NovaDisplay::DismissNotificationBanner() {
    lvgl_port_lock(0);
    if (notif_banner_timer_) {
        lv_timer_del(notif_banner_timer_);
        notif_banner_timer_ = nullptr;
    }
    if (notif_banner_) {
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::DismissOtpHighlight() {
    lvgl_port_lock(0);
    if (otp_timer_) {
        lv_timer_del(otp_timer_);
        otp_timer_ = nullptr;
    }
    if (otp_modal_) {
        lv_obj_del(otp_modal_);
        otp_modal_ = nullptr;
        otp_countdown_label_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowOtpHighlight(const std::string& service, const std::string& otp_code, const std::string& full_message) {
    lvgl_port_lock(0);
    if (notif_banner_) {
        if (notif_banner_timer_) {
            lv_timer_del(notif_banner_timer_);
            notif_banner_timer_ = nullptr;
        }
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    if (otp_timer_) {
        lv_timer_del(otp_timer_);
        otp_timer_ = nullptr;
    }
    if (otp_modal_) {
        lv_obj_del(otp_modal_);
        otp_modal_ = nullptr;
        otp_countdown_label_ = nullptr;
    }

    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    bool is_land = (width_ > 240);
    int modal_w = is_land ? 296 : 228;
    int modal_h = is_land ? 190 : 250;
    int modal_x = (width_ - modal_w) / 2;
    int modal_y = (height_ - modal_h) / 2;

    otp_modal_ = lv_obj_create(top);
    lv_obj_set_pos(otp_modal_, modal_x, modal_y);
    lv_obj_set_size(otp_modal_, modal_w, modal_h);
    lv_obj_set_style_radius(otp_modal_, 14, 0);
    lv_obj_set_style_bg_color(otp_modal_, lv_color_hex(0x0A0F1D), 0);
    lv_obj_set_style_bg_opa(otp_modal_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(otp_modal_, lv_color_hex(0xF59E0B), 0); // Amber gold glowing border
    lv_obj_set_style_border_width(otp_modal_, 2, 0);
    lv_obj_set_style_pad_all(otp_modal_, 8, 0);
    lv_obj_clear_flag(otp_modal_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Header: Lock Icon + Title + Close Button
    lv_obj_t* key_icon = lv_label_create(otp_modal_);
    lv_label_set_text(key_icon, MATERIAL_SYMBOLS_LOCK);
    lv_obj_set_style_text_font(key_icon, FONT_ICON, 0);
    lv_obj_set_style_text_color(key_icon, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_pos(key_icon, 4, 4);

    lv_obj_t* title_lbl = lv_label_create(otp_modal_);
    lv_label_set_text(title_lbl, "KODE OTP / TOKEN BANK");
    lv_obj_set_style_text_font(title_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_pos(title_lbl, 26, 6);

    // Close button [X]
    lv_obj_t* close_btn = lv_btn_create(otp_modal_);
    lv_obj_set_pos(close_btn, modal_w - 40, 2);
    lv_obj_set_size(close_btn, 24, 24);
    StyleButton(close_btn, lv_color_hex(0x1F2937), lv_color_hex(0x374151), 6);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(x_lbl, FONT_ICON, 0);
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissOtpHighlight();
    }, LV_EVENT_CLICKED, this);

    // 2. Service Badge Pill
    lv_obj_t* s_badge = lv_obj_create(otp_modal_);
    lv_obj_set_pos(s_badge, 6, 28);
    lv_obj_set_size(s_badge, modal_w - 28, 22);
    lv_obj_set_style_radius(s_badge, 6, 0);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(s_badge, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_border_width(s_badge, 1, 0);
    lv_obj_set_style_pad_all(s_badge, 2, 0);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* s_lbl = lv_label_create(s_badge);
    std::string s_text = "LAYANAN: " + service;
    lv_label_set_text(s_lbl, s_text.c_str());
    lv_obj_set_style_text_font(s_lbl, FONT_10, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(s_lbl);

    // 3. Central Huge OTP Box (Hero Element!)
    int box_y = 54;
    int box_h = is_land ? 58 : 68;
    lv_obj_t* otp_box = lv_obj_create(otp_modal_);
    lv_obj_set_pos(otp_box, 6, box_y);
    lv_obj_set_size(otp_box, modal_w - 28, box_h);
    lv_obj_set_style_radius(otp_box, 10, 0);
    lv_obj_set_style_bg_color(otp_box, lv_color_hex(0x030712), 0);
    lv_obj_set_style_border_color(otp_box, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_border_width(otp_box, 2, 0);
    lv_obj_set_style_pad_all(otp_box, 4, 0);
    lv_obj_clear_flag(otp_box, LV_OBJ_FLAG_SCROLLABLE);

    std::string spaced_code = "";
    for (size_t i = 0; i < otp_code.length(); ++i) {
        spaced_code += otp_code[i];
        if (i + 1 < otp_code.length() && otp_code[i] != '-' && otp_code[i+1] != '-') {
            spaced_code += " ";
        }
    }

    lv_obj_t* code_lbl = lv_label_create(otp_box);
    lv_label_set_text(code_lbl, spaced_code.c_str());
    lv_obj_set_style_text_font(code_lbl, FONT_36, 0);
    lv_obj_set_style_text_color(code_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(code_lbl);

    // 4. Message context preview (1 line)
    int msg_y = box_y + box_h + 4;
    lv_obj_t* msg_lbl = lv_label_create(otp_modal_);
    lv_label_set_text(msg_lbl, full_message.empty() ? "(Rahasiakan kode OTP Anda)" : full_message.c_str());
    lv_obj_set_style_text_font(msg_lbl, FONT_10, 0);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_pos(msg_lbl, 8, msg_y);
    lv_obj_set_width(msg_lbl, modal_w - 32);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_DOT);

    // 5. Countdown & Action footer (30 seconds)
    int foot_y = modal_h - 28;
    otp_countdown_label_ = lv_label_create(otp_modal_);
    otp_remaining_sec_ = 30;
    char cd_buf[96];
    snprintf(cd_buf, sizeof(cd_buf), "Menutup otomatis dalam %d dtk (Ketuk utk tutup)", otp_remaining_sec_);
    lv_label_set_text(otp_countdown_label_, cd_buf);
    lv_obj_set_style_text_font(otp_countdown_label_, FONT_10, 0);
    lv_obj_set_style_text_color(otp_countdown_label_, lv_color_hex(0xFCD34D), 0);
    lv_obj_set_pos(otp_countdown_label_, 8, foot_y);
    lv_obj_set_width(otp_countdown_label_, modal_w - 32);
    lv_obj_set_style_text_align(otp_countdown_label_, LV_TEXT_ALIGN_CENTER, 0);

    // Clicking anywhere on modal closes it
    lv_obj_add_flag(otp_modal_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(otp_modal_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissOtpHighlight();
    }, LV_EVENT_CLICKED, this);

    // Timer: runs every 1000ms for 30 seconds
    otp_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
        if (!self) return;
        self->otp_remaining_sec_--;
        if (self->otp_remaining_sec_ <= 0) {
            self->DismissOtpHighlight();
        } else if (self->otp_countdown_label_) {
            char buf[96];
            snprintf(buf, sizeof(buf), "Menutup otomatis dalam %d dtk (Ketuk utk tutup)", self->otp_remaining_sec_);
            lv_label_set_text(self->otp_countdown_label_, buf);
        }
    }, 1000, this);
    lv_timer_set_repeat_count(otp_timer_, 31);

    lv_obj_move_foreground(otp_modal_);
    lvgl_port_unlock();
}

void NovaDisplay::ShowBtMusicModal() {
    NavigateTo(PAGE_BT_MUSIC);
}

void NovaDisplay::ShowNavModal() {
    NavigateTo(PAGE_NAV);
}

void NovaDisplay::ShowNotifCenterModal() {
    NavigateTo(PAGE_NOTIF);
}

void NovaDisplay::ShowWeatherModal() {
    NavigateTo(PAGE_WEATHER);
}

void NovaDisplay::ShowPhoneModal() {
    NavigateTo(PAGE_PHONE);
}

// ---- NAVIGATION 48x48 ICON DRAWING HELPERS ----
static inline void nav_put_pixel(uint8_t* buf, int x, int y, uint16_t color) {
    if (x < 0 || x >= 48 || y < 0 || y >= 48) return;
    int idx = (y * 48 + x) * 2;
    buf[idx] = color & 0xFF;
    buf[idx + 1] = (color >> 8) & 0xFF;
}

static void nav_draw_filled_circle(uint8_t* buf, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                nav_put_pixel(buf, cx + dx, cy + dy, color);
            }
        }
    }
}

static void nav_draw_arrow_up(uint8_t* buf, uint16_t color) {
    for (int y = 24; y <= 42; y++)
        for (int x = 21; x <= 26; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 24, 42, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            nav_put_pixel(buf, x, 6 + i, color);
    }
}

static void nav_draw_arrow_left(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 12; x <= 34; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 34, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 6 + i, y, color);
    }
}

static void nav_draw_arrow_right(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 13; x <= 35; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 13, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 41 - i, y, color);
    }
}

static void nav_draw_arrow_slight_left(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 32 - i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            nav_put_pixel(buf, x + d, y, color);
            nav_put_pixel(buf, x, y + d, color);
        }
    }
    nav_draw_filled_circle(buf, 32, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 12 - i / 2, cy = 14 - i / 2;
        for (int d = -half; d <= half; d++)
            nav_put_pixel(buf, cx + d, cy, color);
    }
}

static void nav_draw_arrow_slight_right(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 15 + i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            nav_put_pixel(buf, x + d, y, color);
            nav_put_pixel(buf, x, y + d, color);
        }
    }
    nav_draw_filled_circle(buf, 15, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 35 + i / 2, cy = 14 - i / 2;
        for (int d = -half; d <= half; d++)
            nav_put_pixel(buf, cx + d, cy, color);
    }
}

static void nav_draw_arrow_uturn(uint8_t* buf, uint16_t color) {
    for (int y = 20; y <= 40; y++)
        for (int x = 27; x <= 32; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 29, 40, 2, color);
    for (int a = 0; a <= 180; a += 3) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -2; t <= 2; t++) {
            int x = 20 + (int)((9 + t) * cosf(rad));
            int y = 20 - (int)((9 + t) * sinf(rad));
            nav_put_pixel(buf, x, y, color);
        }
    }
    for (int y = 20; y <= 32; y++)
        for (int x = 9; x <= 14; x++)
            nav_put_pixel(buf, x, y, color);
    for (int i = 0; i < 12; i++) {
        int half = 1 + (i * 7) / 12;
        for (int x = 11 - half; x <= 11 + half; x++)
            nav_put_pixel(buf, x, 32 + i, color);
    }
}

static void nav_draw_roundabout(uint8_t* buf, uint16_t color) {
    int r = 14;
    for (int a = 0; a < 360; a += 2) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -1; t <= 1; t++) {
            int x = 24 + (int)((r + t) * cosf(rad));
            int y = 24 + (int)((r + t) * sinf(rad));
            nav_put_pixel(buf, x, y, color);
        }
    }
    for (int y = 21; y <= 26; y++)
        for (int x = 30; x <= 42; x++)
            nav_put_pixel(buf, x, y, color);
    for (int i = 0; i < 8; i++) {
        int half = 1 + (i * 3) / 8;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 42 - i, y, color);
    }
}

static void nav_draw_arrive(uint8_t* buf, uint16_t color, uint16_t cutout) {
    nav_draw_filled_circle(buf, 24, 14, 9, color);
    nav_draw_filled_circle(buf, 24, 14, 4, cutout);
    for (int i = 0; i < 16; i++) {
        int half = 9 - (i * 6) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            nav_put_pixel(buf, x, 23 + i, color);
    }
    nav_put_pixel(buf, 24, 39, color);
    nav_put_pixel(buf, 24, 40, color);
}

static void nav_draw_car(uint8_t* buf, uint16_t color, uint16_t cutout) {
    for (int y = 24; y <= 36; y++)
        for (int x = 10; x <= 37; x++)
            nav_put_pixel(buf, x, y, color);
    for (int y = 16; y <= 24; y++)
        for (int x = 15; x <= 32; x++)
            nav_put_pixel(buf, x, y, color);
    for (int y = 17; y <= 23; y++)
        for (int x = 16; x <= 22; x++)
            nav_put_pixel(buf, x, y, cutout);
    for (int y = 17; y <= 23; y++)
        for (int x = 25; x <= 31; x++)
            nav_put_pixel(buf, x, y, cutout);
    nav_draw_filled_circle(buf, 15, 37, 4, color);
    nav_draw_filled_circle(buf, 32, 37, 4, color);
    nav_put_pixel(buf, 10, 28, 0xFFE0);
    nav_put_pixel(buf, 10, 29, 0xFFE0);
    nav_put_pixel(buf, 37, 28, 0xFFE0);
    nav_put_pixel(buf, 37, 29, 0xFFE0);
}

void NovaDisplay::RenderNavIcon(const std::string& maneuver, const std::string& directions, bool has_icon, const uint8_t* icon_data) {
    if (!nav_icon_buf_) return;

    uint32_t bg_rgb = lv_color_to_int(C_PANEL_2);
    uint16_t bg_color = (uint16_t)((((bg_rgb >> 16) & 0xF8) << 8) | (((bg_rgb >> 8) & 0xFC) << 3) | ((bg_rgb & 0xF8) >> 3));
    uint32_t fg_rgb = lv_color_to_int(C_CYAN);
    uint16_t fg_color = (uint16_t)((((fg_rgb >> 16) & 0xF8) << 8) | (((fg_rgb >> 8) & 0xFC) << 3) | ((fg_rgb & 0xF8) >> 3));

    if (has_icon && icon_data) {
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
        return;
    }

    for (int i = 0; i < 48 * 48; i++) {
        nav_icon_buf_[i * 2] = bg_color & 0xFF;
        nav_icon_buf_[i * 2 + 1] = (bg_color >> 8) & 0xFF;
    }

    std::string m = maneuver;
    if (m.empty() && !directions.empty()) {
        std::string d = directions;
        for (auto& c : d) c = (char)tolower((unsigned char)c);
        if (d.find("slight left") != std::string::npos || d.find("serong kiri") != std::string::npos || d.find("agak ke kiri") != std::string::npos) {
            m = "SLIGHT_LEFT";
        } else if (d.find("slight right") != std::string::npos || d.find("serong kanan") != std::string::npos || d.find("agak ke kanan") != std::string::npos) {
            m = "SLIGHT_RIGHT";
        } else if (d.find("putar balik") != std::string::npos || d.find("u-turn") != std::string::npos) {
            m = "UTURN";
        } else if (d.find("kiri") != std::string::npos || d.find("left") != std::string::npos) {
            m = "LEFT";
        } else if (d.find("kanan") != std::string::npos || d.find("right") != std::string::npos) {
            m = "RIGHT";
        } else if (d.find("bundaran") != std::string::npos || d.find("roundabout") != std::string::npos) {
            m = "ROUNDABOUT";
        } else if (d.find("sampai") != std::string::npos || d.find("arrive") != std::string::npos || d.find("tujuan") != std::string::npos || d.find("tiba") != std::string::npos) {
            m = "ARRIVE";
        } else if (d.find("lurus") != std::string::npos || d.find("straight") != std::string::npos || d.find("maju") != std::string::npos) {
            m = "STRAIGHT";
        }
    }

    if (m == "LEFT") nav_draw_arrow_left(nav_icon_buf_, fg_color);
    else if (m == "RIGHT") nav_draw_arrow_right(nav_icon_buf_, fg_color);
    else if (m == "SLIGHT_LEFT") nav_draw_arrow_slight_left(nav_icon_buf_, fg_color);
    else if (m == "SLIGHT_RIGHT") nav_draw_arrow_slight_right(nav_icon_buf_, fg_color);
    else if (m == "UTURN") nav_draw_arrow_uturn(nav_icon_buf_, fg_color);
    else if (m == "ROUNDABOUT") nav_draw_roundabout(nav_icon_buf_, fg_color);
    else if (m == "ARRIVE") nav_draw_arrive(nav_icon_buf_, fg_color, bg_color);
    else if (m == "STRAIGHT") nav_draw_arrow_up(nav_icon_buf_, fg_color);
    else nav_draw_car(nav_icon_buf_, fg_color, bg_color);
}

void NovaDisplay::BuildNav() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "NAVIGASI GOOGLE MAPS");

    std::string dist = "--", dir = "Menunggu instruksi...", title = "Buka Google Maps di HP";
    std::string dur = "--", eta = "--:--", spd = "--";
    bool active = false;
    if (callbacks_.get_ble_nav) {
        callbacks_.get_ble_nav(dist, dir, title, dur, eta, spd, active);
    }

    std::string maneuver = last_nav_maneuver_;
    bool has_icon = last_nav_has_icon_;
    const uint8_t* icon_data = last_nav_has_icon_ ? last_nav_icon_data_ : nullptr;
    if (callbacks_.get_ble_nav_extra) {
        callbacks_.get_ble_nav_extra(maneuver, has_icon, icon_data);
    }
    RenderNavIcon(maneuver, dir, has_icon, icon_data);

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    if (!is_land) {
        // === PORTRAIT LAYOUT (240x320) ===
        // Tanpa header/pill "SIAP BUKA MAPS" agar tidak menghalangi dan luas!

        // 1. Kartu Manuver & Belokan Utama (y=36, h=146)
        lv_obj_t* card = MakePanel(root, content_x, 36, content_w, 146, C_PANEL, 
                                   active ? C_CYAN : C_BORDER, 12);
        lv_obj_set_style_border_width(card, 2, 0);

        // Icon Rute Gede & Rapih (54x54 Badge Container dengan Gambar 48x48)
        lv_obj_t* icon_badge = lv_obj_create(card);
        lv_obj_set_pos(icon_badge, 8, 8);
        lv_obj_set_size(icon_badge, 54, 54);
        lv_obj_set_style_radius(icon_badge, 12, 0);
        lv_obj_set_style_bg_color(icon_badge, C_PANEL_2, 0);
        lv_obj_set_style_border_color(icon_badge, C_CYAN, 0);
        lv_obj_set_style_border_width(icon_badge, 2, 0);
        lv_obj_set_style_pad_all(icon_badge, 0, 0);
        lv_obj_clear_flag(icon_badge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        if (nav_icon_buf_) {
            lv_obj_t* icon_img = lv_image_create(icon_badge);
            lv_image_set_src(icon_img, &nav_icon_dsc_);
            lv_obj_center(icon_img);
        }

        // Jarak Belokan Berikutnya
        std::string dist_str = dist.empty() ? "-- m" : dist;
        lv_obj_t* dist_lbl = MakeLabel(card, dist_str.c_str(), 70, 6, content_w - 78, 30, FONT_28, C_CYAN);
        lv_label_set_long_mode(dist_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Status GPS Aktif
        std::string status_txt = active ? "● Rute GPS Aktif" : "Menunggu Rute...";
        MakeLabel(card, status_txt.c_str(), 70, 36, content_w - 78, 18, FONT_10, active ? C_MINT : C_MUTED);

        // Instruksi Manuver (e.g. Belok Kanan)
        std::string dir_str = dir.empty() ? "Mulai rute navigasi di ponsel" : dir;
        lv_obj_t* dir_lbl = MakeLabel(card, dir_str.c_str(), 10, 68, content_w - 20, 24, FONT_14, C_TEXT);
        lv_label_set_long_mode(dir_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Nama Jalan Tujuan (e.g. ke Jl. Sudirman)
        std::string title_str = title.empty() ? "Menunggu tujuan..." : title;
        lv_obj_t* street_lbl = MakeLabel(card, title_str.c_str(), 10, 94, content_w - 20, 22, FONT_12, C_AMBER);
        lv_label_set_long_mode(street_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Catatan Info
        MakeLabel(card, active ? "Notifikasi diredam selama navigasi" : "Tersambung via Bluetooth Chronos", 
                  10, 120, content_w - 20, 16, FONT_10, C_MUTED);

        // 2. Kartu Telemetri (ETA, Durasi, Kecepatan) (y=190, h=76)
        lv_obj_t* tcard = MakePanel(root, content_x, 190, content_w, 76, C_PANEL, C_BORDER, 10);

        // Panel Kiri: Estimasi Tiba (ETA) & Sisa Waktu
        int half_w = (content_w - 16) / 2;
        lv_obj_t* p_left = MakePanel(tcard, 6, 6, half_w, 64, C_PANEL_2, C_BORDER, 8);
        MakeLabel(p_left, "ESTIMASI TIBA", 6, 4, half_w - 12, 14, FONT_10, C_MUTED);
        
        // Teks ETA dikecilkan (FONT_12) agar pas dan rapi tanpa terpotong
        std::string eta_clean = eta.empty() ? "--:--" : eta;
        std::string eta_txt = "ETA: " + eta_clean;
        lv_obj_t* eta_lbl = MakeLabel(p_left, eta_txt.c_str(), 6, 20, half_w - 12, 18, FONT_12, C_AMBER);
        lv_label_set_long_mode(eta_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        std::string dur_txt = dur.empty() ? "-- mnt" : dur;
        lv_obj_t* dur_lbl = MakeLabel(p_left, dur_txt.c_str(), 6, 40, half_w - 12, 16, FONT_10, C_MINT);
        lv_label_set_long_mode(dur_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Panel Kanan: Kecepatan & Status
        lv_obj_t* p_right = MakePanel(tcard, 10 + half_w, 6, half_w, 64, C_PANEL_2, C_BORDER, 8);
        MakeLabel(p_right, "KECEPATAN", 6, 4, half_w - 12, 14, FONT_10, C_MUTED);
        std::string spd_txt = spd.empty() ? "-- km/h" : spd;
        lv_obj_t* spd_lbl = MakeLabel(p_right, spd_txt.c_str(), 6, 20, half_w - 12, 18, FONT_12, C_CYAN);
        lv_label_set_long_mode(spd_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeLabel(p_right, active ? "GPS Live" : "Standby", 6, 40, half_w - 12, 16, FONT_10, C_MUTED);

    } else {
        // === LANDSCAPE LAYOUT (320x240) ===

        // Left Column: Route & Distance (w=176, h=166)
        lv_obj_t* card = MakePanel(root, 8, 36, 176, 166, C_PANEL, active ? C_CYAN : C_BORDER, 12);
        lv_obj_set_style_border_width(card, 2, 0);

        // Large Turn Icon Badge
        lv_obj_t* icon_badge = lv_obj_create(card);
        lv_obj_set_pos(icon_badge, 8, 8);
        lv_obj_set_size(icon_badge, 54, 54);
        lv_obj_set_style_radius(icon_badge, 12, 0);
        lv_obj_set_style_bg_color(icon_badge, C_PANEL_2, 0);
        lv_obj_set_style_border_color(icon_badge, C_CYAN, 0);
        lv_obj_set_style_border_width(icon_badge, 2, 0);
        lv_obj_set_style_pad_all(icon_badge, 0, 0);
        lv_obj_clear_flag(icon_badge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        if (nav_icon_buf_) {
            lv_obj_t* icon_img = lv_image_create(icon_badge);
            lv_image_set_src(icon_img, &nav_icon_dsc_);
            lv_obj_center(icon_img);
        }

        std::string dist_str = dist.empty() ? "-- m" : dist;
        lv_obj_t* dist_lbl = MakeLabel(card, dist_str.c_str(), 68, 6, 100, 30, FONT_24, C_TEXT);
        lv_obj_set_style_text_color(dist_lbl, C_CYAN, 0);
        lv_label_set_long_mode(dist_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        std::string status_txt = active ? "● Rute GPS Aktif" : "Menunggu Rute...";
        MakeLabel(card, status_txt.c_str(), 68, 36, 100, 16, FONT_10, active ? C_MINT : C_MUTED);

        std::string dir_str = dir.empty() ? "Buka Google Maps di HP" : dir;
        lv_obj_t* dir_lbl = MakeLabel(card, dir_str.c_str(), 8, 68, 160, 24, FONT_12, C_TEXT);
        lv_label_set_long_mode(dir_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        std::string title_str = title.empty() ? "Menunggu rute..." : title;
        lv_obj_t* street_lbl = MakeLabel(card, title_str.c_str(), 8, 94, 160, 20, FONT_10, C_AMBER);
        lv_label_set_long_mode(street_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(card, active ? "Notifikasi diredam" : "Bluetooth Chronos", 8, 118, 160, 16, FONT_10, C_MUTED);

        // Right Column: Telemetry Dashboard (w=124, h=166)
        lv_obj_t* tcard = MakePanel(root, 188, 36, 124, 166, C_PANEL, C_BORDER, 10);
        MakeLabel(tcard, "ESTIMASI TIBA", 8, 8, 108, 14, FONT_10, C_MUTED);
        std::string eta_txt = "ETA: " + (eta.empty() ? "--:--" : eta);
        lv_obj_t* eta_lbl = MakeLabel(tcard, eta_txt.c_str(), 8, 24, 108, 18, FONT_12, C_AMBER);
        lv_label_set_long_mode(eta_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(tcard, "SISA WAKTU", 8, 54, 108, 14, FONT_10, C_MUTED);
        std::string dur_txt = dur.empty() ? "-- mnt" : dur;
        lv_obj_t* dur_lbl = MakeLabel(tcard, dur_txt.c_str(), 8, 68, 108, 18, FONT_12, C_MINT);
        lv_label_set_long_mode(dur_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(tcard, "KECEPATAN", 8, 98, 108, 14, FONT_10, C_MUTED);
        std::string spd_txt = spd.empty() ? "-- km/h" : spd;
        lv_obj_t* spd_lbl = MakeLabel(tcard, spd_txt.c_str(), 8, 112, 108, 18, FONT_12, C_CYAN);
        lv_label_set_long_mode(spd_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    MakeBottomNav(root, PAGE_NAV);
}

void NovaDisplay::BuildBtMusic() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MUSIK HP");

    std::string title = "--", artist = "--";
    bool playing = false;
    if (callbacks_.get_ble_music) {
        callbacks_.get_ble_music(title, artist, playing);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 300 : 216;
    int content_x = (width_ - content_w) / 2;

    int card_y = 38;
    int card_h = is_land ? 164 : 236;
    lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_AMBER, 12);

    // Album art / Disc icon
    int art_y = is_land ? 12 : 16;
    int art_x = is_land ? 16 : (content_w / 2 - 24);
    MakeIcon(card, ICON_AUDIO, art_x, art_y, C_AMBER, is_land ? 36 : 48);

    int text_x = is_land ? 70 : 10;
    int text_w = is_land ? (content_w - 80) : (content_w - 20);

    // Title & Artist
    lv_obj_t* t_lbl = MakeCenteredLabel(card, title.empty() ? "Tidak Ada Musik" : title.c_str(), 
                                        text_x, is_land ? 14 : 74, text_w, 22, FONT_14, C_TEXT);
    lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t* a_lbl = MakeCenteredLabel(card, artist.empty() ? "Buka Spotify/YouTube di HP" : artist.c_str(), 
                                        text_x, is_land ? 38 : 98, text_w, 18, FONT_12, C_MUTED);
    lv_label_set_long_mode(a_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Media Control Buttons (Prev, Play/Pause, Next) with clean 16px separation
    int btn_y = is_land ? 74 : 126;
    int btn_w = 42, btn_h = 36;
    int center_btn_x = content_w / 2;

    // Previous Button (left of play_btn with 16px gap)
    lv_obj_t* prev_btn = lv_btn_create(card);
    lv_obj_set_size(prev_btn, btn_w, btn_h);
    lv_obj_set_pos(prev_btn, center_btn_x - 84, btn_y);
    StyleButton(prev_btn, C_PANEL_2, C_BORDER, 8);
    lv_obj_t* p_icon = lv_label_create(prev_btn);
    lv_obj_set_style_text_font(p_icon, FONT_ICON, 0);
    lv_label_set_text(p_icon, MATERIAL_SYMBOLS_SKIP_PREVIOUS);
    lv_obj_set_style_text_color(p_icon, C_TEXT, 0);
    lv_obj_center(p_icon);
    lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(2); // PREV
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Play/Pause Button
    lv_obj_t* play_btn = lv_btn_create(card);
    lv_obj_set_size(play_btn, 52, 42);
    lv_obj_set_pos(play_btn, center_btn_x - 26, btn_y - 3);
    StyleButton(play_btn, C_AMBER, C_AMBER, 10);
    lv_obj_t* play_icon = lv_label_create(play_btn);
    lv_obj_set_style_text_font(play_icon, FONT_ICON, 0);
    lv_label_set_text(play_icon, playing ? MATERIAL_SYMBOLS_PAUSE : MATERIAL_SYMBOLS_PLAY_ARROW);
    lv_obj_set_style_text_color(play_icon, C_BG, 0);
    lv_obj_center(play_icon);
    lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(1); // TOGGLE PLAY/PAUSE
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Next Button (right of play_btn with 16px gap)
    lv_obj_t* next_btn = lv_btn_create(card);
    lv_obj_set_size(next_btn, btn_w, btn_h);
    lv_obj_set_pos(next_btn, center_btn_x + 42, btn_y);
    StyleButton(next_btn, C_PANEL_2, C_BORDER, 8);
    lv_obj_t* n_icon = lv_label_create(next_btn);
    lv_obj_set_style_text_font(n_icon, FONT_ICON, 0);
    lv_label_set_text(n_icon, MATERIAL_SYMBOLS_SKIP_NEXT);
    lv_obj_set_style_text_color(n_icon, C_TEXT, 0);
    lv_obj_center(n_icon);
    lv_obj_add_event_cb(next_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(3); // NEXT
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Volume Buttons
    int vol_y = is_land ? 122 : 180;
    MakeButton(card, "VOL -", center_btn_x - 56, vol_y, 50, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(5); // VOL DOWN
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, this);

    MakeButton(card, "VOL +", center_btn_x + 6, vol_y, 50, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(4); // VOL UP
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, this);

    MakeBottomNav(root, PAGE_BT_MUSIC);
}

void NovaDisplay::BuildNotif() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "NOTIFIKASI HP");

    int notif_count = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    if (notif_count == 0) {
        // Empty state card
        int card_y = 38;
        int card_h = is_land ? 164 : 236;
        lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_CYAN, 12);
        MakeIcon(card, ICON_CHAT, is_land ? 20 : (content_w / 2 - 24), is_land ? 30 : 40, C_CYAN, 48);

        int text_x = is_land ? 80 : 10;
        int text_w = is_land ? (content_w - 90) : (content_w - 20);

        MakeCenteredLabel(card, "Tidak Ada Notifikasi", text_x, is_land ? 36 : 100, text_w, 24, FONT_16, C_TEXT);
        MakeCenteredLabel(card, "Notifikasi baru dari HP (WhatsApp, Telegram, SMS, dll)\nakan langsung tampil di sini dan di jidat layar.", 
                          text_x, is_land ? 66 : 130, text_w, 48, FONT_10, C_MUTED);
    } else {
        // Scrollable list container
        int list_y = 36;
        int list_h = height_ - 67;
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, content_x, list_y);
        lv_obj_set_size(list, content_w, list_h);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 70, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        // Header action: Total count badge + "Hapus Semua" compact button
        lv_obj_t* hbar = lv_obj_create(list);
        lv_obj_set_pos(hbar, 0, 0);
        lv_obj_set_size(hbar, content_w, 28);
        lv_obj_set_style_bg_color(hbar, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_color(hbar, C_BORDER, 0);
        lv_obj_set_style_border_width(hbar, 1, 0);
        lv_obj_set_style_radius(hbar, 6, 0);
        lv_obj_set_style_pad_all(hbar, 2, 0);
        lv_obj_clear_flag(hbar, LV_OBJ_FLAG_SCROLLABLE);

        char cnt_str[32];
        snprintf(cnt_str, sizeof(cnt_str), "%d Notifikasi HP", notif_count);
        MakeLabel(hbar, cnt_str, 8, 4, content_w - 72, 18, FONT_12, C_CYAN);

        MakeButton(hbar, "HAPUS", content_w - 60, 3, 54, 20,
                   lv_color_hex(0x2A1215), lv_color_hex(0xEF4444), lv_color_hex(0xFCA5A5), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.clear_ble_notifs) {
                           self->callbacks_.clear_ble_notifs();
                           self->BuildNotif();
                       }
                   }, this);

        // Notification Cards
        int cur_y = 38;
        for (int i = 0; i < notif_count; ++i) {
            std::string app, title, msg, time;
            if (!callbacks_.get_ble_notif_at || !callbacks_.get_ble_notif_at(i, app, title, msg, time)) {
                continue;
            }

            uint32_t border_color = 0x38BDF8;
            if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
            else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
            else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
            else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

            int ncard_h = 88;
            lv_obj_t* ncard = MakePanel(list, 0, cur_y, content_w, ncard_h, C_PANEL, lv_color_hex(border_color), 10);
            lv_obj_clear_flag(ncard, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(ncard, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(ncard, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_bg_color(ncard, lv_color_hex(0x1E293B), LV_STATE_PRESSED);

            // Badge row
            MakeIcon(ncard, ICON_CHAT, 8, 8, lv_color_hex(border_color), 18);
            std::string a_str = app.empty() ? "Aplikasi" : app;
            MakeLabel(ncard, a_str.c_str(), 28, 8, content_w - 88, 16, FONT_12, lv_color_hex(border_color));
            if (!time.empty()) {
                MakeLabel(ncard, time.c_str(), content_w - 56, 8, 48, 16, FONT_10, C_MUTED);
            }

            // Sender / Subject
            lv_obj_t* s_lbl = MakeLabel(ncard, title.empty() ? "(Tanpa Subjek)" : title.c_str(), 10, 28, content_w - 20, 20, FONT_14, C_TEXT);
            lv_label_set_long_mode(s_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Body: Marquee continuous scroll so long messages move and are fully readable
            lv_obj_t* b_lbl = MakeLabel(ncard, msg.empty() ? "(Pesan kosong)" : msg.c_str(), 10, 48, content_w - 76, 20, FONT_12, C_MUTED);
            lv_label_set_long_mode(b_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Tap cue to indicate clickability
            MakeLabel(ncard, "Detail >", content_w - 52, 64, 48, 14, FONT_10, lv_color_hex(0x38BDF8));

            struct NotifClickCtx {
                NovaDisplay* self;
                int idx;
            };
            auto* ctx = new NotifClickCtx{this, i};
            lv_obj_add_event_cb(ncard, [](lv_event_t* e) {
                auto* ctx = static_cast<NotifClickCtx*>(lv_event_get_user_data(e));
                if (ctx && ctx->self) {
                    ctx->self->ShowNotificationDetail(ctx->idx);
                }
            }, LV_EVENT_CLICKED, ctx);
            lv_obj_add_event_cb(ncard, [](lv_event_t* e) {
                auto* ctx = static_cast<NotifClickCtx*>(lv_event_get_user_data(e));
                delete ctx;
            }, LV_EVENT_DELETE, ctx);

            cur_y += ncard_h + 8;
        }
    }

    MakeBottomNav(root, PAGE_NOTIF);
}

void NovaDisplay::BuildWeather() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "CUACA HP");

    float temp = 0;
    std::string cond = "--";
    int hum = 0;
    std::string loc = "Lokasi HP";
    int high = 0, low = 0;
    bool has_w = false;
    if (callbacks_.get_ble_weather_full) {
        has_w = callbacks_.get_ble_weather_full(temp, cond, hum, loc, high, low);
    } else if (callbacks_.get_ble_weather) {
        has_w = callbacks_.get_ble_weather(temp, cond, hum);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    int card_y = 38;
    int card_h = is_land ? 164 : 236;
    lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_AMBER, 12);

    // Weather Icon
    MakeIcon(card, ICON_WEATHER, is_land ? 16 : (content_w / 2 - 24), is_land ? 16 : 14, C_AMBER, is_land ? 36 : 42);

    char temp_str[32];
    if (has_w) {
        snprintf(temp_str, sizeof(temp_str), "%.0f\xC2\xB0" "C", temp);
    } else {
        snprintf(temp_str, sizeof(temp_str), "--\xC2\xB0" "C");
    }

    int text_x = is_land ? 60 : 0;
    int text_w = is_land ? (content_w - 70) : content_w;

    // Location / City Name
    std::string loc_str = loc.empty() ? "Smartphone Lokasi" : loc;
    lv_obj_t* loc_lbl = MakeCenteredLabel(card, loc_str.c_str(), text_x, is_land ? 12 : 58, text_w, 20, FONT_14, C_CYAN);
    lv_label_set_long_mode(loc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Big Temperature
    MakeCenteredLabel(card, temp_str, text_x, is_land ? 34 : 78, text_w, 36, FONT_36, C_AMBER);

    // Condition
    std::string c_str = cond.empty() ? "Menghubungkan ke HP..." : cond;
    lv_obj_t* cond_lbl = MakeCenteredLabel(card, c_str.c_str(), text_x, is_land ? 72 : 118, text_w, 20, FONT_14, C_TEXT);
    lv_label_set_long_mode(cond_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Temperature Range & Humidity Cards
    char stat_str[64];
    if (high > 0 || low > 0) {
        snprintf(stat_str, sizeof(stat_str), "Min %d\xC2\xB0 | Max %d\xC2\xB0  •  Kelembapan %d%%", low, high, hum);
    } else {
        snprintf(stat_str, sizeof(stat_str), "Kelembapan Udara: %d%%", hum);
    }
    MakeCenteredLabel(card, stat_str, 0, is_land ? 98 : 146, content_w, 18, FONT_12, C_CYAN);

    // Refresh Button
    MakeButton(card, "🔄 SINKRONISASI CUACA HP", (content_w - 180) / 2, is_land ? 124 : 180, 180, 32,
               lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_10,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self && self->callbacks_.ble_sync_request) {
                       self->callbacks_.ble_sync_request();
                   }
                   if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   if (self) self->BuildWeather();
               }, this);

    MakeBottomNav(root, PAGE_WEATHER);
}

void NovaDisplay::BuildPhone() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "STATUS & KONTROL HP");

    int bat = 0;
    bool chg = false;
    std::string net;
    bool has_p = false;
    if (callbacks_.get_ble_phone) {
        has_p = callbacks_.get_ble_phone(bat, chg, net);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    // 1. Phone Status Card (Battery & Network)
    lv_obj_t* scard = MakePanel(root, content_x, 38, content_w, is_land ? 62 : 72, C_PANEL, C_MINT, 12);
    MakeIcon(scard, ICON_PHONE, 10, is_land ? 10 : 14, C_MINT, 28);

    char bat_txt[64];
    if (has_p) {
        snprintf(bat_txt, sizeof(bat_txt), "Baterai HP: %d%% %s", bat, chg ? "(Mengisi Daya)" : "");
    } else {
        snprintf(bat_txt, sizeof(bat_txt), "Baterai HP: Tersambung via BLE");
    }
    MakeLabel(scard, bat_txt, 46, 10, content_w - 56, 18, FONT_14, C_TEXT);

    std::string net_txt = "Jaringan: " + (net.empty() ? "Aktif (Chronos Connected)" : net);
    MakeLabel(scard, net_txt.c_str(), 46, 32, content_w - 56, 16, FONT_12, C_CYAN);

    // 2. Actions Card (Dering HP, Remote Kamera, Sinkronisasi)
    int act_y = is_land ? 104 : 116;
    int act_h = is_land ? 100 : 166;
    lv_obj_t* acard = MakePanel(root, content_x, act_y, content_w, act_h, C_PANEL, lv_color_hex(0xEF4444), 12);

    static bool is_ringing = false;

    if (is_land) {
        // Landscape 2-column layout:
        int btn_w = (content_w - 24) / 2;

        // Col 1, Row 1: Bunyikan HP
        MakeButton(acard, is_ringing ? "BUNYIKAN LAGI" : "BUNYIKAN HP", 
                   8, 10, btn_w, 36,
                   lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = true;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(true);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Col 2, Row 1: Hentikan Dering
        MakeButton(acard, "HENTIKAN DERING", 
                   16 + btn_w, 10, btn_w, 36,
                   lv_color_hex(0x3B1219), lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = false;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(false);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Col 1, Row 2: Shutter Kamera
        MakeButton(acard, "📸 JEPRET FOTO HP", 
                   8, 52, btn_w, 36,
                   lv_color_hex(0x1E1B4B), lv_color_hex(0x818CF8), lv_color_hex(0x818CF8), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_capture_photo) self->callbacks_.ble_capture_photo();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   }, this);

        // Col 2, Row 2: Sinkronisasi
        MakeButton(acard, "🔄 SINKRON HP", 
                   16 + btn_w, 52, btn_w, 36,
                   lv_color_hex(0x164E63), C_CYAN, C_CYAN, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_sync_request) self->callbacks_.ble_sync_request();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);
    } else {
        // Portrait vertical layout:
        int btn_w = content_w - 20;

        // Button 1: BUNYIKAN HP
        MakeButton(acard, is_ringing ? "BUNYIKAN LAGI HP" : "BUNYIKAN / DERINGKAN HP", 
                   10, 10, btn_w, 34,
                   lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = true;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(true);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Button 2: HENTIKAN DERING
        MakeButton(acard, "HENTIKAN DERING", 
                   10, 48, btn_w, 32,
                   lv_color_hex(0x3B1219), lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = false;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(false);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Button 3: SHUTTER KAMERA HP
        MakeButton(acard, "📸 AMBIL FOTO HP (SHUTTER)", 
                   10, 84, btn_w, 32,
                   lv_color_hex(0x1E1B4B), lv_color_hex(0x818CF8), lv_color_hex(0x818CF8), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_capture_photo) self->callbacks_.ble_capture_photo();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   }, this);

        // Button 4: SINKRONISASI HP
        MakeButton(acard, "🔄 SINKRONISASI DATA HP", 
                   10, 120, btn_w, 32,
                   lv_color_hex(0x164E63), C_CYAN, C_CYAN, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_sync_request) self->callbacks_.ble_sync_request();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);
    }

    MakeBottomNav(root, PAGE_PHONE);
}

void NovaDisplay::UpdateNavData(const std::string& dist, const std::string& dir, const std::string& title,
                               const std::string& dur, const std::string& eta, const std::string& spd, bool active,
                               const std::string& maneuver, bool has_icon, const uint8_t* icon_data) {
    lvgl_port_lock(0);
    last_nav_maneuver_ = maneuver;
    last_nav_has_icon_ = has_icon;
    if (has_icon && icon_data) {
        memcpy(last_nav_icon_data_, icon_data, 288);
    }
    if (current_page_ == PAGE_NAV) {
        BuildNav();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdateBtMusicData(const std::string& title, const std::string& artist, bool playing) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_BT_MUSIC) {
        BuildBtMusic();
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowIncomingCallAlert(const std::string& caller, bool active) {
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (call_alert_modal_) {
        lv_obj_del(call_alert_modal_);
        call_alert_modal_ = nullptr;
    }

    if (!active) {
        lvgl_port_unlock();
        return;
    }

    int modal_w = width_ - 20;
    int modal_h = (height_ < 260) ? 96 : 110;
    call_alert_modal_ = lv_obj_create(top);
    lv_obj_set_pos(call_alert_modal_, 10, (height_ < 260) ? 10 : 20);
    lv_obj_set_size(call_alert_modal_, modal_w, modal_h);
    lv_obj_set_style_radius(call_alert_modal_, 14, 0);
    lv_obj_set_style_bg_color(call_alert_modal_, lv_color_hex(0x064E3B), 0);
    lv_obj_set_style_bg_opa(call_alert_modal_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(call_alert_modal_, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(call_alert_modal_, 3, 0);
    lv_obj_set_style_pad_all(call_alert_modal_, 8, 0);
    lv_obj_clear_flag(call_alert_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(call_alert_modal_);

    MakeIcon(call_alert_modal_, ICON_PHONE, 10, 10, lv_color_hex(0x10B981), 32);

    lv_obj_t* title = MakeLabel(call_alert_modal_, "📞 PANGGILAN TELEPON MASUK", 50, 8, modal_w - 60, 20, FONT_14, lv_color_hex(0x10B981));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    std::string cname = caller.empty() ? "Panggilan Suara" : caller;
    lv_obj_t* clbl = MakeLabel(call_alert_modal_, cname.c_str(), 50, 32, modal_w - 60, 24, FONT_16, C_TEXT);
    lv_label_set_long_mode(clbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    MakeButton(call_alert_modal_, "TUTUP PERINGATAN", 
               modal_w - 140, modal_h - 38, 130, 28,
               lv_color_hex(0x047857), lv_color_hex(0x10B981), C_TEXT, FONT_10,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) self->ShowIncomingCallAlert("", false);
               }, this);

    lvgl_port_unlock();
}

void NovaDisplay::ShowFindWatchAlert(bool active) {
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (find_watch_modal_) {
        lv_obj_del(find_watch_modal_);
        find_watch_modal_ = nullptr;
    }

    if (!active) {
        lvgl_port_unlock();
        return;
    }

    bool is_land = (width_ > 240);
    int modal_w = is_land ? 280 : 220;
    int modal_h = is_land ? 160 : 176;

    find_watch_modal_ = lv_obj_create(top);
    lv_obj_set_size(find_watch_modal_, width_, height_);
    lv_obj_set_pos(find_watch_modal_, 0, 0);
    lv_obj_clear_flag(find_watch_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(find_watch_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(find_watch_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(find_watch_modal_, 0, 0);

    lv_obj_t* card = MakePanel(find_watch_modal_, 0, 0, modal_w, modal_h, lv_color_hex(0x270707), lv_color_hex(0xEF4444), 16);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Bell icon
    lv_obj_t* icon = lv_label_create(card);
    lv_obj_set_style_text_font(icon, FONT_ICON, 0);
    lv_label_set_text(icon, MATERIAL_SYMBOLS_NOTIFICATIONS);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xEF4444), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

    // Single-line non-overlapping Title
    MakeCenteredLabel(card, "PENCARIAN SMARTPHONE", 0, 32, modal_w - 20, 20, FONT_12, lv_color_hex(0xEF4444));

    // Subtitle 1
    MakeCenteredLabel(card, "Perangkat ini sedang dicari dari HP", 0, 56, modal_w - 20, 16, FONT_10, lv_color_white());

    // Subtitle 2
    MakeCenteredLabel(card, "Tekan tombol untuk mematikan alarm", 0, 74, modal_w - 20, 16, FONT_10, C_MUTED);

    // Clear distinct button
    int btn_w = modal_w - 36;
    MakeButton(card, "SAYA DI SINI (HENTIKAN)", 18, modal_h - 48, btn_w, 36,
               lv_color_hex(0xDC2626), lv_color_hex(0xF87171), lv_color_white(), FONT_12,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) {
                       self->ShowFindWatchAlert(false);
                   }
               }, this);

    lvgl_port_unlock();
}

void NovaDisplay::UpdateWeatherFull(float temp, const std::string& cond, int hum, const std::string& loc, int high, int low) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_WEATHER) {
        BuildWeather();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdatePhoneStatus(int bat, bool chg, const std::string& net) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_PHONE) {
        BuildPhone();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdateNotif() {
    lvgl_port_lock(0);
    if (top_notif_btn_ && top_notif_icon_ && top_notif_label_) {
        int n_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        if (n_cnt > 0) {
            char n_buf[16];
            snprintf(n_buf, sizeof(n_buf), "%d", n_cnt);
            lv_label_set_text(top_notif_label_, n_buf);
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0x64748B), LV_PART_MAIN);
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(top_notif_btn_, LV_OBJ_FLAG_HIDDEN);
    }
    if (current_page_ == PAGE_NOTIF) {
        BuildNotif();
    }
    lvgl_port_unlock();
}

void NovaDisplay::DismissNotificationDetail() {
    lvgl_port_lock(0);
    if (notif_detail_modal_) {
        lv_obj_del(notif_detail_modal_);
        notif_detail_modal_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowNotificationDetail(int notif_idx) {
    if (!callbacks_.get_ble_notif_at) return;

    std::string app, title, msg, time;
    if (!callbacks_.get_ble_notif_at(notif_idx, app, title, msg, time)) {
        return;
    }

    lvgl_port_lock(0);
    DismissNotificationDetail();

    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    notif_detail_modal_ = lv_obj_create(top);
    lv_obj_set_size(notif_detail_modal_, width_, height_);
    lv_obj_set_pos(notif_detail_modal_, 0, 0);
    lv_obj_clear_flag(notif_detail_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(notif_detail_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(notif_detail_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(notif_detail_modal_, 0, 0);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 296 : 226;
    int card_h = is_land ? 224 : 290;

    uint32_t border_color = lv_color_to_int(C_CYAN);
    if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
    else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
    else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
    else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

    lv_obj_t* card = MakePanel(notif_detail_modal_, 0, 0, card_w, card_h, C_PANEL, lv_color_hex(border_color), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header: App name + time + Close button
    MakeIcon(card, ICON_CHAT, 10, 10, lv_color_hex(border_color), 18);
    MakeLabel(card, app.empty() ? "Notifikasi" : app.c_str(), 32, 10, card_w - 90, 18, FONT_12, lv_color_hex(border_color));
    if (!time.empty()) {
        MakeLabel(card, time.c_str(), card_w - 74, 10, 40, 16, FONT_10, C_MUTED);
    }

    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 24, 22);
    lv_obj_set_pos(close_btn, card_w - 30, 8);
    StyleButton(close_btn, C_PANEL_2, C_BORDER, 4);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, FONT_ICON, 0);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissNotificationDetail();
    }, LV_EVENT_CLICKED, this);

    // Divider line
    lv_obj_t* div = lv_obj_create(card);
    lv_obj_set_pos(div, 8, 34);
    lv_obj_set_size(div, card_w - 16, 1);
    lv_obj_set_style_bg_color(div, C_BORDER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Sender / Title
    std::string sender_str = title.empty() ? "(Tanpa Subjek / Pengirim)" : title;
    MakeLabel(card, "PENGIRIM / JUDUL:", 10, 40, card_w - 20, 14, FONT_10, C_MUTED);
    lv_obj_t* t_lbl = MakeLabel(card, sender_str.c_str(), 10, 54, card_w - 20, 20, FONT_14, C_TEXT);
    lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Scrollable Message Body Container
    int body_y = 80;
    int body_h = card_h - body_y - 44;
    lv_obj_t* body_box = lv_obj_create(card);
    lv_obj_set_pos(body_box, 8, body_y);
    lv_obj_set_size(body_box, card_w - 16, body_h);
    lv_obj_set_style_bg_color(body_box, C_PANEL_2, 0);
    lv_obj_set_style_border_color(body_box, C_BORDER, 0);
    lv_obj_set_style_border_width(body_box, 1, 0);
    lv_obj_set_style_radius(body_box, 8, 0);
    lv_obj_set_style_pad_all(body_box, 6, 0);
    lv_obj_set_scrollbar_mode(body_box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(body_box, LV_DIR_VER);

    lv_obj_t* msg_lbl = lv_label_create(body_box);
    lv_obj_set_width(msg_lbl, card_w - 32);
    lv_obj_set_style_text_font(msg_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(msg_lbl, C_TEXT, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msg_lbl, msg.empty() ? "(Pesan kosong)" : msg.c_str());

    // Bottom action: Close button
    MakeButton(card, "TUTUP DETAIL", 10, card_h - 38, card_w - 20, 30,
               C_PANEL_2, C_CYAN, C_TEXT, FONT_12,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) self->DismissNotificationDetail();
               }, this);

    lvgl_port_unlock();
}
