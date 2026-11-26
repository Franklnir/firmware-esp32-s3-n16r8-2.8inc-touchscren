#include "nova_display_internal.h"

void NovaDisplay::BuildRegion() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, (language_idx_ == 0) ? "BAHASA & WILAYAH" : "LANGUAGE & REGION");

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240 - 100% Fit On 1 Screen - No Scroll) ===
        // Left Column (x=10, w=146): Language, Timezone, 12/24H
        lv_obj_t* lang_card = MakePanel(root, 10, 36, 146, 48, C_PANEL, C_BORDER, 8);
        MakeIcon(lang_card, ICON_REGION, 6, 12, C_CYAN, 22);
        MakeLabel(lang_card, (language_idx_ == 0) ? "Bahasa" : "Language", 32, 6, 62, 14, FONT_10,
                  C_MUTED);
        lv_obj_t* lang_lbl = MakeLabel(lang_card, (language_idx_ == 0) ? "Indonesia" : "English",
                                       32, 22, 62, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(lang_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(
            lang_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 96, 10, 44, 26, C_PANEL_2,
            C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CycleLanguage();
            },
            this);

        lv_obj_t* tz_card = MakePanel(root, 10, 88, 146, 48, C_PANEL, C_BORDER, 8);
        MakeIcon(tz_card, ICON_CHRONOS, 6, 12, C_AMBER, 22);
        MakeLabel(tz_card, (language_idx_ == 0) ? "Zona Waktu" : "Timezone", 32, 6, 62, 14, FONT_10,
                  C_MUTED);
        lv_obj_t* tz_lbl = MakeLabel(tz_card, GetTimezoneName(), 32, 22, 62, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(tz_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(
            tz_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 96, 10, 44, 26, C_PANEL_2, C_BORDER,
            C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CycleTimezone();
            },
            this);

        const char* fmt_txt =
            use_24hour_ ? ((language_idx_ == 0) ? "FORMAT 24 JAM [AKTIF]" : "24-HOUR FORMAT [ON]")
                        : ((language_idx_ == 0) ? "FORMAT 12 JAM [AKTIF]" : "12-HOUR FORMAT [ON]");
        MakeButton(
            root, fmt_txt, 10, 140, 146, 32, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Toggle24Hour();
            },
            this);

        MakeLabel(root, "Zona & Waktu Tersinkron", 12, 178, 142, 16, FONT_10, C_MUTED);

        // Right Column (x=164, w=146, h=166): Clock Preview & NTP Sync
        lv_obj_t* p_card = MakePanel(root, 164, 36, 146, 166, C_PANEL, C_BORDER, 10);
        MakeLabel(p_card, (language_idx_ == 0) ? "WAKTU REALTIME" : "REALTIME CLOCK", 8, 8, 130, 14,
                  FONT_10, C_CYAN, LV_TEXT_ALIGN_CENTER);
        region_time_preview_ =
            MakeLabel(p_card, "--:--:--", 6, 26, 134, 32, FONT_24, C_TEXT, LV_TEXT_ALIGN_CENTER);
        region_date_preview_ = MakeLabel(p_card, "Menghubungkan...", 6, 62, 134, 15, FONT_10,
                                         C_MUTED, LV_TEXT_ALIGN_CENTER);
        region_ntp_status_ = MakeLabel(p_card, "NTP: id.pool.ntp.org", 6, 80, 134, 14, FONT_10,
                                       C_CYAN, LV_TEXT_ALIGN_CENTER);

        MakeButton(
            p_card, (language_idx_ == 0) ? "SINKRONISASI NTP" : "SYNC NTP NOW", 8, 104, 130, 26,
            C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->SyncNtpNow();
            },
            this);

        MakeButton(
            p_card, (language_idx_ == 0) ? "ATUR WI-FI >" : "WIFI CONFIG >", 8, 134, 130, 24,
            C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->NavigateTo(PAGE_WIFI);
            },
            this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Card 1: Bahasa Antarmuka
        MakeLabel(root, (language_idx_ == 0) ? "BAHASA ANTARMUKA" : "INTERFACE LANGUAGE", 14, 40,
                  212, 14, FONT_10, C_MUTED);
        lv_obj_t* lang_card = MakePanel(root, 12, 54, 216, 40, C_PANEL, C_BORDER, 10);
        MakeIcon(lang_card, ICON_REGION, 8, 9, C_CYAN, 22);
        lv_obj_t* lang_lbl =
            MakeLabel(lang_card, GetLanguageName(), 36, 11, 116, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(lang_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(
            lang_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 154, 7, 54, 26, C_PANEL_2,
            C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CycleLanguage();
            },
            this);

        // Card 2: Zona Waktu
        MakeLabel(root, (language_idx_ == 0) ? "ZONA WAKTU LOKAL" : "LOCAL TIMEZONE", 14, 98, 212,
                  14, FONT_10, C_MUTED);
        lv_obj_t* tz_card = MakePanel(root, 12, 112, 216, 40, C_PANEL, C_BORDER, 10);
        MakeIcon(tz_card, ICON_CHRONOS, 8, 9, C_AMBER, 22);
        lv_obj_t* tz_lbl = MakeLabel(tz_card, GetTimezoneName(), 36, 11, 116, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(tz_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(
            tz_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 154, 7, 54, 26, C_PANEL_2, C_BORDER,
            C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CycleTimezone();
            },
            this);

        // Card 3: Format Jam
        MakeLabel(root, (language_idx_ == 0) ? "FORMAT WAKTU" : "TIME FORMAT", 14, 156, 212, 14,
                  FONT_10, C_MUTED);
        const char* fmt_txt =
            use_24hour_
                ? ((language_idx_ == 0) ? "FORMAT 24 JAM [AKTIF]" : "24-HOUR FORMAT [ACTIVE]")
                : ((language_idx_ == 0) ? "FORMAT 12 JAM AM/PM [AKTIF]"
                                        : "12-HOUR FORMAT AM/PM [ACTIVE]");
        MakeButton(
            root, fmt_txt, 12, 170, 216, 28, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Toggle24Hour();
            },
            this);

        // Card 4: Preview Jam & Sinkronisasi NTP
        lv_obj_t* p_card = MakePanel(root, 12, 202, 216, 78, C_PANEL, C_BORDER, 10);
        region_time_preview_ =
            MakeLabel(p_card, "--:--:--", 10, 3, 196, 22, FONT_20, C_CYAN, LV_TEXT_ALIGN_CENTER);
        region_date_preview_ = MakeLabel(p_card, "Menghubungkan Waktu...", 10, 24, 196, 15, FONT_10,
                                         C_TEXT, LV_TEXT_ALIGN_CENTER);
        region_ntp_status_ = MakeLabel(p_card, "NTP: id.pool.ntp.org", 10, 39, 196, 14, FONT_10,
                                       C_MUTED, LV_TEXT_ALIGN_CENTER);

        MakeButton(
            p_card, (language_idx_ == 0) ? "SINKRONISASI NTP" : "SYNC NTP NOW", 18, 55, 180, 18,
            C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->SyncNtpNow();
            },
            this);
    }

    MakeBottomNav(root, PAGE_REGION);
}

