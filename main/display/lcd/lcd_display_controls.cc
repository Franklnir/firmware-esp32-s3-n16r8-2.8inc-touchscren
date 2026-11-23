#include "lcd_display_internal.h"

void LcdDisplay::SetBrightness(uint8_t brightness) {
    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight) {
        backlight->SetBrightness(brightness);
    }
}

void LcdDisplay::CreateControlCenter() {
    auto screen = lv_screen_active();
    if (!screen)
        return;

    // 1. Semi-transparent backdrop (click to dismiss) - Deep Obsidian Glass
    cp_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(cp_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(cp_backdrop_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(cp_backdrop_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(cp_backdrop_, 0, 0);
    lv_obj_set_style_radius(cp_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(cp_backdrop_, 0, 0);
    lv_obj_align(cp_backdrop_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        cp_backdrop_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;

    // 2. Control Drawer (Bottom half screen - Nova Cyber Glass Panel)
    cp_drawer_ = lv_obj_create(screen);
    lv_obj_set_size(cp_drawer_, LV_HOR_RES, 205);
    lv_obj_set_style_radius(cp_drawer_, 16, 0);
    lv_obj_set_style_bg_color(cp_drawer_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(cp_drawer_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(cp_drawer_, 2, 0);
    lv_obj_set_style_border_color(cp_drawer_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(cp_drawer_, 6, 0);
    lv_obj_set_style_pad_row(cp_drawer_, 5, 0);
    lv_obj_set_flex_flow(cp_drawer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cp_drawer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(cp_drawer_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(cp_drawer_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);

    // Top Grab Handle pill
    lv_obj_t* handle = lv_obj_create(cp_drawer_);
    lv_obj_set_size(handle, 32, 4);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(0x71717A), 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);

    // Top Quick Info Row: Wi-Fi, CPU, Mode
    lv_obj_t* top_row = lv_obj_create(cp_drawer_);
    lv_obj_set_size(top_row, 228, 28);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);

    // Wi-Fi Chip
    lv_obj_t* wifi_chip = lv_button_create(top_row);
    lv_obj_set_size(wifi_chip, 72, 26);
    lv_obj_set_style_bg_color(wifi_chip, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(wifi_chip, 6, 0);
    lv_obj_set_style_border_width(wifi_chip, 0, 0);
    lv_obj_set_style_pad_all(wifi_chip, 2, 0);
    cp_wifi_label_ = lv_label_create(wifi_chip);
    lv_label_set_text(cp_wifi_label_, MATERIAL_SYMBOLS_WIFI " Wi-Fi");
    lv_obj_set_style_text_color(cp_wifi_label_, lv_color_hex(0x22C55E), 0);
    lv_obj_center(cp_wifi_label_);
    lv_obj_add_event_cb(
        wifi_chip,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideControlCenter();
            display->ShowWifiConfigModal();
        },
        LV_EVENT_CLICKED, this);

    // Text Size Button Chip (Replaced 240M as requested)
    cp_text_size_btn_ = lv_button_create(top_row);
    lv_obj_set_size(cp_text_size_btn_, 72, 26);
    lv_obj_set_style_bg_color(cp_text_size_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(cp_text_size_btn_, 6, 0);
    lv_obj_set_style_border_width(cp_text_size_btn_, 0, 0);
    lv_obj_set_style_pad_all(cp_text_size_btn_, 2, 0);
    cp_text_size_label_ = lv_label_create(cp_text_size_btn_);
    lv_label_set_text(cp_text_size_label_, MATERIAL_SYMBOLS_EDIT_SQUARE " Teks");
    lv_obj_set_style_text_color(cp_text_size_label_, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(cp_text_size_label_);
    lv_obj_add_event_cb(
        cp_text_size_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideControlCenter();
            display->ShowTextSizeOverlay();
        },
        LV_EVENT_CLICKED, this);

    // Mode Switch Button Chip
    cp_mode_btn_ = lv_button_create(top_row);
    lv_obj_set_size(cp_mode_btn_, 72, 26);
    lv_obj_set_style_bg_color(cp_mode_btn_, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(cp_mode_btn_, 6, 0);
    lv_obj_set_style_border_width(cp_mode_btn_, 0, 0);
    lv_obj_set_style_pad_all(cp_mode_btn_, 2, 0);
    cp_mode_label_ = lv_label_create(cp_mode_btn_);
    Settings chronchi_set("chronchi", true);
    int current_mode = chronchi_set.GetInt("mode", 0);
    lv_label_set_text(cp_mode_label_, current_mode == 1 ? (MATERIAL_SYMBOLS_WATCH " Watch")
                                                        : (MATERIAL_SYMBOLS_ROBOT_2 " AI"));
    lv_obj_set_style_text_color(cp_mode_label_, lv_color_white(), 0);
    lv_obj_center(cp_mode_label_);
    lv_obj_add_event_cb(
        cp_mode_btn_,
        [](lv_event_t* e) {
            (void)e;
            Settings set("chronchi", true);
            int cur = set.GetInt("mode", 0);
            int target = (cur == 1) ? 0 : 1;
            Board::GetInstance().SwitchToMode(target);
        },
        LV_EVENT_CLICKED, this);

    // Middle Row: Volume Slider, Brightness Slider, Theme & Color Card
    lv_obj_t* mid_row = lv_obj_create(cp_drawer_);
    lv_obj_set_size(mid_row, 228, 108);
    lv_obj_set_flex_flow(mid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(mid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_row, 0, 0);
    lv_obj_set_style_pad_all(mid_row, 0, 0);

    // 1. Speaker Volume Card (Vertical Slider)
    lv_obj_t* vol_card = lv_obj_create(mid_row);
    lv_obj_set_size(vol_card, 52, 104);
    lv_obj_set_style_bg_color(vol_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(vol_card, 12, 0);
    lv_obj_set_style_border_width(vol_card, 0, 0);
    lv_obj_set_style_pad_all(vol_card, 3, 0);
    lv_obj_set_flex_flow(vol_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;

    cp_vol_label_ = lv_label_create(vol_card);
    lv_label_set_text_fmt(cp_vol_label_, "%d%%", cur_vol);
    lv_obj_set_style_text_color(cp_vol_label_, lv_color_white(), 0);

    cp_vol_slider_ = lv_slider_create(vol_card);
    lv_obj_set_size(cp_vol_slider_, 22, 54);
    lv_slider_set_range(cp_vol_slider_, 0, 100);
    lv_slider_set_value(cp_vol_slider_, cur_vol, LV_ANIM_OFF);
    lv_obj_set_style_radius(cp_vol_slider_, 8, 0);
    lv_obj_set_style_bg_color(cp_vol_slider_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_bg_color(cp_vol_slider_, lv_color_hex(0x06B6D4), LV_PART_INDICATOR);
    lv_obj_set_style_radius(cp_vol_slider_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cp_vol_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(cp_vol_slider_, 0, LV_PART_KNOB);

    cp_vol_icon_ = lv_label_create(vol_card);
    lv_label_set_text(cp_vol_icon_,
                      cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    lv_obj_set_style_text_color(cp_vol_icon_, lv_color_hex(0x06B6D4), 0);

    lv_obj_add_event_cb(
        cp_vol_slider_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto c = Board::GetInstance().GetAudioCodec();
            if (c)
                c->SetOutputVolume(val);
            if (display->cp_vol_label_)
                lv_label_set_text_fmt(display->cp_vol_label_, "%d%%", val);
            if (display->cp_vol_icon_) {
                lv_label_set_text(display->cp_vol_icon_, val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP
                                                                 : MATERIAL_SYMBOLS_VOLUME_OFF);
            }
        },
        LV_EVENT_VALUE_CHANGED, this);

    // 2. Brightness Card (Vertical Slider)
    lv_obj_t* br_card = lv_obj_create(mid_row);
    lv_obj_set_size(br_card, 52, 104);
    lv_obj_set_style_bg_color(br_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(br_card, 12, 0);
    lv_obj_set_style_border_width(br_card, 0, 0);
    lv_obj_set_style_pad_all(br_card, 3, 0);
    lv_obj_set_flex_flow(br_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(br_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);

    cp_bright_label_ = lv_label_create(br_card);
    lv_label_set_text_fmt(cp_bright_label_, "%d%%", cur_br);
    lv_obj_set_style_text_color(cp_bright_label_, lv_color_white(), 0);

    cp_bright_slider_ = lv_slider_create(br_card);
    lv_obj_set_size(cp_bright_slider_, 22, 54);
    lv_slider_set_range(cp_bright_slider_, 10, 100);
    lv_slider_set_value(cp_bright_slider_, cur_br, LV_ANIM_OFF);
    lv_obj_set_style_radius(cp_bright_slider_, 8, 0);
    lv_obj_set_style_bg_color(cp_bright_slider_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_bg_color(cp_bright_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(cp_bright_slider_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cp_bright_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(cp_bright_slider_, 0, LV_PART_KNOB);

    lv_obj_t* br_icon = lv_label_create(br_card);
    lv_label_set_text(br_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

    lv_obj_add_event_cb(
        cp_bright_slider_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            display->SetBrightness(val);
            Settings s("display", true);
            s.SetInt("brightness", val);
            if (display->cp_bright_label_)
                lv_label_set_text_fmt(display->cp_bright_label_, "%d%%", val);
        },
        LV_EVENT_VALUE_CHANGED, this);

    // 3. Theme & Color Card
    lv_obj_t* act_card = lv_obj_create(mid_row);
    lv_obj_set_size(act_card, 110, 104);
    lv_obj_set_style_bg_color(act_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(act_card, 12, 0);
    lv_obj_set_style_border_width(act_card, 0, 0);
    lv_obj_set_style_pad_all(act_card, 4, 0);
    lv_obj_set_flex_flow(act_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(act_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Button: Icon Thema (Click toggles between Thema 1 and Thema 2)
    cp_theme_icon_btn_ = lv_button_create(act_card);
    lv_obj_set_size(cp_theme_icon_btn_, 100, 42);
    lv_obj_set_style_radius(cp_theme_icon_btn_, 8, 0);
    lv_obj_set_style_bg_color(cp_theme_icon_btn_, lv_color_hex(0x3F3F46), 0);
    cp_theme_icon_label_ = lv_label_create(cp_theme_icon_btn_);
    if (ui_theme_id_ == 1) {
        lv_label_set_text(cp_theme_icon_label_, MATERIAL_SYMBOLS_EDIT_SQUARE " Ke Thema 2");
        lv_obj_set_style_text_color(cp_theme_icon_label_, lv_color_hex(0x22C55E), 0);
    } else {
        lv_label_set_text(cp_theme_icon_label_, MATERIAL_SYMBOLS_ROBOT_2 " Ke Thema 1");
        lv_obj_set_style_text_color(cp_theme_icon_label_, lv_color_hex(0xFFE600), 0);
    }
    lv_obj_center(cp_theme_icon_label_);

    lv_obj_add_event_cb(
        cp_theme_icon_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideControlCenter();
            lv_async_call(
                [](void* user_data) {
                    auto disp = static_cast<LcdDisplay*>(user_data);
                    int target = (disp->GetUiThemeId() == 1) ? 2 : 1;
                    disp->SetUiThemeId(target);
                    Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                },
                display);
        },
        LV_EVENT_CLICKED, this);

    // Button: Icon Warna (Click opens Header Icon Color Picker!)
    lv_obj_t* color_btn = lv_button_create(act_card);
    lv_obj_set_size(color_btn, 100, 42);
    lv_obj_set_style_radius(color_btn, 8, 0);
    lv_obj_set_style_bg_color(color_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* color_lbl = lv_label_create(color_btn);
    lv_label_set_text(color_lbl, MATERIAL_SYMBOLS_EXPLORE " Warna Icon");
    lv_obj_set_style_text_color(color_lbl, lv_color_hex(header_icon_color_), 0);
    lv_obj_center(color_lbl);

    lv_obj_add_event_cb(
        color_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideControlCenter();
            display->ShowHeaderColorOverlay();
        },
        LV_EVENT_CLICKED, this);

    // Bottom Color Palette Row (Real-time color choices)
    cp_color_box_ = lv_obj_create(cp_drawer_);
    lv_obj_set_size(cp_color_box_, 228, 36);
    lv_obj_set_flex_flow(cp_color_box_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cp_color_box_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cp_color_box_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(cp_color_box_, 8, 0);
    lv_obj_set_style_border_width(cp_color_box_, 0, 0);
    lv_obj_set_style_pad_all(cp_color_box_, 4, 0);
    lv_obj_set_style_pad_column(cp_color_box_, 6, 0);
    lv_obj_set_scroll_dir(cp_color_box_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(cp_color_box_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);

    struct ColorItem {
        const char* theme_name;
        uint32_t color_hex;
    };
    static const ColorItem color_items[] = {
        {"putih", 0xFFFFFF},  {"biru", 0x38BDF8},   {"hijau", 0x22C55E}, {"cyan", 0x06B6D4},
        {"kuning", 0xFFE600}, {"oranye", 0xF97316}, {"merah", 0xEF4444}, {"ungu", 0xA855F7},
        {"pink", 0xEC4899},   {"silver", 0x94A3B8}};

    for (const auto& item : color_items) {
        lv_obj_t* p = lv_button_create(cp_color_box_);
        lv_obj_set_size(p, 24, 24);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(p, lv_color_hex(item.color_hex), 0);
        lv_obj_set_style_border_width(p, 2, 0);
        lv_obj_set_style_border_color(p, lv_color_white(), 0);
        lv_obj_set_style_pad_all(p, 0, 0);

        struct EventData {
            LcdDisplay* display;
            const char* name;
            uint32_t color_hex;
        };
        auto* ed = new EventData{this, item.theme_name, item.color_hex};
        lv_obj_add_event_cb(
            p,
            [](lv_event_t* e) {
                auto* d = static_cast<EventData*>(lv_event_get_user_data(e));
                d->display->SetHeaderIconColor(d->color_hex);
                auto th = LvglThemeManager::GetInstance().GetTheme(d->name);
                if (th) {
                    SyncThemeGlobal(d->name);
                    d->display->SetTheme(th);
                }
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                d->display->ShowNotification(std::string("Warna Icon: ") + d->name);
            },
            LV_EVENT_CLICKED, ed);
    }
}

void LcdDisplay::ToggleControlCenter() {
    DisplayLockGuard lock(this);
    if (!cp_drawer_ || !cp_backdrop_)
        return;

    HideOverlays();

    cp_visible_ = !cp_visible_;
    if (cp_visible_) {
        UpdateControlCenterValues();
        lv_obj_remove_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cp_backdrop_);
        lv_obj_move_foreground(cp_drawer_);
    } else {
        lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        if (cp_color_box_)
            lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::UpdateControlCenterValues() {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec && cp_vol_slider_) {
        int v = codec->output_volume();
        lv_slider_set_value(cp_vol_slider_, v, LV_ANIM_OFF);
        if (cp_vol_label_)
            lv_label_set_text_fmt(cp_vol_label_, "%d%%", v);
        if (cp_vol_icon_) {
            lv_label_set_text(cp_vol_icon_,
                              v > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
        }
    }
    if (cp_bright_slider_) {
        Settings s("display", true);
        int b = s.GetInt("brightness", 70);
        lv_slider_set_value(cp_bright_slider_, b, LV_ANIM_OFF);
        if (cp_bright_label_)
            lv_label_set_text_fmt(cp_bright_label_, "%d%%", b);
    }
    if (cp_mode_label_) {
        Settings cs("chronchi", true);
        int m = cs.GetInt("mode", 0);
        lv_label_set_text(cp_mode_label_, m == 1 ? (MATERIAL_SYMBOLS_WATCH " Watch")
                                                 : (MATERIAL_SYMBOLS_ROBOT_2 " AI"));
    }
    if (cp_theme_icon_label_) {
        if (ui_theme_id_ == 1) {
            lv_label_set_text(cp_theme_icon_label_, MATERIAL_SYMBOLS_EDIT_SQUARE " Ke Thema 2");
            lv_obj_set_style_text_color(cp_theme_icon_label_, lv_color_hex(0x22C55E), 0);
        } else {
            lv_label_set_text(cp_theme_icon_label_, MATERIAL_SYMBOLS_ROBOT_2 " Ke Thema 1");
            lv_obj_set_style_text_color(cp_theme_icon_label_, lv_color_hex(0xFFE600), 0);
        }
    }
}

void LcdDisplay::CreateDirectOverlays() {
    auto screen = lv_screen_active();
    if (!screen)
        return;

    // Common overlay backdrop (tap outside to dismiss) - Obsidian Deep Glass
    overlay_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(overlay_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
    lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
    lv_obj_align(overlay_backdrop_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        overlay_backdrop_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->HideOverlays();
        },
        LV_EVENT_CLICKED, this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : nullptr;
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : nullptr;

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x1E293B;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;
    uint32_t accent_hex = synced ? synced->accent_color : 0x00E5FF;

    // --- 1. Volume Overlay Card (Nova Cyber Neon Style) ---
    vol_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(vol_overlay_card_, 120, 195);
    lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_radius(vol_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(vol_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(vol_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(vol_overlay_card_, 8, 0);
    lv_obj_set_style_pad_row(vol_overlay_card_, 4, 0);
    lv_obj_set_flex_flow(vol_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(vol_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // Header: Icon + Value
    lv_obj_t* vol_hdr = lv_obj_create(vol_overlay_card_);
    lv_obj_set_size(vol_hdr, 104, 26);
    lv_obj_set_style_bg_opa(vol_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_hdr, 0, 0);
    lv_obj_set_style_pad_all(vol_hdr, 0, 0);
    lv_obj_set_flex_flow(vol_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vol_hdr, 6, 0);

    vol_overlay_icon_ = lv_label_create(vol_hdr);
    lv_label_set_text(vol_overlay_icon_, MATERIAL_SYMBOLS_VOLUME_UP);
    if (icon_font)
        lv_obj_set_style_text_font(vol_overlay_icon_, icon_font, 0);
    lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(accent_hex), 0);

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;

    vol_overlay_label_ = lv_label_create(vol_hdr);
    lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (text_font)
        lv_obj_set_style_text_font(vol_overlay_label_, text_font, 0);
    lv_obj_set_style_text_color(vol_overlay_label_, lv_color_white(), 0);

    // Large Vertical Slider: 46px wide x 120px tall
    vol_overlay_slider_ = lv_slider_create(vol_overlay_card_);
    lv_obj_set_size(vol_overlay_slider_, 46, 120);
    lv_slider_set_range(vol_overlay_slider_, 0, 100);
    lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    lv_obj_set_style_radius(vol_overlay_slider_, 10, 0);
    lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(accent_hex), LV_PART_INDICATOR);
    lv_obj_set_style_radius(vol_overlay_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(vol_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(vol_overlay_slider_, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(
        vol_overlay_slider_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto c = Board::GetInstance().GetAudioCodec();
            if (c)
                c->SetOutputVolume(val);
            if (display->vol_overlay_label_)
                lv_label_set_text_fmt(display->vol_overlay_label_, "%d%%", val);
            if (display->vol_overlay_icon_) {
                lv_label_set_text(display->vol_overlay_icon_, val > 0
                                                                  ? MATERIAL_SYMBOLS_VOLUME_UP
                                                                  : MATERIAL_SYMBOLS_VOLUME_OFF);
            }
            if (display->mute_label_) {
                lv_label_set_text(display->mute_label_, val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP
                                                                : MATERIAL_SYMBOLS_VOLUME_OFF);
            }
            if (display->cp_vol_slider_) {
                lv_slider_set_value(display->cp_vol_slider_, val, LV_ANIM_OFF);
            }
            if (display->cp_vol_label_) {
                lv_label_set_text_fmt(display->cp_vol_label_, "%d%%", val);
            }
        },
        LV_EVENT_VALUE_CHANGED, this);

    // Hint label
    lv_obj_t* vol_hint = lv_label_create(vol_overlay_card_);
    lv_label_set_text(vol_hint, "▲ Geser Ke Atas/Bawah ▼");
    lv_obj_set_style_text_color(vol_hint, lv_color_hex(0x94A3B8), 0);

    // --- 2. Brightness Overlay Card (Nova Cyber Neon Style) ---
    bright_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(bright_overlay_card_, 120, 195);
    lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_radius(bright_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(bright_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bright_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(bright_overlay_card_, 8, 0);
    lv_obj_set_style_pad_row(bright_overlay_card_, 4, 0);
    lv_obj_set_flex_flow(bright_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bright_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(bright_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);

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
    if (icon_font)
        lv_obj_set_style_text_font(br_icon, icon_font, 0);
    lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);

    bright_overlay_label_ = lv_label_create(br_hdr);
    lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);
    if (text_font)
        lv_obj_set_style_text_font(bright_overlay_label_, text_font, 0);
    lv_obj_set_style_text_color(bright_overlay_label_, lv_color_white(), 0);

    // Large Vertical Slider: 46px wide x 120px tall
    bright_overlay_slider_ = lv_slider_create(bright_overlay_card_);
    lv_obj_set_size(bright_overlay_slider_, 46, 120);
    lv_slider_set_range(bright_overlay_slider_, 10, 100);
    lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    lv_obj_set_style_radius(bright_overlay_slider_, 10, 0);
    lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bright_overlay_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bright_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bright_overlay_slider_, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(
        bright_overlay_slider_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            display->SetBrightness(val);
            Settings s("display", true);
            s.SetInt("brightness", val);
            if (display->bright_overlay_label_)
                lv_label_set_text_fmt(display->bright_overlay_label_, "%d%%", val);
            if (display->cp_bright_slider_) {
                lv_slider_set_value(display->cp_bright_slider_, val, LV_ANIM_OFF);
            }
            if (display->cp_bright_label_) {
                lv_label_set_text_fmt(display->cp_bright_label_, "%d%%", val);
            }
        },
        LV_EVENT_VALUE_CHANGED, this);

    // Hint label
    lv_obj_t* br_hint = lv_label_create(bright_overlay_card_);
    lv_label_set_text(br_hint, "▲ Geser Ke Atas/Bawah ▼");
    lv_obj_set_style_text_color(br_hint, lv_color_hex(0x94A3B8), 0);

    // --- 3. Text Size Overlay Card (Nova Cyber Neon Style) ---
    text_size_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(text_size_overlay_card_, 200, 115);
    lv_obj_align(text_size_overlay_card_, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(text_size_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(text_size_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(text_size_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(text_size_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(text_size_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(text_size_overlay_card_, 10, 0);
    lv_obj_set_flex_flow(text_size_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_size_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(text_size_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // Title: "Ukuran Teks Chat"
    lv_obj_t* ts_hdr = lv_label_create(text_size_overlay_card_);
    lv_label_set_text(ts_hdr, MATERIAL_SYMBOLS_EDIT_SQUARE " Ukuran Teks Chat");
    lv_obj_set_style_text_color(ts_hdr, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(ts_hdr, &font_noto_sans_basic_14_1, 0);

    // Control Row: [-] [ Val ] [+]
    lv_obj_t* ts_row = lv_obj_create(text_size_overlay_card_);
    lv_obj_set_size(ts_row, 180, 48);
    lv_obj_set_style_bg_opa(ts_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ts_row, 0, 0);
    lv_obj_set_style_pad_all(ts_row, 0, 0);
    lv_obj_set_flex_flow(ts_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ts_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Button Minus [-]
    lv_obj_t* btn_minus = lv_button_create(ts_row);
    lv_obj_set_size(btn_minus, 44, 40);
    lv_obj_set_style_radius(btn_minus, 10, 0);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(btn_minus, 1, 0);
    lv_obj_set_style_border_color(btn_minus, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_set_style_text_font(lbl_minus, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(lbl_minus, lv_color_hex(0xEF4444), 0);
    lv_obj_center(lbl_minus);
    lv_obj_add_event_cb(
        btn_minus,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display->GetChatFontLevel() > 0) {
                display->SetChatFontLevel(display->GetChatFontLevel() - 1);
            }
        },
        LV_EVENT_CLICKED, this);

    // Center Value Label
    text_size_val_label_ = lv_label_create(ts_row);
    lv_obj_set_width(text_size_val_label_, 84);
    lv_obj_set_style_text_align(text_size_val_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(text_size_val_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_size_val_label_, &font_noto_sans_basic_14_1, 0);
    UpdateTextSizeOverlayUI();

    // Button Plus [+]
    lv_obj_t* btn_plus = lv_button_create(ts_row);
    lv_obj_set_size(btn_plus, 44, 40);
    lv_obj_set_style_radius(btn_plus, 10, 0);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(btn_plus, 1, 0);
    lv_obj_set_style_border_color(btn_plus, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_set_style_text_font(lbl_plus, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(lbl_plus, lv_color_hex(0x22C55E), 0);
    lv_obj_center(lbl_plus);
    lv_obj_add_event_cb(
        btn_plus,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display->GetChatFontLevel() < 3) {
                display->SetChatFontLevel(display->GetChatFontLevel() + 1);
            }
        },
        LV_EVENT_CLICKED, this);

    // Subtitle Hint
    lv_obj_t* ts_hint = lv_label_create(text_size_overlay_card_);
    lv_label_set_text(ts_hint, "Batas: 14px - 30px");
    lv_obj_set_style_text_color(ts_hint, lv_color_hex(0x71717A), 0);
    lv_obj_set_style_text_font(ts_hint, &font_noto_sans_basic_14_1, 0);

    // --- 4. Header Icon Color Overlay Card ---
    header_color_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(header_color_overlay_card_, 228, 195);
    lv_obj_align(header_color_overlay_card_, LV_ALIGN_CENTER, 0, -5);
    lv_obj_set_style_radius(header_color_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(header_color_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(header_color_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(header_color_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(header_color_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(header_color_overlay_card_, 8, 0);
    lv_obj_set_flex_flow(header_color_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header_color_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(header_color_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(header_color_overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // Title: "Warna Icon Header"
    lv_obj_t* hc_hdr = lv_label_create(header_color_overlay_card_);
    lv_label_set_text(hc_hdr, MATERIAL_SYMBOLS_EXPLORE " Warna Icon Header");
    lv_obj_set_style_text_color(hc_hdr, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(hc_hdr, &font_noto_sans_basic_14_1, 0);

    // Live Preview Strip: Sleek dark sub-panel showing all 5 header icons
    lv_obj_t* hc_prev = lv_obj_create(header_color_overlay_card_);
    lv_obj_set_size(hc_prev, 210, 30);
    lv_obj_set_style_bg_color(hc_prev, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_radius(hc_prev, 6, 0);
    lv_obj_set_style_border_width(hc_prev, 1, 0);
    lv_obj_set_style_border_color(hc_prev, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(hc_prev, 0, 0);
    lv_obj_set_style_pad_left(hc_prev, 8, 0);
    lv_obj_set_style_pad_right(hc_prev, 8, 0);
    lv_obj_set_flex_flow(hc_prev, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hc_prev, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_color_t curr_icon_clr = lv_color_hex(header_icon_color_);

    preview_rot_label_ = lv_label_create(hc_prev);
    lv_label_set_text(preview_rot_label_, MATERIAL_SYMBOLS_REFRESH);
    if (icon_font)
        lv_obj_set_style_text_font(preview_rot_label_, icon_font, 0);
    lv_obj_set_style_text_color(preview_rot_label_, curr_icon_clr, 0);

    preview_bright_label_ = lv_label_create(hc_prev);
    lv_label_set_text(preview_bright_label_, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    if (icon_font)
        lv_obj_set_style_text_font(preview_bright_label_, icon_font, 0);
    lv_obj_set_style_text_color(preview_bright_label_, curr_icon_clr, 0);

    preview_vol_label_ = lv_label_create(hc_prev);
    lv_label_set_text(preview_vol_label_, MATERIAL_SYMBOLS_VOLUME_UP);
    if (icon_font)
        lv_obj_set_style_text_font(preview_vol_label_, icon_font, 0);
    lv_obj_set_style_text_color(preview_vol_label_, curr_icon_clr, 0);

    preview_wifi_label_ = lv_label_create(hc_prev);
    lv_label_set_text(preview_wifi_label_, MATERIAL_SYMBOLS_WIFI);
    if (icon_font)
        lv_obj_set_style_text_font(preview_wifi_label_, icon_font, 0);
    lv_obj_set_style_text_color(preview_wifi_label_, curr_icon_clr, 0);

    lv_obj_t* bat_box = lv_obj_create(hc_prev);
    lv_obj_set_size(bat_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bat_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_box, 0, 0);
    lv_obj_set_style_pad_all(bat_box, 0, 0);
    lv_obj_set_flex_flow(bat_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bat_box, 2, 0);

    preview_bat_label_ = lv_label_create(bat_box);
    lv_label_set_text(preview_bat_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
    if (icon_font)
        lv_obj_set_style_text_font(preview_bat_label_, icon_font, 0);
    lv_obj_set_style_text_color(preview_bat_label_, curr_icon_clr, 0);

    preview_bat_pct_label_ = lv_label_create(bat_box);
    lv_label_set_text(preview_bat_pct_label_, "98%");
    lv_obj_set_style_text_color(preview_bat_pct_label_, curr_icon_clr, 0);

    // Color Palette Grid: 2 rows of 5 dots
    lv_obj_t* hc_palette = lv_obj_create(header_color_overlay_card_);
    lv_obj_set_size(hc_palette, 210, 80);
    lv_obj_set_style_bg_opa(hc_palette, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hc_palette, 0, 0);
    lv_obj_set_style_pad_all(hc_palette, 2, 0);
    lv_obj_set_flex_flow(hc_palette, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(hc_palette, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hc_palette, 6, 0);
    lv_obj_set_style_pad_column(hc_palette, 6, 0);
    lv_obj_set_scrollbar_mode(hc_palette, LV_SCROLLBAR_MODE_OFF);

    struct HeaderColorDef {
        const char* name;
        uint32_t hex;
    };
    static const HeaderColorDef kHeaderColors[] = {
        {"Putih", 0xFFFFFF},       {"Biru Langit", 0x38BDF8}, {"Hijau Neon", 0x22C55E},
        {"Cyan Modern", 0x06B6D4}, {"Kuning Emas", 0xFFE600}, {"Oranye", 0xF97316},
        {"Merah", 0xEF4444},       {"Ungu", 0xA855F7},        {"Pink", 0xEC4899},
        {"Silver", 0x94A3B8}};

    for (const auto& c : kHeaderColors) {
        lv_obj_t* dot = lv_button_create(hc_palette);
        lv_obj_set_size(dot, 32, 32);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(c.hex), 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, lv_color_white(), 0);
        lv_obj_set_style_pad_all(dot, 0, 0);

        struct DotCbData {
            LcdDisplay* disp;
            const char* name;
            uint32_t hex;
        };
        auto* data = new DotCbData{this, c.name, c.hex};
        lv_obj_add_event_cb(
            dot,
            [](lv_event_t* e) {
                auto* d = static_cast<DotCbData*>(lv_event_get_user_data(e));
                if (!d || !d->disp)
                    return;
                d->disp->SetHeaderIconColor(d->hex);
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                d->disp->ShowNotification(std::string("Warna Icon: ") + d->name);
            },
            LV_EVENT_CLICKED, data);
    }

    // Hint Label
    lv_obj_t* hc_hint = lv_label_create(header_color_overlay_card_);
    lv_label_set_text(hc_hint, "Sentuh warna untuk memilih");
    lv_obj_set_style_text_color(hc_hint, lv_color_hex(0x71717A), 0);
    lv_obj_set_style_text_font(hc_hint, &font_noto_sans_basic_14_1, 0);
}

void LcdDisplay::ShowVolumeOverlay() {
    DisplayLockGuard lock(this);
    if (!vol_overlay_card_ || !overlay_backdrop_)
        return;

    HideOverlays();
    if (cp_visible_)
        ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(synced->border_color), 0);
        if (vol_overlay_slider_) {
            lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(synced->panel2_color), 0);
            lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(synced->accent_color),
                                      LV_PART_INDICATOR);
        }
        if (vol_overlay_icon_) {
            lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(synced->accent_color), 0);
        }
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;
    if (vol_overlay_slider_)
        lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    if (vol_overlay_label_)
        lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (vol_overlay_icon_) {
        lv_label_set_text(vol_overlay_icon_,
                          cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    }

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(vol_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::ShowBrightnessOverlay() {
    DisplayLockGuard lock(this);
    if (!bright_overlay_card_ || !overlay_backdrop_)
        return;

    HideOverlays();
    if (cp_visible_)
        ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(synced->border_color), 0);
        if (bright_overlay_slider_) {
            lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(synced->panel2_color),
                                      0);
        }
    }

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);
    if (bright_overlay_slider_)
        lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    if (bright_overlay_label_)
        lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(bright_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::ShowTextSizeOverlay() {
    DisplayLockGuard lock(this);
    if (!text_size_overlay_card_ || !overlay_backdrop_)
        return;

    HideOverlays();
    if (cp_visible_)
        ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(text_size_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(text_size_overlay_card_, lv_color_hex(synced->border_color),
                                      0);
    }

    UpdateTextSizeOverlayUI();

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(text_size_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::ShowHeaderColorOverlay() {
    DisplayLockGuard lock(this);
    if (!header_color_overlay_card_ || !overlay_backdrop_)
        return;

    HideOverlays();
    if (cp_visible_)
        ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(header_color_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(header_color_overlay_card_,
                                      lv_color_hex(synced->border_color), 0);
    }

    UpdateHeaderColorOverlayUI();

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(header_color_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(header_color_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::UpdateHeaderColorOverlayUI() {
    lv_color_t clr = lv_color_hex(header_icon_color_);
    if (preview_rot_label_)
        lv_obj_set_style_text_color(preview_rot_label_, clr, 0);
    if (preview_bright_label_)
        lv_obj_set_style_text_color(preview_bright_label_, clr, 0);
    if (preview_vol_label_)
        lv_obj_set_style_text_color(preview_vol_label_, clr, 0);
    if (preview_wifi_label_)
        lv_obj_set_style_text_color(preview_wifi_label_, clr, 0);
    if (preview_bat_label_)
        lv_obj_set_style_text_color(preview_bat_label_, clr, 0);
    if (preview_bat_pct_label_)
        lv_obj_set_style_text_color(preview_bat_pct_label_, clr, 0);
}

void LcdDisplay::SetHeaderIconColor(uint32_t color_hex) {
    DisplayLockGuard lock(this);
    if (header_icon_color_ == color_hex)
        return;
    header_icon_color_ = color_hex;
    Settings disp_set("display", true);
    disp_set.SetInt("hdr_icon_clr", (int)color_hex);
    lv_color_t clr = lv_color_hex(color_hex);
    if (rot_icon_label_)
        lv_obj_set_style_text_color(rot_icon_label_, clr, 0);
    if (br_icon_label_)
        lv_obj_set_style_text_color(br_icon_label_, clr, 0);
    if (mute_label_)
        lv_obj_set_style_text_color(mute_label_, clr, 0);
    if (network_label_)
        lv_obj_set_style_text_color(network_label_, clr, 0);
    if (battery_label_)
        lv_obj_set_style_text_color(battery_label_, clr, 0);
    if (battery_pct_label_)
        lv_obj_set_style_text_color(battery_pct_label_, clr, 0);
    UpdateHeaderColorOverlayUI();
    UpdateStatusBar(true);
    if (on_header_icon_color_changed_) {
        on_header_icon_color_changed_(color_hex);
    }
}

void LcdDisplay::CloseRecentAppsModal() {
    if (recent_apps_modal_ != nullptr) {
        lv_obj_del_async(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
}

void LcdDisplay::ShowRecentAppsModal() {
    DisplayLockGuard lock(this);
    CloseRecentAppsModal();
    if (overlay_backdrop_)
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_)
        lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_)
        lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (text_size_overlay_card_)
        lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (header_color_overlay_card_)
        lv_obj_add_flag(header_color_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    overlay_visible_ = false;
    if (IsControlCenterVisible()) {
        ToggleControlCenter();
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x1E293B;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;

    auto screen = lv_screen_active();
    recent_apps_modal_ = lv_obj_create(screen);
    lv_obj_set_pos(recent_apps_modal_, 0, 0);
    lv_obj_set_size(recent_apps_modal_, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(recent_apps_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(recent_apps_modal_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(recent_apps_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(recent_apps_modal_, 0, 0);
    lv_obj_set_style_radius(recent_apps_modal_, 0, 0);
    lv_obj_set_style_pad_all(recent_apps_modal_, 0, 0);

    // Header Title
    lv_obj_t* title_lbl = lv_label_create(recent_apps_modal_);
    lv_label_set_text(title_lbl, "APLIKASI TERBUKA");
    lv_obj_set_pos(title_lbl, 14, 10);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_t* sub_lbl = lv_label_create(recent_apps_modal_);
    lv_label_set_text(sub_lbl, "Ketuk aplikasi untuk beralih instan");
    lv_obj_set_pos(sub_lbl, 14, 28);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x94A3B8), 0);

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : &BUILTIN_ICON_FONT;

    int modal_w = LV_HOR_RES;
    int modal_h = LV_VER_RES;
    int list_w = (modal_w > 240) ? 280 : (modal_w - 20);
    int list_x = (modal_w - list_w) / 2;
    int list_h = (modal_h < 260) ? (modal_h - 88) : 230;

    // Close button (X)
    lv_obj_t* close_btn = lv_button_create(recent_apps_modal_);
    lv_obj_set_pos(close_btn, modal_w - 36, 8);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, icon_font, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(
        close_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display)
                display->CloseRecentAppsModal();
        },
        LV_EVENT_CLICKED, this);

    // Scrollable container for App Cards
    lv_obj_t* list = lv_obj_create(recent_apps_modal_);
    lv_obj_set_pos(list, list_x, 46);
    lv_obj_set_size(list, list_w, list_h);
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
        int action;  // 0: close (active xz), 1: watch, 2: nova os, 3: control center
    };

    const AppCardItem items[] = {{"Nova Mini OS", "Beranda & Dashboard HP", MATERIAL_SYMBOLS_HOME,
                                  lv_color_hex(0x00E5FF), 2},
                                 {"Xiaozhi AI", "Asisten Suara [Sedang Aktif]",
                                  MATERIAL_SYMBOLS_ROBOT_2, lv_color_hex(0x10B981), 0},
                                 {"Radio Streaming", "Stasiun Musik Online & Live",
                                  MATERIAL_SYMBOLS_HEADPHONES, lv_color_hex(0xEC4899), 2},
                                 {"Chronchi Watch", "Smartwatch & Kalender Pintar",
                                  MATERIAL_SYMBOLS_WATCH, lv_color_hex(0xF59E0B), 1},
                                 {"Control Center", "Pengaturan Cepat, Kecerahan & Vol",
                                  MATERIAL_SYMBOLS_SETTINGS, lv_color_hex(0xA855F7), 3}};

    const int total_items = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < total_items; ++i) {
        lv_obj_t* card = lv_button_create(list);
        lv_obj_set_pos(card, 0, i * 44);
        lv_obj_set_size(card, list_w - 14, 38);
        lv_obj_set_style_bg_color(card, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(border_hex), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        // App Icon
        lv_obj_t* icon_lbl = lv_label_create(card);
        lv_label_set_text(icon_lbl, items[i].icon);
        lv_obj_set_style_text_font(icon_lbl, icon_font, 0);
        lv_obj_set_style_text_color(icon_lbl, items[i].color, 0);
        lv_obj_set_pos(icon_lbl, 8, 9);

        // Titles
        lv_obj_t* c_title = lv_label_create(card);
        lv_label_set_text(c_title, items[i].name);
        lv_obj_set_pos(c_title, 36, 4);
        lv_obj_set_size(c_title, list_w - 70, 17);
        lv_label_set_long_mode(c_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_title, lv_color_white(), 0);

        lv_obj_t* c_desc = lv_label_create(card);
        lv_label_set_text(c_desc, items[i].desc);
        lv_obj_set_pos(c_desc, 36, 20);
        lv_obj_set_size(c_desc, list_w - 70, 14);
        lv_label_set_long_mode(c_desc, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_desc, lv_color_hex(0x64748B), 0);

        // Right arrow
        lv_obj_t* arr = lv_label_create(card);
        lv_label_set_text(arr, ">");
        lv_obj_set_pos(arr, list_w - 30, 10);
        lv_obj_set_style_text_color(arr, items[i].color, 0);

        lv_obj_set_user_data(card, (void*)(intptr_t)items[i].action);
        lv_obj_add_event_cb(
            card,
            [](lv_event_t* ev) {
                auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(ev));
                if (!display)
                    return;
                int act = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
                display->CloseRecentAppsModal();
                if (act == 0) {
                    // Already in Xiaozhi AI
                    return;
                } else if (act == 1) {
                    Board::GetInstance().SwitchToMode(1);
                } else if (act == 2) {
                    Board::GetInstance().SwitchToMode(2);
                } else if (act == 3) {
                    display->ToggleControlCenter();
                }
            },
            LV_EVENT_CLICKED, this);
    }

    // Bottom Button: KEMBALI KE BERANDA NOVA
    lv_obj_t* home_btn_bar = lv_button_create(recent_apps_modal_);
    lv_obj_set_pos(home_btn_bar, list_x, modal_h - 36);
    lv_obj_set_size(home_btn_bar, list_w, 30);
    lv_obj_set_style_bg_color(home_btn_bar, lv_color_hex(0x132238), 0);
    lv_obj_set_style_border_color(home_btn_bar, lv_color_hex(0x06B6D4), 0);
    lv_obj_set_style_border_width(home_btn_bar, 1, 0);
    lv_obj_set_style_radius(home_btn_bar, 8, 0);
    lv_obj_set_style_pad_all(home_btn_bar, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(home_btn_bar);
    lv_label_set_text(btn_lbl, "KEMBALI KE BERANDA NOVA");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(
        home_btn_bar,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display) {
                display->CloseRecentAppsModal();
                Board::GetInstance().SwitchToMode(2);
            }
        },
        LV_EVENT_CLICKED, this);
}

void LcdDisplay::HideOverlays() {
    DisplayLockGuard lock(this);
    if (overlay_backdrop_)
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_)
        lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_)
        lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (text_size_overlay_card_)
        lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (header_color_overlay_card_)
        lv_obj_add_flag(header_color_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    CloseRecentAppsModal();
    overlay_visible_ = false;
}
