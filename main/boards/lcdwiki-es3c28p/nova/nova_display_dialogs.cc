#include "nova_display_internal.h"

void NovaDisplay::CloseRgbMixerModal() {
    if (rgb_modal_) {
        lv_obj_del(rgb_modal_);
        rgb_modal_ = nullptr;
        rgb_preview_box_ = nullptr;
        rgb_hex_lbl_ = nullptr;
        rgb_slider_r_ = nullptr;
        rgb_slider_g_ = nullptr;
        rgb_slider_b_ = nullptr;
        rgb_val_r_ = nullptr;
        rgb_val_g_ = nullptr;
        rgb_val_b_ = nullptr;
    }
}

void NovaDisplay::UpdateRgbMixerPreview() {
    if (rgb_slider_r_)
        mixer_r_ = (uint8_t)lv_slider_get_value(rgb_slider_r_);
    if (rgb_slider_g_)
        mixer_g_ = (uint8_t)lv_slider_get_value(rgb_slider_g_);
    if (rgb_slider_b_)
        mixer_b_ = (uint8_t)lv_slider_get_value(rgb_slider_b_);

    if (rgb_preview_box_ && lv_obj_is_valid(rgb_preview_box_)) {
        lv_obj_set_style_bg_color(rgb_preview_box_, lv_color_make(mixer_r_, mixer_g_, mixer_b_), 0);
    }
    if (rgb_hex_lbl_ && lv_obj_is_valid(rgb_hex_lbl_)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", mixer_r_, mixer_g_, mixer_b_);
        lv_label_set_text(rgb_hex_lbl_, buf);
    }
    if (rgb_val_r_ && lv_obj_is_valid(rgb_val_r_)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", mixer_r_);
        lv_label_set_text(rgb_val_r_, buf);
    }
    if (rgb_val_g_ && lv_obj_is_valid(rgb_val_g_)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", mixer_g_);
        lv_label_set_text(rgb_val_g_, buf);
    }
    if (rgb_val_b_ && lv_obj_is_valid(rgb_val_b_)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", mixer_b_);
        lv_label_set_text(rgb_val_b_, buf);
    }
}

