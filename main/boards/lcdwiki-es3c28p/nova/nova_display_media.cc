#include "nova_display_internal.h"

void NovaDisplay::BuildCurrentPage() {
    switch (current_page_) {
        case PAGE_SPLASH:
            BuildSplash();
            break;
        case PAGE_HOME:
            BuildHome();
            break;
        case PAGE_APPS:
            BuildApps();
            break;
        case PAGE_QUICK:
            BuildQuick();
            break;
        case PAGE_WIFI:
            BuildWifi();
            break;
        case PAGE_AI:
            BuildAi();
            break;
        case PAGE_SETTINGS:
            BuildSettings();
            break;
        case PAGE_DISPLAY:
            BuildDisplay();
            break;
        case PAGE_AUDIO:
            BuildAudio();
            break;
        case PAGE_FILES:
            BuildFiles();
            break;
        case PAGE_BATTERY:
            BuildBattery();
            break;
        case PAGE_REGION:
            BuildRegion();
            break;
        case PAGE_SYSTEM:
            BuildSystem();
            break;
        case PAGE_CHRONOS:
            BuildChronos();
            break;
        case PAGE_PINLAB:
            BuildPinLab();
            break;
        case PAGE_PLAYLIST:
            BuildRadio();
            break;
        case PAGE_CALCULATOR:
            BuildCalculator();
            break;
        case PAGE_FLAPPY:
            BuildFlappy();
            break;
        case PAGE_RADIO:
            BuildRadio();
            break;
        case PAGE_2048:
            Build2048();
            break;
        case PAGE_SNAKE:
            BuildSnake();
            break;
        case PAGE_BRICK:
            BuildBrick();
            break;
        case PAGE_MINESWEEPER:
            BuildMinesweeper();
            break;
        case PAGE_RACER:
            BuildRacer();
            break;
        case PAGE_PIANO:
            BuildPiano();
            break;
        case PAGE_SKETCH:
            BuildSketch();
            break;
        case PAGE_MONITOR:
            BuildMonitor();
            break;
        case PAGE_NAV:
            BuildNav();
            break;
        case PAGE_BT_MUSIC:
            BuildBtMusic();
            break;
        case PAGE_NOTIF:
            BuildNotif();
            break;
        case PAGE_WEATHER:
            BuildWeather();
            break;
        case PAGE_PHONE:
            BuildPhone();
            break;
    }
}