void NovaDisplay::ShowRebootModal() {
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, width_, height_);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);

    lv_obj_t* dialog = MakePanel(overlay, 0, 0, 216, 115, C_PANEL, C_RED, 14);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);

    MakeLabel(dialog, "Mulai ulang perangkat?", 14, 14, 188, 22, FONT_14, C_TEXT);
    MakeLabel(dialog, "Semua proses akan dihentikan dan sistem reboot.", 14, 38, 188, 28, FONT_10,
              C_MUTED);

    MakeButton(
        dialog, "BATAL", 12, 74, 90, 28, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
        [](lv_event_t* e2) {
            if (lv_event_get_code(e2) == LV_EVENT_CLICKED) {
                lv_obj_t* ov = static_cast<lv_obj_t*>(lv_event_get_user_data(e2));
                if (ov)
                    lv_obj_del(ov);
            }
        },
        overlay);

    MakeButton(dialog, "REBOOT", 114, 74, 90, 28, C_RED, C_RED, C_BG, FONT_12, [](lv_event_t* e2) {
        if (lv_event_get_code(e2) == LV_EVENT_CLICKED)
            esp_restart();
    });
}

void NovaDisplay::BuildSystem() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "INFORMASI SISTEM");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    char core_str[64];
    snprintf(core_str, sizeof(core_str), "Cores: %d | Silicon Rev: %d", chip_info.cores,
             chip_info.revision);

    uint32_t free_heap = esp_get_free_heap_size() / 1024;
    uint32_t min_heap = esp_get_minimum_free_heap_size() / 1024;
    char heap_str[64];
    snprintf(heap_str, sizeof(heap_str), "SRAM: 512 KB (Sisa: %lu KB)", (unsigned long)free_heap);