void NovaDisplay::ShowRgbMixerModal(bool is_text_color) {
    CloseRgbMixerModal();
    mixer_is_text_ = is_text_color;

    if (mixer_is_text_) {
        const auto* cur_tc = GetCurrentSyncedTextColor();
        if (cur_tc && strcmp(cur_tc->id, "auto") != 0) {
            mixer_r_ = (cur_tc->text_color >> 16) & 0xFF;
            mixer_g_ = (cur_tc->text_color >> 8) & 0xFF;
            mixer_b_ = cur_tc->text_color & 0xFF;
        } else {
            mixer_r_ = (lv_color_to_int(C_TEXT) >> 16) & 0xFF;
            mixer_g_ = (lv_color_to_int(C_TEXT) >> 8) & 0xFF;
            mixer_b_ = lv_color_to_int(C_TEXT) & 0xFF;
        }
    } else {
        const auto* cur_bg = GetCurrentSyncedBg();
        if (cur_bg) {
            mixer_r_ = (cur_bg->bg_color >> 16) & 0xFF;
            mixer_g_ = (cur_bg->bg_color >> 8) & 0xFF;
            mixer_b_ = cur_bg->bg_color & 0xFF;
        }
    }

    lv_obj_t* root = lv_layer_top();
    rgb_modal_ = lv_obj_create(root);
    lv_obj_set_size(rgb_modal_, width_, height_);
    lv_obj_clear_flag(rgb_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(rgb_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rgb_modal_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(rgb_modal_, 0, 0);
    lv_obj_set_style_radius(rgb_modal_, 0, 0);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 280 : 224;
    int card_h = is_land ? 214 : 260;

    lv_obj_t* card = MakePanel(rgb_modal_, 0, 0, card_w, card_h, C_PANEL, C_CYAN, 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    // Title
    MakeLabel(card, mixer_is_text_ ? "🎨 KUSTOM RGB WARNA TEKS" : "🎨 CUSTOM RGB MIXER (LATAR)", 12,
              8, card_w - 24, 18, FONT_12, C_CYAN);

    // Preview row
    rgb_preview_box_ = lv_obj_create(card);
    lv_obj_set_pos(rgb_preview_box_, 12, 30);
    lv_obj_set_size(rgb_preview_box_, 44, 26);
    lv_obj_set_style_radius(rgb_preview_box_, 6, 0);
    lv_obj_set_style_border_width(rgb_preview_box_, 2, 0);
    lv_obj_set_style_border_color(rgb_preview_box_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(rgb_preview_box_, lv_color_make(mixer_r_, mixer_g_, mixer_b_), 0);
    lv_obj_set_style_bg_opa(rgb_preview_box_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(rgb_preview_box_, LV_OBJ_FLAG_SCROLLABLE);

    char hex_buf[32];
    snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", mixer_r_, mixer_g_, mixer_b_);
    rgb_hex_lbl_ = MakeLabel(card, hex_buf, 64, 32, 140, 20, FONT_12, C_TEXT);

    // Slider rows
    auto make_slider_row = [&](int y, const char* label, lv_color_t color, uint8_t val,
                               lv_obj_t** out_slider, lv_obj_t** out_val) {
        lv_obj_t* lbl = MakeLabel(card, label, 12, y, 16, 16, FONT_10, color);
        (void)lbl;
        char val_str[16];
        snprintf(val_str, sizeof(val_str), "%u", val);
        *out_val =
            MakeLabel(card, val_str, card_w - 44, y, 32, 16, FONT_10, color, LV_TEXT_ALIGN_RIGHT);

        lv_obj_t* slider = lv_slider_create(card);
        lv_obj_set_pos(slider, 30, y + 2);
        lv_obj_set_size(slider, card_w - 76, 12);
        lv_slider_set_range(slider, 0, 255);
        lv_slider_set_value(slider, val, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x27272A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);
        lv_obj_add_event_cb(
            slider,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->UpdateRgbMixerPreview();
            },
            LV_EVENT_VALUE_CHANGED, this);
        *out_slider = slider;
    };

    int base_y = is_land ? 62 : 66;
    int step_y = is_land ? 28 : 34;

    make_slider_row(base_y, "R", lv_color_hex(0xEF4444), mixer_r_, &rgb_slider_r_, &rgb_val_r_);
    make_slider_row(base_y + step_y, "G", lv_color_hex(0x22C55E), mixer_g_, &rgb_slider_g_,
                    &rgb_val_g_);
    make_slider_row(base_y + step_y * 2, "B", lv_color_hex(0x3B82F6), mixer_b_, &rgb_slider_b_,
                    &rgb_val_b_);

    int btn_y = is_land ? 162 : 188;
    int btn_w = (card_w - 32) / 2;

    // Apply button
    MakeButton(
        card, "TERAPKAN", 12, btn_y, btn_w, 32, C_PANEL, C_CYAN, C_CYAN, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            uint32_t custom_hex = ((uint32_t)self->mixer_r_ << 16) |
                                  ((uint32_t)self->mixer_g_ << 8) | (uint32_t)self->mixer_b_;
            if (self->mixer_is_text_) {
                SyncTextColorCustom(custom_hex);
                self->CloseRgbMixerModal();
                self->ApplyTextColor("custom", true);
            } else {
                SyncBgCustom(custom_hex);
                self->CloseRgbMixerModal();
                self->ApplyBackground("custom", true);
            }
            if (self->callbacks_.trigger_click)
                self->callbacks_.trigger_click();
        },
        this);

    // Cancel button
    MakeButton(
        card, "BATAL", 12 + btn_w + 8, btn_y, btn_w, 32, C_PANEL, C_BORDER, C_MUTED, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->CloseRgbMixerModal();
        },
        this);
}

void NovaDisplay::CloseGamesModal() {
    if (games_modal_) {
        lv_obj_del(games_modal_);
        games_modal_ = nullptr;
    }
}

void NovaDisplay::CloseHeaderColorModal() {
    if (header_color_modal_) {
        lv_obj_del(header_color_modal_);
        header_color_modal_ = nullptr;
        modal_prev_rot_ = nullptr;
        modal_prev_br_ = nullptr;
        modal_prev_bt_ = nullptr;
        modal_prev_vol_ = nullptr;
        modal_prev_wifi_ = nullptr;
        modal_prev_bat_ = nullptr;
        modal_prev_pct_ = nullptr;
    }
}

void NovaDisplay::UpdateHeaderColorModalPreview() {
    if (!header_color_modal_)
        return;
    lv_color_t iclr = lv_color_hex(header_icon_color_);
    if (modal_prev_rot_)
        lv_obj_set_style_text_color(modal_prev_rot_, iclr, 0);
    if (modal_prev_br_)
        lv_obj_set_style_text_color(modal_prev_br_, iclr, 0);
    if (modal_prev_bt_)
        lv_obj_set_style_text_color(modal_prev_bt_, iclr, 0);
    if (modal_prev_vol_)
        lv_obj_set_style_text_color(modal_prev_vol_, iclr, 0);
    if (modal_prev_wifi_)
        lv_obj_set_style_text_color(modal_prev_wifi_, iclr, 0);
    if (modal_prev_bat_)
        lv_obj_set_style_text_color(modal_prev_bat_, iclr, 0);
    if (modal_prev_pct_)
        lv_obj_set_style_text_color(modal_prev_pct_, iclr, 0);
}

void NovaDisplay::ShowHeaderColorModal() {
    CloseHeaderColorModal();
    lv_obj_t* root = lv_layer_top();
    header_color_modal_ = lv_obj_create(root);
    lv_obj_set_size(header_color_modal_, width_, height_);
    lv_obj_clear_flag(header_color_modal_,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_set_style_bg_color(header_color_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(header_color_modal_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header_color_modal_, 0, 0);
    lv_obj_set_style_radius(header_color_modal_, 0, 0);

    lv_obj_add_event_cb(
        header_color_modal_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->CloseHeaderColorModal();
            }
        },
        LV_EVENT_CLICKED, this);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 276 : 228;
    int card_h = is_land ? 204 : 226;

    lv_obj_t* card = MakePanel(header_color_modal_, 0, 0, card_w, card_h, C_PANEL, C_CYAN, 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));

    // Title
    MakeLabel(card, "🎨 WARNA IKON HEADER", 10, 8, card_w - 46, 18, FONT_12, C_CYAN);

    // Close button (✕)
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 22, 22);
    lv_obj_set_pos(close_btn, card_w - 30, 6);
    StyleButton(close_btn, C_PANEL, C_BORDER, 6);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, "✕");
    lv_obj_set_style_text_font(x_lbl, FONT_10, 0);
    lv_obj_set_style_text_color(x_lbl, C_MUTED, 0);
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(
        close_btn,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->CloseHeaderColorModal();
        },
        LV_EVENT_CLICKED, this);

    // Live Preview Bar Strip
    lv_obj_t* prev_box = lv_obj_create(card);
    lv_obj_set_pos(prev_box, 8, 30);
    lv_obj_set_size(prev_box, card_w - 16, 30);
    lv_obj_set_style_bg_color(prev_box, C_DARK, 0);
    lv_obj_set_style_border_color(prev_box, C_BORDER, 0);
    lv_obj_set_style_border_width(prev_box, 1, 0);
    lv_obj_set_style_radius(prev_box, 6, 0);
    lv_obj_set_style_pad_all(prev_box, 0, 0);
    lv_obj_set_style_pad_hor(prev_box, 8, 0);
    lv_obj_clear_flag(prev_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_set_flex_flow(prev_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prev_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_color_t iclr = lv_color_hex(header_icon_color_);

    modal_prev_rot_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_rot_, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(modal_prev_rot_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_rot_, iclr, 0);

    modal_prev_br_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_br_, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(modal_prev_br_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_br_, iclr, 0);

    modal_prev_bt_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_bt_, MATERIAL_SYMBOLS_BLUETOOTH);
    lv_obj_set_style_text_font(modal_prev_bt_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_bt_, iclr, 0);

    modal_prev_vol_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_vol_, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(modal_prev_vol_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_vol_, iclr, 0);

    modal_prev_wifi_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_wifi_, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_font(modal_prev_wifi_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_wifi_, iclr, 0);

    modal_prev_bat_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_bat_, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
    lv_obj_set_style_text_font(modal_prev_bat_, FONT_ICON, 0);
    lv_obj_set_style_text_color(modal_prev_bat_, iclr, 0);

    modal_prev_pct_ = lv_label_create(prev_box);
    lv_label_set_text(modal_prev_pct_, "100%");
    lv_obj_set_style_text_font(modal_prev_pct_, FONT_10, 0);
    lv_obj_set_style_text_color(modal_prev_pct_, iclr, 0);

    // 10-Color Palette Container (2 rows of 5 dots)
    lv_obj_t* pal_grid = lv_obj_create(card);
    lv_obj_set_pos(pal_grid, 8, 66);
    lv_obj_set_size(pal_grid, card_w - 16, is_land ? 94 : 114);
    lv_obj_set_style_bg_opa(pal_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pal_grid, 0, 0);
    lv_obj_set_style_pad_all(pal_grid, 2, 0);
    lv_obj_set_flex_flow(pal_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pal_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(pal_grid, 6, 0);
    lv_obj_set_style_pad_column(pal_grid, 6, 0);
    lv_obj_clear_flag(pal_grid, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));

    static const uint32_t kModalColors[] = {0xFFFFFF, 0x38BDF8, 0x22C55E, 0x06B6D4, 0xFFE600,
                                            0xF97316, 0xEF4444, 0xA855F7, 0xEC4899, 0x94A3B8};

    int dot_sz = is_land ? 28 : 32;
    for (size_t i = 0; i < sizeof(kModalColors) / sizeof(kModalColors[0]); ++i) {
        lv_obj_t* dot = lv_btn_create(pal_grid);
        lv_obj_set_size(dot, dot_sz, dot_sz);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(kModalColors[i]), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(dot, 0, 0);
        bool is_act = (header_icon_color_ == kModalColors[i]);
        lv_obj_set_style_border_width(dot, is_act ? 3 : 1, 0);
        lv_obj_set_style_border_color(dot, is_act ? lv_color_white() : lv_color_hex(0x475569), 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot,
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_CHAIN));
        lv_obj_set_user_data(dot, (void*)(uintptr_t)kModalColors[i]);

        lv_obj_add_event_cb(
            dot,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                uint32_t clr =
                    (uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                self->ApplyHeaderIconColor(clr, true);
                if (self->callbacks_.trigger_click)
                    self->callbacks_.trigger_click();

                // Update border highlight on all dots inside pal_grid
                lv_obj_t* p = lv_obj_get_parent((lv_obj_t*)lv_event_get_target(e));
                if (p) {
                    uint32_t cnt = lv_obj_get_child_cnt(p);
                    for (uint32_t ci = 0; ci < cnt; ++ci) {
                        lv_obj_t* ch = lv_obj_get_child(p, ci);
                        uint32_t d_clr = (uint32_t)(uintptr_t)lv_obj_get_user_data(ch);
                        bool act = (d_clr == clr);
                        lv_obj_set_style_border_width(ch, act ? 3 : 1, 0);
                        lv_obj_set_style_border_color(
                            ch, act ? lv_color_white() : lv_color_hex(0x475569), 0);
                    }
                }
            },
            LV_EVENT_CLICKED, this);
    }

    // Bottom Close Button
    MakeButton(
        card, "SELESAI", 8, card_h - 34, card_w - 16, 26, C_PANEL, C_CYAN, C_CYAN, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->CloseHeaderColorModal();
        },
        this);
}

