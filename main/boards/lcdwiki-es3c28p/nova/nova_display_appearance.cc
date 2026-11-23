#include "nova_display_internal.h"

void NovaDisplay::BuildAi() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "XIAOZHI AI");

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=12, w=144): AI Card & Status Info
        lv_obj_t* card = MakePanel(root, 12, 40, 144, 76, C_PANEL, C_BORDER, 12);
        MakeIcon(card, ICON_AI, 10, 14, C_CYAN, 26);
        MakeLabel(card, "Xiaozhi AI", 40, 8, 96, 20, FONT_16, C_TEXT);
        MakeLabel(card, "Asisten suara AI", 40, 30, 96, 14, FONT_10, C_MUTED);

        lv_obj_t* scard = MakePanel(root, 12, 122, 144, 78, C_PANEL, C_BORDER, 10);
        MakeLabel(scard, "STATUS SISTEM", 8, 6, 128, 14, FONT_10, C_MUTED);
        MakeLabel(scard, "AI Voice siap. Tekan tombol kanan untuk mulai chat suara.", 8, 22, 128,
                  48, FONT_10, C_AMBER);

        // Right Column (x=164, w=144): Large Action Buttons
        lv_obj_t* ai_btn = lv_btn_create(root);
        lv_obj_set_pos(ai_btn, 164, 40);
        lv_obj_set_size(ai_btn, 144, 96);
        StyleButton(ai_btn, C_PANEL, C_CYAN, 12);
        MakeIcon(ai_btn, ICON_AI, 12, 30, C_CYAN, 32);
        MakeLabel(ai_btn, "BUKA ASISTEN", 48, 26, 90, 20, FONT_14, C_CYAN);
        MakeLabel(ai_btn, "Mulai dialog LLM", 48, 48, 90, 14, FONT_10, C_TEXT);
        lv_obj_add_event_cb(ai_btn, OnLaunchAiVoice, LV_EVENT_CLICKED, this);

        lv_obj_t* wbtn =
            MakeButton(root, "ATUR WI-FI", 164, 144, 144, 44, C_PANEL, C_BORDER, C_TEXT, FONT_12);
        lv_obj_set_user_data(wbtn, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wbtn, OnNavClicked, LV_EVENT_CLICKED, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "ASISTEN SUARA", 14, 44, 212, 16, FONT_10, C_CYAN);

        lv_obj_t* card = MakePanel(root, 12, 64, 216, 76, C_PANEL, C_BORDER, 14);
        MakeIcon(card, ICON_AI, 14, 14, C_CYAN, 30);
        MakeLabel(card, "Xiaozhi", 52, 10, 145, 24, FONT_16, C_TEXT);
        MakeLabel(card, "Asisten suara AI & musik", 52, 36, 145, 24, FONT_10, C_MUTED);

        MakeLabel(root, "STATUS", 14, 148, 212, 14, FONT_10, C_MUTED);
        MakeLabel(root, "Mode AI siap. Tekan tombol untuk membuka percakapan suara penuh.", 14, 164,
                  212, 30, FONT_10, C_AMBER);

        MakeButton(root, "BUKA ASISTEN SUARA", 12, 200, 216, 36, C_CYAN, C_CYAN, C_BG, FONT_12,
                   OnLaunchAiVoice, this);

        lv_obj_t* wbtn = MakeButton(root, "ATUR KONEKSI WI-FI", 12, 242, 216, 28, C_PANEL, C_BORDER,
                                    C_TEXT, FONT_10);
        lv_obj_set_user_data(wbtn, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wbtn, OnNavClicked, LV_EVENT_CLICKED, this);
    }

    MakeBottomNav(root, PAGE_AI);
}