#if CONFIG_SPIRAM
    size_t psram_sz = esp_psram_get_size() / (1024 * 1024);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    char psram_str[64];
    snprintf(psram_str, sizeof(psram_str), "PSRAM: %u MB (Sisa: %lu KB)", (unsigned)psram_sz,
             (unsigned long)free_psram);
#else
    const char* psram_str = "PSRAM: N/A";
#endif

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    char ip_txt[48] = "Wi-Fi: Terputus";
    if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        char s_ip[20] = "";
        esp_ip4addr_ntoa(&ip_info.ip, s_ip, sizeof(s_ip));
        snprintf(ip_txt, sizeof(ip_txt), "IP: %s", s_ip);
    }

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_txt[48];
    snprintf(mac_txt, sizeof(mac_txt), "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    int64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    int up_h = (int)(uptime_sec / 3600);
    int up_m = (int)((uptime_sec % 3600) / 60);
    int up_s = (int)(uptime_sec % 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "Uptime: %02dh %02dm %02ds", up_h, up_m, up_s);

    esp_reset_reason_t rst_reason = esp_reset_reason();
    const char* rst_str = "Power-on";
    if (rst_reason == ESP_RST_SW)
        rst_str = "Software Reset";
    else if (rst_reason == ESP_RST_PANIC)
        rst_str = "Exception Panic";
    else if (rst_reason == ESP_RST_INT_WDT || rst_reason == ESP_RST_TASK_WDT)
        rst_str = "Watchdog";
    else if (rst_reason == ESP_RST_DEEPSLEEP)
        rst_str = "Deep Sleep";

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN LAYOUT (320x240 - 100% Fit On 1 Screen - No Scroll) ===
        // Left Column (x=8, w=148): Processor & Memory
        lv_obj_t* chipCard = MakePanel(root, 8, 38, 148, 78, C_PANEL, C_BORDER, 8);
        MakeIcon(chipCard, ICON_CHIP, 6, 8, C_CYAN, 20);
        MakeLabel(chipCard, "ESP32-S3 (v0.2)", 32, 6, 112, 18, FONT_12, C_TEXT);
        MakeLabel(chipCard, "Dual LX7 @ 240 MHz", 32, 22, 112, 14, FONT_10, C_MUTED);
        MakeLabel(chipCard, core_str, 8, 40, 132, 15, FONT_10, C_CYAN);
        MakeLabel(chipCard, "Wi-Fi 802.11bgn, BLE 5.0", 8, 56, 132, 15, FONT_10, C_MUTED);

        lv_obj_t* memCard = MakePanel(root, 8, 120, 148, 84, C_PANEL, C_BORDER, 8);
        MakeIcon(memCard, ICON_CHIP, 6, 8, C_VIOLET, 20);
        MakeLabel(memCard, "Memori & Flash", 32, 6, 112, 18, FONT_12, C_TEXT);
        MakeLabel(memCard, heap_str, 8, 26, 132, 14, FONT_10, C_TEXT);
        MakeLabel(memCard, psram_str, 8, 42, 132, 14, FONT_10, C_MINT);
        MakeLabel(memCard, "Flash: 16 MB External QSPI", 8, 58, 132, 14, FONT_10, C_CYAN);

        // Right Column (x=164, w=148): Hardware Board & Network + Reboot
        lv_obj_t* periCard = MakePanel(root, 164, 38, 148, 78, C_PANEL, C_BORDER, 8);
        MakeIcon(periCard, ICON_SETTINGS, 6, 8, C_AMBER, 20);
        MakeLabel(periCard, "Hardware Board", 32, 6, 112, 18, FONT_12, C_TEXT);
        MakeLabel(periCard, "2.8\" IPS 240x320 ILI9341V", 8, 24, 132, 14, FONT_10, C_TEXT);
        MakeLabel(periCard, "FT6336G | Codec ES8311", 8, 40, 132, 14, FONT_10, C_CYAN);
        MakeLabel(periCard, "Amp SC8002B | Bat TP4054", 8, 56, 132, 14, FONT_10, C_MUTED);

        lv_obj_t* netCard = MakePanel(root, 164, 120, 148, 84, C_PANEL, C_BORDER, 8);
        MakeIcon(netCard, ICON_WIFI, 6, 8, C_MINT, 20);
        MakeLabel(netCard, "Jaringan & Daya", 32, 6, 112, 18, FONT_12, C_TEXT);
        MakeLabel(netCard, ip_txt, 8, 26, 132, 14, FONT_10, C_MINT);
        MakeLabel(netCard, uptime_txt, 8, 42, 132, 14, FONT_10, C_CYAN);

        MakeButton(
            netCard, "REBOOT SISTEM", 8, 58, 132, 22, C_PANEL_2, C_RED, C_RED, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRebootModal();
            },
            this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 8, 36);
        lv_obj_set_size(list, 224, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        int cur_y = 4;

        // 1. Processor & SoC Card
        lv_obj_t* chipCard = MakePanel(list, 4, cur_y, 208, 80, C_PANEL, C_BORDER, 10);
        MakeIcon(chipCard, ICON_CHIP, 8, 8, C_CYAN, 24);
        MakeLabel(chipCard, "ESP32-S3 (v0.2)", 38, 6, 160, 18, FONT_12, C_TEXT);
        MakeLabel(chipCard, "Xtensa Dual-Core LX7 @ 240 MHz", 38, 24, 160, 14, FONT_10, C_MUTED);
        MakeLabel(chipCard, core_str, 10, 44, 188, 15, FONT_10, C_CYAN);
        MakeLabel(chipCard, "Fitur: Wi-Fi 802.11bgn, BLE 5.0", 10, 60, 188, 15, FONT_10, C_MUTED);
        cur_y += 86;

        // 2. Memory & PSRAM Card
        lv_obj_t* memCard = MakePanel(list, 4, cur_y, 208, 96, C_PANEL, C_BORDER, 10);
        MakeIcon(memCard, ICON_CHIP, 8, 8, C_VIOLET, 24);
        MakeLabel(memCard, "Memori & Heap RAM", 38, 6, 160, 18, FONT_12, C_TEXT);
        MakeLabel(memCard, heap_str, 10, 30, 188, 15, FONT_10, C_TEXT);
        char min_heap_str[64];
        snprintf(min_heap_str, sizeof(min_heap_str), "Min Free Heap: %lu KB",
                 (unsigned long)min_heap);
        MakeLabel(memCard, min_heap_str, 10, 46, 188, 15, FONT_10, C_MUTED);
        MakeLabel(memCard, psram_str, 10, 62, 188, 15, FONT_10, C_MINT);
        MakeLabel(memCard, "SPI Flash: 16 MB External Quad-SPI", 10, 78, 188, 15, FONT_10, C_CYAN);
        cur_y += 102;

        // 3. Display & Peripherals Card
        lv_obj_t* periCard = MakePanel(list, 4, cur_y, 208, 114, C_PANEL, C_BORDER, 10);
        MakeIcon(periCard, ICON_SETTINGS, 8, 8, C_AMBER, 24);
        MakeLabel(periCard, "IC & Hardware Board", 38, 6, 160, 18, FONT_12, C_TEXT);
        MakeLabel(periCard, "Layar: 2.8\" IPS TFT 240x320 ILI9341V", 10, 28, 188, 15, FONT_10,
                  C_TEXT);
        MakeLabel(periCard, "Touchscreen: FT6336G Capacitive (0x38)", 10, 44, 188, 15, FONT_10,
                  C_TEXT);
        MakeLabel(periCard, "Audio Codec: ES8311 (I2S + I2C 0x18)", 10, 60, 188, 15, FONT_10,
                  C_CYAN);
        MakeLabel(periCard, "Audio Amplifier: SC8002B Class-AB (IO1)", 10, 76, 188, 15, FONT_10,
                  C_MUTED);
        MakeLabel(periCard, "Power/Bat: TP4054 Charger | LED: WS2812", 10, 92, 188, 15, FONT_10,
                  C_MINT);
        cur_y += 120;

        // 4. Network & System Stats
        lv_obj_t* netCard = MakePanel(list, 4, cur_y, 208, 98, C_PANEL, C_BORDER, 10);
        MakeIcon(netCard, ICON_WIFI, 8, 8, C_MINT, 24);
        MakeLabel(netCard, "Jaringan & Uptime", 38, 6, 160, 18, FONT_12, C_TEXT);
        MakeLabel(netCard, ip_txt, 10, 28, 188, 15, FONT_10, C_MINT);
        MakeLabel(netCard, mac_txt, 10, 44, 188, 15, FONT_10, C_MUTED);
        MakeLabel(netCard, uptime_txt, 10, 60, 188, 15, FONT_10, C_CYAN);
        char rst_txt[48];
        snprintf(rst_txt, sizeof(rst_txt), "Reset Terakhir: %s", rst_str);
        MakeLabel(netCard, rst_txt, 10, 76, 188, 15, FONT_10, C_MUTED);
        cur_y += 104;

        // 5. Restart Device Button with centered modal
        MakeLabel(list, "KONTROL PERANGKAT", 8, cur_y + 2, 200, 16, FONT_10, C_RED);
        MakeButton(
            list, "MULAI ULANG PERANGKAT", 4, cur_y + 20, 208, 32, C_PANEL, C_RED, C_RED, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowRebootModal();
            },
            this);
    }

    MakeBottomNav(root, PAGE_SYSTEM);
}