void NovaDisplay::UpdateLiveLabels() {
    time_t now_sec = time(nullptr);
    struct tm info;
    localtime_r(&now_sec, &info);

    static const char* kDaysId[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    static const char* kMonthsId[] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
                                      "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};
    static const char* kDaysEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonthsEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char time_text[24] = "--:--", date_text[48] = "WAKTU BELUM TERSINKRON";
    char time_preview[32] = "--:--:--";
    bool valid_time = (info.tm_year > (2020 - 1900));

    if (valid_time) {
        if (use_24hour_) {
            snprintf(time_text, sizeof(time_text), "%02d:%02d", info.tm_hour, info.tm_min);
            snprintf(time_preview, sizeof(time_preview), "%02d:%02d:%02d", info.tm_hour,
                     info.tm_min, info.tm_sec);
        } else {
            int h = info.tm_hour % 12;
            if (h == 0)
                h = 12;
            snprintf(time_text, sizeof(time_text), "%d:%02d %s", h, info.tm_min,
                     info.tm_hour >= 12 ? "PM" : "AM");
            snprintf(time_preview, sizeof(time_preview), "%d:%02d:%02d %s", h, info.tm_min,
                     info.tm_sec, info.tm_hour >= 12 ? "PM" : "AM");
        }

        if (language_idx_ == 0) {
            snprintf(date_text, sizeof(date_text), "%s, %02d %s %04d", kDaysId[info.tm_wday % 7],
                     info.tm_mday, kMonthsId[info.tm_mon % 12], info.tm_year + 1900);
        } else {
            snprintf(date_text, sizeof(date_text), "%s, %s %02d, %04d", kDaysEn[info.tm_wday % 7],
                     kMonthsEn[info.tm_mon % 12], info.tm_mday, info.tm_year + 1900);
        }
    }

    if (clock_label_)
        lv_label_set_text(clock_label_, time_text);
    if (date_label_)
        lv_label_set_text(date_label_, date_text);
    if (region_time_preview_)
        lv_label_set_text(region_time_preview_, time_preview);
    if (region_date_preview_)
        lv_label_set_text(region_date_preview_, date_text);
    if (region_ntp_status_) {
        lv_label_set_text(region_ntp_status_,
                          valid_time ? "NTP: Tersinkron (Akurat)" : "NTP: Menunggu Koneksi Wi-Fi");
        lv_obj_set_style_text_color(region_ntp_status_, valid_time ? C_MINT : C_AMBER,
                                    LV_PART_MAIN);
    }

    if (timer_label_) {
        uint16_t shown = timer_remaining_seconds_;
        if (timer_running_) {
            uint32_t now = esp_timer_get_time() / 1000;
            int32_t delta = (int32_t)(timer_end_at_ - now);
            if (delta <= 0) {
                timer_running_ = false;
                timer_remaining_seconds_ = 0;
                shown = 0;
            } else {
                shown = (uint16_t)((delta + 999) / 1000);
            }
        }
        char timer_txt[16];
        snprintf(timer_txt, sizeof(timer_txt), "%02u:%02u", shown / 60, shown % 60);
        lv_label_set_text(timer_label_, timer_txt);
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected =
        (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    lv_color_t icon_clr = lv_color_hex(header_icon_color_);

    bool ble_conn = false;
    std::string bname, bmac;
    if (callbacks_.get_ble_status) {
        callbacks_.get_ble_status(ble_conn, bname, bmac);
    }

    static uint32_t status_blink_tick = 0;
    status_blink_tick++;

    if (top_bluetooth_icon_) {
        lv_label_set_text(top_bluetooth_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
        if (ble_conn) {
            lv_obj_set_style_text_color(top_bluetooth_icon_, lv_color_hex(0x22C55E), LV_PART_MAIN);
            lv_obj_set_style_text_opa(top_bluetooth_icon_, LV_OPA_COVER, LV_PART_MAIN);
        } else {
            // Kedap-kedip ketika Bluetooth tidak terhubung (berganti tiap 500ms seperti ikon Wi-Fi)
            lv_obj_set_style_text_color(top_bluetooth_icon_, icon_clr, LV_PART_MAIN);
            lv_obj_set_style_text_opa(top_bluetooth_icon_,
                                      (status_blink_tick % 2 == 0) ? LV_OPA_COVER : LV_OPA_0,
                                      LV_PART_MAIN);
        }
    }

    if (bt_modal_) {
        if (ble_conn != bt_was_connected_) {
            bt_was_connected_ = ble_conn;
            UpdateBluetoothModal(ble_conn);
        }
    }

    if (top_notif_btn_ && top_notif_icon_ && top_notif_label_) {
        int n_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        if (n_cnt > 0) {
            char n_buf[16];
            snprintf(n_buf, sizeof(n_buf), "%d", n_cnt);
            lv_label_set_text(top_notif_label_, n_buf);
            lv_obj_set_style_text_color(top_notif_icon_, icon_clr, LV_PART_MAIN);
            lv_obj_set_style_text_color(top_notif_label_, icon_clr, LV_PART_MAIN);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(top_notif_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    if (top_mute_icon_) {
        lv_label_set_text(top_mute_icon_,
                          muted_ ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
        lv_obj_set_style_text_color(top_mute_icon_, icon_clr, LV_PART_MAIN);
    }

    if (top_wifi_icon_) {
        if (wifi_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                int8_t rssi = ap_info.rssi;
                if (rssi >= -65) {
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI);
                } else if (rssi >= -78) {
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI_2_BAR);
                } else {
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI_1_BAR);
                }
            } else {
                lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI);
            }
        } else {
            lv_label_set_text(top_wifi_icon_, (status_blink_tick % 2 == 0)
                                                  ? MATERIAL_SYMBOLS_REFRESH
                                                  : MATERIAL_SYMBOLS_WIFI_OFF);
        }
        lv_obj_set_style_text_color(top_wifi_icon_, icon_clr, LV_PART_MAIN);
    }

    if (battery_percent_label_ || battery_volt_label_ || top_battery_icon_ || top_battery_label_) {
        int level = 100, mv = 0, raw = 0;
        bool chg = false;
        if (callbacks_.get_battery)
            callbacks_.get_battery(level, chg, mv, raw);

        if (top_battery_icon_) {
            lv_label_set_text(top_battery_icon_, chg ? MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT
                                                     : MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
            lv_obj_set_style_text_color(top_battery_icon_, icon_clr, LV_PART_MAIN);
            if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
                lv_obj_add_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (top_battery_label_) {
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", level);
            lv_label_set_text(top_battery_label_, pct);
            lv_obj_set_style_text_color(top_battery_label_, icon_clr, LV_PART_MAIN);
            if (bat_style_ == BAT_STYLE_ICON_ONLY) {
                lv_obj_add_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (battery_percent_label_) {
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", level);
            lv_label_set_text(battery_percent_label_, pct);
        }
        if (battery_volt_label_) {
            char v[48];
            float v_float = mv / 1000.0f;
            snprintf(v, sizeof(v),
                     (width_ > 240) ? "Tegangan: %d mV (%.2fV)" : "Tegangan: %d mV (%.2f V)", mv,
                     v_float);
            lv_label_set_text(battery_volt_label_, v);
        }
        if (battery_raw_label_) {
            char r[48];
            snprintf(r, sizeof(r),
                     (width_ > 240) ? "ADC Mentah: %d" : "Sensor ADC Mentah: %d (Divider 1:1)",
                     raw);
            lv_label_set_text(battery_raw_label_, r);
        }
    }

    if (wifi_status_label_) {
        auto& mgr = WifiManager::GetInstance();
        if (wifi_connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid =
                (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + "  |  " + ip_str;
            lv_label_set_text(wifi_status_label_, details.c_str());
            lv_obj_set_style_text_color(wifi_status_label_, C_CYAN, LV_PART_MAIN);
        } else if (mgr.IsInitialized() && mgr.IsConfigMode()) {
            std::string ap_name = mgr.GetApSsid();
            if (ap_name.empty())
                ap_name = "Xiaozhi AP";
            std::string details = ap_name + "  |  192.168.4.1";
            lv_label_set_text(wifi_status_label_, details.c_str());
            lv_obj_set_style_text_color(wifi_status_label_, C_CYAN, LV_PART_MAIN);
        } else {
            lv_label_set_text(wifi_status_label_, "Menghubungkan kembali...");
            lv_obj_set_style_text_color(wifi_status_label_, lv_color_hex(0xF59E0B), LV_PART_MAIN);
        }
    }
}

void NovaDisplay::Update() { UpdateLiveLabels(); }

void NovaDisplay::ShowFileModal(const std::string& filename, size_t size) {
    if (file_modal_) {
        lv_obj_del(file_modal_);
        file_modal_ = nullptr;
    }

    std::string ext = "";
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = filename.substr(dot_pos);
        for (auto& c : ext)
            c = tolower(c);
    }
    bool is_audio = (ext == ".wav" || ext == ".mp3" || ext == ".pcm" || ext == ".m4a");
    bool is_image = (ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".png");
    bool is_text = (!is_audio && !is_image);

    std::string clean_title = filename;
    if (clean_title.find("music/") == 0) {
        clean_title = clean_title.substr(6);
    } else if (clean_title.find("/sdcard/music/") == 0) {
        clean_title = clean_title.substr(14);
    } else if (clean_title.find("/sdcard/") == 0) {
        clean_title = clean_title.substr(8);
    }

    if (is_audio) {
        ShowAudioPlayerModal(clean_title, filename, false, size);
        return;
    }

    lv_obj_t* root = lv_layer_top();
    file_modal_ = lv_obj_create(root);
    lv_obj_set_size(file_modal_, width_, height_);
    lv_obj_clear_flag(file_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(file_modal_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(file_modal_, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(file_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(file_modal_, 0, LV_PART_MAIN);

    // === NON-AUDIO (TEXT / IMAGE) FILE MODAL ===
    int card_w = (width_ > 240) ? 280 : 216;
    int card_h = (height_ < 260) ? 222 : 244;
    lv_obj_t* card = MakePanel(file_modal_, 0, 0, card_w, card_h, lv_color_hex(0x18181B),
                               is_image ? C_AMBER : C_CYAN, 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    const char* h_icn_str = is_image ? MATERIAL_SYMBOLS_IMAGE : MATERIAL_SYMBOLS_EDIT_SQUARE;
    lv_color_t h_col = is_image ? C_AMBER : C_CYAN;

    lv_obj_t* h_icon = lv_label_create(card);
    lv_obj_set_style_text_font(h_icon, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(h_icon, h_icn_str);
    lv_obj_set_style_text_color(h_icon, h_col, LV_PART_MAIN);
    lv_obj_set_pos(h_icon, 10, 8);

    lv_obj_t* h_title = lv_label_create(card);
    lv_label_set_text(h_title, clean_title.c_str());
    lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(h_title, 34, 6);
    lv_obj_set_width(h_title, card_w - 44);
    lv_label_set_long_mode(h_title, LV_LABEL_LONG_DOT);

    char sz_str[48];
    if (size >= 1024 * 1024) {
        snprintf(sz_str, sizeof(sz_str), "Ukuran: %.2f MB", (double)size / (1024.0 * 1024.0));
    } else if (size >= 1024) {
        snprintf(sz_str, sizeof(sz_str), "Ukuran: %.1f KB (%u B)", (double)size / 1024.0,
                 (unsigned)size);
    } else {
        snprintf(sz_str, sizeof(sz_str), "Ukuran: %u Bytes", (unsigned)size);
    }
    MakeLabel(card, sz_str, 34, 22, card_w - 44, 14, FONT_10, C_MUTED);

    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_pos(sep, 8, 38);
    lv_obj_set_size(sep, card_w - 16, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x27272A), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);

    int box_h = card_h - 78;
    if (is_text) {
        std::string full_text_path =
            (filename.find("/sdcard/") == 0) ? filename : ("/sdcard/" + filename);
        std::string content = "";
        if (callbacks_.read_text_file) {
            content = callbacks_.read_text_file(full_text_path);
        }
        if (content.empty()) {
            content = "(Berkas teks kosong)";
        }

        lv_obj_t* text_box = lv_obj_create(card);
        lv_obj_set_pos(text_box, 8, 44);
        lv_obj_set_size(text_box, card_w - 16, box_h);
        lv_obj_set_style_bg_color(text_box, lv_color_hex(0x09090B), LV_PART_MAIN);
        lv_obj_set_style_border_color(text_box, lv_color_hex(0x27272A), LV_PART_MAIN);
        lv_obj_set_style_border_width(text_box, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(text_box, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(text_box, 6, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(text_box, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(text_box, LV_DIR_VER);

        lv_obj_t* txt_lbl = lv_label_create(text_box);
        lv_label_set_text(txt_lbl, content.c_str());
        lv_obj_set_style_text_font(txt_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(txt_lbl, lv_color_hex(0xE4E4E7), LV_PART_MAIN);
        lv_obj_set_width(txt_lbl, card_w - 32);
        lv_label_set_long_mode(txt_lbl, LV_LABEL_LONG_WRAP);
    } else {
        lv_obj_t* info_box = MakePanel(card, 8, 44, card_w - 16, box_h, lv_color_hex(0x111827),
                                       lv_color_hex(0x1F2937), 8);
        MakeIcon(info_box, ICON_FILES, (card_w - 16 - 32) / 2, (box_h < 150) ? 10 : 16, C_AMBER,
                 32);

        MakeLabel(info_box, "Berkas Gambar / Biner", 6, (box_h < 150) ? 46 : 60, card_w - 28, 16,
                  FONT_12, C_AMBER);
        MakeLabel(info_box,
                  "Rekomendasi Format:\nBMP 240x320 24-bit / 16-bit\nAtau RAW RGB565 Image", 10,
                  (box_h < 150) ? 66 : 82, card_w - 36, 42, FONT_10, C_MUTED);
    }

    MakeButton(
        card, "TUTUP", 8, card_h - 32, card_w - 16, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->file_modal_) {
                if (self->callbacks_.trigger_click)
                    self->callbacks_.trigger_click();
                lv_obj_del(self->file_modal_);
                self->file_modal_ = nullptr;
            }
        },
        this);
}

void NovaDisplay::ShowOnlinePlayerModal(const std::string& title, const std::string& video_id,
                                        const std::string& duration) {
    ShowAudioPlayerModal(title, video_id, true, 0, duration);
}

void NovaDisplay::ShowAudioPlayerModal(const std::string& title,
                                       const std::string& source_id_or_path, bool is_online,
                                       size_t file_size, const std::string& duration) {
    if (file_modal_) {
        lv_obj_del(file_modal_);
        file_modal_ = nullptr;
    }
    if (online_modal_) {
        lv_obj_del(online_modal_);
        online_modal_ = nullptr;
    }

    lv_obj_t* root = lv_layer_top();
    lv_obj_t* modal = lv_obj_create(root);
    if (is_online)
        online_modal_ = modal;
    else
        file_modal_ = modal;

    lv_obj_set_size(modal, width_, height_);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modal, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(modal, 0, LV_PART_MAIN);

    int card_w = (width_ > 240) ? 280 : 216;
    int card_h = (height_ < 260) ? 222 : 264;
    lv_obj_t* card = MakePanel(modal, 0, 0, card_w, card_h, lv_color_hex(0x121212), C_CYAN, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    // Header: Audio Headphones Icon + Badge
    lv_obj_t* h_icon = lv_label_create(card);
    lv_obj_set_style_text_font(h_icon, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(h_icon, MATERIAL_SYMBOLS_HEADPHONES);
    lv_obj_set_style_text_color(h_icon, C_CYAN, LV_PART_MAIN);
    lv_obj_set_pos(h_icon, 10, (height_ < 260 ? 5 : 8));

    MakeLabel(card, is_online ? "PEMUTAR AUDIO ONLINE" : "PEMUTAR MUSIK", 34,
              (height_ < 260 ? 4 : 6), card_w - 44, 14, FONT_10, C_CYAN);

    lv_obj_t* h_title = lv_label_create(card);
    lv_label_set_text(h_title, title.c_str());
    lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_pos(h_title, 34, (height_ < 260 ? 17 : 20));
    lv_obj_set_width(h_title, card_w - 44);
    lv_label_set_long_mode(h_title, LV_LABEL_LONG_DOT);

    char sub_str[64];
    if (is_online) {
        Settings wifi_settings("wifi", false);
        std::string s_domain = wifi_settings.GetString("server_domain");
        if (s_domain.empty()) {
            Settings sys_settings("system", false);
            s_domain = sys_settings.GetString("server_domain");
        }
        if (s_domain.empty()) {
            s_domain = "xiaozhiscig.biz.id";
        }
        if (!s_domain.empty()) {
            snprintf(sub_str, sizeof(sub_str), "%s • ES8311 DAC", s_domain.c_str());
        } else {
            snprintf(sub_str, sizeof(sub_str), "Online Stream • ES8311 DAC");
        }
    } else {
        if (file_size >= 1024 * 1024) {
            snprintf(sub_str, sizeof(sub_str), "SD Card • %.2f MB • WAV",
                     (double)file_size / (1024.0 * 1024.0));
        } else if (file_size >= 1024) {
            snprintf(sub_str, sizeof(sub_str), "SD Card • %u KB • WAV",
                     (unsigned)(file_size / 1024));
        } else {
            snprintf(sub_str, sizeof(sub_str), "SD Card • %u B • WAV", (unsigned)file_size);
        }
    }
    MakeLabel(card, sub_str, 10, (height_ < 260 ? 31 : 38), card_w - 20, 14, FONT_10,
              lv_color_hex(0x71717A));

    // Separator line
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_pos(sep, 8, (height_ < 260 ? 44 : 52));
    lv_obj_set_size(sep, card_w - 16, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x27272A), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);

    // Center Earphone Visualizer Box
    int anim_y = (height_ < 260) ? 48 : 58;
    int anim_h = (height_ < 260) ? 52 : 70;
    lv_obj_t* anim_box = MakePanel(card, 8, anim_y, card_w - 16, anim_h, lv_color_hex(0x181818),
                                   lv_color_hex(0x282828), 10);

    int hp_size = (height_ < 260) ? 32 : 40;
    int hp_x = ((card_w - 16) - hp_size) / 2;
    lv_obj_t* hp_circle = lv_obj_create(anim_box);
    lv_obj_set_pos(hp_circle, hp_x, 4);
    lv_obj_set_size(hp_circle, hp_size, hp_size);
    lv_obj_set_style_radius(hp_circle, hp_size / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hp_circle, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_border_color(hp_circle, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_width(hp_circle, 2, LV_PART_MAIN);
    lv_obj_clear_flag(hp_circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hp_icon = lv_label_create(hp_circle);
    lv_obj_set_style_text_font(hp_icon, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(hp_icon, MATERIAL_SYMBOLS_HEADPHONES);
    lv_obj_set_style_text_color(hp_icon, C_CYAN, LV_PART_MAIN);
    lv_obj_center(hp_icon);

    lv_obj_t* bars[6];
    int bar_x[6] = {hp_x - 44,           hp_x - 30,           hp_x - 16,
                    hp_x + hp_size + 12, hp_x + hp_size + 26, hp_x + hp_size + 40};
    for (int i = 0; i < 6; i++) {
        bars[i] = lv_obj_create(anim_box);
        lv_obj_set_pos(bars[i], bar_x[i], (height_ < 260 ? 14 : 20));
        lv_obj_set_size(bars[i], 4, 8);
        lv_obj_set_style_bg_color(bars[i], C_CYAN, LV_PART_MAIN);
        lv_obj_set_style_radius(bars[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(bars[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    bool is_playing = false;
    if (is_online) {
        is_playing = callbacks_.is_online_playing && callbacks_.is_online_playing();
    } else {
        is_playing = callbacks_.is_audio_playing && callbacks_.is_audio_playing();
    }

    lv_obj_t* state_lbl =
        MakeLabel(anim_box, is_playing ? "● SEDANG MEMUTAR (ES8311)" : "○ SIAP DIPUTAR", 4,
                  (height_ < 260 ? 36 : 48), card_w - 24, 16, FONT_10,
                  is_playing ? C_CYAN : lv_color_hex(0x71717A));
    lv_obj_set_style_text_align(state_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    struct AnimCtx {
        NovaDisplay* self;
        lv_obj_t* bars[6];
        lv_obj_t* hp_icon;
        lv_obj_t* state_lbl;
        lv_timer_t* timer;
        uint32_t phase;
        bool is_online;
    };
    auto* actx = new AnimCtx{this,     {bars[0], bars[1], bars[2], bars[3], bars[4], bars[5]},
                             hp_icon,  state_lbl,
                             nullptr,  0,
                             is_online};
    actx->timer = lv_timer_create(
        [](lv_timer_t* t) {
            auto* c = static_cast<AnimCtx*>(lv_timer_get_user_data(t));
            if (!c || !c->self)
                return;
            bool playing = false;
            if (c->is_online) {
                playing = c->self->callbacks_.is_online_playing &&
                          c->self->callbacks_.is_online_playing();
            } else {
                playing =
                    c->self->callbacks_.is_audio_playing && c->self->callbacks_.is_audio_playing();
            }
            c->phase++;
            if (playing) {
                for (int i = 0; i < 6; i++) {
                    float s = sinf((float)c->phase * 0.35f + (float)i * 1.15f);
                    float cs = cosf((float)c->phase * 0.22f - (float)i * 0.8f);
                    int h = (int)(6 + (s * 0.5f + 0.5f) * 14.0f + (cs * 0.5f + 0.5f) * 10.0f);
                    if (h < 4)
                        h = 4;
                    if (h > 26)
                        h = 26;
                    lv_obj_set_height(c->bars[i], h);
                    lv_obj_set_y(c->bars[i], 24 - (h / 2));
                }
                lv_obj_set_style_text_color(
                    c->hp_icon, ((c->phase / 4) % 2 == 0) ? C_CYAN : lv_color_hex(0xA5F3FC),
                    LV_PART_MAIN);
                lv_label_set_text(c->state_lbl, "● SEDANG MEMUTAR (ES8311)");
                lv_obj_set_style_text_color(c->state_lbl, C_CYAN, LV_PART_MAIN);
            } else {
                for (int i = 0; i < 6; i++) {
                    lv_obj_set_height(c->bars[i], 4);
                    lv_obj_set_y(c->bars[i], 22);
                }
                lv_obj_set_style_text_color(c->hp_icon, lv_color_hex(0x71717A), LV_PART_MAIN);
                lv_label_set_text(c->state_lbl, "○ SIAP DIPUTAR");
                lv_obj_set_style_text_color(c->state_lbl, lv_color_hex(0x71717A), LV_PART_MAIN);
            }
        },
        80, actx);

    lv_obj_add_event_cb(
        anim_box,
        [](lv_event_t* e) {
            auto* c = static_cast<AnimCtx*>(lv_event_get_user_data(e));
            if (c) {
                if (c->timer)
                    lv_timer_del(c->timer);
                delete c;
            }
        },
        LV_EVENT_DELETE, actx);

    // Repeat Mode Button
    bool repeat_active = callbacks_.get_repeat ? callbacks_.get_repeat() : false;
    struct PlayerCtx {
        NovaDisplay* self;
        std::string title;
        std::string source;
        bool is_online;
        size_t file_size;
        std::string duration;
    };
    auto* pctx = new PlayerCtx{this, title, source_id_or_path, is_online, file_size, duration};

    int rep_y = (height_ < 260) ? 104 : 134;
    int rep_h = (height_ < 260) ? 24 : 26;
    MakeButton(
        card, repeat_active ? "🔁 ULANG: TERUS (LOOP)" : "▶️ PUTAR: 1x (SEKALI)", 8, rep_y,
        card_w - 16, rep_h, repeat_active ? C_CYAN : lv_color_hex(0x242424),
        repeat_active ? C_CYAN : lv_color_hex(0x3E3E3E),
        repeat_active ? lv_color_black() : lv_color_hex(0xD4D4D8), FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
            if (!pc || !pc->self)
                return;
            if (pc->self->callbacks_.trigger_click)
                pc->self->callbacks_.trigger_click();
            bool cur = pc->self->callbacks_.get_repeat ? pc->self->callbacks_.get_repeat() : false;
            bool nxt = !cur;
            if (pc->self->callbacks_.set_repeat)
                pc->self->callbacks_.set_repeat(nxt);

            lv_obj_t* b = static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_obj_t* l = lv_obj_get_child(b, 0);
            if (l) {
                lv_label_set_text(l, nxt ? "🔁 ULANG: TERUS (LOOP)" : "▶️ PUTAR: 1x (SEKALI)");
                lv_obj_set_style_text_color(l, nxt ? lv_color_black() : lv_color_hex(0xD4D4D8),
                                            LV_PART_MAIN);
            }
            lv_obj_set_style_bg_color(b, nxt ? C_CYAN : lv_color_hex(0x242424), LV_PART_MAIN);
            lv_obj_set_style_border_color(b, nxt ? C_CYAN : lv_color_hex(0x3E3E3E), LV_PART_MAIN);
        },
        pctx);

    // Play / Stop Button
    int play_y = (height_ < 260) ? 132 : 166;
    int play_h = (height_ < 260) ? 30 : 34;
    MakeButton(
        card, is_playing ? "⏹ HENTIKAN MUSIK" : "▶ PUTAR SEKARANG", 8, play_y, card_w - 16, play_h,
        is_playing ? lv_color_hex(0xE11D48) : C_CYAN, is_playing ? lv_color_hex(0xBE123C) : C_CYAN,
        is_playing ? lv_color_white() : lv_color_black(), FONT_12,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
            if (pc && pc->self) {
                if (pc->self->callbacks_.trigger_click)
                    pc->self->callbacks_.trigger_click();
                if (pc->is_online) {
                    bool play = pc->self->callbacks_.is_online_playing &&
                                pc->self->callbacks_.is_online_playing();
                    if (play) {
                        if (pc->self->callbacks_.stop_online)
                            pc->self->callbacks_.stop_online();
                    } else {
                        if (pc->self->callbacks_.play_online)
                            pc->self->callbacks_.play_online(pc->source, pc->title, pc->duration);
                    }
                } else {
                    bool play = pc->self->callbacks_.is_audio_playing &&
                                pc->self->callbacks_.is_audio_playing();
                    if (play) {
                        if (pc->self->callbacks_.stop_audio)
                            pc->self->callbacks_.stop_audio();
                    } else {
                        std::string p = (pc->source.find("/sdcard/") == 0)
                                            ? pc->source
                                            : ("/sdcard/" + pc->source);
                        if (pc->self->callbacks_.play_audio)
                            pc->self->callbacks_.play_audio(p);
                    }
                }
                std::string t = pc->title;
                std::string s = pc->source;
                bool o = pc->is_online;
                size_t sz = pc->file_size;
                std::string dur = pc->duration;
                pc->self->ShowAudioPlayerModal(t, s, o, sz, dur);
            }
        },
        pctx);

    int bot_y = (height_ < 260) ? 166 : 206;
    int bot_h = (height_ < 260) ? 24 : 28;
    if (is_online) {
        int half_w = (card_w - 24) / 2;
        MakeButton(
            card, "⬇ UNDUH SD", 8, bot_y, half_w, bot_h, lv_color_hex(0x13382C), C_MINT, C_MINT,
            FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
                if (pc && pc->self) {
                    if (pc->self->callbacks_.trigger_click)
                        pc->self->callbacks_.trigger_click();
                    if (pc->self->callbacks_.download_online) {
                        pc->self->callbacks_.download_online(pc->source, pc->title);
                    }
                    if (pc->self->online_modal_) {
                        lv_obj_del(pc->self->online_modal_);
                        pc->self->online_modal_ = nullptr;
                    }
                }
            },
            pctx);

        MakeButton(
            card, "✕ TUTUP", 8 + half_w + 8, bot_y, half_w, bot_h, lv_color_hex(0x1E1E22),
            lv_color_hex(0x2D2D32), lv_color_hex(0xA1A1AA), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->online_modal_) {
                    if (self->callbacks_.trigger_click)
                        self->callbacks_.trigger_click();
                    lv_obj_del(self->online_modal_);
                    self->online_modal_ = nullptr;
                }
            },
            this);
    } else {
        MakeButton(
            card, "✕ KEMBALI KE FILE HUB", 8, bot_y, card_w - 16, bot_h, lv_color_hex(0x1E1E22),
            lv_color_hex(0x2D2D32), lv_color_hex(0xA1A1AA), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->file_modal_) {
                    if (self->callbacks_.trigger_click)
                        self->callbacks_.trigger_click();
                    lv_obj_del(self->file_modal_);
                    self->file_modal_ = nullptr;
                }
            },
            this);
    }

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
            auto* c = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
            if (c)
                delete c;
        },
        LV_EVENT_DELETE, pctx);
}

void NovaDisplay::ShowDownloadModal(const std::string& title, int progress_pct,
                                    const std::string& status_text) {
    if (progress_pct < 0)
        progress_pct = 0;
    if (progress_pct > 100)
        progress_pct = 100;

    if (!dl_modal_) {
        // Full screen dim backdrop on lv_layer_top()
        lv_obj_t* root = lv_layer_top();
        dl_modal_ = lv_obj_create(root);
        lv_obj_set_size(dl_modal_, width_, height_);
        lv_obj_clear_flag(dl_modal_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(dl_modal_, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dl_modal_, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_border_width(dl_modal_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(dl_modal_, 0, LV_PART_MAIN);

        // Center card with modern dark aesthetic
        int card_w = (width_ > 240) ? 280 : 216;
        int card_h = (height_ < 260) ? 214 : 236;
        dl_card_ = MakePanel(dl_modal_, 0, 0, card_w, card_h, lv_color_hex(0x18181B),
                             (progress_pct >= 100) ? C_MINT : C_CYAN, 14);
        lv_obj_align(dl_card_, LV_ALIGN_CENTER, 0, 0);

        // Header Icon
        dl_icon_label_ = lv_label_create(dl_card_);
        lv_obj_set_style_text_font(dl_icon_label_, FONT_ICON, LV_PART_MAIN);
        lv_label_set_text(dl_icon_label_, (progress_pct >= 100) ? MATERIAL_SYMBOLS_CHECK
                                                                : MATERIAL_SYMBOLS_ARROW_DOWNWARD);
        lv_obj_set_style_text_color(dl_icon_label_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                    LV_PART_MAIN);
        lv_obj_set_pos(dl_icon_label_, 12, (height_ < 260 ? 6 : 10));

        // Header Title
        dl_hdr_label_ = lv_label_create(dl_card_);
        lv_label_set_text(dl_hdr_label_,
                          (progress_pct >= 100) ? "UNDUHAN SUKSES" : "MENGUNDUH KE FOLDER MUSIK");
        lv_obj_set_style_text_font(dl_hdr_label_, FONT_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_hdr_label_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                    LV_PART_MAIN);
        lv_obj_set_pos(dl_hdr_label_, 36, (height_ < 260 ? 8 : 12));

        // Separator line
        lv_obj_t* sep = lv_obj_create(dl_card_);
        lv_obj_set_pos(sep, 8, (height_ < 260 ? 28 : 36));
        lv_obj_set_size(sep, card_w - 16, 1);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x27272A), LV_PART_MAIN);
        lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);

        // Song Title
        dl_title_label_ = lv_label_create(dl_card_);
        lv_label_set_text(dl_title_label_, title.empty() ? "YouTube Audio" : title.c_str());
        lv_obj_set_style_text_font(dl_title_label_, FONT_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_title_label_, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_pos(dl_title_label_, 10, (height_ < 260 ? 34 : 44));
        lv_obj_set_width(dl_title_label_, card_w - 20);
        lv_label_set_long_mode(dl_title_label_, LV_LABEL_LONG_DOT);

        // Target Folder Label
        dl_folder_label_ = lv_label_create(dl_card_);
        lv_label_set_text(dl_folder_label_, "Folder: /sdcard/music/");
        lv_obj_set_style_text_font(dl_folder_label_, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_folder_label_, C_MINT, LV_PART_MAIN);
        lv_obj_set_pos(dl_folder_label_, 10, (height_ < 260 ? 50 : 62));

        // Big Percentage Label (e.g. "45%" or "100%")
        dl_pct_label_ = lv_label_create(dl_card_);
        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct);
        lv_label_set_text(dl_pct_label_, pct_str);
        lv_obj_set_style_text_font(dl_pct_label_, (height_ < 260 ? FONT_20 : FONT_24),
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_pct_label_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                    LV_PART_MAIN);
        lv_obj_set_pos(dl_pct_label_, 10, (height_ < 260 ? 66 : 78));
        lv_obj_set_width(dl_pct_label_, card_w - 20);
        lv_obj_set_style_text_align(dl_pct_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        // Animated Progress Bar
        dl_bar_ = lv_bar_create(dl_card_);
        lv_obj_set_size(dl_bar_, card_w - 20, (height_ < 260 ? 12 : 14));
        lv_obj_set_pos(dl_bar_, 10, (height_ < 260 ? 98 : 118));
        lv_bar_set_range(dl_bar_, 0, 100);
        lv_bar_set_value(dl_bar_, progress_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(dl_bar_, lv_color_hex(0x27272A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dl_bar_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                  LV_PART_INDICATOR);
        lv_obj_set_style_radius(dl_bar_, 7, LV_PART_MAIN);
        lv_obj_set_style_radius(dl_bar_, 7, LV_PART_INDICATOR);

        // Status text label below bar
        dl_status_label_ = lv_label_create(dl_card_);
        std::string initial_status =
            status_text.empty() ? "Menyimpan ke /sdcard/music/..." : status_text;
        lv_label_set_text(dl_status_label_, initial_status.c_str());
        lv_obj_set_style_text_font(dl_status_label_, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_status_label_, (progress_pct >= 100) ? C_MINT : C_MUTED,
                                    LV_PART_MAIN);
        lv_obj_set_pos(dl_status_label_, 10, (height_ < 260 ? 116 : 140));
        lv_obj_set_width(dl_status_label_, card_w - 20);
        lv_obj_set_style_text_align(dl_status_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(dl_status_label_, LV_LABEL_LONG_DOT);

        // Close / Cancel button
        int btn_y = (height_ < 260) ? 144 : 182;
        int btn_h = (height_ < 260) ? 26 : 32;
        MakeButton(
            dl_card_, (progress_pct >= 100) ? "SELESAI" : "TUTUP", 12, btn_y, card_w - 24, btn_h,
            C_PANEL_2, C_BORDER, C_TEXT, (height_ < 260 ? FONT_10 : FONT_12),
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->CloseDownloadModal(true);
                }
            },
            this);
    } else {
        // Modal already exists, update dynamic fields
        if (dl_bar_) {
            lv_bar_set_value(dl_bar_, progress_pct, LV_ANIM_ON);
            lv_obj_set_style_bg_color(dl_bar_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                      LV_PART_INDICATOR);
        }
        if (dl_pct_label_) {
            char pct_str[16];
            snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct);
            lv_label_set_text(dl_pct_label_, pct_str);
            lv_obj_set_style_text_color(dl_pct_label_, (progress_pct >= 100) ? C_MINT : C_CYAN,
                                        LV_PART_MAIN);
        }
        if (dl_title_label_ && !title.empty()) {
            lv_label_set_text(dl_title_label_, title.c_str());
        }
        if (dl_status_label_ && !status_text.empty()) {
            lv_label_set_text(dl_status_label_, status_text.c_str());
            lv_obj_set_style_text_color(dl_status_label_, (progress_pct >= 100) ? C_MINT : C_MUTED,
                                        LV_PART_MAIN);
        }
    }

    // Success State Transitions
    if (progress_pct >= 100) {
        if (dl_card_) {
            lv_obj_set_style_border_color(dl_card_, C_MINT, LV_PART_MAIN);
        }
        if (dl_icon_label_) {
            lv_label_set_text(dl_icon_label_, MATERIAL_SYMBOLS_CHECK);
            lv_obj_set_style_text_color(dl_icon_label_, C_MINT, LV_PART_MAIN);
        }
        if (dl_hdr_label_) {
            lv_label_set_text(dl_hdr_label_, "UNDUHAN SUKSES");
            lv_obj_set_style_text_color(dl_hdr_label_, C_MINT, LV_PART_MAIN);
        }
        if (dl_status_label_ && status_text.empty()) {
            lv_label_set_text(dl_status_label_, "100% Selesai! Disimpan di SD Card");
            lv_obj_set_style_text_color(dl_status_label_, C_MINT, LV_PART_MAIN);
        }

        // Auto close after 2.5 seconds
        if (!dl_timer_) {
            dl_timer_ = lv_timer_create(
                [](lv_timer_t* t) {
                    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
                    if (self) {
                        self->CloseDownloadModal(true);
                    }
                },
                2500, this);
            lv_timer_set_repeat_count(dl_timer_, 1);
        }
    }
}

void NovaDisplay::CloseDownloadModal(bool success) {
    if (dl_timer_) {
        lv_timer_del(dl_timer_);
        dl_timer_ = nullptr;
    }
    if (dl_modal_) {
        lv_obj_del(dl_modal_);
        dl_modal_ = nullptr;
        dl_card_ = nullptr;
        dl_bar_ = nullptr;
        dl_pct_label_ = nullptr;
        dl_status_label_ = nullptr;
        dl_title_label_ = nullptr;
        dl_folder_label_ = nullptr;
        dl_icon_label_ = nullptr;
        dl_hdr_label_ = nullptr;
    }
}

// ============================================================================
// KALKULATOR SENTUH MODERN
// ============================================================================
