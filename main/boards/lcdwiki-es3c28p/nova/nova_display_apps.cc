#include "nova_display_internal.h"

static void OnCalcBtnClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self)
        return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(event);
    const char* key = (const char*)lv_obj_get_user_data(target);
    if (key)
        self->OnCalcKey(key);
}

static std::string FormatCalcNumber(double val) {
    if (std::isnan(val) || std::isinf(val))
        return "Error";
    if (std::abs(val) < 1e-12)
        return "0";

    double int_part;
    double frac_part = std::modf(val, &int_part);
    // Use std::to_string to prevent newlib-nano "%lld" bug which prints "ld" (looks like "id")
    if (std::abs(frac_part) < 1e-9 && std::abs(val) < 2e9) {
        return std::to_string((long)std::round(val));
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", val);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s.empty() ? "0" : s;
}

void NovaDisplay::OnCalcKey(const std::string& key) {
    if (callbacks_.trigger_click)
        callbacks_.trigger_click();

    if (key == "C") {
        calc_expr_ = "";
        calc_val_ = "0";
        calc_accum_ = 0.0;
        calc_pending_op_ = "";
        calc_reset_next_ = false;
        calc_has_result_ = false;
    } else if (key == "DEL") {
        if (!calc_has_result_ && !calc_reset_next_ && calc_val_.length() > 1) {
            calc_val_.pop_back();
            if (calc_val_ == "-")
                calc_val_ = "0";
        } else {
            calc_val_ = "0";
        }
    } else if (key == "+/-") {
        if (calc_val_ != "Error" && calc_val_ != "0") {
            if (calc_has_result_) {
                double v = -atof(calc_val_.c_str());
                calc_val_ = FormatCalcNumber(v);
                calc_accum_ = v;
                calc_expr_ = calc_val_;
            } else {
                if (calc_val_[0] == '-')
                    calc_val_.erase(0, 1);
                else
                    calc_val_ = "-" + calc_val_;
            }
        }
    } else if (key == "%") {
        if (calc_val_ != "Error") {
            double cur = atof(calc_val_.c_str());
            if (!calc_pending_op_.empty() && (calc_pending_op_ == "+" || calc_pending_op_ == "-")) {
                double pct = calc_accum_ * (cur / 100.0);
                calc_val_ = FormatCalcNumber(pct);
            } else {
                calc_val_ = FormatCalcNumber(cur / 100.0);
            }
        }
    } else if (key == ".") {
        if (calc_has_result_) {
            calc_expr_ = "";
            calc_val_ = "0.";
            calc_accum_ = 0.0;
            calc_pending_op_ = "";
            calc_reset_next_ = false;
            calc_has_result_ = false;
        } else if (calc_reset_next_) {
            calc_val_ = "0.";
            calc_reset_next_ = false;
        } else if (calc_val_.find('.') == std::string::npos) {
            calc_val_ += ".";
        }
    } else if (key >= "0" && key <= "9") {
        if (calc_has_result_) {
            calc_expr_ = "";
            calc_val_ = key;
            calc_accum_ = 0.0;
            calc_pending_op_ = "";
            calc_reset_next_ = false;
            calc_has_result_ = false;
        } else if (calc_reset_next_ || calc_val_ == "0" || calc_val_ == "Error") {
            calc_val_ = key;
            calc_reset_next_ = false;
        } else if (calc_val_.length() < 12) {
            calc_val_ += key;
        }
    } else if (key == "+" || key == "-" || key == "x" || key == "X" || key == "*" || key == "×" ||
               key == "/" || key == "÷") {
        if (calc_val_ == "Error") {
            calc_val_ = "0";
            calc_expr_ = "";
            calc_has_result_ = false;
        }

        double current = atof(calc_val_.c_str());

        // If an operator was already pending and user entered another number before hitting
        // operator again
        if (!calc_pending_op_.empty() && !calc_reset_next_) {
            bool div_zero = false;
            if (calc_pending_op_ == "+") {
                calc_accum_ += current;
            } else if (calc_pending_op_ == "-") {
                calc_accum_ -= current;
            } else if (calc_pending_op_ == "x" || calc_pending_op_ == "X" ||
                       calc_pending_op_ == "×" || calc_pending_op_ == "*") {
                calc_accum_ *= current;
            } else if (calc_pending_op_ == "/" || calc_pending_op_ == "÷") {
                if (std::abs(current) < 1e-12)
                    div_zero = true;
                else
                    calc_accum_ /= current;
            }

            if (div_zero) {
                calc_expr_ = "";
                calc_val_ = "Error";
                calc_pending_op_ = "";
                calc_reset_next_ = true;
                calc_has_result_ = true;
                if (calc_expr_lbl_)
                    lv_label_set_text(calc_expr_lbl_, calc_expr_.c_str());
                if (calc_val_lbl_)
                    lv_label_set_text(calc_val_lbl_, calc_val_.c_str());
                return;
            }
            calc_val_ = FormatCalcNumber(calc_accum_);
        } else {
            calc_accum_ = current;
        }

        calc_pending_op_ = key;
        calc_expr_ = FormatCalcNumber(calc_accum_) + " " + key;
        calc_reset_next_ = true;
        calc_has_result_ = false;
    } else if (key == "=") {
        if (!calc_pending_op_.empty() && calc_val_ != "Error") {
            double current = atof(calc_val_.c_str());
            bool div_zero = false;
            if (calc_pending_op_ == "+") {
                calc_accum_ += current;
            } else if (calc_pending_op_ == "-") {
                calc_accum_ -= current;
            } else if (calc_pending_op_ == "x" || calc_pending_op_ == "X" ||
                       calc_pending_op_ == "×" || calc_pending_op_ == "*") {
                calc_accum_ *= current;
            } else if (calc_pending_op_ == "/" || calc_pending_op_ == "÷") {
                if (std::abs(current) < 1e-12)
                    div_zero = true;
                else
                    calc_accum_ /= current;
            }

            if (div_zero) {
                calc_expr_ += " " + calc_val_ + " =";
                calc_val_ = "Error";
            } else {
                calc_expr_ += " " + calc_val_ + " =";
                calc_val_ = FormatCalcNumber(calc_accum_);
            }
            calc_pending_op_ = "";
            calc_reset_next_ = true;
            calc_has_result_ = true;
        }
    }

    if (calc_expr_lbl_)
        lv_label_set_text(calc_expr_lbl_, calc_expr_.c_str());
    if (calc_val_lbl_)
        lv_label_set_text(calc_val_lbl_, calc_val_.c_str());
}

void NovaDisplay::BuildCalculator() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "KALKULATOR");

    bool is_land = (width_ > 240);

    // Keypad layout (5 rows x 4 cols)
    static const struct {
        const char* key;
        int col;
        int row;
        int span;
        lv_color_t fill;
        lv_color_t text_color;
    } kButtons[] = {{"C", 0, 0, 1, lv_color_hex(0x3B1D28), lv_color_hex(0xFF5A70)},
                    {"+/-", 1, 0, 1, C_PANEL_2, C_TEXT},
                    {"%", 2, 0, 1, C_PANEL_2, C_TEXT},
                    {"/", 3, 0, 1, C_PANEL_2, C_AMBER},

                    {"7", 0, 1, 1, C_PANEL, C_TEXT},
                    {"8", 1, 1, 1, C_PANEL, C_TEXT},
                    {"9", 2, 1, 1, C_PANEL, C_TEXT},
                    {"x", 3, 1, 1, C_PANEL_2, C_AMBER},

                    {"4", 0, 2, 1, C_PANEL, C_TEXT},
                    {"5", 1, 2, 1, C_PANEL, C_TEXT},
                    {"6", 2, 2, 1, C_PANEL, C_TEXT},
                    {"-", 3, 2, 1, C_PANEL_2, C_AMBER},

                    {"1", 0, 3, 1, C_PANEL, C_TEXT},
                    {"2", 1, 3, 1, C_PANEL, C_TEXT},
                    {"3", 2, 3, 1, C_PANEL, C_TEXT},
                    {"+", 3, 3, 1, C_PANEL_2, C_AMBER},

                    {"0", 0, 4, 2, C_PANEL, C_TEXT},
                    {".", 2, 4, 1, C_PANEL, C_TEXT},
                    {"=", 3, 4, 1, C_CYAN, C_BG}};

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Display Card on left (w=122, h=164)
        lv_obj_t* disp_card = MakePanel(root, 8, 38, 122, 164, C_PANEL, C_BORDER, 10);
        calc_expr_lbl_ = MakeLabel(disp_card, calc_expr_.c_str(), 6, 8, 110, 18, FONT_10, C_MUTED,
                                   LV_TEXT_ALIGN_RIGHT);
        calc_val_lbl_ = MakeLabel(disp_card, calc_val_.c_str(), 6, 36, 110, 40, FONT_24, C_CYAN,
                                  LV_TEXT_ALIGN_RIGHT);
        MakeLabel(disp_card, "KALKULATOR", 6, 136, 110, 16, FONT_10, C_MUTED, LV_TEXT_ALIGN_CENTER);

        // Keypad on right (x=136..310, y=38..198)
        const int start_x = 136;
        const int start_y = 38;
        const int btn_w = 38;
        const int btn_h = 28;
        const int gap_x = 5;
        const int gap_y = 5;

        for (const auto& b : kButtons) {
            int x = start_x + b.col * (btn_w + gap_x);
            int y = start_y + b.row * (btn_h + gap_y);
            int w = (b.span == 2) ? (btn_w * 2 + gap_x) : btn_w;

            lv_obj_t* btn = lv_btn_create(root);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_size(btn, w, btn_h);
            StyleButton(btn, b.fill, C_BORDER, 6);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_text(lbl, b.key);
            lv_obj_set_style_text_font(lbl, FONT_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, b.text_color, LV_PART_MAIN);
            lv_obj_center(lbl);

            lv_obj_set_user_data(btn, (void*)b.key);
            lv_obj_add_event_cb(btn, OnCalcBtnClicked, LV_EVENT_CLICKED, this);
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* disp_card = MakePanel(root, 10, 40, 220, 64, C_PANEL, C_BORDER, 12);
        calc_expr_lbl_ = MakeLabel(disp_card, calc_expr_.c_str(), 8, 6, 204, 18, FONT_12, C_MUTED,
                                   LV_TEXT_ALIGN_RIGHT);
        calc_val_lbl_ = MakeLabel(disp_card, calc_val_.c_str(), 8, 24, 204, 34, FONT_28, C_CYAN,
                                  LV_TEXT_ALIGN_RIGHT);

        const int start_x = 10;
        const int start_y = 110;
        const int btn_w = 48;
        const int btn_h = 32;
        const int gap_x = 9;
        const int gap_y = 4;

        for (const auto& b : kButtons) {
            int x = start_x + b.col * (btn_w + gap_x);
            int y = start_y + b.row * (btn_h + gap_y);
            int w = (b.span == 2) ? (btn_w * 2 + gap_x) : btn_w;

            lv_obj_t* btn = lv_btn_create(root);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_size(btn, w, btn_h);
            StyleButton(btn, b.fill, C_BORDER, 8);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_text(lbl, b.key);

            const lv_font_t* font = FONT_14;
            if (strcmp(b.key, "+") == 0 || strcmp(b.key, "-") == 0 || strcmp(b.key, "x") == 0 ||
                strcmp(b.key, "/") == 0 || strcmp(b.key, "=") == 0 || strcmp(b.key, ".") == 0 ||
                (b.key[0] >= '0' && b.key[0] <= '9')) {
                font = FONT_20;
            }
            lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, b.text_color, LV_PART_MAIN);
            lv_obj_center(lbl);

            lv_obj_set_user_data(btn, (void*)b.key);
            lv_obj_add_event_cb(btn, OnCalcBtnClicked, LV_EVENT_CLICKED, this);
        }
    }

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// GAME FLAPPY BIRD TOUCH RETRO
// ============================================================================