void NovaDisplay::TimerAction() {
    if (timer_running_) {
        uint32_t now = esp_timer_get_time() / 1000;
        int32_t rem = (int32_t)(timer_end_at_ - now);
        timer_remaining_seconds_ = (rem > 0) ? (uint16_t)((rem + 999) / 1000) : 0;
        timer_running_ = false;
    } else if (timer_remaining_seconds_ > 0) {
        timer_end_at_ = (esp_timer_get_time() / 1000) + (uint32_t)timer_remaining_seconds_ * 1000UL;
        timer_running_ = true;
    }
    BuildCurrentPage();
}

void NovaDisplay::TimerReset() {
    timer_running_ = false;
    timer_remaining_seconds_ = 25 * 60;
    timer_end_at_ = 0;
    BuildCurrentPage();
}

void NovaDisplay::BuildChronos() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "CHRONOS");

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

    uint16_t shown = timer_remaining_seconds_;
    if (timer_running_) {
        uint32_t now = esp_timer_get_time() / 1000;
        int32_t delta = (int32_t)(timer_end_at_ - now);
        shown = (delta > 0) ? (uint16_t)((delta + 999) / 1000) : 0;
    }
    char timer_txt[16];
    snprintf(timer_txt, sizeof(timer_txt), "%02u:%02u", shown / 60, shown % 60);
    const char* action_txt = timer_running_ ? "JEDA" : (shown == 0 ? "SELESAI" : "MULAI");

    if (width_ > 240) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left Column (w=144): Clock card + Launch Chronchi button
        lv_obj_t* clk_card = MakePanel(root, 10, 38, 144, 92, C_PANEL, C_BORDER, 10);
        MakeLabel(clk_card, "WAKTU LOKAL", 10, 6, 124, 14, FONT_10, C_CYAN);
        clock_label_ = MakeCenteredLabel(clk_card, time_text, 0, 20, 144, 38, FONT_28, C_TEXT);
        date_label_ = MakeCenteredLabel(clk_card, date_text, 0, 62, 144, 16, FONT_10, C_MUTED);

        lv_obj_t* btn_nav = MakeButton(root, "🧭 BUKA NAVIGASI", 10, 136, 144, 66, lv_color_hex(0x132238),
                   lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), FONT_12, OnNavClicked, this);
        lv_obj_set_user_data(btn_nav, (void*)(intptr_t)PAGE_NAV);

        // Right Column (w=150): Pomodoro Timer card
        lv_obj_t* tcard = MakePanel(root, 160, 38, 150, 164, C_PANEL, C_BORDER, 10);
        MakeIcon(tcard, ICON_CHRONOS, 8, 8, C_AMBER, 20);
        MakeLabel(tcard, "TIMER POMODORO", 32, 10, 110, 16, FONT_10, C_MUTED);
        timer_label_ = MakeCenteredLabel(tcard, timer_txt, 0, 32, 150, 36, FONT_28, C_CYAN);

        MakeButton(
            tcard, action_txt, 10, 74, 130, 28, C_CYAN, C_CYAN, C_BG, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->TimerAction();
            },
            this);

        MakeButton(
            tcard, "ULANGI", 10, 108, 130, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->TimerReset();
            },
            this);

        MakeCenteredLabel(tcard, timer_running_ ? "Sesi berjalan" : "Fokus 25 menit", 0, 140, 150,
                          14, FONT_10, C_MUTED);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "JAM LOKAL & WAKTU", 14, 40, 212, 14, FONT_10, C_CYAN);
        clock_label_ = MakeCenteredLabel(root, time_text, 8, 54, 224, 36, FONT_36, C_TEXT);
        date_label_ = MakeCenteredLabel(root, date_text, 8, 92, 224, 16, FONT_12, C_MUTED);

        lv_obj_t* tcard = MakePanel(root, 12, 114, 216, 104, C_PANEL, C_BORDER, 12);
        MakeIcon(tcard, ICON_CHRONOS, 10, 10, C_AMBER, 22);
        MakeLabel(tcard, "TIMER FOKUS POMODORO", 38, 12, 164, 16, FONT_10, C_MUTED);

        timer_label_ = MakeLabel(tcard, timer_txt, 12, 34, 111, 36, FONT_28, C_CYAN);

        MakeButton(
            tcard, action_txt, 130, 34, 74, 28, C_CYAN, C_CYAN, C_BG, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->TimerAction();
            },
            this);

        MakeButton(
            tcard, "ULANGI", 130, 66, 74, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->TimerReset();
            },
            this);

        MakeLabel(tcard, timer_running_ ? "Sesi fokus berjalan" : "Sesi fokus 25 menit", 12, 76,
                  110, 16, FONT_10, C_MUTED);

        // Tombol pintasan langsung ke Navigasi Google Maps
        lv_obj_t* btn_nav = MakeButton(root, "🧭 BUKA NAVIGASI GOOGLE MAPS", 12, 226, 216, 40, lv_color_hex(0x132238),
                   lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), FONT_12, OnNavClicked, this);
        lv_obj_set_user_data(btn_nav, (void*)(intptr_t)PAGE_NAV);
    }

    MakeBottomNav(root, PAGE_CHRONOS);
}