void NovaDisplay::ShowGamesModal() {
    CloseGamesModal();

    lv_obj_t* root = lv_layer_top();
    games_modal_ = lv_obj_create(root);
    lv_obj_set_pos(games_modal_, 0, 0);
    lv_obj_set_size(games_modal_, width_, height_);
    lv_obj_clear_flag(games_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(games_modal_, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(games_modal_, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(games_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(games_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(games_modal_, 0, LV_PART_MAIN);

    // Backdrop click outside card closes modal
    lv_obj_add_event_cb(
        games_modal_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && target == self->games_modal_) {
                self->CloseGamesModal();
            }
        },
        LV_EVENT_CLICKED, this);

    struct GameItem {
        const char* name;
        const char* subtitle;
        const char* icon_sym;
        lv_color_t color;
        Page page;
        const char* tag;
    };

    static const GameItem kGames[] = {
        {"Flappy Bird", "Ketuk layar & terbang", MATERIAL_SYMBOLS_SPORTS_ESPORTS,
         lv_color_hex(0xF59E0B), PAGE_FLAPPY, "ARCADE"},
        {"Game 2048", "Puzzle geser angka", MATERIAL_SYMBOLS_CALCULATE, lv_color_hex(0x10B981),
         PAGE_2048, "PUZZLE"},
        {"Retro Snake", "Ular makan apel klasik", MATERIAL_SYMBOLS_EXPLORE, lv_color_hex(0x84CC16),
         PAGE_SNAKE, "RETRO"},
        {"Brick Breaker", "Pantul bola & hancurkan", MATERIAL_SYMBOLS_STAR, lv_color_hex(0x06B6D4),
         PAGE_BRICK, "ACTION"},
        {"Minesweeper", "Hindari ranjau bom", MATERIAL_SYMBOLS_WARNING, lv_color_hex(0xEF4444),
         PAGE_MINESWEEPER, "LOGIC"},
        {"Highway Racer", "Balapan mobil jalan raya", MATERIAL_SYMBOLS_NAVIGATION,
         lv_color_hex(0xF97316), PAGE_RACER, "SPEED"}};

    bool is_land = (width_ > 240);
    int card_w = is_land ? 296 : 226;
    int card_h = is_land ? 214 : 286;
    int card_x = (width_ - card_w) / 2;
    int card_y = (height_ - card_h) / 2;

    lv_obj_t* card = lv_obj_create(games_modal_);
    lv_obj_set_pos(card, card_x, card_y);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header Game Icon Badge
    lv_obj_t* head_icon = lv_label_create(card);
    lv_label_set_text(head_icon, MATERIAL_SYMBOLS_SPORTS_ESPORTS);
    lv_obj_set_style_text_font(head_icon, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(head_icon, C_AMBER, LV_PART_MAIN);
    lv_obj_set_pos(head_icon, 10, 6);

    // Title & Subtitle (Clean & compact, no overlap with close button)
    MakeLabel(card, is_land ? "GAME ARCADE" : "KOLEKSI GAME", 36, 4, card_w - 76, 18, FONT_14,
              C_TEXT);
    MakeLabel(card, is_land ? "Koleksi game retro siap dimainkan" : "6 Game Retro Siap Dimainkan",
              36, 22, card_w - 76, 14, FONT_10, C_MUTED);

    // Close button (X)
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_pos(close_btn, card_w - 30, 4);
    lv_obj_set_size(close_btn, 24, 24);
    StyleButton(close_btn, C_PANEL_2, C_BORDER, 6);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(close_lbl, C_RED, LV_PART_MAIN);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(
        close_btn,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->CloseGamesModal();
        },
        LV_EVENT_CLICKED, this);

    if (is_land) {
        // === LANDSCAPE 3x2 GRID (Presisi di tengah card) ===
        const int cols = 3;
        const int tile_w = 90;
        const int tile_h = 76;
        const int gap_x = 6;
        const int gap_y = 6;
        // Total grid width: 3 * 90 + 2 * 6 = 282
        // Centered inside card_w = 296: (296 - 282) / 2 = 7 px!
        const int start_x = 7;
        const int start_y = 42;

        for (int i = 0; i < 6; ++i) {
            int c = i % cols;
            int r = i / cols;
            int x = start_x + c * (tile_w + gap_x);
            int y = start_y + r * (tile_h + gap_y);

            lv_obj_t* btn = lv_btn_create(card);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_size(btn, tile_w, tile_h);
            StyleButton(btn, C_PANEL_2, C_BORDER, 10);

            // Icon container box
            lv_obj_t* icon_box = lv_obj_create(btn);
            lv_obj_set_pos(icon_box, 6, 6);
            lv_obj_set_size(icon_box, 26, 26);
            lv_obj_set_style_radius(icon_box, 6, 0);
            lv_obj_set_style_bg_color(icon_box, kGames[i].color, 0);
            lv_obj_set_style_bg_opa(icon_box, LV_OPA_20, 0);
            lv_obj_set_style_border_color(icon_box, kGames[i].color, 0);
            lv_obj_set_style_border_width(icon_box, 1, 0);
            lv_obj_clear_flag(icon_box,
                              (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(icon_box);

            lv_obj_t* icon_lbl = lv_label_create(icon_box);
            lv_label_set_text(icon_lbl, kGames[i].icon_sym);
            lv_obj_set_style_text_font(icon_lbl, FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(icon_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(icon_lbl);

            // Category tag
            MakeLabel(btn, kGames[i].tag, 34, 10, tile_w - 40, 14, FONT_10, kGames[i].color,
                      LV_TEXT_ALIGN_RIGHT);

            // Game Name
            lv_obj_t* name_lbl =
                MakeLabel(btn, kGames[i].name, 6, 36, tile_w - 12, 16, FONT_10, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Play badge
            MakeLabel(btn, "MAIN  ▶", 6, 54, tile_w - 12, 16, FONT_10, kGames[i].color);

            lv_obj_set_user_data(btn, (void*)(intptr_t)kGames[i].page);
            lv_obj_add_event_cb(
                btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    Page target = (Page)(intptr_t)lv_obj_get_user_data(
                        (lv_obj_t*)lv_event_get_current_target(e));
                    self->CloseGamesModal();
                    self->NavigateTo(target);
                },
                LV_EVENT_CLICKED, this);
        }
    } else {
        // === PORTRAIT 6 HORIZONTAL CARDS (Presisi di tengah card) ===
        // card_w = 226. List width = 216. Margin kiri/kanan = (226 - 216) / 2 = 5 px!
        lv_obj_t* list = lv_obj_create(card);
        lv_obj_set_pos(list, 5, 38);
        lv_obj_set_size(list, 216, 240);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        for (int i = 0; i < 6; ++i) {
            lv_obj_t* card_btn = lv_btn_create(list);
            lv_obj_set_pos(card_btn, 0, i * 39);
            lv_obj_set_size(card_btn, 216, 36);
            StyleButton(card_btn, C_PANEL_2, C_BORDER, 8);

            // Icon container box
            lv_obj_t* icon_box = lv_obj_create(card_btn);
            lv_obj_set_pos(icon_box, 5, 4);
            lv_obj_set_size(icon_box, 28, 28);
            lv_obj_set_style_radius(icon_box, 6, 0);
            lv_obj_set_style_bg_color(icon_box, kGames[i].color, 0);
            lv_obj_set_style_bg_opa(icon_box, LV_OPA_20, 0);
            lv_obj_set_style_border_color(icon_box, kGames[i].color, 0);
            lv_obj_set_style_border_width(icon_box, 1, 0);
            lv_obj_clear_flag(icon_box,
                              (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(icon_box);

            lv_obj_t* icon_lbl = lv_label_create(icon_box);
            lv_label_set_text(icon_lbl, kGames[i].icon_sym);
            lv_obj_set_style_text_font(icon_lbl, FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(icon_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(icon_lbl);

            // Name & Subtitle
            lv_obj_t* name_lbl =
                MakeLabel(card_btn, kGames[i].name, 38, 3, 116, 16, FONT_12, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* sub_lbl =
                MakeLabel(card_btn, kGames[i].subtitle, 38, 19, 116, 14, FONT_10, C_MUTED);
            lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Play button pill (Presisi di sisi kanan)
            lv_obj_t* play_pill = lv_obj_create(card_btn);
            lv_obj_set_pos(play_pill, 161, 7);
            lv_obj_set_size(play_pill, 50, 22);
            lv_obj_set_style_radius(play_pill, 6, 0);
            lv_obj_set_style_bg_color(play_pill, kGames[i].color, 0);
            lv_obj_set_style_bg_opa(play_pill, LV_OPA_20, 0);
            lv_obj_set_style_border_color(play_pill, kGames[i].color, 0);
            lv_obj_set_style_border_width(play_pill, 1, 0);
            lv_obj_clear_flag(play_pill,
                              (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(play_pill);

            lv_obj_t* play_lbl = lv_label_create(play_pill);
            lv_label_set_text(play_lbl, "MAIN ▶");
            lv_obj_set_style_text_font(play_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(play_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(play_lbl);

            lv_obj_set_user_data(card_btn, (void*)(intptr_t)kGames[i].page);
            lv_obj_add_event_cb(
                card_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    Page target = (Page)(intptr_t)lv_obj_get_user_data(
                        (lv_obj_t*)lv_event_get_current_target(e));
                    self->CloseGamesModal();
                    self->NavigateTo(target);
                },
                LV_EVENT_CLICKED, this);
        }
    }
}

void NovaDisplay::ShowWifiPasswordDialog(const std::string& ssid) { ShowWifiInputDialog(ssid); }

void NovaDisplay::ShowWifiInputDialog(const std::string& default_ssid) {
    if (wifi_modal_open_)
        return;
    selected_ssid_ = default_ssid;
    wifi_modal_open_ = true;
    wifi_pwd_visible_ = true;

    // 1. Backdrop semi-transparan hitam (asli XiaoZhi)
    lv_obj_t* root = lv_layer_top();
    wifi_modal_ = lv_obj_create(root);
    lv_obj_set_pos(wifi_modal_, 0, 0);
    lv_obj_set_size(wifi_modal_, width_, height_);
    lv_obj_clear_flag(wifi_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(wifi_modal_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_modal_, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_modal_, 0, LV_PART_MAIN);

    bool is_manual = default_ssid.empty();
    bool is_land = (width_ > 240);

    int card_w = is_land ? (width_ - 8) : 236;
    int card_h = is_land ? 224 : 276;

    // 2. Kartu Dialog Mengambang (Floating Card asli XiaoZhi)
    lv_obj_t* card = lv_obj_create(wifi_modal_);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18181B), LV_PART_MAIN);  // Zinc 900
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2563EB),
                                  LV_PART_MAIN);  // Vibrant Blue Border
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (is_land) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Row 1: Header + Close Button (Height: 22px)
        lv_obj_t* header = lv_obj_create(card);
        lv_obj_set_size(header, card_w - 8, 22);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* h_icon = lv_label_create(header);
        lv_obj_set_style_text_font(h_icon, FONT_ICON, LV_PART_MAIN);
        lv_label_set_text(h_icon, MATERIAL_SYMBOLS_WIFI);
        lv_obj_set_style_text_color(h_icon, lv_color_hex(0x38BDF8), LV_PART_MAIN);
        lv_obj_align(h_icon, LV_ALIGN_LEFT_MID, 2, 0);

        lv_obj_t* h_title = lv_label_create(header);
        lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(h_title, is_manual ? "Tambah Jaringan Wi-Fi" : "Sambung Wi-Fi");
        lv_obj_align(h_title, LV_ALIGN_LEFT_MID, 22, 0);

        lv_obj_t* close_btn = lv_btn_create(header);
        lv_obj_set_size(close_btn, 24, 20);
        lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        StyleButton(close_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 4);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_obj_set_style_text_font(close_lbl, FONT_ICON, LV_PART_MAIN);
        lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
        lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_center(close_lbl);
        lv_obj_add_event_cb(
            close_btn,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->wifi_modal_) {
                    lv_obj_del(self->wifi_modal_);
                    self->wifi_modal_ = nullptr;
                    self->wifi_ssid_input_ = nullptr;
                    self->wifi_password_input_ = nullptr;
                    self->wifi_keyboard_ = nullptr;
                    self->wifi_modal_open_ = false;
                }
            },
            LV_EVENT_CLICKED, this);

        // Row 2: Inputs & Action buttons in clean spacious layout
        if (!is_manual) {
            lv_obj_t* ssid_lbl = lv_label_create(card);
            lv_obj_set_pos(ssid_lbl, 4, 26);
            lv_obj_set_size(ssid_lbl, card_w - 16, 20);
            lv_obj_set_style_text_font(ssid_lbl, FONT_12, LV_PART_MAIN);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFDE047), LV_PART_MAIN);
            lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text_fmt(ssid_lbl, "SSID: %s", default_ssid.c_str());

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 4, 50);
            lv_obj_set_size(wifi_password_input_, 148, 30);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6),
                                          LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_12, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_password_input_, 4, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 156, 50);
            lv_obj_set_size(eye_btn, 34, 30);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(
                eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(
                eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B),
                LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(
                eye_btn,
                [](lv_event_t* e) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self || !self->wifi_password_input_)
                        return;
                    self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                    lv_textarea_set_password_mode(self->wifi_password_input_,
                                                  !self->wifi_pwd_visible_);
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                    if (lbl) {
                        lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN
                                                                       : MATERIAL_SYMBOLS_LOCK);
                        lv_obj_set_style_text_color(lbl,
                                                    self->wifi_pwd_visible_
                                                        ? lv_color_hex(0x10B981)
                                                        : lv_color_hex(0xF59E0B),
                                                    LV_PART_MAIN);
                    }
                },
                LV_EVENT_CLICKED, this);

            MakeButton(
                card, "BATAL", 194, 50, 52, 30, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B),
                lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                },
                this);

            MakeButton(
                card, "OK", 250, 50, 54, 30, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E),
                lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_password_input_)
                        return;
                    std::string pass = lv_textarea_get_text(self->wifi_password_input_);
                    self->ConnectWifi(self->selected_ssid_, pass);
                },
                this);
        } else {
            wifi_ssid_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_ssid_input_, 4, 26);
            lv_obj_set_size(wifi_ssid_input_, 144, 28);
            lv_textarea_set_one_line(wifi_ssid_input_, true);
            lv_textarea_set_placeholder_text(wifi_ssid_input_, "Nama SSID...");
            lv_obj_set_style_bg_color(wifi_ssid_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_ssid_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_ssid_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_ssid_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_ssid_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_ssid_input_, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_ssid_input_, 3, LV_PART_MAIN);

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 152, 26);
            lv_obj_set_size(wifi_password_input_, 120, 28);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6),
                                          LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_password_input_, 3, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 276, 26);
            lv_obj_set_size(eye_btn, 28, 28);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(
                eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(
                eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B),
                LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(
                eye_btn,
                [](lv_event_t* e) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self || !self->wifi_password_input_)
                        return;
                    self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                    lv_textarea_set_password_mode(self->wifi_password_input_,
                                                  !self->wifi_pwd_visible_);
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                    if (lbl) {
                        lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN
                                                                       : MATERIAL_SYMBOLS_LOCK);
                        lv_obj_set_style_text_color(lbl,
                                                    self->wifi_pwd_visible_
                                                        ? lv_color_hex(0x10B981)
                                                        : lv_color_hex(0xF59E0B),
                                                    LV_PART_MAIN);
                    }
                },
                LV_EVENT_CLICKED, this);

            MakeButton(
                card, "BATAL", 152, 58, 60, 26, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B),
                lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                },
                this);

            MakeButton(
                card, "HUBUNGKAN", 218, 58, 86, 26, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E),
                lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_ssid_input_)
                        return;
                    std::string s = lv_textarea_get_text(self->wifi_ssid_input_);
                    std::string p = self->wifi_password_input_
                                        ? lv_textarea_get_text(self->wifi_password_input_)
                                        : "";
                    if (!s.empty()) {
                        self->ConnectWifi(s, p);
                    }
                },
                this);
        }

        // Virtual Keyboard with standard landscape proportion (Height 120px)
        wifi_keyboard_ = lv_keyboard_create(card);
        lv_obj_set_size(wifi_keyboard_, card_w - 8, 120);
        lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, -3);
        StyleKeyboard(wifi_keyboard_);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // 3. Header Dialog
        lv_obj_t* header = lv_obj_create(card);
        lv_obj_set_size(header, 226, 28);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* h_icon = lv_label_create(header);
        lv_obj_set_style_text_font(h_icon, FONT_ICON, LV_PART_MAIN);
        lv_label_set_text(h_icon, MATERIAL_SYMBOLS_WIFI);
        lv_obj_set_style_text_color(h_icon, lv_color_hex(0x38BDF8), LV_PART_MAIN);
        lv_obj_align(h_icon, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t* h_title = lv_label_create(header);
        lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(h_title, is_manual ? "Tambah Jaringan" : "Sambung Wi-Fi");
        lv_obj_align(h_title, LV_ALIGN_LEFT_MID, 28, 0);

        lv_obj_t* close_btn = lv_btn_create(header);
        lv_obj_set_size(close_btn, 28, 24);
        lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -2, 0);
        StyleButton(close_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_obj_set_style_text_font(close_lbl, FONT_ICON, LV_PART_MAIN);
        lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
        lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_center(close_lbl);
        lv_obj_add_event_cb(
            close_btn,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->wifi_modal_) {
                    lv_obj_del(self->wifi_modal_);
                    self->wifi_modal_ = nullptr;
                    self->wifi_ssid_input_ = nullptr;
                    self->wifi_password_input_ = nullptr;
                    self->wifi_keyboard_ = nullptr;
                    self->wifi_modal_open_ = false;
                }
            },
            LV_EVENT_CLICKED, this);

        if (!is_manual) {
            // SSID Terpilih
            lv_obj_t* ssid_lbl = lv_label_create(card);
            lv_obj_set_size(ssid_lbl, 222, 18);
            lv_obj_align(ssid_lbl, LV_ALIGN_TOP_MID, 0, 32);
            lv_obj_set_style_text_font(ssid_lbl, FONT_12, LV_PART_MAIN);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFDE047), LV_PART_MAIN);
            lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text_fmt(ssid_lbl, "SSID: %s", default_ssid.c_str());

            // Kotak Sandi
            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 6, 54);
            lv_obj_set_size(wifi_password_input_, 170, 32);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6),
                                          LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_12, LV_PART_MAIN);

            // Tombol Intip Sandi
            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 180, 54);
            lv_obj_set_size(eye_btn, 44, 32);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(
                eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(
                eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B),
                LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(
                eye_btn,
                [](lv_event_t* e) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self || !self->wifi_password_input_)
                        return;
                    self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                    lv_textarea_set_password_mode(self->wifi_password_input_,
                                                  !self->wifi_pwd_visible_);
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                    if (lbl) {
                        lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN
                                                                       : MATERIAL_SYMBOLS_LOCK);
                        lv_obj_set_style_text_color(lbl,
                                                    self->wifi_pwd_visible_
                                                        ? lv_color_hex(0x10B981)
                                                        : lv_color_hex(0xF59E0B),
                                                    LV_PART_MAIN);
                    }
                },
                LV_EVENT_CLICKED, this);

            // Tombol Aksi: Batal & Hubungkan
            MakeButton(
                card, "BATAL", 6, 92, 80, 30, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B),
                lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                },
                this);

            MakeButton(
                card, "HUBUNGKAN", 92, 92, 132, 30, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E),
                lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_password_input_)
                        return;
                    std::string pass = lv_textarea_get_text(self->wifi_password_input_);
                    self->ConnectWifi(self->selected_ssid_, pass);
                },
                this);
        } else {
            // Mode Manual: Input SSID Baru
            wifi_ssid_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_ssid_input_, 6, 32);
            lv_obj_set_size(wifi_ssid_input_, 218, 28);
            lv_textarea_set_one_line(wifi_ssid_input_, true);
            lv_textarea_set_placeholder_text(wifi_ssid_input_, "Nama SSID / Kode...");
            lv_obj_set_style_bg_color(wifi_ssid_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_ssid_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_ssid_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_ssid_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_ssid_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_ssid_input_, FONT_12, LV_PART_MAIN);

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 6, 64);
            lv_obj_set_size(wifi_password_input_, 170, 28);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata Sandi (opsional)...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6),
                                          LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_12, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 180, 64);
            lv_obj_set_size(eye_btn, 44, 28);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(
                eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(
                eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B),
                LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(
                eye_btn,
                [](lv_event_t* e) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self || !self->wifi_password_input_)
                        return;
                    self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                    lv_textarea_set_password_mode(self->wifi_password_input_,
                                                  !self->wifi_pwd_visible_);
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                    if (lbl) {
                        lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN
                                                                       : MATERIAL_SYMBOLS_LOCK);
                        lv_obj_set_style_text_color(lbl,
                                                    self->wifi_pwd_visible_
                                                        ? lv_color_hex(0x10B981)
                                                        : lv_color_hex(0xF59E0B),
                                                    LV_PART_MAIN);
                    }
                },
                LV_EVENT_CLICKED, this);

            MakeButton(
                card, "BATAL", 6, 96, 80, 28, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B),
                lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                },
                this);

            MakeButton(
                card, "HUBUNGKAN", 92, 96, 132, 28, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E),
                lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_ssid_input_)
                        return;
                    std::string s = lv_textarea_get_text(self->wifi_ssid_input_);
                    std::string p = self->wifi_password_input_
                                        ? lv_textarea_get_text(self->wifi_password_input_)
                                        : "";
                    if (!s.empty()) {
                        self->ConnectWifi(s, p);
                    }
                },
                this);
        }

        // 4. Keyboard Virtual Standard Proportioned (Height 136px)
        wifi_keyboard_ = lv_keyboard_create(card);
        lv_obj_set_size(wifi_keyboard_, 226, 136);
        lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, -4);
        StyleKeyboard(wifi_keyboard_);
    }

    lv_keyboard_set_textarea(wifi_keyboard_, is_manual ? wifi_ssid_input_ : wifi_password_input_);
    lv_obj_add_state(is_manual ? wifi_ssid_input_ : wifi_password_input_, LV_STATE_FOCUSED);

    if (is_manual) {
        lv_obj_add_event_cb(
            wifi_ssid_input_,
            [](lv_event_t* e) {
                auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                if (kb)
                    lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_CLICKED, wifi_keyboard_);

        lv_obj_add_event_cb(
            wifi_password_input_,
            [](lv_event_t* e) {
                auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                if (kb)
                    lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_CLICKED, wifi_keyboard_);
    }

    // Event listener keyboard asli: LV_EVENT_READY & LV_EVENT_CANCEL
    lv_obj_add_event_cb(
        wifi_keyboard_,
        [](lv_event_t* e) {
            auto code = lv_event_get_code(e);
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            if (code == LV_EVENT_READY) {
                std::string s = self->selected_ssid_;
                if (self->wifi_ssid_input_) {
                    const char* st = lv_textarea_get_text(self->wifi_ssid_input_);
                    if (st && st[0] != '\0')
                        s = st;
                }
                std::string p = self->wifi_password_input_
                                    ? lv_textarea_get_text(self->wifi_password_input_)
                                    : "";
                if (!s.empty()) {
                    self->ConnectWifi(s, p);
                }
            } else if (code == LV_EVENT_CANCEL) {
                if (self->wifi_modal_) {
                    lv_obj_del(self->wifi_modal_);
                    self->wifi_modal_ = nullptr;
                    self->wifi_ssid_input_ = nullptr;
                    self->wifi_password_input_ = nullptr;
                    self->wifi_keyboard_ = nullptr;
                    self->wifi_modal_open_ = false;
                }
            }
        },
        LV_EVENT_ALL, this);
}