void NovaDisplay::SettingsRow(lv_obj_t* root, int y, const char* name, const char* detail,
                              IconKind icon, lv_color_t accent, Page target) {
    lv_obj_t* row = lv_btn_create(root);
    lv_obj_set_pos(row, 12, y);
    lv_obj_set_size(row, 216, 31);
    StyleButton(row, C_PANEL, C_BORDER, 9);
    MakeIcon(row, icon, 6, 4, accent, 23);
    lv_obj_t* title = MakeLabel(row, name, 36, 1, 151, 15, FONT_12, C_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t* sub = MakeLabel(row, detail, 36, 16, 151, 13, FONT_10, C_MUTED);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
    MakeLabel(row, ">", 193, 5, 15, 19, FONT_14, C_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_user_data(row, (void*)(intptr_t)target);
    lv_obj_add_event_cb(row, OnNavClicked, LV_EVENT_CLICKED, this);
}

void NovaDisplay::BuildSettings() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "PENGATURAN");

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN SETTINGS (100% Fit On 1 Screen - No Scroll) ===
        struct SettingItem {
            const char* name;
            const char* detail;
            IconKind icon;
            lv_color_t accent;
            Page target;
        };

        static const SettingItem kLeftItems[] = {
            {"Tampilan", "Kecerahan layar", ICON_BRIGHTNESS, C_AMBER, PAGE_DISPLAY},
            {"Jaringan", "Wi-Fi & koneksi", ICON_WIFI, C_CYAN, PAGE_WIFI},
            {"Audio & suara", "Volume, haptik", ICON_AUDIO, C_AMBER, PAGE_AUDIO},
            {"Penyimpanan", "MicroSD & flash", ICON_FILES, C_VIOLET, PAGE_FILES}};

        static const SettingItem kRightItems[] = {
            {"Baterai & daya", "Status ADC TP4054", ICON_BATTERY, C_MINT, PAGE_BATTERY},
            {"Bahasa & wilayah", "Indonesia / WIB", ICON_REGION, C_CYAN, PAGE_REGION},
            {"Informasi sistem", "Chip ESP32-S3", ICON_CHIP, C_VIOLET, PAGE_SYSTEM},
            {"Akses Cepat", "Launcher pintasan", ICON_SETTINGS, C_CYAN, PAGE_QUICK}};

        auto make_col_row = [this, root](int x, int y, int w, int h, const SettingItem& it) {
            lv_obj_t* row = lv_btn_create(root);
            lv_obj_set_pos(row, x, y);
            lv_obj_set_size(row, w, h);
            StyleButton(row, C_PANEL, C_BORDER, 8);
            MakeIcon(row, it.icon, 6, 6, it.accent, 22);
            lv_obj_t* title = MakeLabel(row, it.name, 32, 3, w - 50, 16, FONT_12, C_TEXT);
            lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* sub = MakeLabel(row, it.detail, 32, 19, w - 50, 14, FONT_10, C_MUTED);
            lv_label_set_long_mode(sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
            MakeLabel(row, ">", w - 16, 8, 12, 18, FONT_12, C_MUTED, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(row, (void*)(intptr_t)it.target);
            lv_obj_add_event_cb(row, OnNavClicked, LV_EVENT_CLICKED, this);
        };

        for (int i = 0; i < 4; ++i) {
            int y = 36 + i * 42;
            make_col_row(10, y, 146, 38, kLeftItems[i]);
            make_col_row(164, y, 146, 38, kRightItems[i]);
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, (width_ > 240 ? (width_ - 240) / 2 : 0), 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 48, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        MakeLabel(list, "PREFERENSI PERANGKAT", 14, 4, 212, 16, FONT_12, C_MUTED);

        SettingsRow(list, 24, "Tampilan", "Kecerahan layar", ICON_BRIGHTNESS, C_AMBER,
                    PAGE_DISPLAY);
        SettingsRow(list, 58, "Jaringan", "Wi-Fi & koneksi", ICON_WIFI, C_CYAN, PAGE_WIFI);
        SettingsRow(list, 92, "Audio & suara", "Volume, mute, haptik", ICON_AUDIO, C_AMBER,
                    PAGE_AUDIO);
        SettingsRow(list, 126, "Penyimpanan", "Flash & sistem berkas", ICON_FILES, C_VIOLET,
                    PAGE_FILES);
        SettingsRow(list, 160, "Baterai & daya", "Pembacaan ADC & status", ICON_BATTERY, C_MINT,
                    PAGE_BATTERY);
        SettingsRow(list, 194, "Bahasa & wilayah", "Indonesia / WIB", ICON_REGION, C_CYAN,
                    PAGE_REGION);
        SettingsRow(list, 228, "Informasi sistem", "Chip & memori", ICON_CHIP, C_VIOLET,
                    PAGE_SYSTEM);
    }

    MakeBottomNav(root, PAGE_SETTINGS);
}

void NovaDisplay::BuildDisplay() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "TAMPILAN");

    const auto* cur_bg = GetCurrentSyncedBg();
    const auto* cur_tc = GetCurrentSyncedTextColor();
    int cur_weight = GetCurrentTextWeight();
    int cur_contrast = GetCurrentContrast();
    int cur_sharpness = GetCurrentSharpness();

    auto on_weight_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
            return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self)
            return;
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* p_list = lv_obj_get_parent(target);
        if (p_list)
            self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
        int w = (int)(intptr_t)lv_obj_get_user_data(target);
        self->ApplyTextWeight(w, true);
        if (self->callbacks_.trigger_click)
            self->callbacks_.trigger_click();
    };

    auto on_contrast_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
            return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self)
            return;
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* p_list = lv_obj_get_parent(target);
        if (p_list)
            self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
        int c = (int)(intptr_t)lv_obj_get_user_data(target);
        self->ApplyContrast(c, true);
        if (self->callbacks_.trigger_click)
            self->callbacks_.trigger_click();
    };

    auto on_sharpness_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
            return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self)
            return;
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* p_list = lv_obj_get_parent(target);
        if (p_list)
            self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
        int s = (int)(intptr_t)lv_obj_get_user_data(target);
        self->ApplySharpness(s, true);
        if (self->callbacks_.trigger_click)
            self->callbacks_.trigger_click();
    };

    auto on_icon_mode_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
            return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self)
            return;
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* p_list = lv_obj_get_parent(target);
        if (p_list)
            self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
        int m = (int)(intptr_t)lv_obj_get_user_data(target);
        self->SetIconColorMode((uint8_t)m);
        if (self->callbacks_.trigger_click)
            self->callbacks_.trigger_click();
        self->BuildDisplay();
    };

    auto make_level_button = [&](lv_obj_t* p, const char* label, int x, int y, int w, int h,
                                 bool active, lv_event_cb_t cb, int val) {
        lv_color_t fill = active ? C_CYAN : C_PANEL;
        lv_color_t border = active ? C_CYAN : C_BORDER;
        lv_color_t txt_col = active ? C_BG : C_TEXT;
        lv_obj_t* btn = MakeButton(p, label, x, y, w, h, fill, border, txt_col, FONT_10, cb, this);
        lv_obj_set_user_data(btn, (void*)(intptr_t)val);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
        if (active) {
            lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        }
        return btn;
    };

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=8, w=146): Brightness & Rotation Controls
        lv_obj_t* card = MakePanel(root, 8, 36, 146, 66, C_PANEL, C_BORDER, 8);
        MakeLabel(card, "KECERAHAN", 8, 6, 80, 14, FONT_10, C_MUTED);
        char val[24];
        snprintf(val, sizeof(val), "%u / 255", brightness_);
        brightness_value_label_ =
            MakeLabel(card, val, 76, 6, 60, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(card, 8, 26, 130);

        MakeButton(
            root, "REDUP", 8, 106, 45, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(55);
            },
            this);

        MakeButton(
            root, "SEDANG", 58, 106, 45, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(145);
            },
            this);

        MakeButton(
            root, "TERANG", 108, 106, 46, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(235);
            },
            this);

        MakeButton(
            root, "ROTASI LAYAR 90°", 8, 136, 146, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            },
            nullptr);

        MakeLabel(root, "IPS Color Engine", 8, 168, 146, 14, FONT_10, C_MUTED);

        // Right Column (x=160, w=152): Vertical Scrollview for all theme & font controls
        lv_obj_t* right_list = lv_obj_create(root);
        lv_obj_set_pos(right_list, 158, 36);
        lv_obj_set_size(right_list, 158, height_ - 68);
        lv_obj_set_style_bg_opa(right_list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(right_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(right_list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(right_list, 120, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(right_list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(right_list, LV_DIR_VER);

        // 1. Theme Accent Colors
        MakeLabel(right_list, "TEMA AKSEN (10 WARNA)", 2, 2, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* pal_box = lv_obj_create(right_list);
        lv_obj_set_pos(pal_box, 2, 16);
        lv_obj_set_size(pal_box, 148, 28);
        lv_obj_set_flex_flow(pal_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pal_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(pal_box, C_PANEL, 0);
        lv_obj_set_style_border_color(pal_box, C_BORDER, 0);
        lv_obj_set_style_border_width(pal_box, 1, 0);
        lv_obj_set_style_radius(pal_box, 6, 0);
        lv_obj_set_style_pad_all(pal_box, 2, 0);
        lv_obj_set_style_pad_column(pal_box, 4, 0);
        lv_obj_set_scroll_dir(pal_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(pal_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(pal_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(pal_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(pal_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(pal_box, LV_SCROLLBAR_MODE_OFF);

        size_t theme_count = 0;
        const auto* themes = GetThemeSyncTable(&theme_count);
        for (size_t i = 0; i < theme_count; ++i) {
            lv_obj_t* dot = lv_btn_create(pal_box);
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(themes[i].accent_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active =
                (lv_color_to_int(C_CYAN) == lv_color_to_int(lv_color_hex(themes[i].accent_color)));
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(
                dot, is_active ? lv_color_white() : lv_color_hex(themes[i].border_color), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);
            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* th_table = GetThemeSyncTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_pal_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyTheme(th_table[idx].theme_name, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        // 2. Background Colors
        MakeLabel(right_list, "WARNA LATAR (25 PILIHAN)", 2, 48, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* bg_box = lv_obj_create(right_list);
        lv_obj_set_pos(bg_box, 2, 62);
        lv_obj_set_size(bg_box, 148, 28);
        lv_obj_set_flex_flow(bg_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bg_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(bg_box, C_PANEL, 0);
        lv_obj_set_style_border_color(bg_box, C_BORDER, 0);
        lv_obj_set_style_border_width(bg_box, 1, 0);
        lv_obj_set_style_radius(bg_box, 6, 0);
        lv_obj_set_style_pad_all(bg_box, 2, 0);
        lv_obj_set_style_pad_column(bg_box, 4, 0);
        lv_obj_set_scroll_dir(bg_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(bg_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(bg_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bg_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(bg_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(bg_box, LV_SCROLLBAR_MODE_OFF);

        size_t bg_count = 0;
        const auto* bgs = GetBackgroundTable(&bg_count);
        for (size_t i = 0; i < bg_count; ++i) {
            lv_obj_t* dot = lv_btn_create(bg_box);
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(bgs[i].swatch_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (cur_bg && std::string(cur_bg->id) == bgs[i].id);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);
            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* bg_table = GetBackgroundTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_bg_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyBackground(bg_table[idx].id, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        MakeButton(
            right_list, "🎨 RGB LATAR", 2, 94, 148, 22, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRgbMixerModal(false);
            },
            this);

        // 3. Text Colors (Full Palette)
        MakeLabel(right_list, "WARNA TEKS (31 PILIHAN)", 2, 120, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* tc_box = lv_obj_create(right_list);
        lv_obj_set_pos(tc_box, 2, 134);
        lv_obj_set_size(tc_box, 148, 28);
        lv_obj_set_flex_flow(tc_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tc_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(tc_box, C_PANEL, 0);
        lv_obj_set_style_border_color(tc_box, C_BORDER, 0);
        lv_obj_set_style_border_width(tc_box, 1, 0);
        lv_obj_set_style_radius(tc_box, 6, 0);
        lv_obj_set_style_pad_all(tc_box, 2, 0);
        lv_obj_set_style_pad_column(tc_box, 4, 0);
        lv_obj_set_scroll_dir(tc_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(tc_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(tc_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tc_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(tc_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(tc_box, LV_SCROLLBAR_MODE_OFF);

        size_t tc_count = 0;
        const auto* tcs = GetTextColorTable(&tc_count);
        for (size_t i = 0; i < tc_count; ++i) {
            lv_obj_t* dot = lv_btn_create(tc_box);
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(tcs[i].swatch_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (cur_tc && std::string(cur_tc->id) == tcs[i].id);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            if (strcmp(tcs[i].id, "auto") == 0) {
                lv_obj_t* albl = lv_label_create(dot);
                lv_label_set_text(albl, "A");
                lv_obj_set_style_text_font(albl, FONT_10, 0);
                lv_obj_set_style_text_color(albl, lv_color_black(), 0);
                lv_obj_center(albl);
            }

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* tc_table = GetTextColorTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_tc_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyTextColor(tc_table[idx].id, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        MakeButton(
            right_list, "🎨 RGB TEKS", 2, 166, 148, 22, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRgbMixerModal(true);
            },
            this);

        // 4. Text Weight (Ketebalan Teks)
        MakeLabel(right_list, "KETEBALAN TEKS", 2, 192, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "NORM", 2, 206, 35, 22, cur_weight == 0, on_weight_click, 0);
        make_level_button(right_list, "MDM", 39, 206, 35, 22, cur_weight == 1, on_weight_click, 1);
        make_level_button(right_list, "BOLD", 76, 206, 36, 22, cur_weight == 2, on_weight_click, 2);
        make_level_button(right_list, "XTRA", 114, 206, 36, 22, cur_weight == 3, on_weight_click,
                          3);

        // 5. Contrast (Kontras Tampilan)
        MakeLabel(right_list, "KONTRAS TAMPILAN", 2, 232, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "LMBT", 2, 246, 35, 22, cur_contrast == 0, on_contrast_click,
                          0);
        make_level_button(right_list, "NORM", 39, 246, 35, 22, cur_contrast == 1, on_contrast_click,
                          1);
        make_level_button(right_list, "TNGI", 76, 246, 36, 22, cur_contrast == 2, on_contrast_click,
                          2);
        make_level_button(right_list, "EKST", 114, 246, 36, 22, cur_contrast == 3,
                          on_contrast_click, 3);

        // 6. Sharpness (Ketajaman Aksen Tema)
        MakeLabel(right_list, "KETAJAMAN WARNA", 2, 272, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "HLUS", 2, 286, 35, 22, cur_sharpness == 0,
                          on_sharpness_click, 0);
        make_level_button(right_list, "STND", 39, 286, 35, 22, cur_sharpness == 1,
                          on_sharpness_click, 1);
        make_level_button(right_list, "TAJM", 76, 286, 36, 22, cur_sharpness == 2,
                          on_sharpness_click, 2);
        make_level_button(right_list, "EKST", 114, 286, 36, 22, cur_sharpness == 3,
                          on_sharpness_click, 3);

        // 7. Icon Color Mode (Warna Ikon Aplikasi)
        MakeLabel(right_list, "WARNA IKON APLIKASI", 2, 312, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "CMPR", 2, 326, 46, 22, icon_color_mode_ == ICON_COLOR_MIXED,
                          on_icon_mode_click, ICON_COLOR_MIXED);
        make_level_button(right_list, "ACAK", 51, 326, 46, 22,
                          icon_color_mode_ == ICON_COLOR_RANDOM, on_icon_mode_click,
                          ICON_COLOR_RANDOM);
        make_level_button(right_list, "TEMA", 100, 326, 46, 22,
                          icon_color_mode_ == ICON_COLOR_THEME, on_icon_mode_click,
                          ICON_COLOR_THEME);

        // 8. Header Icon Color (Warna Ikon Header Status Bar)
        MakeLabel(right_list, "WARNA IKON HEADER", 2, 352, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* hdr_box = lv_obj_create(right_list);
        lv_obj_set_pos(hdr_box, 2, 366);
        lv_obj_set_size(hdr_box, 148, 30);
        lv_obj_set_flex_flow(hdr_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(hdr_box, C_PANEL, 0);
        lv_obj_set_style_border_color(hdr_box, C_BORDER, 0);
        lv_obj_set_style_border_width(hdr_box, 1, 0);
        lv_obj_set_style_radius(hdr_box, 6, 0);
        lv_obj_set_style_pad_all(hdr_box, 2, 0);
        lv_obj_set_style_pad_column(hdr_box, 4, 0);
        lv_obj_set_scroll_dir(hdr_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(hdr_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(hdr_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(hdr_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(hdr_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(hdr_box, LV_SCROLLBAR_MODE_OFF);

        static const uint32_t kHdrColors[] = {0xFFFFFF, 0x38BDF8, 0x22C55E, 0x06B6D4, 0xFFE600,
                                              0xF97316, 0xEF4444, 0xA855F7, 0xEC4899, 0x94A3B8};
        for (size_t i = 0; i < sizeof(kHdrColors) / sizeof(kHdrColors[0]); ++i) {
            lv_obj_t* dot = lv_btn_create(hdr_box);
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(kHdrColors[i]), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (header_icon_color_ == kHdrColors[i]);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            lv_obj_set_user_data(dot, (void*)(uintptr_t)kHdrColors[i]);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    uint32_t clr = (uint32_t)(uintptr_t)lv_obj_get_user_data(
                        (lv_obj_t*)lv_event_get_target(e));
                    self->ApplyHeaderIconColor(clr, true);
                    if (self->callbacks_.trigger_click)
                        self->callbacks_.trigger_click();

                    // Update active border without page rebuild
                    lv_obj_t* p_box = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
                    if (p_box) {
                        uint32_t c_cnt = lv_obj_get_child_cnt(p_box);
                        for (uint32_t ci = 0; ci < c_cnt; ++ci) {
                            lv_obj_t* ch = lv_obj_get_child(p_box, ci);
                            uint32_t c_clr = (uint32_t)(uintptr_t)lv_obj_get_user_data(ch);
                            bool act = (c_clr == clr);
                            lv_obj_set_style_border_width(ch, act ? 2 : 1, 0);
                            lv_obj_set_style_border_color(
                                ch, act ? lv_color_white() : lv_color_hex(0x475569), 0);
                        }
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        MakeButton(
            right_list, "🎨 POPUP PALET HEADER", 2, 402, 148, 22, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowHeaderColorModal();
            },
            this);

        lv_obj_add_event_cb(
            right_list,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_scroll_y_ =
                        lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            pal_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_pal_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            bg_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_bg_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            tc_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_tc_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            hdr_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_hdr_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);

        lv_obj_update_layout(root);
        if (display_scroll_y_ > 0)
            lv_obj_scroll_to_y(right_list, display_scroll_y_, LV_ANIM_OFF);
        if (display_pal_scroll_x_ > 0)
            lv_obj_scroll_to_x(pal_box, display_pal_scroll_x_, LV_ANIM_OFF);
        if (display_bg_scroll_x_ > 0)
            lv_obj_scroll_to_x(bg_box, display_bg_scroll_x_, LV_ANIM_OFF);
        if (display_tc_scroll_x_ > 0)
            lv_obj_scroll_to_x(tc_box, display_tc_scroll_x_, LV_ANIM_OFF);
        if (display_hdr_scroll_x_ > 0)
            lv_obj_scroll_to_x(hdr_box, display_hdr_scroll_x_, LV_ANIM_OFF);

    } else {
        // === PORTRAIT LAYOUT (240x320) WITH SMOOTH SCROLLING ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 300, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        MakeLabel(list, "Kecerahan layar", 14, 6, 212, 20, FONT_14, C_TEXT);
        MakeLabel(list, "Atur intensitas backlight perangkat.", 14, 26, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* card = MakePanel(list, 12, 46, 216, 66, C_PANEL, C_BORDER, 10);
        char val[24];
        snprintf(val, sizeof(val), "%u / 255", brightness_);
        brightness_value_label_ =
            MakeLabel(card, val, 12, 6, 190, 16, FONT_12, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(card, 12, 28, 190);

        MakeLabel(list, "PILIHAN KECERAHAN", 14, 118, 212, 14, FONT_10, C_MUTED);

        MakeButton(
            list, "REDUP", 12, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(55);
            },
            this);

        MakeButton(
            list, "SEDANG", 88, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(145);
            },
            this);

        MakeButton(
            list, "TERANG", 164, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ApplyBrightness(235);
            },
            this);

        // Section 1: Synchronized Theme Accent Colors (10 Themes)
        MakeLabel(list, "TEMA WARNA SINKRON (AKSEN)", 14, 172, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* pal_box = lv_obj_create(list);
        lv_obj_set_pos(pal_box, 12, 188);
        lv_obj_set_size(pal_box, 216, 34);
        lv_obj_set_flex_flow(pal_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pal_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(pal_box, C_PANEL, 0);
        lv_obj_set_style_border_color(pal_box, C_BORDER, 0);
        lv_obj_set_style_border_width(pal_box, 1, 0);
        lv_obj_set_style_radius(pal_box, 8, 0);
        lv_obj_set_style_pad_all(pal_box, 3, 0);
        lv_obj_set_style_pad_column(pal_box, 6, 0);
        lv_obj_set_scroll_dir(pal_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(pal_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(pal_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(pal_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(pal_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(pal_box, LV_SCROLLBAR_MODE_OFF);

        size_t theme_count = 0;
        const auto* themes = GetThemeSyncTable(&theme_count);
        for (size_t i = 0; i < theme_count; ++i) {
            lv_obj_t* dot = lv_btn_create(pal_box);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(themes[i].accent_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active =
                (lv_color_to_int(C_CYAN) == lv_color_to_int(lv_color_hex(themes[i].accent_color)));
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(
                dot, is_active ? lv_color_white() : lv_color_hex(themes[i].border_color), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* th_table = GetThemeSyncTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_pal_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyTheme(th_table[idx].theme_name, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        // Section 2: Custom Background Colors (25 Choices)
        MakeLabel(list, "WARNA BACKGROUND (25 PILIHAN)", 14, 228, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* bg_box = lv_obj_create(list);
        lv_obj_set_pos(bg_box, 12, 244);
        lv_obj_set_size(bg_box, 216, 34);
        lv_obj_set_flex_flow(bg_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bg_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(bg_box, C_PANEL, 0);
        lv_obj_set_style_border_color(bg_box, C_BORDER, 0);
        lv_obj_set_style_border_width(bg_box, 1, 0);
        lv_obj_set_style_radius(bg_box, 8, 0);
        lv_obj_set_style_pad_all(bg_box, 3, 0);
        lv_obj_set_style_pad_column(bg_box, 6, 0);
        lv_obj_set_scroll_dir(bg_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(bg_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(bg_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bg_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(bg_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(bg_box, LV_SCROLLBAR_MODE_OFF);

        size_t bg_count = 0;
        const auto* bgs = GetBackgroundTable(&bg_count);
        for (size_t i = 0; i < bg_count; ++i) {
            lv_obj_t* dot = lv_btn_create(bg_box);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(bgs[i].swatch_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (cur_bg && std::string(cur_bg->id) == bgs[i].id);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* bg_table = GetBackgroundTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_bg_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyBackground(bg_table[idx].id, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        // Custom RGB Mixer Button for Background
        MakeButton(
            list, "🎨 ATUR RGB LATAR SENDIRI", 12, 284, 216, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRgbMixerModal(false);
            },
            this);

        if (cur_bg) {
            char bg_txt[48];
            snprintf(bg_txt, sizeof(bg_txt), "Latar: %s", cur_bg->display_name);
            MakeLabel(list, bg_txt, 14, 316, 212, 14, FONT_10, C_CYAN);
        }

        // Section 3: High Contrast Text Colors (Full 31 Choices)
        MakeLabel(list, "WARNA TEKS & KONTRAS (31 PILIHAN)", 14, 334, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* tc_box = lv_obj_create(list);
        lv_obj_set_pos(tc_box, 12, 350);
        lv_obj_set_size(tc_box, 216, 34);
        lv_obj_set_flex_flow(tc_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tc_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(tc_box, C_PANEL, 0);
        lv_obj_set_style_border_color(tc_box, C_BORDER, 0);
        lv_obj_set_style_border_width(tc_box, 1, 0);
        lv_obj_set_style_radius(tc_box, 8, 0);
        lv_obj_set_style_pad_all(tc_box, 3, 0);
        lv_obj_set_style_pad_column(tc_box, 6, 0);
        lv_obj_set_scroll_dir(tc_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(tc_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(tc_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tc_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(tc_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(tc_box, LV_SCROLLBAR_MODE_OFF);

        size_t tc_count = 0;
        const auto* tcs = GetTextColorTable(&tc_count);
        for (size_t i = 0; i < tc_count; ++i) {
            lv_obj_t* dot = lv_btn_create(tc_box);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(tcs[i].swatch_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (cur_tc && std::string(cur_tc->id) == tcs[i].id);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            if (strcmp(tcs[i].id, "auto") == 0) {
                lv_obj_t* albl = lv_label_create(dot);
                lv_label_set_text(albl, "A");
                lv_obj_set_style_text_font(albl, FONT_10, 0);
                lv_obj_set_style_text_color(albl, lv_color_black(), 0);
                lv_obj_center(albl);
            }

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    int idx =
                        (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    size_t count = 0;
                    const auto* tc_table = GetTextColorTable(&count);
                    if (idx >= 0 && idx < (int)count) {
                        lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                        lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                        if (p_box) {
                            self->display_tc_scroll_x_ = lv_obj_get_scroll_x(p_box);
                            lv_obj_t* p_list = lv_obj_get_parent(p_box);
                            if (p_list)
                                self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                        }
                        self->ApplyTextColor(tc_table[idx].id, true);
                        if (self->callbacks_.trigger_click)
                            self->callbacks_.trigger_click();
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        // Custom RGB Button for Text Color
        MakeButton(
            list, "🎨 ATUR RGB TEKS SENDIRI", 12, 388, 216, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRgbMixerModal(true);
            },
            this);

        if (cur_tc) {
            char tc_txt[48];
            snprintf(tc_txt, sizeof(tc_txt), "Teks: %s", cur_tc->display_name);
            MakeLabel(list, tc_txt, 14, 420, 212, 14, FONT_10, C_CYAN);
        }

        // Section 4: Text Weight (Ketebalan Teks - 4 Level)
        MakeLabel(list, "KETEBALAN TEKS (FONT WEIGHT)", 14, 440, 212, 14, FONT_10, C_MUTED);
        make_level_button(list, "NORMAL", 12, 456, 48, 28, cur_weight == 0, on_weight_click, 0);
        make_level_button(list, "SEDANG", 68, 456, 48, 28, cur_weight == 1, on_weight_click, 1);
        make_level_button(list, "TEBAL", 124, 456, 48, 28, cur_weight == 2, on_weight_click, 2);
        make_level_button(list, "EKSTRA", 180, 456, 48, 28, cur_weight == 3, on_weight_click, 3);

        // Section 5: Contrast (Kontras Background & Panel - 4 Level)
        MakeLabel(list, "KONTRAS LATAR & PANEL", 14, 492, 212, 14, FONT_10, C_MUTED);
        make_level_button(list, "LEMBUT", 12, 508, 48, 28, cur_contrast == 0, on_contrast_click, 0);
        make_level_button(list, "NORMAL", 68, 508, 48, 28, cur_contrast == 1, on_contrast_click, 1);
        make_level_button(list, "TINGGI", 124, 508, 48, 28, cur_contrast == 2, on_contrast_click,
                          2);
        make_level_button(list, "EKSTREM", 180, 508, 48, 28, cur_contrast == 3, on_contrast_click,
                          3);

        // Section 6: Sharpness (Ketajaman & Saturasi Tema - 4 Level)
        MakeLabel(list, "KETAJAMAN WARNA TEMA", 14, 544, 212, 14, FONT_10, C_MUTED);
        make_level_button(list, "HALUS", 12, 560, 48, 28, cur_sharpness == 0, on_sharpness_click,
                          0);
        make_level_button(list, "STANDAR", 68, 560, 48, 28, cur_sharpness == 1, on_sharpness_click,
                          1);
        make_level_button(list, "TAJAM", 124, 560, 48, 28, cur_sharpness == 2, on_sharpness_click,
                          2);
        make_level_button(list, "EKSTREM", 180, 560, 48, 28, cur_sharpness == 3, on_sharpness_click,
                          3);

        // Section 7: Icon Color Mode (Warna Ikon Aplikasi)
        MakeLabel(list, "WARNA IKON APLIKASI", 14, 596, 212, 14, FONT_10, C_MUTED);
        make_level_button(list, "CAMPURAN", 12, 612, 68, 28, icon_color_mode_ == ICON_COLOR_MIXED,
                          on_icon_mode_click, ICON_COLOR_MIXED);
        make_level_button(list, "ACAK", 84, 612, 68, 28, icon_color_mode_ == ICON_COLOR_RANDOM,
                          on_icon_mode_click, ICON_COLOR_RANDOM);
        make_level_button(list, "TEMA", 156, 612, 68, 28, icon_color_mode_ == ICON_COLOR_THEME,
                          on_icon_mode_click, ICON_COLOR_THEME);

        // Section 8: Header Icon Color (Warna Ikon Header Status Bar)
        MakeLabel(list, "WARNA IKON HEADER (STATUS BAR)", 14, 652, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* hdr_box = lv_obj_create(list);
        lv_obj_set_pos(hdr_box, 12, 668);
        lv_obj_set_size(hdr_box, 216, 34);
        lv_obj_set_flex_flow(hdr_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hdr_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(hdr_box, C_PANEL, 0);
        lv_obj_set_style_border_color(hdr_box, C_BORDER, 0);
        lv_obj_set_style_border_width(hdr_box, 1, 0);
        lv_obj_set_style_radius(hdr_box, 8, 0);
        lv_obj_set_style_pad_all(hdr_box, 3, 0);
        lv_obj_set_style_pad_column(hdr_box, 6, 0);
        lv_obj_set_scroll_dir(hdr_box, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(hdr_box, LV_SCROLL_SNAP_NONE);
        lv_obj_add_flag(hdr_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(hdr_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
        lv_obj_add_flag(hdr_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_scrollbar_mode(hdr_box, LV_SCROLLBAR_MODE_OFF);

        static const uint32_t kHdrColorsPort[] = {0xFFFFFF, 0x38BDF8, 0x22C55E, 0x06B6D4, 0xFFE600,
                                                  0xF97316, 0xEF4444, 0xA855F7, 0xEC4899, 0x94A3B8};
        for (size_t i = 0; i < sizeof(kHdrColorsPort) / sizeof(kHdrColorsPort[0]); ++i) {
            lv_obj_t* dot = lv_btn_create(hdr_box);
            lv_obj_set_size(dot, 24, 24);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(kHdrColorsPort[i]), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_width(dot, 0, 0);
            bool is_active = (header_icon_color_ == kHdrColorsPort[i]);
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot,
                                          is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_CHAIN);

            lv_obj_set_user_data(dot, (void*)(uintptr_t)kHdrColorsPort[i]);
            lv_obj_add_event_cb(
                dot,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    uint32_t clr = (uint32_t)(uintptr_t)lv_obj_get_user_data(
                        (lv_obj_t*)lv_event_get_target(e));
                    self->ApplyHeaderIconColor(clr, true);
                    if (self->callbacks_.trigger_click)
                        self->callbacks_.trigger_click();

                    // Update active border highlight without page rebuild
                    lv_obj_t* p_box = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
                    if (p_box) {
                        uint32_t c_cnt = lv_obj_get_child_cnt(p_box);
                        for (uint32_t ci = 0; ci < c_cnt; ++ci) {
                            lv_obj_t* ch = lv_obj_get_child(p_box, ci);
                            uint32_t c_clr = (uint32_t)(uintptr_t)lv_obj_get_user_data(ch);
                            bool act = (c_clr == clr);
                            lv_obj_set_style_border_width(ch, act ? 2 : 1, 0);
                            lv_obj_set_style_border_color(
                                ch, act ? lv_color_white() : lv_color_hex(0x475569), 0);
                        }
                    }
                },
                LV_EVENT_CLICKED, this);
        }

        // Section 9: Screen Rotation Button & Header Color Modal Button
        MakeButton(
            list, "🎨 PALET POPUP IKON HEADER", 12, 708, 216, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowHeaderColorModal();
            },
            this);

        MakeButton(
            list, "ROTASI LAYAR 90°", 12, 742, 216, 32, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            },
            nullptr);

        lv_obj_add_event_cb(
            list,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_scroll_y_ =
                        lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            pal_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_pal_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            bg_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_bg_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            tc_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_tc_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(
            hdr_box,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->display_hdr_scroll_x_ =
                        lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);

        lv_obj_update_layout(root);
        if (display_scroll_y_ > 0)
            lv_obj_scroll_to_y(list, display_scroll_y_, LV_ANIM_OFF);
        if (display_pal_scroll_x_ > 0)
            lv_obj_scroll_to_x(pal_box, display_pal_scroll_x_, LV_ANIM_OFF);
        if (display_bg_scroll_x_ > 0)
            lv_obj_scroll_to_x(bg_box, display_bg_scroll_x_, LV_ANIM_OFF);
        if (display_tc_scroll_x_ > 0)
            lv_obj_scroll_to_x(tc_box, display_tc_scroll_x_, LV_ANIM_OFF);
        if (display_hdr_scroll_x_ > 0)
            lv_obj_scroll_to_x(hdr_box, display_hdr_scroll_x_, LV_ANIM_OFF);
    }

    MakeBottomNav(root, PAGE_DISPLAY);
}