struct RadioStation {
    const char* name;
    const char* genre;
    const char* video_id;
    const char* query;
    lv_color_t color;
};

static const RadioStation kRadioStations[] = {
    {"Prambors Radio", "102.2 FM - Top 40 Hits", "a9uVJphGruM", "Prambors Top 40 Hits Terbaru",
     lv_color_hex(0xF59E0B)},
    {"Gen FM Jakarta", "98.7 FM - Suara Musik Indo", "mmkuC9IgfkE", "Gen FM Hits Musik Indonesia",
     lv_color_hex(0xEC4899)},
    {"Lofi Chill Beats", "Lo-Fi Work / Study Music", "CLeZyIID9Bo", "Lofi Hip Hop Chill Beats",
     lv_color_hex(0x10B981)},
    {"Hard Rock FM", "87.6 FM - Rock & Lifestyle", "O1I8QOWQ2RU", "Hard Rock Hits Terbaik",
     lv_color_hex(0xEF4444)},
    {"Akustik Cafe", "Pop Akustik Santai Sore", "qIzOXW9-Zsw", "Akustik Cafe Indonesia Populer",
     lv_color_hex(0x8B5CF6)},
    {"Billboard Hot 100", "Top Global Billboard Hits", "AOYJ4Mx1jsQ", "Billboard Hot 100 Pop Hits",
     lv_color_hex(0x3B82F6)},
    {"I-Radio Jakarta", "89.6 FM - 100% Musik Indo", "PJ4jj_3S670",
     "I-Radio Musik Pop Indo Terbaik", lv_color_hex(0x06B6D4)}};

