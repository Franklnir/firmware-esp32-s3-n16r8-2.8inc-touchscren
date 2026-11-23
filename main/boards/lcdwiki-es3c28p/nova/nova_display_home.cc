#include "nova_display_internal.h"

void OnLaunchAiVoice(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self && self->GetCallbacks().switch_mode) {
        ESP_LOGI(TAG, "Launching XiaoZhi AI Mode (Mode 0)...");
        self->GetCallbacks().switch_mode(0);
    }
}

void OnLaunchChronchi(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self && self->GetCallbacks().switch_mode) {
        ESP_LOGI(TAG, "Launching Chronchi Smartwatch Mode (Mode 1)...");
        self->GetCallbacks().switch_mode(1);
    }
}

void NovaDisplay::BuildHome() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "NOVA MINI OS", false);

    char time_text[16] = "--:--", date_text[32] = "WAKTU BELUM TERSINKRON";
    time_t now_sec = time(nullptr);
    struct tm info;
    localtime_r(&now_sec, &info);
    if (info.tm_year > (2020 - 1900)) {
        if (use_24hour_) {
            snprintf(time_text, sizeof(time_text), "%02d:%02d", info.tm_hour, info.tm_min);
        } else {
            int h = info.tm_hour % 12;
            if (h == 0)
                h = 12;
            snprintf(time_text, sizeof(time_text), "%d:%02d %s", h, info.tm_min,
                     info.tm_hour >= 12 ? "PM" : "AM");
        }
        static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN",
                                       "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};
        snprintf(date_text, sizeof(date_text), "%02d %s %04d", info.tm_mday, months[info.tm_mon],
                 info.tm_year + 1900);
    }

    if (width_ > 240) {
        // Landscape Mode (320x240): Tata letak seimbang dan terpusat (Center Aligned)
        // Baris Atas (Y=48, H=72): Kartu Jam & Tanggal (Kiri) dan Kartu Xiaozhi AI (Kanan)
        lv_obj_t* time_card = MakePanel(root, 12, 48, 142, 72, C_PANEL, C_BORDER, 12);
        clock_label_ = MakeLabel(time_card, time_text, 12, 6, 120, 28, FONT_28, C_TEXT);
        MakeLabel(time_card, "WAKTU LOKAL", 12, 34, 120, 14, FONT_10, C_CYAN);
        date_label_ = MakeLabel(time_card, date_text, 12, 48, 120, 16, FONT_10, C_MUTED);

        lv_obj_t* ai_card = MakePanel(root, 166, 48, 142, 72, C_PANEL, C_BORDER, 12);
        MakeIcon(ai_card, ICON_AI, 8, 14, GetAppIconColor(0, ICON_AI), 24);
        MakeLabel(ai_card, "Xiaozhi AI", 36, 6, 100, 18, FONT_14, C_TEXT);
        MakeLabel(ai_card, "Asisten suara LLM", 36, 24, 100, 14, FONT_10, C_MUTED);
        MakeButton(ai_card, "BUKA ASISTEN", 36, 43, 98, 22, C_CYAN, C_CYAN, C_BG, FONT_10,
                   OnLaunchAiVoice, this);

        // Baris Bawah (Y=134, H=56): 3 Tile Aplikasi presisi di tengah layar (margin kiri/kanan
        // 12px)
        const int tile_x[] = {12, 114, 216};
        const char* tile_names[] = {"Wi-Fi", "Watch", "Apps"};
        const IconKind tile_icons[] = {ICON_WIFI, ICON_CHRONOS, ICON_APPS};
        const lv_color_t tile_colors[] = {GetAppIconColor(8, ICON_WIFI),
                                          GetAppIconColor(7, ICON_CHRONOS),
                                          GetAppIconColor(16, ICON_APPS)};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* tile = lv_btn_create(root);
            lv_obj_set_pos(tile, tile_x[i], 134);
            lv_obj_set_size(tile, 92, 56);
            StyleButton(tile, C_PANEL, C_BORDER, 10);
            MakeIcon(tile, tile_icons[i], 12, 16, tile_colors[i], 24);
            MakeLabel(tile, tile_names[i], 42, 18, 46, 18, FONT_12, C_TEXT);

            if (i == 0) {
                lv_obj_set_user_data(tile, (void*)(intptr_t)PAGE_WIFI);
                lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
            } else if (i == 1) {
                lv_obj_add_event_cb(tile, OnLaunchChronchi, LV_EVENT_CLICKED, this);
            } else {
                lv_obj_set_user_data(tile, (void*)(intptr_t)PAGE_APPS);
                lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
            }
        }
    } else {
        // Portrait Mode (240x320): Jam saja diturunkan 1mm (ke Y=56), teks waktu/tanggal tetap di
        // posisi semula
        clock_label_ = MakeLabel(root, time_text, 15, 56, 210, 32, FONT_36, C_TEXT);
        MakeLabel(root, "WAKTU LOKAL", 17, 91, 100, 15, FONT_12, C_CYAN);
        date_label_ = MakeLabel(root, date_text, 17, 106, 207, 16, FONT_12, C_MUTED);

        lv_obj_t* ai_card = MakePanel(root, 12, 126, 216, 74, C_PANEL, C_BORDER, 14);
        MakeIcon(ai_card, ICON_AI, 12, 15, GetAppIconColor(0, ICON_AI), 28);
        MakeLabel(ai_card, "Xiaozhi AI", 48, 7, 153, 22, FONT_14, C_TEXT);
        MakeLabel(ai_card, "Asisten suara LLM", 48, 28, 153, 16, FONT_10, C_MUTED);
        MakeButton(ai_card, "BUKA ASISTEN", 48, 47, 154, 22, C_CYAN, C_CYAN, C_BG, FONT_10,
                   OnLaunchAiVoice, this);

        const int tile_x[] = {12, 88, 164};
        const char* tile_names[] = {"Radio", "Watch", "Apps"};
        const IconKind tile_icons[] = {ICON_RADIO, ICON_CHRONOS, ICON_APPS};
        const lv_color_t tile_colors[] = {GetAppIconColor(1, ICON_RADIO),
                                          GetAppIconColor(7, ICON_CHRONOS),
                                          GetAppIconColor(16, ICON_APPS)};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t* tile = lv_btn_create(root);
            lv_obj_set_pos(tile, tile_x[i], 208);
            lv_obj_set_size(tile, 64, 62);
            StyleButton(tile, C_PANEL, C_BORDER, 12);
            MakeIcon(tile, tile_icons[i], 19, 7, tile_colors[i], 25);
            MakeCenteredLabel(tile, tile_names[i], 0, 39, 64, 16, FONT_12, C_TEXT);

            if (i == 0) {
                lv_obj_set_user_data(tile, (void*)(intptr_t)PAGE_RADIO);
                lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
            } else if (i == 1) {
                lv_obj_add_event_cb(tile, OnLaunchChronchi, LV_EVENT_CLICKED, this);
            } else {
                lv_obj_set_user_data(tile, (void*)(intptr_t)PAGE_APPS);
                lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
            }
        }
    }
    MakeBottomNav(root, PAGE_HOME);
}

