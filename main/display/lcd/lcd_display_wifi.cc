#include "lcd_display_internal.h"

struct WifiItemData {
    LcdDisplay* display;
    std::string ssid;
    bool encrypted;
};

void LcdDisplay::CreateWifiModal() {
    if (wifi_modal_ != nullptr)
        return;

    auto screen = lv_screen_active();
    if (!screen)
        return;

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    // 1. Semi-transparent Backdrop
    wifi_modal_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(wifi_modal_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(wifi_modal_backdrop_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wifi_modal_backdrop_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(wifi_modal_backdrop_, 0, 0);
    lv_obj_set_style_radius(wifi_modal_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(wifi_modal_backdrop_, 0, 0);
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        wifi_modal_backdrop_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideWifiConfigModal();
        },
        LV_EVENT_CLICKED, this);
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);

    // 2. Main Modal Card (Width: 236/312, Height: 304/232)
    bool is_land = (LV_HOR_RES > 260);
    int modal_w = is_land ? (LV_HOR_RES - 8) : 236;
    int modal_h = is_land ? (LV_VER_RES - 8) : 304;
    int view_w = modal_w - 10;

    wifi_modal_ = lv_obj_create(screen);
    lv_obj_set_size(wifi_modal_, modal_w, modal_h);
    lv_obj_align(wifi_modal_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(wifi_modal_, lv_color_hex(0x18181B), 0);  // Zinc 900
    lv_obj_set_style_bg_opa(wifi_modal_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_modal_, 2, 0);
    lv_obj_set_style_border_color(wifi_modal_, lv_color_hex(0x2563EB), 0);  // Vibrant Blue Border
    lv_obj_set_style_radius(wifi_modal_, 12, 0);
    lv_obj_set_style_pad_all(wifi_modal_, 4, 0);
    lv_obj_clear_flag(wifi_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);

    // Top Modal Header: Title, Refresh button, Close button
    lv_obj_t* header = lv_obj_create(wifi_modal_);
    lv_obj_set_size(header, view_w, 32);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 2, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title with Wifi Icon
    lv_obj_t* title_box = lv_obj_create(header);
    lv_obj_set_size(title_box, 140, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_box, 0, 0);
    lv_obj_set_style_pad_all(title_box, 0, 0);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_box, 4, 0);
    lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* h_icon = lv_label_create(title_box);
    lv_obj_set_style_text_font(h_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(h_icon, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_color(h_icon, lv_color_hex(0x38BDF8), 0);

    lv_obj_t* h_title = lv_label_create(title_box);
    lv_obj_set_style_text_font(h_title, text_font, 0);
    lv_obj_set_style_text_color(h_title, lv_color_white(), 0);
    lv_label_set_text(h_title, "Pilih Wi-Fi");

    // Action button container: Refresh and Close
    lv_obj_t* h_btns = lv_obj_create(header);
    lv_obj_set_size(h_btns, 76, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(h_btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h_btns, 0, 0);
    lv_obj_set_style_pad_all(h_btns, 0, 0);
    lv_obj_set_flex_flow(h_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h_btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(h_btns, 6, 0);
    lv_obj_clear_flag(h_btns, LV_OBJ_FLAG_SCROLLABLE);

    wifi_ref_btn_ = lv_button_create(h_btns);
    lv_obj_set_size(wifi_ref_btn_, 32, 28);
    lv_obj_set_style_radius(wifi_ref_btn_, 6, 0);
    lv_obj_set_style_bg_color(wifi_ref_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(wifi_ref_btn_, 0, 0);
    lv_obj_t* ref_lbl = lv_label_create(wifi_ref_btn_);
    lv_obj_set_style_text_font(ref_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(ref_lbl, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_color(ref_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(ref_lbl);
    lv_obj_add_event_cb(
        wifi_ref_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->StartWifiScan();
        },
        LV_EVENT_CLICKED, this);

    lv_obj_t* close_btn = lv_button_create(h_btns);
    lv_obj_set_size(close_btn, 32, 28);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(
        close_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideWifiConfigModal();
        },
        LV_EVENT_CLICKED, this);

    // ─── VIEW 1: Scanned Wi-Fi List ───
    wifi_list_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_list_view_, view_w, modal_h - 44);
    lv_obj_align(wifi_list_view_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_opa(wifi_list_view_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_view_, 0, 0);
    lv_obj_clear_flag(wifi_list_view_, LV_OBJ_FLAG_SCROLLABLE);

    wifi_scan_status_label_ = lv_label_create(wifi_list_view_);
    lv_obj_set_style_text_font(wifi_scan_status_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_scan_status_label_, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(wifi_scan_status_label_, "Memindai Wi-Fi di sekitar...");
    lv_obj_align(wifi_scan_status_label_, LV_ALIGN_TOP_LEFT, 4, 2);

    wifi_list_container_ = lv_obj_create(wifi_list_view_);
    lv_obj_set_size(wifi_list_container_, view_w, modal_h - 70);
    lv_obj_align(wifi_list_container_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_list_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list_container_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_container_, 0, 0);
    lv_obj_set_flex_flow(wifi_list_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_list_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_list_container_, 6, 0);
    lv_obj_add_flag(wifi_list_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wifi_list_container_, LV_DIR_VER);

    // ─── VIEW 2: Password Input & Virtual Keyboard ───
    wifi_pwd_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_pwd_view_, view_w, modal_h - 40);
    lv_obj_align(wifi_pwd_view_, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_opa(wifi_pwd_view_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_pwd_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_pwd_view_, 0, 0);
    lv_obj_clear_flag(wifi_pwd_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);

    // Subheader: Back button + Selected SSID
    lv_obj_t* pwd_sub = lv_obj_create(wifi_pwd_view_);
    lv_obj_set_size(pwd_sub, view_w, 26);
    lv_obj_set_style_bg_opa(pwd_sub, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pwd_sub, 0, 0);
    lv_obj_set_style_pad_all(pwd_sub, 0, 0);
    lv_obj_set_flex_flow(pwd_sub, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pwd_sub, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pwd_sub, 6, 0);
    lv_obj_clear_flag(pwd_sub, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(pwd_sub);
    lv_obj_set_size(back_btn, 28, 24);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_obj_set_style_text_font(back_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(
        back_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_CLICKED, this);

    wifi_pwd_ssid_label_ = lv_label_create(pwd_sub);
    lv_obj_set_style_text_font(wifi_pwd_ssid_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_pwd_ssid_label_, lv_color_hex(0xFDE047), 0);
    lv_obj_set_width(wifi_pwd_ssid_label_, view_w - 40);
    lv_label_set_long_mode(wifi_pwd_ssid_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(wifi_pwd_ssid_label_, "SSID: -");

    // Input row: Text Area + Eye Toggle Button (+ action buttons in landscape)
    lv_obj_t* input_row = lv_obj_create(wifi_pwd_view_);
    lv_obj_set_size(input_row, view_w, is_land ? 28 : 36);
    lv_obj_align(input_row, LV_ALIGN_TOP_MID, 0, is_land ? 24 : 28);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(input_row, 4, 0);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    wifi_pwd_ta_ = lv_textarea_create(input_row);
    lv_obj_set_size(wifi_pwd_ta_, is_land ? 134 : (view_w - 44), is_land ? 26 : 34);
    lv_textarea_set_one_line(wifi_pwd_ta_, true);
    lv_textarea_set_placeholder_text(wifi_pwd_ta_, "Kata Sandi");
    // User requested password to be visible!
    wifi_pwd_visible_ = true;
    lv_textarea_set_password_mode(wifi_pwd_ta_, !wifi_pwd_visible_);
    lv_obj_set_style_bg_color(wifi_pwd_ta_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_text_color(wifi_pwd_ta_, lv_color_white(), 0);
    lv_obj_set_style_border_width(wifi_pwd_ta_, 1, 0);
    lv_obj_set_style_border_color(wifi_pwd_ta_, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_radius(wifi_pwd_ta_, 6, 0);
    lv_obj_set_style_pad_all(wifi_pwd_ta_, is_land ? 3 : 6, 0);

    // Eye toggle button (intip sandi)
    wifi_pwd_eye_btn_ = lv_button_create(input_row);
    lv_obj_set_size(wifi_pwd_eye_btn_, is_land ? 28 : 38, is_land ? 26 : 34);
    lv_obj_set_style_radius(wifi_pwd_eye_btn_, 6, 0);
    lv_obj_set_style_bg_color(wifi_pwd_eye_btn_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(wifi_pwd_eye_btn_, 0, 0);
    wifi_pwd_eye_icon_ = lv_label_create(wifi_pwd_eye_btn_);
    lv_obj_set_style_text_font(wifi_pwd_eye_icon_, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
    lv_obj_set_style_text_color(wifi_pwd_eye_icon_, lv_color_hex(0x10B981), 0);
    lv_obj_center(wifi_pwd_eye_icon_);

    lv_obj_add_event_cb(
        wifi_pwd_eye_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->wifi_pwd_visible_ = !display->wifi_pwd_visible_;
            if (display->wifi_pwd_ta_) {
                lv_textarea_set_password_mode(display->wifi_pwd_ta_, !display->wifi_pwd_visible_);
            }
            if (display->wifi_pwd_eye_icon_) {
                if (display->wifi_pwd_visible_) {
                    lv_label_set_text(display->wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
                    lv_obj_set_style_text_color(display->wifi_pwd_eye_icon_, lv_color_hex(0x10B981),
                                                0);  // Green = Visible
                } else {
                    lv_label_set_text(display->wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK);
                    lv_obj_set_style_text_color(display->wifi_pwd_eye_icon_, lv_color_hex(0xF59E0B),
                                                0);  // Amber = Hidden
                }
            }
        },
        LV_EVENT_CLICKED, this);

    // In landscape, put action buttons directly into input_row for 0px wasted vertical space!
    lv_obj_t* btn_parent = is_land ? input_row : lv_obj_create(wifi_pwd_view_);
    if (!is_land) {
        lv_obj_set_size(btn_parent, view_w, 30);
        lv_obj_align(btn_parent, LV_ALIGN_TOP_MID, 0, 66);
        lv_obj_set_style_bg_opa(btn_parent, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_parent, 0, 0);
        lv_obj_set_style_pad_all(btn_parent, 0, 0);
        lv_obj_set_flex_flow(btn_parent, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_parent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btn_parent, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* cancel_btn = lv_button_create(btn_parent);
    lv_obj_set_size(cancel_btn, is_land ? 52 : 80, is_land ? 26 : 28);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(cancel_btn, 0, 0);
    lv_obj_t* c_lbl = lv_label_create(cancel_btn);
    lv_obj_set_style_text_font(c_lbl, text_font, 0);
    lv_label_set_text(c_lbl, "Batal");
    lv_obj_center(c_lbl);
    lv_obj_add_event_cb(
        cancel_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
        },
        LV_EVENT_CLICKED, this);

    lv_obj_t* conn_btn = lv_button_create(btn_parent);
    lv_obj_set_size(conn_btn, is_land ? 72 : 138, is_land ? 26 : 28);
    lv_obj_set_style_radius(conn_btn, 6, 0);
    lv_obj_set_style_bg_color(conn_btn, lv_color_hex(0x16A34A), 0);  // Green Connect
    lv_obj_set_style_pad_all(conn_btn, 0, 0);
    lv_obj_t* conn_lbl = lv_label_create(conn_btn);
    lv_obj_set_style_text_font(conn_lbl, text_font, 0);
    lv_label_set_text(conn_lbl, is_land ? "Konek" : "Hubungkan");
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(
        conn_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            const char* pwd = lv_textarea_get_text(display->wifi_pwd_ta_);
            display->ConnectToWifi(display->wifi_selected_ssid_, pwd ? pwd : "");
        },
        LV_EVENT_CLICKED, this);

    // Virtual Keyboard (Width: view_w, Height: modal_h - 56 in landscape)
#if LV_USE_KEYBOARD
    wifi_keyboard_ = lv_keyboard_create(wifi_pwd_view_);
    lv_obj_set_size(wifi_keyboard_, view_w, is_land ? (modal_h - 58) : 160);
    lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_keyboard_, wifi_pwd_ta_);
    lv_obj_add_event_cb(
        wifi_keyboard_,
        [](lv_event_t* e) {
            auto code = lv_event_get_code(e);
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (code == LV_EVENT_READY) {
                const char* pwd = lv_textarea_get_text(display->wifi_pwd_ta_);
                display->ConnectToWifi(display->wifi_selected_ssid_, pwd ? pwd : "");
            } else if (code == LV_EVENT_CANCEL) {
                lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
            }
        },
        LV_EVENT_ALL, this);
#endif

    // ─── VIEW 3: Connecting Status Overlay ───
    wifi_connecting_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_connecting_view_, view_w, modal_h - 44);
    lv_obj_align(wifi_connecting_view_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(wifi_connecting_view_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_border_width(wifi_connecting_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_connecting_view_, 12, 0);
    lv_obj_set_flex_flow(wifi_connecting_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_connecting_view_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_connecting_view_, 12, 0);
    lv_obj_clear_flag(wifi_connecting_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* conn_icon = lv_label_create(wifi_connecting_view_);
    lv_obj_set_style_text_font(conn_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(conn_icon, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_color(conn_icon, lv_color_hex(0x38BDF8), 0);

    wifi_connecting_label_ = lv_label_create(wifi_connecting_view_);
    lv_obj_set_style_text_font(wifi_connecting_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_connecting_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(wifi_connecting_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(wifi_connecting_label_, "Menghubungkan...");
}

void LcdDisplay::ShowWifiConfigModal() {
    DisplayLockGuard lock(this);
    if (wifi_modal_) {
        bool is_land = (LV_HOR_RES > 260);
        int expected_w = is_land ? (LV_HOR_RES - 8) : 236;
        if (lv_obj_get_width(wifi_modal_) != expected_w) {
            lv_obj_delete_async(wifi_modal_);
            wifi_modal_ = nullptr;
            if (wifi_modal_backdrop_) {
                lv_obj_delete_async(wifi_modal_backdrop_);
                wifi_modal_backdrop_ = nullptr;
            }
        }
    }
    CreateWifiModal();
    if (!wifi_modal_ || !wifi_modal_backdrop_)
        return;

    wifi_modal_visible_ = true;
    lv_obj_remove_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    StartWifiScan();
}

void LcdDisplay::HideWifiConfigModal() {
    DisplayLockGuard lock(this);
    if (!wifi_modal_ || !wifi_modal_backdrop_)
        return;

    wifi_modal_visible_ = false;
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::StartWifiScan() {
    if (!wifi_modal_ || !wifi_list_container_)
        return;

    // Prevent concurrent/overlapping scans that cause ESP_ERR_WIFI_STATE conflicts
    if (wifi_scanning_.exchange(true)) {
        ESP_LOGI("LcdDisplay", "Wi-Fi scan already in progress, ignoring duplicate trigger");
        return;
    }

    lv_obj_clean(wifi_list_container_);
    if (wifi_scan_status_label_) {
        lv_label_set_text(wifi_scan_status_label_, "Memindai Wi-Fi di sekitar...");
    }
    if (wifi_ref_btn_) {
        lv_obj_add_state(wifi_ref_btn_, LV_STATE_DISABLED);
    }

    xTaskCreate(
        [](void* arg) {
            auto display = static_cast<LcdDisplay*>(arg);

            // 1. Ensure WiFi driver is active and in Station mode
            wifi_mode_t cur_mode;
            if (esp_wifi_get_mode(&cur_mode) != ESP_OK) {
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
            } else if (cur_mode != WIFI_MODE_STA && cur_mode != WIFI_MODE_APSTA) {
                esp_wifi_set_mode(WIFI_MODE_STA);
            }

            // 2. Temporarily disable modem sleep / power save so the radio catches all beacons &
            // probes
            wifi_ps_type_t orig_ps = WIFI_PS_NONE;
            esp_wifi_get_ps(&orig_ps);
            esp_wifi_set_ps(WIFI_PS_NONE);

            std::vector<std::tuple<std::string, int, bool>> results;
            std::vector<std::string> seen;

            // 3. Robust scan loop with up to 3 attempts
            for (int attempt = 1; attempt <= 3; attempt++) {
                // Cancel any lingering or background scan
                esp_wifi_scan_stop();
                vTaskDelay(pdMS_TO_TICKS(100));

                wifi_scan_config_t scan_config = {};
                scan_config.show_hidden = false;
                scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
                scan_config.scan_time.active.min = 120;
                scan_config.scan_time.active.max = 280;

                esp_err_t err = esp_wifi_scan_start(&scan_config, true);
                if (err == ESP_OK) {
                    uint16_t ap_count = 0;
                    esp_wifi_scan_get_ap_num(&ap_count);
                    if (ap_count > 0) {
                        std::vector<wifi_ap_record_t> records(ap_count);
                        esp_wifi_scan_get_ap_records(&ap_count, records.data());

                        std::sort(records.begin(), records.end(),
                                  [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
                                      return a.rssi > b.rssi;
                                  });

                        for (const auto& ap : records) {
                            std::string ssid = reinterpret_cast<const char*>(ap.ssid);
                            if (ssid.empty())
                                continue;
                            if (std::find(seen.begin(), seen.end(), ssid) != seen.end())
                                continue;
                            seen.push_back(ssid);
                            bool enc = (ap.authmode != WIFI_AUTH_OPEN);
                            results.push_back({ssid, ap.rssi, enc});
                            if (results.size() >= 20)
                                break;
                        }

                        if (!results.empty()) {
                            ESP_LOGI("LcdDisplay",
                                     "Wi-Fi scan succeeded on attempt %d: found %d APs", attempt,
                                     (int)results.size());
                            break;
                        }
                    }
                } else if (err == ESP_ERR_WIFI_STATE) {
                    ESP_LOGW("LcdDisplay",
                             "Wi-Fi driver busy on attempt %d (ESP_ERR_WIFI_STATE), checking "
                             "cached scan...",
                             attempt);
                    uint16_t cached_ap_count = 0;
                    if (esp_wifi_scan_get_ap_num(&cached_ap_count) == ESP_OK &&
                        cached_ap_count > 0) {
                        std::vector<wifi_ap_record_t> records(cached_ap_count);
                        esp_wifi_scan_get_ap_records(&cached_ap_count, records.data());
                        for (const auto& ap : records) {
                            std::string ssid = reinterpret_cast<const char*>(ap.ssid);
                            if (ssid.empty())
                                continue;
                            if (std::find(seen.begin(), seen.end(), ssid) != seen.end())
                                continue;
                            seen.push_back(ssid);
                            bool enc = (ap.authmode != WIFI_AUTH_OPEN);
                            results.push_back({ssid, ap.rssi, enc});
                            if (results.size() >= 20)
                                break;
                        }
                        if (!results.empty()) {
                            ESP_LOGI("LcdDisplay",
                                     "Recovered %d APs from completed background scan",
                                     (int)results.size());
                            break;
                        }
                    }
                    // Disconnect any ongoing connect attempt to free up the radio
                    esp_wifi_scan_stop();
                    esp_wifi_disconnect();
                    vTaskDelay(pdMS_TO_TICKS(250));
                } else {
                    ESP_LOGW("LcdDisplay", "esp_wifi_scan_start attempt %d returned %s", attempt,
                             esp_err_to_name(err));
                    vTaskDelay(pdMS_TO_TICKS(150));
                }
            }

            // 4. Restore original power saving mode
            esp_wifi_set_ps(orig_ps);

            display->wifi_scanning_ = false;

            Application::GetInstance().Schedule(
                [display, results]() { display->PopulateWifiScanResults(results); });

            vTaskDelete(NULL);
        },
        "wifi_scanner", 4096, this, 3, NULL);
}

void LcdDisplay::PopulateWifiScanResults(
    const std::vector<std::tuple<std::string, int, bool>>& aps) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_ || !wifi_list_container_)
        return;

    if (wifi_ref_btn_) {
        lv_obj_remove_state(wifi_ref_btn_, LV_STATE_DISABLED);
    }

    lv_obj_clean(wifi_list_container_);

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    if (aps.empty()) {
        if (wifi_scan_status_label_) {
            lv_label_set_text(wifi_scan_status_label_, "Tidak ada Wi-Fi ditemukan.");
        }

        lv_obj_t* retry_btn = lv_button_create(wifi_list_container_);
        lv_obj_set_size(retry_btn, 170, 36);
        lv_obj_set_style_radius(retry_btn, 8, 0);
        lv_obj_set_style_bg_color(retry_btn, lv_color_hex(0x27272A), 0);
        lv_obj_set_style_border_width(retry_btn, 1, 0);
        lv_obj_set_style_border_color(retry_btn, lv_color_hex(0x0284C7), 0);
        lv_obj_align(retry_btn, LV_ALIGN_CENTER, 0, 16);

        lv_obj_t* retry_lbl = lv_label_create(retry_btn);
        lv_obj_set_style_text_font(retry_lbl, text_font, 0);
        lv_label_set_text(retry_lbl, "🔄 Pindai Ulang");
        lv_obj_set_style_text_color(retry_lbl, lv_color_hex(0x38BDF8), 0);
        lv_obj_center(retry_lbl);

        lv_obj_add_event_cb(
            retry_btn,
            [](lv_event_t* e) {
                auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
                display->StartWifiScan();
            },
            LV_EVENT_CLICKED, this);
        return;
    }

    if (wifi_scan_status_label_) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Ditemukan %d Wi-Fi (Sentuh untuk pilih):", (int)aps.size());
        lv_label_set_text(wifi_scan_status_label_, buf);
    }

    for (const auto& [ssid, rssi, enc] : aps) {
        lv_obj_t* btn = lv_button_create(wifi_list_container_);
        lv_obj_set_size(btn, 206, 38);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x27272A), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x3F3F46), 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        // Left box: Wifi icon + SSID name
        lv_obj_t* left_box = lv_obj_create(btn);
        lv_obj_set_size(left_box, 155, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left_box, 0, 0);
        lv_obj_set_style_pad_all(left_box, 0, 0);
        lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(left_box, 6, 0);
        lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* icon = lv_label_create(left_box);
        lv_obj_set_style_text_font(icon, &BUILTIN_ICON_FONT, 0);
        const char* wifi_glyph = (rssi >= -65) ? MATERIAL_SYMBOLS_WIFI
                                               : ((rssi >= -78) ? MATERIAL_SYMBOLS_WIFI_2_BAR
                                                                : MATERIAL_SYMBOLS_WIFI_1_BAR);
        lv_label_set_text(icon, wifi_glyph);
        lv_obj_set_style_text_color(
            icon, lv_color_hex(rssi >= -65 ? 0x22C55E : (rssi >= -78 ? 0xEAB308 : 0xEF4444)), 0);

        lv_obj_t* name = lv_label_create(left_box);
        lv_obj_set_style_text_font(name, text_font, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_width(name, 125);
        lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(name, ssid.c_str());

        // Right box: Lock icon
        lv_obj_t* lock = lv_label_create(btn);
        lv_obj_set_style_text_font(lock, &BUILTIN_ICON_FONT, 0);
        if (enc) {
            lv_label_set_text(lock, MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(lock, lv_color_hex(0xF59E0B), 0);
        } else {
            lv_label_set_text(lock, MATERIAL_SYMBOLS_LOCK_OPEN);
            lv_obj_set_style_text_color(lock, lv_color_hex(0x10B981), 0);
        }

        auto* item_data = new WifiItemData{this, ssid, enc};
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* data = static_cast<WifiItemData*>(lv_event_get_user_data(e));
                if (!data)
                    return;
                if (data->encrypted) {
                    data->display->ShowWifiPasswordInput(data->ssid);
                } else {
                    data->display->ConnectToWifi(data->ssid, "");
                }
            },
            LV_EVENT_CLICKED, item_data);

        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                    auto* data = static_cast<WifiItemData*>(lv_event_get_user_data(e));
                    delete data;
                }
            },
            LV_EVENT_DELETE, item_data);
    }
}

void LcdDisplay::ShowWifiPasswordInput(const std::string& ssid) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_)
        return;

    wifi_selected_ssid_ = ssid;

    lv_obj_add_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    if (wifi_pwd_ssid_label_) {
        std::string txt = "SSID: " + ssid;
        lv_label_set_text(wifi_pwd_ssid_label_, txt.c_str());
    }

    if (wifi_pwd_ta_) {
        lv_textarea_set_text(wifi_pwd_ta_, "");
        // User requested: "saya mau kata sandi nya bisa di lihat ya"
        wifi_pwd_visible_ = true;
        lv_textarea_set_password_mode(wifi_pwd_ta_, !wifi_pwd_visible_);
    }

    if (wifi_pwd_eye_icon_) {
        lv_label_set_text(wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
        lv_obj_set_style_text_color(wifi_pwd_eye_icon_, lv_color_hex(0x10B981), 0);
    }

#if LV_USE_KEYBOARD
    if (wifi_keyboard_ && wifi_pwd_ta_) {
        lv_keyboard_set_textarea(wifi_keyboard_, wifi_pwd_ta_);
    }
#endif
}

void LcdDisplay::ConnectToWifi(const std::string& ssid, const std::string& password) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_)
        return;

    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    if (wifi_connecting_label_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Menghubungkan ke\n%s...", ssid.c_str());
        lv_label_set_text(wifi_connecting_label_, buf);
    }

    // Save SSID & Password to NVS
    SsidManager::GetInstance().AddSsid(ssid, password);

    // Launch connection attempt in background
    struct ConnectParams {
        LcdDisplay* display;
        std::string ssid;
        std::string password;
    };
    auto* params = new ConnectParams{this, ssid, password};

    xTaskCreate(
        [](void* arg) {
            auto* p = static_cast<ConnectParams*>(arg);
            auto display = p->display;
            std::string target_ssid = p->ssid;
            std::string target_password = p->password;
            delete p;

            // Restart station so WifiManager and WifiStation reload the new SSID from SsidManager
            // immediately
            auto& mgr = WifiManager::GetInstance();
            if (mgr.IsInitialized()) {
                mgr.StopStation();
                vTaskDelay(pdMS_TO_TICKS(100));
                mgr.StartStation();
            } else {
                wifi_config_t wifi_cfg = {};
                strncpy((char*)wifi_cfg.sta.ssid, target_ssid.c_str(),
                        sizeof(wifi_cfg.sta.ssid) - 1);
                strncpy((char*)wifi_cfg.sta.password, target_password.c_str(),
                        sizeof(wifi_cfg.sta.password) - 1);
                wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                esp_wifi_connect();
            }

            // Wait up to 15 seconds for connection
            bool ok = false;
            for (int i = 0; i < 30; i++) {
                vTaskDelay(pdMS_TO_TICKS(500));
                if (WifiManager::GetInstance().IsConnected()) {
                    ok = true;
                    break;
                }
            }

            if (ok) {
                Application::GetInstance().Schedule([display]() {
                    DisplayLockGuard lock(display);
                    display->ShowNotification("Wi-Fi Berhasil Terhubung!");
                    display->HideWifiConfigModal();
                    display->UpdateStatusBar(true);
                });
            } else {
                Application::GetInstance().Schedule([display, target_ssid]() {
                    DisplayLockGuard lock(display);
                    if (display->wifi_connecting_label_) {
                        char err_msg[128];
                        snprintf(err_msg, sizeof(err_msg),
                                 "Gagal terhubung ke\n%s!\nPeriksa kata sandi.",
                                 target_ssid.c_str());
                        lv_label_set_text(display->wifi_connecting_label_, err_msg);
                    }
                });
                vTaskDelay(pdMS_TO_TICKS(2000));
                Application::GetInstance().Schedule([display]() {
                    DisplayLockGuard lock(display);
                    lv_obj_add_flag(display->wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
                });
            }

            vTaskDelete(NULL);
        },
        "wifi_connector", 4096, params, 3, NULL);
}
