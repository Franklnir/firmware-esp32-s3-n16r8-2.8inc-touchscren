#include "nova_display_internal.h"

void NovaDisplay::StartWifiScan() {
    if (scan_state_ == 0) {
        return;
    }
    scan_state_ = 0;
    wifi_message_ = "Sedang memindai jaringan...";
    scanned_aps_.clear();
    BuildCurrentPage();

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<NovaDisplay*>(arg);
            std::vector<ScannedAp> results;
            auto add_or_update_ap = [&results](const std::string& ssid, int rssi, bool open) {
                if (ssid.empty())
                    return;
                for (auto& existing : results) {
                    if (existing.ssid == ssid) {
                        if (rssi > existing.rssi) {
                            existing.rssi = rssi;
                            existing.open = open;
                        }
                        return;
                    }
                }
                ScannedAp ap;
                ap.ssid = ssid;
                ap.rssi = rssi;
                ap.open = open;
                results.push_back(std::move(ap));
            };

            auto& mgr = WifiManager::GetInstance();
            if (mgr.IsInitialized() && mgr.IsConfigMode()) {
                // In Config AP mode, WifiConfigurationAp consumes WIFI_EVENT_SCAN_DONE
                // and caches scanned APs behind http://192.168.4.1/scan.
                auto* network = Board::GetInstance().GetNetwork();
                for (int attempt = 0; attempt < 4 && results.empty(); ++attempt) {
                    if (attempt > 0) {
                        vTaskDelay(pdMS_TO_TICKS(1200));
                    }
                    if (network) {
                        auto http = network->CreateHttp(0);
                        if (http) {
                            http->SetTimeout(4000);
                            if (http->Open("GET", "http://192.168.4.1/scan")) {
                                if (http->GetStatusCode().value_or(0) == 200) {
                                    std::string body = http->ReadAll();
                                    cJSON* root = cJSON_Parse(body.c_str());
                                    if (root) {
                                        cJSON* aps = cJSON_GetObjectItem(root, "aps");
                                        if (cJSON_IsArray(aps)) {
                                            cJSON* item = nullptr;
                                            cJSON_ArrayForEach (item, aps) {
                                                cJSON* j_ssid = cJSON_GetObjectItem(item, "ssid");
                                                cJSON* j_rssi = cJSON_GetObjectItem(item, "rssi");
                                                cJSON* j_auth =
                                                    cJSON_GetObjectItem(item, "authmode");
                                                if (cJSON_IsString(j_ssid) && j_ssid->valuestring &&
                                                    j_ssid->valuestring[0] != '\0') {
                                                    int rssi = cJSON_IsNumber(j_rssi)
                                                                   ? j_rssi->valueint
                                                                   : -75;
                                                    bool open =
                                                        cJSON_IsNumber(j_auth)
                                                            ? (j_auth->valueint == WIFI_AUTH_OPEN)
                                                            : false;
                                                    add_or_update_ap(j_ssid->valuestring, rssi,
                                                                     open);
                                                }
                                            }
                                        }
                                        cJSON_Delete(root);
                                    }
                                }
                                http->Close();
                            }
                        }
                    }
                }
                ESP_LOGI(TAG, "Config AP /scan returned %d AP(s)", (int)results.size());
            } else {
                // Stop WifiStation temporarily so its WIFI_EVENT_SCAN_DONE handler
                // neither drains esp_wifi_scan_get_ap_records() nor calls esp_wifi_connect().
                bool was_initialized = mgr.IsInitialized();
                bool was_connected = was_initialized && mgr.IsConnected();
                bool has_saved = !SsidManager::GetInstance().GetSsidList().empty();

                if (was_initialized) {
                    mgr.StopStation();
                    vTaskDelay(pdMS_TO_TICKS(120));
                } else {
                    WifiManagerConfig cfg;
                    cfg.ssid_prefix = "Xiaozhi";
                    mgr.Initialize(cfg);
                }

                wifi_mode_t mode = WIFI_MODE_NULL;
                if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
                    esp_wifi_set_mode(WIFI_MODE_STA);
                } else if (mode == WIFI_MODE_AP) {
                    esp_wifi_set_mode(WIFI_MODE_APSTA);
                }
                esp_wifi_start();
                esp_wifi_set_ps(WIFI_PS_NONE);

                wifi_scan_config_t scan_cfg = {};
                scan_cfg.show_hidden = false;
                esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
                if (err == ESP_ERR_WIFI_STATE) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    err = esp_wifi_scan_start(&scan_cfg, true);
                }

                uint16_t ap_count = 0;
                if (err == ESP_OK) {
                    esp_wifi_scan_get_ap_num(&ap_count);
                } else {
                    ESP_LOGW(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(err));
                }
                if (ap_count > 0) {
                    std::vector<wifi_ap_record_t> records(ap_count);
                    if (esp_wifi_scan_get_ap_records(&ap_count, records.data()) == ESP_OK) {
                        for (uint16_t i = 0; i < ap_count && i < 20; ++i) {
                            if (records[i].ssid[0] != '\0') {
                                add_or_update_ap((char*)records[i].ssid, records[i].rssi,
                                                 records[i].authmode == WIFI_AUTH_OPEN);
                            }
                        }
                    }
                }
                ESP_LOGI(TAG, "STA Wi-Fi scan found %d AP(s)", (int)results.size());

                if (was_connected || (was_initialized && has_saved)) {
                    mgr.StartStation();
                } else {
                    esp_wifi_stop();
                }
            }

            // Tampilkan juga jaringan yang tersimpan di SsidManager agar mudah dipilih
            const auto& saved_list = SsidManager::GetInstance().GetSsidList();
            for (const auto& item : saved_list) {
                if (!item.ssid.empty()) {
                    add_or_update_ap(item.ssid, -60, item.password.empty());
                }
            }

            std::sort(results.begin(), results.end(),
                      [](const ScannedAp& a, const ScannedAp& b) { return a.rssi > b.rssi; });
            if (results.size() > 20) {
                results.resize(20);
            }

            lvgl_port_lock(0);
            self->scanned_aps_ = std::move(results);
            self->scan_state_ = 1;
            self->wifi_message_ = self->scanned_aps_.empty() ? "Tidak ada jaringan ditemukan."
                                                             : "Ketuk jaringan untuk memilih.";
            if (self->current_page_ == PAGE_WIFI) {
                self->BuildCurrentPage();
            }
            lvgl_port_unlock();

            vTaskDelete(NULL);
        },
        "nova_wifi_scan", 6144, this, 3, NULL);
}