static void OnLaunchChronchiScreen(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    lv_indev_t* indev = lv_indev_active();
    if (indev && lv_indev_get_scroll_obj(indev) != nullptr)
        return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(event);
    if (btn && (lv_obj_is_scrolling(btn) ||
                (lv_obj_get_parent(btn) && lv_obj_is_scrolling(lv_obj_get_parent(btn)))))
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    int screen = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (!self)
        return;
    if (screen == 1) {
        self->NavigateTo(NovaDisplay::PAGE_NAV);
    } else if (screen == 4) {
        self->NavigateTo(NovaDisplay::PAGE_BT_MUSIC);
    } else if (screen == 2) {
        self->NavigateTo(NovaDisplay::PAGE_NOTIF);
    } else if (screen == 3) {
        self->NavigateTo(NovaDisplay::PAGE_WEATHER);
    } else if (screen == 5) {
        self->NavigateTo(NovaDisplay::PAGE_PHONE);
    } else {
        if (self->GetCallbacks().switch_mode) {
            self->GetCallbacks().switch_mode(1);
        }
    }
}

void NovaDisplay::ChronchiTile(lv_obj_t* root, int x, int y, const char* name,
                               const char* description, IconKind icon, lv_color_t accent,
                               int screen) {
    lv_obj_t* tile = lv_btn_create(root);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 102, 54);
    StyleButton(tile, C_PANEL, C_BORDER, 12);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_SCROLL_CHAIN);
    MakeIcon(tile, icon, 8, 13, accent, 26);
    lv_obj_t* name_lbl = MakeLabel(tile, name, 40, 7, 58, 19, FONT_14, C_TEXT);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t* desc_lbl = MakeLabel(tile, description, 40, 27, 58, 18, FONT_10, C_MUTED);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_user_data(tile, (void*)(intptr_t)screen);
    lv_obj_add_event_cb(tile, OnLaunchChronchiScreen, LV_EVENT_CLICKED, this);
}