void NovaDisplay::BuildPinLab() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "PIN LAB & DIAGNOSTIK");

    int lvl = 0, mv = 0, raw = 0;
    bool chg = false;
    if (callbacks_.get_battery)
        callbacks_.get_battery(lvl, chg, mv, raw);
    int boot_val = gpio_get_level(GPIO_NUM_0);

    struct PinGroup {
        const char* title;
        const char* pins;
        lv_color_t color;
    };
    static const PinGroup pin_groups[] = {
        {"Audio ES8311 & PA", "MCLK:4 BCLK:5 DIN:6 WS:7 DOUT:8 PA:1", C_CYAN},
        {"LCD ILI9341V SPI", "CS:10 MOSI:11 SCK:12 MISO:13 DC:46 BL:45", C_AMBER},
        {"Touch FT6336G I2C", "SCL:15 SDA:16 INT:17 RST:18", C_MINT},
        {"MicroSD SDIO 4-Bit", "CLK:38 D0:39 CMD:40 D1:41 D2:48 D3:47", C_VIOLET},
        {"Sistem & Tombol", "BOOT:0 BATT:9 WS2812:42 UART:43,44", C_CYAN},
        {"Header Ekspansi IO", "GPIO 2, 3, 14, 21 (Available)", C_TEXT}};
    const size_t num_groups = sizeof(pin_groups) / sizeof(pin_groups[0]);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN PIN LAB (100% Fit On 1 Screen - No Scroll) ===
        // Left Column (x=8, w=148, y=38, h=166): Hardware Controls & Diagnostics Panel
        lv_obj_t* card = MakePanel(root, 8, 38, 148, 166, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_CHIP, 6, 8, C_CYAN, 20);
        MakeLabel(card, "Uji Hardware", 30, 6, 112, 16, FONT_12, C_TEXT);

        MakeLabel(card, "Backlight PWM (IO45):", 6, 26, 134, 14, FONT_10, C_MUTED);
        struct BrightPreset {
            const char* label;
            uint8_t val;
        };
        const BrightPreset bpresets[] = {{"25%", 64}, {"50%", 128}, {"75%", 192}, {"MAX", 255}};
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* bbtn = MakeButton(
                card, bpresets[i].label, 6 + i * 34, 42, 31, 22, C_PANEL_2, C_BORDER, C_TEXT,
                FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    uint8_t val =
                        (uint8_t)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (self)
                        self->ApplyBrightness(val);
                },
                this);
            lv_obj_set_user_data(bbtn, (void*)(intptr_t)bpresets[i].val);
        }

        MakeButton(
            card, "TES KLIK AUDIO (IO1)", 6, 70, 134, 26, C_PANEL_2, C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.trigger_click)
                    self->callbacks_.trigger_click();
            },
            this);

        char bat_sense_str[48];
        snprintf(bat_sense_str, sizeof(bat_sense_str), "Sensor IO9: %d mV (ADC:%d)", mv, raw);
        lv_obj_t* bat_lbl = MakeLabel(card, bat_sense_str, 6, 102, 134, 14, FONT_10, C_MINT);
        lv_label_set_long_mode(bat_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        char boot_str[48];
        snprintf(boot_str, sizeof(boot_str), "Tombol BOOT: %s",
                 (boot_val == 0) ? "TEKAN (LOW)" : "LEPAS (HIGH)");
        lv_obj_t* boot_lbl = MakeLabel(card, boot_str, 6, 118, 134, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(boot_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(card, "Board LCDWiki ES3C28P", 6, 140, 134, 14, FONT_10, C_CYAN);

        // Right Column (x=160, y=38, w=152, h=166): Dedicated Scrollable Pinout Map
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 160, 38);
        lv_obj_set_size(list, 152, 166);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 50, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        for (size_t i = 0; i < num_groups; ++i) {
            lv_obj_t* prow = MakePanel(list, 0, (int)i * 40, 144, 36, C_PANEL, C_BORDER, 6);
            MakeLabel(prow, pin_groups[i].title, 6, 3, 132, 15, FONT_10, pin_groups[i].color);
            lv_obj_t* pins_lbl =
                MakeLabel(prow, pin_groups[i].pins, 6, 18, 132, 13, FONT_10, C_MUTED);
            lv_label_set_long_mode(pins_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 8, 36);
        lv_obj_set_size(list, 224, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 80, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        int cur_y = 4;

        lv_obj_t* testCard = MakePanel(list, 4, cur_y, 208, 122, C_PANEL, C_BORDER, 10);
        MakeIcon(testCard, ICON_CHIP, 6, 6, C_CYAN, 20);
        MakeLabel(testCard, "Uji Live Hardware", 32, 5, 164, 16, FONT_12, C_TEXT);

        MakeLabel(testCard, "Kecerahan Backlight PWM (IO45):", 8, 22, 192, 14, FONT_10, C_MUTED);
        struct BrightPreset {
            const char* label;
            uint8_t val;
        };
        const BrightPreset bpresets[] = {{"25%", 64}, {"50%", 128}, {"75%", 192}, {"100%", 255}};
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* bbtn = MakeButton(
                testCard, bpresets[i].label, 8 + i * 48, 36, 44, 22, C_PANEL_2, C_BORDER, C_TEXT,
                FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    uint8_t val =
                        (uint8_t)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (self)
                        self->ApplyBrightness(val);
                },
                this);
            lv_obj_set_user_data(bbtn, (void*)(intptr_t)bpresets[i].val);
        }

        MakeButton(
            testCard, "TES KLIK AUDIO PA (IO1)", 8, 62, 192, 24, C_PANEL_2, C_BORDER, C_CYAN,
            FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.trigger_click)
                    self->callbacks_.trigger_click();
            },
            this);

        char bat_sense_str[64];
        snprintf(bat_sense_str, sizeof(bat_sense_str), "Sensor Baterai (IO9): %d mV (ADC: %d)", mv,
                 raw);
        lv_obj_t* bat_lbl = MakeLabel(testCard, bat_sense_str, 8, 89, 192, 14, FONT_10, C_MINT);
        lv_label_set_long_mode(bat_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        char boot_str[64];
        snprintf(boot_str, sizeof(boot_str), "Tombol BOOT (IO0): %s",
                 (boot_val == 0) ? "DITEKAN (LOW)" : "LEPAS (HIGH)");
        lv_obj_t* boot_lbl = MakeLabel(testCard, boot_str, 8, 104, 192, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(boot_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        cur_y += 128;

        MakeLabel(list, "MAP PINOUT GPIO HARDWARE", 8, cur_y, 200, 16, FONT_10, C_CYAN);
        cur_y += 18;

        for (size_t i = 0; i < num_groups; ++i) {
            lv_obj_t* prow = MakePanel(list, 4, cur_y, 208, 38, C_PANEL, C_BORDER, 8);
            MakeLabel(prow, pin_groups[i].title, 8, 3, 192, 15, FONT_10, pin_groups[i].color);
            lv_obj_t* pins_lbl =
                MakeLabel(prow, pin_groups[i].pins, 8, 19, 192, 14, FONT_10, C_MUTED);
            lv_label_set_long_mode(pins_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            cur_y += 42;
        }
    }

    MakeBottomNav(root, PAGE_PINLAB);
}

void NovaDisplay::BuildPlaylist() { BuildRadio(); }