void NovaDisplay::ConnectWifi(const std::string& ssid, const std::string& password) {
    if (wifi_modal_) {
        lv_obj_del(wifi_modal_);
        wifi_modal_ = nullptr;
        wifi_ssid_input_ = nullptr;
        wifi_password_input_ = nullptr;
        wifi_keyboard_ = nullptr;
        wifi_modal_open_ = false;
    }

    selected_ssid_ = ssid;
    wifi_connect_pending_ = true;
    wifi_connect_started_at_ = esp_timer_get_time() / 1000;
    wifi_message_ = "Menghubungkan ke " + ssid + "...";

    // 1. Simpan ke SsidManager NVS (Kunci utama agar tersimpan lintas mode XiaoZhi / Chronchi /
    // Nova OS)
    SsidManager::GetInstance().AddSsid(ssid, password);

    // 2. Simpan juga ke Settings NVS lokal
    Settings prefs("wifi", true);
    prefs.SetString("ssid", ssid);
    prefs.SetString("password", password);

    // 3. Restart WiFi Station agar langsung menerapkan koneksi baru
    auto& mgr = WifiManager::GetInstance();
    if (mgr.IsInitialized()) {
        mgr.StopStation();
        vTaskDelay(pdMS_TO_TICKS(100));
        mgr.StartStation();
    } else {
        wifi_config_t wifi_cfg = {};
        strncpy((char*)wifi_cfg.sta.ssid, ssid.c_str(), sizeof(wifi_cfg.sta.ssid) - 1);
        strncpy((char*)wifi_cfg.sta.password, password.c_str(), sizeof(wifi_cfg.sta.password) - 1);
        wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        esp_wifi_connect();
    }

    BuildCurrentPage();
}