void NovaDisplay::AppTile(lv_obj_t* root, int x, int y, const char* name, const char* description,
                          IconKind icon, lv_color_t accent, Page destination) {
    lv_obj_t* tile = lv_btn_create(root);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 102, 54);
    StyleButton(tile, C_PANEL, C_BORDER, 12);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_SCROLL_CHAIN);
    MakeIcon(tile, icon, 8, 13, accent, 26);
    lv_obj_t* name_lbl = MakeLabel(tile, name, 40, 7, 58, 19, FONT_14, C_TEXT);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t* desc_lbl = MakeLabel(tile, description, 40, 27, 58, 18, FONT_10, C_MUTED);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_user_data(tile, (void*)(intptr_t)destination);
    lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
}

void NovaDisplay::BuildApps() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "APLIKASI", false);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 6-COLUMN APP GRID (Semua Aplikasi & Fitur Chronos HP) ===
        struct AppGridItem {
            const char* name;
            IconKind icon;
            lv_color_t color;
            Page page;
            int chronchi_screen;  // -1: normal page, >= 0: Chronchi screen
            bool is_games_modal;
        };

        static const AppGridItem kApps[] = {
            {"Xiaozhi", ICON_AI, C_CYAN, PAGE_AI, -1, false},
            {"Navigasi", ICON_NAV, C_MINT, PAGE_NAV, -1, false},
            {"Musik HP", ICON_AUDIO, C_AMBER, PAGE_BT_MUSIC, -1, false},
            {"Notif HP", ICON_CHAT, C_CYAN, PAGE_NOTIF, -1, false},
            {"Cuaca HP", ICON_WEATHER, C_AMBER, PAGE_WEATHER, -1, false},
            {"Cari HP", ICON_PHONE, C_MINT, PAGE_PHONE, -1, false},
            {"Smartwatch", ICON_CHRONOS, C_AMBER, PAGE_HOME, 0, false},
            {"Radio", ICON_RADIO, C_MINT, PAGE_RADIO, -1, false},
            {"Wi-Fi", ICON_WIFI, C_CYAN, PAGE_WIFI, -1, false},
            {"Audio", ICON_AUDIO, C_AMBER, PAGE_AUDIO, -1, false},
            {"File Hub", ICON_FILES, C_VIOLET, PAGE_FILES, -1, false},
            {"Chronos", ICON_CHRONOS, C_AMBER, PAGE_CHRONOS, -1, false},
            {"Tampilan", ICON_BRIGHTNESS, C_AMBER, PAGE_DISPLAY, -1, false},
            {"Baterai", ICON_BATTERY, C_MINT, PAGE_BATTERY, -1, false},
            {"Atur", ICON_SETTINGS, C_VIOLET, PAGE_SETTINGS, -1, false},
            {"Kalkulator", ICON_CALC, C_CYAN, PAGE_CALCULATOR, -1, false},
            {"Game", ICON_GAME, C_AMBER, PAGE_APPS, -1, true},
            {"Pin Lab", ICON_CHIP, C_CYAN, PAGE_PINLAB, -1, false},
            {"Pintasan", ICON_SETTINGS, C_CYAN, PAGE_QUICK, -1, false},
            {"Piano", ICON_PIANO, C_CYAN, PAGE_PIANO, -1, false},
            {"Sketch", ICON_DRAW, C_VIOLET, PAGE_SKETCH, -1, false},
            {"Monitor", ICON_DASHBOARD, C_MINT, PAGE_MONITOR, -1, false}};

        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 32);
        lv_obj_set_size(list, 320, 178);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 14, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        const int total_apps = sizeof(kApps) / sizeof(kApps[0]);
        const int cols = 6;
        const int tile_w = 46;
        const int tile_h = 52;
        const int gap_x = 6;
        const int gap_y = 6;
        const int start_x = 7;
        const int start_y = 4;

        for (int i = 0; i < total_apps; ++i) {
            int c = i % cols;
            int r = i / cols;
            int x = start_x + c * (tile_w + gap_x);
            int y = start_y + r * (tile_h + gap_y);

            lv_obj_t* tile = lv_btn_create(list);
            lv_obj_set_pos(tile, x, y);
            lv_obj_set_size(tile, tile_w, tile_h);
            StyleButton(tile, C_PANEL, C_BORDER, 8);

            MakeIcon(tile, kApps[i].icon, 11, 5, GetAppIconColor(i, kApps[i].icon), 24);
            lv_obj_t* lbl =
                MakeCenteredLabel(tile, kApps[i].name, 0, 32, tile_w, 14, FONT_10, C_TEXT);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            if (kApps[i].chronchi_screen >= 0) {
                lv_obj_set_user_data(tile, (void*)(intptr_t)kApps[i].chronchi_screen);
                lv_obj_add_event_cb(tile, OnLaunchChronchiScreen, LV_EVENT_CLICKED, this);
            } else if (kApps[i].is_games_modal) {
                lv_obj_add_event_cb(
                    tile,
                    [](lv_event_t* e) {
                        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                            return;
                        lv_indev_t* indev = lv_indev_active();
                        if (indev && lv_indev_get_scroll_obj(indev) != nullptr)
                            return;
                        lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
                        if (b &&
                            (lv_obj_is_scrolling(b) ||
                             (lv_obj_get_parent(b) && lv_obj_is_scrolling(lv_obj_get_parent(b)))))
                            return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                        if (self)
                            self->ShowGamesModal();
                    },
                    LV_EVENT_CLICKED, this);
            } else {
                lv_obj_set_user_data(tile, (void*)(intptr_t)kApps[i].page);
                lv_obj_add_event_cb(tile, OnNavClicked, LV_EVENT_CLICKED, this);
            }
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 70, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        MakeLabel(list, "SEMUA APLIKASI", 14, 4, 212, 16, FONT_12, C_MUTED);

        // Row 1: Xiaozhi AI & Radio Streaming Live
        AppTile(list, 12, 24, "Xiaozhi", "Asisten AI", ICON_AI, GetAppIconColor(0, ICON_AI),
                PAGE_AI);
        AppTile(list, 126, 24, "Radio Live", "Online Stream", ICON_RADIO,
                GetAppIconColor(1, ICON_RADIO), PAGE_RADIO);

        // Row 2: Navigasi Google Maps & Musik HP
        AppTile(list, 12, 84, "Navigasi", "Google Maps", ICON_NAV, GetAppIconColor(2, ICON_NAV),
                PAGE_NAV);
        AppTile(list, 126, 84, "Musik HP", "Remote Media", ICON_AUDIO,
                GetAppIconColor(3, ICON_AUDIO), PAGE_BT_MUSIC);

        // Row 3: Notifikasi HP & Cuaca HP
        AppTile(list, 12, 144, "Notif HP", "Pusat Pesan", ICON_CHAT, GetAppIconColor(4, ICON_CHAT),
                PAGE_NOTIF);
        AppTile(list, 126, 144, "Cuaca HP", "Info Cuaca", ICON_WEATHER,
                GetAppIconColor(5, ICON_WEATHER), PAGE_WEATHER);

        // Row 4: Cari HP & Smartwatch Chronos
        AppTile(list, 12, 204, "Cari HP", "Deringkan HP", ICON_PHONE,
                GetAppIconColor(6, ICON_PHONE), PAGE_PHONE);
        ChronchiTile(list, 126, 204, "Smartwatch", "Jam & Wajah", ICON_CHRONOS,
                     GetAppIconColor(7, ICON_CHRONOS), 0);

        // Row 5
        AppTile(list, 12, 264, "Wi-Fi", "Jaringan", ICON_WIFI, GetAppIconColor(8, ICON_WIFI),
                PAGE_WIFI);
        AppTile(list, 126, 264, "Audio", "Suara I/O", ICON_AUDIO, GetAppIconColor(9, ICON_AUDIO),
                PAGE_AUDIO);

        // Row 6
        AppTile(list, 12, 324, "File Hub", "Berkas", ICON_FILES, GetAppIconColor(10, ICON_FILES),
                PAGE_FILES);
        AppTile(list, 126, 324, "Chronos", "Jam / fokus", ICON_CHRONOS,
                GetAppIconColor(11, ICON_CHRONOS), PAGE_CHRONOS);

        // Row 7
        AppTile(list, 12, 384, "Tampilan", "Kecerahan", ICON_BRIGHTNESS,
                GetAppIconColor(12, ICON_BRIGHTNESS), PAGE_DISPLAY);
        AppTile(list, 126, 384, "Baterai", "Status daya", ICON_BATTERY,
                GetAppIconColor(13, ICON_BATTERY), PAGE_BATTERY);

        // Row 8
        AppTile(list, 12, 444, "Atur", "Sistem", ICON_SETTINGS, GetAppIconColor(14, ICON_SETTINGS),
                PAGE_SETTINGS);
        AppTile(list, 126, 444, "Kalkulator", "Hitung cepat", ICON_CALC,
                GetAppIconColor(15, ICON_CALC), PAGE_CALCULATOR);

        // Row 9
        lv_obj_t* game_tile = lv_btn_create(list);
        lv_obj_set_pos(game_tile, 12, 504);
        lv_obj_set_size(game_tile, 102, 54);
        StyleButton(game_tile, C_PANEL, C_BORDER, 12);
        MakeIcon(game_tile, ICON_GAME, 8, 13, GetAppIconColor(16, ICON_GAME), 26);
        lv_obj_t* g_name = MakeLabel(game_tile, "Game", 40, 7, 58, 19, FONT_14, C_TEXT);
        lv_label_set_long_mode(g_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* g_desc = MakeLabel(game_tile, "Koleksi Game", 40, 27, 58, 18, FONT_10, C_MUTED);
        lv_label_set_long_mode(g_desc, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_flag(game_tile, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(
            game_tile,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                lv_indev_t* indev = lv_indev_active();
                if (indev && lv_indev_get_scroll_obj(indev) != nullptr)
                    return;
                lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
                if (b && (lv_obj_is_scrolling(b) ||
                          (lv_obj_get_parent(b) && lv_obj_is_scrolling(lv_obj_get_parent(b)))))
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowGamesModal();
            },
            LV_EVENT_CLICKED, this);

        AppTile(list, 126, 504, "Pin Lab", "Hardware", ICON_CHIP, GetAppIconColor(17, ICON_CHIP),
                PAGE_PINLAB);

        // Row 10
        AppTile(list, 12, 564, "Pintasan", "Aksi Cepat", ICON_SETTINGS,
                GetAppIconColor(18, ICON_SETTINGS), PAGE_QUICK);
        AppTile(list, 126, 564, "Mini Piano", "Synthesizer", ICON_PIANO,
                GetAppIconColor(19, ICON_PIANO), PAGE_PIANO);

        // Row 11
        AppTile(list, 12, 624, "Sketchpad", "Papan Gambar", ICON_DRAW,
                GetAppIconColor(20, ICON_DRAW), PAGE_SKETCH);
        AppTile(list, 126, 624, "Monitor", "Task & CPU", ICON_DASHBOARD,
                GetAppIconColor(21, ICON_DASHBOARD), PAGE_MONITOR);
    }

    MakeBottomNav(root, PAGE_APPS);
}

static void OnBrightnessSliderEvent(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self)
        return;
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(event);
    self->ApplyBrightness((uint8_t)lv_slider_get_value(slider));
}