static void OnRadioStationClick(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
    if (!self)
        return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    self->OnRadioStationSelect(idx);
}

void NovaDisplay::OnRadioStationSelect(int idx) {
    if (radio_active_idx_ == idx) {
        // Stop playing
        if (callbacks_.stop_online)
            callbacks_.stop_online();
        radio_active_idx_ = -1;
    } else {
        // Hentikan stream / lagu sebelumnya dengan bersih
        if (callbacks_.stop_online)
            callbacks_.stop_online();
        radio_active_idx_ = idx;
        const auto& st = kRadioStations[idx];
        if (callbacks_.play_online) {
            std::string vid = (st.video_id != nullptr) ? st.video_id : "";
            std::string q = (st.query != nullptr) ? st.query : st.name;
            callbacks_.play_online(vid, q, "LIVE");
        }
    }
    if (callbacks_.trigger_click)
        callbacks_.trigger_click();
    BuildCurrentPage();
}

void NovaDisplay::BuildRadio() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "RADIO STREAMING");

    if (callbacks_.is_online_playing && !callbacks_.is_online_playing()) {
        radio_active_idx_ = -1;
    }

    const int count = sizeof(kRadioStations) / sizeof(kRadioStations[0]);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN RADIO (100% Fit On 1 Screen - No Scroll) ===
        auto make_station_card = [this, root](int x, int y, int w, int h, int idx) {
            const auto& st = kRadioStations[idx];
            bool is_active = (radio_active_idx_ == idx);

            lv_obj_t* card = lv_btn_create(root);
            lv_obj_set_pos(card, x, y);
            lv_obj_set_size(card, w, h);
            StyleButton(card, is_active ? lv_color_hex(0x0C2B22) : C_PANEL,
                        is_active ? C_MINT : C_BORDER, 8);

            MakeIcon(card, ICON_RADIO, 4, 7, is_active ? C_MINT : st.color, 22);

            lv_obj_t* name_lbl = MakeLabel(card, st.name, 28, 4, w - 74, 15, FONT_10, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* genre_lbl = MakeLabel(card, st.genre, 28, 19, w - 74, 13, FONT_10,
                                            is_active ? C_MINT : C_MUTED);
            lv_label_set_long_mode(genre_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            lv_obj_t* act_btn = lv_btn_create(card);
            lv_obj_set_pos(act_btn, w - 46, 6);
            lv_obj_set_size(act_btn, 42, 24);
            StyleButton(act_btn, is_active ? lv_color_hex(0xEF4444) : C_CYAN,
                        is_active ? lv_color_hex(0xEF4444) : C_CYAN, 6);
            lv_obj_t* act_lbl = lv_label_create(act_btn);
            lv_label_set_text(act_lbl, is_active ? "STOP" : "PLAY");
            lv_obj_set_style_text_font(act_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(act_lbl, C_BG, LV_PART_MAIN);
            lv_obj_center(act_lbl);

            lv_obj_set_user_data(card, (void*)(intptr_t)idx);
            lv_obj_add_event_cb(card, OnRadioStationClick, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(act_btn, (void*)(intptr_t)idx);
            lv_obj_add_event_cb(act_btn, OnRadioStationClick, LV_EVENT_CLICKED, this);
        };

        // Left Column (x=8, w=148): stations 0, 1, 2, 3
        for (int i = 0; i < 4; ++i) {
            make_station_card(8, 36 + i * 42, 148, 38, i);
        }

        // Right Column (x=164, w=148): stations 4, 5, 6
        for (int i = 4; i < count; ++i) {
            make_station_card(164, 36 + (i - 4) * 42, 148, 38, i);
        }

        // Slot 8 (x=164, y=162): Quick Stop All button
        lv_obj_t* stop_btn = lv_btn_create(root);
        lv_obj_set_pos(stop_btn, 164, 162);
        lv_obj_set_size(stop_btn, 148, 38);
        StyleButton(stop_btn, (radio_active_idx_ >= 0) ? lv_color_hex(0x3B1219) : C_PANEL,
                    (radio_active_idx_ >= 0) ? C_RED : C_BORDER, 8);
        lv_obj_t* s_lbl = lv_label_create(stop_btn);
        lv_label_set_text(s_lbl, (radio_active_idx_ >= 0) ? "✕ HENTIKAN RADIO" : "STATUS: STANDBY");
        lv_obj_set_style_text_font(s_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl, (radio_active_idx_ >= 0) ? C_RED : C_MUTED,
                                    LV_PART_MAIN);
        lv_obj_center(s_lbl);
        lv_obj_add_event_cb(
            stop_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    if (self->callbacks_.stop_online)
                        self->callbacks_.stop_online();
                    self->radio_active_idx_ = -1;
                    self->BuildCurrentPage();
                }
            },
            LV_EVENT_CLICKED, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 52, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        MakeLabel(list, "STASIUN LIVE INTERNET", 14, 4, 212, 16, FONT_12, C_MUTED);

        for (int i = 0; i < count; i++) {
            const auto& st = kRadioStations[i];
            bool is_active = (radio_active_idx_ == i);

            lv_obj_t* card = lv_btn_create(list);
            lv_obj_set_pos(card, 10, 24 + i * 50);
            lv_obj_set_size(card, 220, 44);
            StyleButton(card, is_active ? lv_color_hex(0x0C2B22) : C_PANEL,
                        is_active ? C_MINT : C_BORDER, 10);

            // Station Icon
            MakeIcon(card, ICON_RADIO, 8, 10, is_active ? C_MINT : st.color, 24);

            // Station Name & Genre
            lv_obj_t* name_lbl = MakeLabel(card, st.name, 38, 5, 120, 18, FONT_12, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* genre_lbl =
                MakeLabel(card, st.genre, 38, 22, 120, 15, FONT_10, is_active ? C_MINT : C_MUTED);
            lv_label_set_long_mode(genre_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Action Button: Play or Stop
            lv_obj_t* act_btn = lv_btn_create(card);
            lv_obj_set_pos(act_btn, 160, 8);
            lv_obj_set_size(act_btn, 52, 28);
            StyleButton(act_btn, is_active ? lv_color_hex(0xEF4444) : C_CYAN,
                        is_active ? lv_color_hex(0xEF4444) : C_CYAN, 8);
            lv_obj_t* act_lbl = lv_label_create(act_btn);
            lv_label_set_text(act_lbl, is_active ? "STOP" : "PUTAR");
            lv_obj_set_style_text_font(act_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(act_lbl, C_BG, LV_PART_MAIN);
            lv_obj_center(act_lbl);

            lv_obj_set_user_data(card, (void*)(intptr_t)i);
            lv_obj_add_event_cb(card, OnRadioStationClick, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(act_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(act_btn, OnRadioStationClick, LV_EVENT_CLICKED, this);
        }
    }

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME 2048 IMPLEMENTATION ---
// ============================================================================