void NovaDisplay::ForgetWifi() {
    // 1. Hapus dari SsidManager (Penyimpanan global lintas mode XiaoZhi / Chronchi / Nova OS)
    SsidManager::GetInstance().Clear();

    // 2. Hapus dari Settings NVS
    Settings prefs("wifi", true);
    prefs.EraseKey("ssid");
    prefs.EraseKey("password");

    // 3. Putuskan sambungan stasiun Wi-Fi aktif
    auto& mgr = WifiManager::GetInstance();
    if (mgr.IsInitialized()) {
        mgr.StopStation();
    }
    esp_wifi_disconnect();

    wifi_message_ = "Wi-Fi dilupakan. Gunakan Pindai atau Web Hotspot.";
    BuildCurrentPage();
}

void NovaDisplay::StartWebHotspot() {
    auto& mgr = WifiManager::GetInstance();
    if (!mgr.IsInitialized()) {
        WifiManagerConfig cfg;
        cfg.ssid_prefix = "Xiaozhi";
        mgr.Initialize(cfg);
    }
    mgr.StartConfigAp();

    std::string ap_name = mgr.GetApSsid();
    if (ap_name.empty())
        ap_name = "Xiaozhi-BE55";

    wifi_message_ = "Hotspot Aktif: " + ap_name + " (192.168.4.1)";
    BuildCurrentPage();

    lv_obj_t* root = lv_layer_top();
    lv_obj_t* modal = lv_obj_create(root);
    lv_obj_set_size(modal, width_, height_);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(modal, 0, LV_PART_MAIN);

    lv_obj_t* card =
        MakePanel(modal, 12, 50, 216, 216, lv_color_hex(0x18181B), lv_color_hex(0x38BDF8), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    MakeLabel(card, "HOTSPOT WEB AKTIF", 12, 12, 192, 20, FONT_14, lv_color_hex(0x38BDF8));
    MakeLabel(card, "1. Sambungkan HP ke Wi-Fi:", 12, 36, 192, 16, FONT_10, C_MUTED);
    MakeLabel(card, ap_name.c_str(), 12, 52, 192, 18, FONT_12, lv_color_hex(0xFDE047));
    MakeLabel(card, "2. Buka browser HP ke URL:", 12, 74, 192, 16, FONT_10, C_MUTED);
    MakeLabel(card, "http://192.168.4.1", 12, 90, 192, 18, FONT_12, lv_color_hex(0x22C55E));
    MakeLabel(card, "Tip: Matikan Data Seluler HP agar koneksi tidak keluar sendiri.", 12, 114, 192,
              34, FONT_10, lv_color_hex(0x94A3B8));

    MakeButton(
        card, "TUTUP", 12, 162, 192, 34, lv_color_hex(0x2563EB), lv_color_hex(0x3B82F6),
        lv_color_white(), FONT_12,
        [](lv_event_t* e) {
            lv_obj_t* m = (lv_obj_t*)lv_event_get_user_data(e);
            if (m)
                lv_obj_del(m);
        },
        modal);
}

void NovaDisplay::CloseBluetoothModal() {
    if (bt_modal_) {
        lv_obj_del(bt_modal_);
        bt_modal_ = nullptr;
        bt_qr_box_ = nullptr;
        bt_success_box_ = nullptr;
        bt_status_lbl_ = nullptr;
        bt_inst_lbl_ = nullptr;
        bt_h_icon_ = nullptr;
    }
}

void NovaDisplay::OnBleConnectionChanged(bool connected) {
    if (top_bluetooth_icon_) {
        lv_label_set_text(top_bluetooth_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
        lv_obj_set_style_text_color(
            top_bluetooth_icon_,
            connected ? lv_color_hex(0x22C55E) : lv_color_hex(header_icon_color_), LV_PART_MAIN);
        lv_obj_set_style_text_opa(top_bluetooth_icon_, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (bt_modal_) {
        bt_was_connected_ = connected;
        UpdateBluetoothModal(connected);
    }
}

void NovaDisplay::UpdateBluetoothModal(bool ble_conn) {
    if (!bt_modal_)
        return;

    if (bt_h_icon_) {
        lv_obj_set_style_text_color(
            bt_h_icon_, ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8), LV_PART_MAIN);
    }

    if (bt_status_lbl_) {
        const char* st_txt =
            (width_ > 240) ? (ble_conn ? "● Terhubung" : "○ Siap Di-Scan")
                           : (ble_conn ? "● BERHASIL TERHUBUNG!" : "○ SIAP DI-SCAN APP CHRONOS");
        lv_color_t st_col = ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8);
        lv_label_set_text(bt_status_lbl_, st_txt);
        lv_obj_set_style_text_color(bt_status_lbl_, st_col, LV_PART_MAIN);
    }

    if (bt_inst_lbl_) {
        const char* inst_txt =
            (width_ > 240)
                ? (ble_conn ? "Chronos App Siap Digunakan" : "Scan QR lewat Chronos App")
                : (ble_conn ? "Chronos App Siap Digunakan" : "Buka Chronos App di HP > Scan QR");
        lv_label_set_text(bt_inst_lbl_, inst_txt);
        lv_obj_set_style_text_color(bt_inst_lbl_, ble_conn ? lv_color_hex(0x86EFAC) : C_MUTED,
                                    LV_PART_MAIN);
    }

    if (ble_conn) {
        if (bt_qr_box_)
            lv_obj_add_flag(bt_qr_box_, LV_OBJ_FLAG_HIDDEN);
        if (bt_success_box_) {
            lv_obj_clear_flag(bt_success_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t* badge = lv_obj_get_child(bt_success_box_, 0);
            if (badge) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, badge);
                lv_anim_set_values(&a, 40, 256);
                lv_anim_set_time(&a, 450);
                lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
                lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {
                    lv_obj_set_style_transform_scale((lv_obj_t*)var, (int16_t)val, 0);
                });
                lv_anim_start(&a);
            }
        }
        if (callbacks_.trigger_click)
            callbacks_.trigger_click();
    } else {
        if (bt_qr_box_)
            lv_obj_clear_flag(bt_qr_box_, LV_OBJ_FLAG_HIDDEN);
        if (bt_success_box_)
            lv_obj_add_flag(bt_success_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void NovaDisplay::ShowBluetoothModal() {
    bool ble_conn = false;
    std::string bname = "Chronchi", bmac = "--:--:--:--:--:--";
    if (callbacks_.get_ble_status) {
        callbacks_.get_ble_status(ble_conn, bname, bmac);
    }
    bt_was_connected_ = ble_conn;

    CloseBluetoothModal();

    lv_obj_t* root = lv_layer_top();
    bt_modal_ = lv_obj_create(root);
    lv_obj_set_size(bt_modal_, width_, height_);
    lv_obj_clear_flag(bt_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bt_modal_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bt_modal_, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(bt_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bt_modal_, 0, LV_PART_MAIN);
    lv_obj_add_flag(bt_modal_, LV_OBJ_FLAG_CLICKABLE);

    // Clicking dark backdrop outside card closes modal
    lv_obj_add_event_cb(
        bt_modal_,
        [](lv_event_t* e) {
            if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CloseBluetoothModal();
            }
        },
        LV_EVENT_CLICKED, this);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 300 : 224;
    int card_h = is_land ? 216 : 286;

    lv_obj_t* card = MakePanel(bt_modal_, 0, 0, card_w, card_h, lv_color_hex(0x18181B),
                               lv_color_hex(0x38BDF8), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    // Header Icon
    bt_h_icon_ = lv_label_create(card);
    lv_obj_clear_flag(bt_h_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(bt_h_icon_, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(bt_h_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
    lv_obj_set_style_text_color(
        bt_h_icon_, ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_pos(bt_h_icon_, 10, 8);

    lv_obj_t* h_title = lv_label_create(card);
    lv_obj_clear_flag(h_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(h_title, "PAIRING APP CHRONOS");
    lv_obj_set_pos(h_title, 30, 8);

    // Close button (X) with enlarged touch area and unclickable label
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 36, 30);
    lv_obj_set_pos(close_btn, card_w - 42, 4);
    StyleButton(close_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
    lv_obj_set_ext_click_area(close_btn, 10);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_clear_flag(close_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(close_lbl, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), LV_PART_MAIN);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(
        close_btn,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->CloseBluetoothModal();
        },
        LV_EVENT_CLICKED, this);

    // Render High-Contrast QR Code Encoding BLE MAC Address for instant Chronos pairing
    struct QrModalData {
        uint16_t* img_buf = nullptr;
        lv_image_dsc_t img_dsc = {};
    };
    auto* qmd = new QrModalData();
    int qr_target_dim = is_land ? 116 : 120;
    qmd->img_buf = (uint16_t*)heap_caps_malloc(qr_target_dim * qr_target_dim * sizeof(uint16_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!qmd->img_buf) {
        qmd->img_buf = (uint16_t*)malloc(qr_target_dim * qr_target_dim * sizeof(uint16_t));
    }

    int qr_box_w = qr_target_dim + 8;
    int qr_box_h = qr_target_dim + 8;
    int qr_box_x = is_land ? 12 : (card_w - qr_box_w) / 2;
    int qr_box_y = is_land ? 38 : 32;

    if (qmd->img_buf) {
        for (int i = 0; i < qr_target_dim * qr_target_dim; i++) {
            qmd->img_buf[i] = 0xFFFF;  // Pure white background
        }

        uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
        uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
        const char* qr_text = bmac.c_str();
        if (qrcodegen_encodeText(qr_text, tempBuffer, qrcode, qrcodegen_Ecc_LOW, 1, 10,
                                 qrcodegen_Mask_AUTO, true)) {
            int qr_size = qrcodegen_getSize(qrcode);
            int border = 2;
            int total_modules = qr_size + border * 2;
            int scale = qr_target_dim / total_modules;
            if (scale < 1)
                scale = 1;
            int offset_x = (qr_target_dim - (total_modules * scale)) / 2 + border * scale;
            int offset_y = (qr_target_dim - (total_modules * scale)) / 2 + border * scale;

            for (int y = 0; y < qr_size; y++) {
                for (int x = 0; x < qr_size; x++) {
                    if (qrcodegen_getModule(qrcode, x, y)) {
                        int px_start = offset_x + x * scale;
                        int py_start = offset_y + y * scale;
                        for (int dy = 0; dy < scale; dy++) {
                            int py = py_start + dy;
                            if (py < 0 || py >= qr_target_dim)
                                continue;
                            for (int dx = 0; dx < scale; dx++) {
                                int px = px_start + dx;
                                if (px < 0 || px >= qr_target_dim)
                                    continue;
                                qmd->img_buf[py * qr_target_dim + px] = 0x0000;  // Black module
                            }
                        }
                    }
                }
            }
        }

        memset(&qmd->img_dsc, 0, sizeof(qmd->img_dsc));
        qmd->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        qmd->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        qmd->img_dsc.header.w = qr_target_dim;
        qmd->img_dsc.header.h = qr_target_dim;
        qmd->img_dsc.header.stride = qr_target_dim * 2;
        qmd->img_dsc.data_size = qr_target_dim * qr_target_dim * 2;
        qmd->img_dsc.data = (const uint8_t*)qmd->img_buf;

        bt_qr_box_ = MakePanel(card, qr_box_x, qr_box_y, qr_box_w, qr_box_h, lv_color_white(),
                               lv_color_hex(0x38BDF8), 8);
        lv_obj_clear_flag(bt_qr_box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* qr_img = lv_image_create(bt_qr_box_);
        lv_image_set_src(qr_img, &qmd->img_dsc);
        lv_obj_center(qr_img);

        lv_obj_add_event_cb(
            bt_modal_,
            [](lv_event_t* e) {
                auto* d = static_cast<QrModalData*>(lv_event_get_user_data(e));
                if (d) {
                    if (d->img_buf) {
                        heap_caps_free(d->img_buf);
                    }
                    delete d;
                }
            },
            LV_EVENT_DELETE, qmd);
    }

    // Success Checkmark Box (shown when QR is scanned & connected)
    bt_success_box_ = MakePanel(card, qr_box_x, qr_box_y, qr_box_w, qr_box_h,
                                lv_color_hex(0x064E3B), lv_color_hex(0x10B981), 12);
    lv_obj_clear_flag(bt_success_box_, LV_OBJ_FLAG_SCROLLABLE);

    int badge_size = is_land ? 56 : 64;
    lv_obj_t* badge = MakePanel(bt_success_box_, (qr_box_w - badge_size) / 2,
                                (qr_box_h - badge_size) / 2 - 10, badge_size, badge_size,
                                lv_color_hex(0x10B981), lv_color_hex(0x34D399), badge_size / 2);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* check_icon = lv_label_create(badge);
    lv_obj_clear_flag(check_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(check_icon, MATERIAL_SYMBOLS_CHECK);
    lv_obj_set_style_text_font(check_icon, &font_material_symbols_30_4, LV_PART_MAIN);
    lv_obj_set_style_text_color(check_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(check_icon);

    lv_obj_t* succ_txt = lv_label_create(bt_success_box_);
    lv_obj_clear_flag(succ_txt, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(succ_txt, "TERKONEKSI");
    lv_obj_set_style_text_font(succ_txt, FONT_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(succ_txt, lv_color_hex(0x6EE7B7), LV_PART_MAIN);
    lv_obj_align(succ_txt, LV_ALIGN_BOTTOM_MID, 0, -4);

    if (ble_conn) {
        if (bt_qr_box_)
            lv_obj_add_flag(bt_qr_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bt_success_box_, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, badge);
        lv_anim_set_values(&a, 40, 256);
        lv_anim_set_time(&a, 450);
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
        lv_anim_set_exec_cb(&a, [](void* var, int32_t val) {
            lv_obj_set_style_transform_scale((lv_obj_t*)var, (int16_t)val, 0);
        });
        lv_anim_start(&a);
    } else {
        if (bt_qr_box_)
            lv_obj_clear_flag(bt_qr_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bt_success_box_, LV_OBJ_FLAG_HIDDEN);
    }

    // Modal cleanup on delete
    lv_obj_add_event_cb(
        bt_modal_,
        [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                self->bt_modal_ = nullptr;
                self->bt_qr_box_ = nullptr;
                self->bt_success_box_ = nullptr;
                self->bt_status_lbl_ = nullptr;
                self->bt_inst_lbl_ = nullptr;
                self->bt_h_icon_ = nullptr;
            }
        },
        LV_EVENT_DELETE, this);

    if (!is_land) {
        // Portrait info rows
        char info_line[64];
        snprintf(info_line, sizeof(info_line), "%s • %s", bname.c_str(), bmac.c_str());
        MakeCenteredLabel(card, info_line, 0, 166, card_w - 20, 16, FONT_10,
                          lv_color_hex(0xFDE047));

        const char* st_txt = ble_conn ? "● BERHASIL TERHUBUNG!" : "○ SIAP DI-SCAN APP CHRONOS";
        lv_color_t st_col = ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8);
        bt_status_lbl_ = MakeCenteredLabel(card, st_txt, 0, 186, card_w - 20, 16, FONT_10, st_col);

        bt_inst_lbl_ = MakeCenteredLabel(
            card, ble_conn ? "Chronos App Siap Digunakan" : "Buka Chronos App di HP > Scan QR", 0,
            204, card_w - 20, 16, FONT_10, ble_conn ? lv_color_hex(0x86EFAC) : C_MUTED);

        MakeButton(
            card, "TUTUP", 12, 244, card_w - 24, 32, lv_color_hex(0x0284C7),
            lv_color_hex(0x38BDF8), lv_color_white(), FONT_12,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->CloseBluetoothModal();
                }
            },
            this);
    } else {
        // Landscape side-by-side
        int rx = 146;
        int rw = card_w - rx - 10;
        char name_buf[48], mac_buf[48];
        snprintf(name_buf, sizeof(name_buf), "Nama: %s", bname.c_str());
        snprintf(mac_buf, sizeof(mac_buf), "MAC: %s", bmac.c_str());

        MakeLabel(card, name_buf, rx, 38, rw, 16, FONT_12, lv_color_hex(0xFDE047));
        MakeLabel(card, mac_buf, rx, 56, rw, 16, FONT_10, C_MUTED);

        const char* st_txt = ble_conn ? "● Terhubung" : "○ Siap Di-Scan";
        lv_color_t st_col = ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8);
        bt_status_lbl_ = MakeLabel(card, st_txt, rx, 76, rw, 16, FONT_12, st_col);

        bt_inst_lbl_ = MakeLabel(
            card, ble_conn ? "Chronos App Siap Digunakan" : "Scan QR lewat Chronos App", rx, 96, rw,
            32, FONT_10, ble_conn ? lv_color_hex(0x86EFAC) : lv_color_hex(0x93C5FD));

        MakeButton(
            card, "TUTUP", rx, 168, rw, 32, lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8),
            lv_color_white(), FONT_12,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->CloseBluetoothModal();
                }
            },
            this);
    }
}

static void OnScanWifiBtn(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->StartWifiScan();
}

void NovaDisplay::BuildWifi() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "WI-FI & JARINGAN");

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool connected =
        (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);
    auto& mgr = WifiManager::GetInstance();
    bool config_mode = mgr.IsInitialized() && mgr.IsConfigMode();

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=10, w=146): Status & Action Buttons
        lv_obj_t* scard = MakePanel(root, 10, 40, 146, 44, C_PANEL, C_BORDER, 8);
        MakeIcon(scard, ICON_WIFI, 8, 10, connected ? C_MINT : (config_mode ? C_CYAN : C_AMBER),
                 22);
        MakeLabel(scard,
                  connected ? "Terhubung" : (config_mode ? "Hotspot Aktif" : "Menghubungkan..."),
                  36, 4, 104, 16, FONT_12, C_TEXT);

        if (connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid =
                (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + " | " + ip_str;
            wifi_status_label_ =
                MakeLabel(scard, details.c_str(), 36, 22, 104, 14, FONT_10, C_CYAN);
        } else if (config_mode) {
            std::string ap_name = mgr.GetApSsid();
            if (ap_name.empty())
                ap_name = "Xiaozhi AP";
            std::string details = ap_name + " | 192.168.4.1";
            wifi_status_label_ =
                MakeLabel(scard, details.c_str(), 36, 22, 104, 14, FONT_10, C_CYAN);
        } else {
            wifi_status_label_ = MakeLabel(scard, "Wi-Fi Terputus", 36, 22, 104, 14, FONT_10,
                                           lv_color_hex(0xF59E0B));
        }
        lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        const char* scan_txt = (scan_state_ == 0) ? "PINDAI..." : "PINDAI";
        MakeButton(root, scan_txt, 10, 88, 70, 26, C_CYAN, C_CYAN, C_BG, FONT_10, OnScanWifiBtn,
                   this);
        MakeButton(
            root, "+ MANUAL", 84, 88, 72, 26, C_PANEL, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowWifiInputDialog("");
            },
            this);

        MakeButton(
            root, "LUPAKAN", 10, 118, 70, 26, C_PANEL, lv_color_hex(0xEF4444),
            lv_color_hex(0xEF4444), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ForgetWifi();
            },
            this);

        MakeButton(
            root, "HOTSPOT", 84, 118, 72, 26, C_PANEL, lv_color_hex(0x38BDF8),
            lv_color_hex(0x38BDF8), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->StartWebHotspot();
            },
            this);

        MakeLabel(root, "Pilih SSID untuk menyambung", 10, 150, 146, 28, FONT_10, C_MUTED);

        // Right Column (x=160, y=38, w=152, h=166): Scanned AP list
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 160, 38);
        lv_obj_set_size(list, 152, 166);
        lv_obj_set_style_bg_color(list, C_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 36, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        if (scan_state_ == 0) {
            MakeLabel(list, "Memindai sinyal...", 4, 8, 142, 24, FONT_12, C_CYAN);
        } else if (scanned_aps_.empty()) {
            const char* empty_txt =
                (scan_state_ < 0) ? "Tekan PINDAI untuk mencari." : "Tidak ada Wi-Fi.";
            MakeLabel(list, empty_txt, 4, 8, 142, 42, FONT_10, C_MUTED);
        } else {
            for (size_t i = 0; i < scanned_aps_.size(); ++i) {
                const int y = (int)i * 32;
                lv_obj_t* row = lv_btn_create(list);
                lv_obj_set_pos(row, 0, y);
                lv_obj_set_size(row, 144, 28);
                StyleButton(row, C_PANEL, C_BORDER, 6);

                lv_obj_t* ssid_lbl =
                    MakeLabel(row, scanned_aps_[i].ssid.c_str(), 6, 5, 90, 16, FONT_10, C_TEXT);
                lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", scanned_aps_[i].rssi);
                MakeLabel(row, rssi_str, 96, 5, 44, 16, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);

                lv_obj_set_user_data(row, (void*)(intptr_t)i);
                lv_obj_add_event_cb(
                    row,
                    [](lv_event_t* ev) {
                        if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                            return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                        if (!self)
                            return;
                        int idx =
                            (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
                        if (idx >= 0 && idx < (int)self->scanned_aps_.size()) {
                            const auto& ap = self->scanned_aps_[idx];
                            if (ap.open) {
                                self->ConnectWifi(ap.ssid, "");
                            } else {
                                self->ShowWifiPasswordDialog(ap.ssid);
                            }
                        }
                    },
                    LV_EVENT_CLICKED, this);
            }
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* scard = MakePanel(root, 12, 42, 216, 48, C_PANEL, C_BORDER, 10);
        MakeIcon(scard, ICON_WIFI, 10, 11, connected ? C_MINT : (config_mode ? C_CYAN : C_AMBER),
                 25);
        MakeLabel(scard,
                  connected ? "Terhubung" : (config_mode ? "Hotspot Aktif" : "Menghubungkan..."),
                  44, 5, 157, 18, FONT_14, C_TEXT);

        if (connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid =
                (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + "  |  " + ip_str;
            wifi_status_label_ =
                MakeLabel(scard, details.c_str(), 44, 25, 160, 18, FONT_10, C_CYAN);
        } else if (config_mode) {
            std::string ap_name = mgr.GetApSsid();
            if (ap_name.empty())
                ap_name = "Xiaozhi AP";
            std::string details = ap_name + "  |  192.168.4.1";
            wifi_status_label_ =
                MakeLabel(scard, details.c_str(), 44, 25, 160, 18, FONT_10, C_CYAN);
        } else {
            wifi_status_label_ = MakeLabel(scard, "Menghubungkan kembali...", 44, 25, 160, 18,
                                           FONT_10, lv_color_hex(0xF59E0B));
        }
        lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        const char* scan_txt = (scan_state_ == 0) ? "MEMINDAI..." : "PINDAI";
        MakeButton(root, scan_txt, 12, 92, 102, 26, C_CYAN, C_CYAN, C_BG, FONT_10, OnScanWifiBtn,
                   this);
        MakeButton(
            root, "+ MANUAL", 126, 92, 102, 26, C_PANEL, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ShowWifiInputDialog("");
            },
            this);

        MakeButton(
            root, "LUPAKAN", 12, 122, 102, 26, C_PANEL, lv_color_hex(0xEF4444),
            lv_color_hex(0xEF4444), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ForgetWifi();
            },
            this);

        MakeButton(
            root, "HOTSPOT", 126, 122, 102, 26, C_PANEL, lv_color_hex(0x38BDF8),
            lv_color_hex(0x38BDF8), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->StartWebHotspot();
            },
            this);

        MakeLabel(root, "Pilih SSID untuk menyambung", 14, 152, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 12, 168);
        lv_obj_set_size(list, 216, 120);
        lv_obj_set_style_bg_color(list, C_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 40, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        if (scan_state_ == 0) {
            MakeLabel(list, "Sedang memindai sinyal...", 4, 8, 204, 24, FONT_10, C_CYAN);
        } else if (scanned_aps_.empty()) {
            const char* empty_txt = (scan_state_ < 0) ? "Tekan pindai untuk mencari jaringan."
                                                      : "Tidak ada jaringan. Coba pindai lagi.";
            MakeLabel(list, empty_txt, 4, 8, 204, 42, FONT_10, C_MUTED);
        } else {
            for (size_t i = 0; i < scanned_aps_.size(); ++i) {
                const int y = (int)i * 34;
                lv_obj_t* row = lv_btn_create(list);
                lv_obj_set_pos(row, 0, y);
                lv_obj_set_size(row, 202, 30);
                StyleButton(row, C_PANEL, C_BORDER, 8);

                lv_obj_t* ssid_lbl =
                    MakeLabel(row, scanned_aps_[i].ssid.c_str(), 8, 6, 142, 18, FONT_10, C_TEXT);
                lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", scanned_aps_[i].rssi);
                MakeLabel(row, rssi_str, 149, 6, 47, 18, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);

                lv_obj_set_user_data(row, (void*)(intptr_t)i);
                lv_obj_add_event_cb(
                    row,
                    [](lv_event_t* ev) {
                        if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                            return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                        if (!self)
                            return;
                        int idx =
                            (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
                        if (idx >= 0 && idx < (int)self->scanned_aps_.size()) {
                            const auto& ap = self->scanned_aps_[idx];
                            if (ap.open) {
                                self->ConnectWifi(ap.ssid, "");
                            } else {
                                self->ShowWifiPasswordDialog(ap.ssid);
                            }
                        }
                    },
                    LV_EVENT_CLICKED, this);
            }
        }
    }

    MakeBottomNav(root, PAGE_WIFI);
}