lv_obj_t* NovaDisplay::MakeBrightnessSlider(lv_obj_t* parent, int x, int y, int width) {
    lv_obj_t* slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, width, 14);
    lv_slider_set_range(slider, 8, 255);
    lv_slider_set_value(slider, brightness_, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, C_PANEL_2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 7, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_clear_flag(slider, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                              LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                                              LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_ext_click_area(slider, 10);

    // Lock ancestor scrollable containers while sliding so the page never scrolls by accident
    lv_obj_add_event_cb(
        slider,
        [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            lv_obj_t* sld = (lv_obj_t*)lv_event_get_target(e);
            lv_obj_remove_flag(sld, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                                    LV_OBJ_FLAG_SCROLL_CHAIN_VER));

            static lv_obj_t* locked_parent = nullptr;
            if (code == LV_EVENT_PRESSED) {
                lv_obj_t* p = lv_obj_get_parent(sld);
                while (p) {
                    if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE)) {
                        locked_parent = p;
                        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
                        break;
                    }
                    p = lv_obj_get_parent(p);
                }
            } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
                if (locked_parent && lv_obj_is_valid(locked_parent)) {
                    lv_obj_add_flag(locked_parent, LV_OBJ_FLAG_SCROLLABLE);
                }
                locked_parent = nullptr;
                lv_obj_remove_flag(sld, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                                        LV_OBJ_FLAG_SCROLL_CHAIN_VER));
            }
        },
        LV_EVENT_ALL, nullptr);

    lv_obj_add_event_cb(slider, OnBrightnessSliderEvent, LV_EVENT_VALUE_CHANGED, this);
    return slider;
}

void NovaDisplay::ApplyBrightness(uint8_t value, bool persist) {
    brightness_ = (uint8_t)std::clamp((int)value, 8, 255);
    if (callbacks_.set_brightness) {
        callbacks_.set_brightness(brightness_);
    }
    if (persist) {
        Settings prefs("nova", true);
        prefs.SetInt("bright", brightness_);
        Settings disp("display", true);
        disp.SetInt("brightness", (brightness_ * 100) / 255);
        Settings chr("chronchi", true);
        chr.SetInt("bright", brightness_);
    }
    if (brightness_value_label_) {
        char val_txt[24];
        snprintf(val_txt, sizeof(val_txt), "%u / 255", brightness_);
        lv_label_set_text(brightness_value_label_, val_txt);
    }
}

void NovaDisplay::ToggleMute() {
    muted_ = !muted_;
    Settings prefs("nova", true);
    prefs.SetInt("mute", muted_ ? 1 : 0);

    if (callbacks_.set_volume) {
        if (muted_) {
            if (callbacks_.get_volume) {
                int cur = callbacks_.get_volume();
                if (cur > 0)
                    saved_volume_ = cur;
            }
            callbacks_.set_volume(0);
        } else {
            callbacks_.set_volume(saved_volume_);
        }
    }
    BuildCurrentPage();
}

void NovaDisplay::Toggle24Hour() {
    use_24hour_ = !use_24hour_;
    Settings prefs("nova", true);
    prefs.SetInt("24hour", use_24hour_ ? 1 : 0);
    BuildCurrentPage();
}

void NovaDisplay::SetBatteryStyle(uint8_t style) {
    if (style > BAT_STYLE_PERCENT_ONLY)
        style = BAT_STYLE_ICON_PERCENT;
    bat_style_ = style;
    Settings prefs("nova", true);
    prefs.SetInt("bat_style", bat_style_);
    BuildCurrentPage();
}

void NovaDisplay::SetIconColorMode(uint8_t mode) {
    if (mode > ICON_COLOR_THEME)
        mode = ICON_COLOR_MIXED;
    icon_color_mode_ = mode;
    Settings prefs("nova", true);
    prefs.SetInt("icon_mode", icon_color_mode_);
}

lv_color_t NovaDisplay::GetAppIconColor(int index, IconKind kind) {
    if (icon_color_mode_ == ICON_COLOR_THEME) {
        return C_CYAN;
    }

    static const uint32_t VIBRANT_PALETTE[] = {
        0x00E5FF,  // 0: Electric Cyan (Xiaozhi AI)
        0x10B981,  // 1: Neon Emerald (Radio Live)
        0x3B82F6,  // 2: Royal Blue (Google Maps Nav)
        0xEC4899,  // 3: Hot Pink (Musik HP)
        0x22C55E,  // 4: WhatsApp Lime Green (Notif HP)
        0xF59E0B,  // 5: Sunflower Amber (Cuaca HP)
        0xEF4444,  // 6: Coral Crimson (Cari HP)
        0x8B5CF6,  // 7: Cyber Violet (Smartwatch)
        0x06B6D4,  // 8: Turquoise Cyan (Wi-Fi)
        0xF97316,  // 9: Sunset Orange (Audio I/O)
        0xEAB308,  // 10: Golden Yellow (File Hub)
        0x6366F1,  // 11: Neon Indigo (Chronos)
        0xFBBF24,  // 12: Bright Amber (Tampilan)
        0x84CC16,  // 13: Fresh Lime (Baterai)
        0xD946EF,  // 14: Orchid Fuchsia (Atur Sistem)
        0x0EA5E9,  // 15: Sky Blue (Kalkulator)
        0xFF5722,  // 16: Flame Tangerine (Game Arcade)
        0xA855F7,  // 17: Electric Purple (Pin Lab)
        0x14B8A6,  // 18: Teal Aqua (Pintasan)
        0xF43F5E,  // 19: Electric Rose (Mini Piano)
        0xFB7185,  // 20: Pastel Coral (Sketchpad)
        0x2DD4BF   // 21: Mint Teal (Monitor CPU)
    };
    constexpr size_t PALETTE_SZ = sizeof(VIBRANT_PALETTE) / sizeof(VIBRANT_PALETTE[0]);

    if (icon_color_mode_ == ICON_COLOR_RANDOM) {
        uint32_t seed = (uint32_t)(esp_timer_get_time() / 1000000) * 7 + (uint32_t)index * 5 +
                        (uint32_t)kind * 3;
        return lv_color_hex(VIBRANT_PALETTE[seed % PALETTE_SZ]);
    }

    if (index >= 0 && index < (int)PALETTE_SZ) {
        return lv_color_hex(VIBRANT_PALETTE[index]);
    }
    return lv_color_hex(VIBRANT_PALETTE[((size_t)kind + (size_t)index) % PALETTE_SZ]);
}

void OnMuteTileClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->ToggleMute();
}

void NovaDisplay::BuildQuick() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "KONTROL CEPAT");

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected =
        (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=10, w=146): Quick toggles & Brightness slider
        lv_obj_t* wifi_tile = lv_btn_create(root);
        lv_obj_set_pos(wifi_tile, 10, 40);
        lv_obj_set_size(wifi_tile, 70, 44);
        StyleButton(wifi_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(wifi_tile, ICON_WIFI, 6, 10, C_CYAN, 20);
        MakeLabel(wifi_tile, "Wi-Fi", 28, 6, 40, 16, FONT_12, C_TEXT);
        MakeLabel(wifi_tile, wifi_connected ? "ON" : "OFF", 28, 22, 40, 14, FONT_10,
                  wifi_connected ? C_MINT : C_MUTED);
        lv_obj_set_user_data(wifi_tile, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wifi_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* audio_tile = lv_btn_create(root);
        lv_obj_set_pos(audio_tile, 84, 40);
        lv_obj_set_size(audio_tile, 72, 44);
        StyleButton(audio_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(audio_tile, ICON_AUDIO, 6, 10, muted_ ? C_MUTED : C_AMBER, 20);
        MakeLabel(audio_tile, "Audio", 28, 6, 42, 16, FONT_12, C_TEXT);
        MakeLabel(audio_tile, muted_ ? "MUTE" : "ON", 28, 22, 42, 14, FONT_10,
                  muted_ ? C_AMBER : C_MINT);
        lv_obj_add_event_cb(audio_tile, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* disp_tile = lv_btn_create(root);
        lv_obj_set_pos(disp_tile, 10, 88);
        lv_obj_set_size(disp_tile, 70, 44);
        StyleButton(disp_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(disp_tile, ICON_BRIGHTNESS, 6, 10, C_AMBER, 20);
        MakeLabel(disp_tile, "Layar", 28, 6, 40, 16, FONT_12, C_TEXT);
        MakeLabel(disp_tile, "Terang", 28, 22, 40, 14, FONT_10, C_MUTED);
        lv_obj_set_user_data(disp_tile, (void*)(intptr_t)PAGE_DISPLAY);
        lv_obj_add_event_cb(disp_tile, OnNavClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(
            disp_tile,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self)
                        self->ShowHeaderColorModal();
                }
            },
            LV_EVENT_LONG_PRESSED, this);

        lv_obj_t* set_tile = lv_btn_create(root);
        lv_obj_set_pos(set_tile, 84, 88);
        lv_obj_set_size(set_tile, 72, 44);
        StyleButton(set_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(set_tile, ICON_SETTINGS, 6, 10, C_VIOLET, 20);
        MakeLabel(set_tile, "Atur", 28, 6, 42, 16, FONT_12, C_TEXT);
        MakeLabel(set_tile, "Opsi", 28, 22, 42, 14, FONT_10, C_MUTED);
        lv_obj_set_user_data(set_tile, (void*)(intptr_t)PAGE_SETTINGS);
        lv_obj_add_event_cb(set_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* b_card = MakePanel(root, 10, 136, 146, 68, C_PANEL, C_BORDER, 10);
        MakeLabel(b_card, "KECERAHAN", 8, 6, 80, 14, FONT_10, C_MUTED);
        char b_txt[24];
        snprintf(b_txt, sizeof(b_txt), "%u", brightness_);
        brightness_value_label_ =
            MakeLabel(b_card, b_txt, 90, 6, 48, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(b_card, 8, 30, 130);

        // Right Column (x=164, w=146): Big mode switch cards
        lv_obj_t* ai_btn = lv_btn_create(root);
        lv_obj_set_pos(ai_btn, 164, 40);
        lv_obj_set_size(ai_btn, 146, 76);
        StyleButton(ai_btn, C_PANEL, C_CYAN, 12);
        MakeIcon(ai_btn, ICON_AI, 10, 20, C_CYAN, 28);
        MakeLabel(ai_btn, "Xiaozhi AI", 42, 12, 98, 20, FONT_14, C_TEXT);
        MakeLabel(ai_btn, "Asisten Suara LLM", 42, 32, 98, 14, FONT_10, C_MUTED);
        MakeLabel(ai_btn, "BUKA ASISTEN >", 42, 50, 98, 14, FONT_10, C_CYAN);
        lv_obj_add_event_cb(ai_btn, OnLaunchAiVoice, LV_EVENT_CLICKED, this);

        lv_obj_t* chr_btn = lv_btn_create(root);
        lv_obj_set_pos(chr_btn, 164, 124);
        lv_obj_set_size(chr_btn, 146, 76);
        StyleButton(chr_btn, C_PANEL, C_AMBER, 12);
        MakeIcon(chr_btn, ICON_CHRONOS, 10, 20, C_AMBER, 28);
        MakeLabel(chr_btn, "Chronchi", 42, 12, 98, 20, FONT_14, C_TEXT);
        MakeLabel(chr_btn, "Smartwatch BLE", 42, 32, 98, 14, FONT_10, C_MUTED);
        MakeLabel(chr_btn, "BUKA WATCH >", 42, 50, 98, 14, FONT_10, C_AMBER);
        lv_obj_add_event_cb(chr_btn, OnLaunchChronchi, LV_EVENT_CLICKED, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "AKSES PERANGKAT", 14, 42, 212, 16, FONT_12, C_MUTED);

        lv_obj_t* wifi_tile = lv_btn_create(root);
        lv_obj_set_pos(wifi_tile, 12, 60);
        lv_obj_set_size(wifi_tile, 102, 50);
        StyleButton(wifi_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(wifi_tile, ICON_WIFI, 10, 12, C_CYAN, 24);
        lv_obj_t* w_name = MakeLabel(wifi_tile, "Wi-Fi", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(w_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* w_sub = MakeLabel(wifi_tile, wifi_connected ? "Terhubung" : "Buka menu", 42, 26,
                                    56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(w_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_user_data(wifi_tile, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wifi_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* audio_tile = lv_btn_create(root);
        lv_obj_set_pos(audio_tile, 126, 60);
        lv_obj_set_size(audio_tile, 102, 50);
        StyleButton(audio_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(audio_tile, ICON_AUDIO, 10, 12, muted_ ? C_MUTED : C_AMBER, 24);
        lv_obj_t* a_name = MakeLabel(audio_tile, "Audio", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(a_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* a_sub = MakeLabel(audio_tile, muted_ ? "UI senyap" : "UI aktif", 42, 26, 56, 17,
                                    FONT_10, C_MUTED);
        lv_label_set_long_mode(a_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_event_cb(audio_tile, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* disp_tile = lv_btn_create(root);
        lv_obj_set_pos(disp_tile, 12, 114);
        lv_obj_set_size(disp_tile, 102, 50);
        StyleButton(disp_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(disp_tile, ICON_BRIGHTNESS, 10, 12, C_AMBER, 24);
        lv_obj_t* d_name = MakeLabel(disp_tile, "Tampilan", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(d_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* d_sub = MakeLabel(disp_tile, "Brightness", 42, 26, 56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(d_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_user_data(disp_tile, (void*)(intptr_t)PAGE_DISPLAY);
        lv_obj_add_event_cb(disp_tile, OnNavClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(
            disp_tile,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self)
                        self->ShowHeaderColorModal();
                }
            },
            LV_EVENT_LONG_PRESSED, this);

        lv_obj_t* set_tile = lv_btn_create(root);
        lv_obj_set_pos(set_tile, 126, 114);
        lv_obj_set_size(set_tile, 102, 50);
        StyleButton(set_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(set_tile, ICON_SETTINGS, 10, 12, C_VIOLET, 24);
        lv_obj_t* s_name = MakeLabel(set_tile, "Atur", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(s_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* s_sub = MakeLabel(set_tile, "Semua opsi", 42, 26, 56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(s_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_user_data(set_tile, (void*)(intptr_t)PAGE_SETTINGS);
        lv_obj_add_event_cb(set_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* b_card = MakePanel(root, 12, 168, 216, 52, C_PANEL, C_BORDER, 10);
        MakeLabel(b_card, "KECERAHAN LAYAR", 12, 7, 130, 15, FONT_10, C_MUTED);
        char b_txt[24];
        snprintf(b_txt, sizeof(b_txt), "%u / 255", brightness_);
        brightness_value_label_ =
            MakeLabel(b_card, b_txt, 140, 6, 62, 16, FONT_12, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(b_card, 12, 28, 192);

        // Launcher buttons for Modes
        MakeButton(root, "XIAOZHI AI", 12, 226, 102, 48, C_PANEL, C_CYAN, C_CYAN, FONT_12,
                   OnLaunchAiVoice, this);
        MakeButton(root, "CHRONCHI", 126, 226, 102, 48, C_PANEL, C_AMBER, C_AMBER, FONT_12,
                   OnLaunchChronchi, this);
    }

    MakeBottomNav(root, PAGE_QUICK);
}
