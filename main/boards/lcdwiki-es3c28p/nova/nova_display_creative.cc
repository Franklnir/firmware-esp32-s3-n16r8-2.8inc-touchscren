#include "nova_display_internal.h"

void NovaDisplay::PlayPianoNote(int note_idx) {
    if (note_idx < 0 || note_idx >= 13)
        return;
    int fx_id = 100 + piano_instrument_ * 20 + note_idx;
    if (callbacks_.play_sound) {
        callbacks_.play_sound(fx_id);
    }
    static const char* kNoteNames[13] = {
        "C4 (261.6 Hz)",  "D4 (293.7 Hz)",  "E4 (329.6 Hz)", "F4 (349.2 Hz)",  "G4 (392.0 Hz)",
        "A4 (440.0 Hz)",  "B4 (493.9 Hz)",  "C5 (523.3 Hz)", "C#4 (277.2 Hz)", "D#4 (311.1 Hz)",
        "F#4 (370.0 Hz)", "G#4 (415.3 Hz)", "A#4 (466.2 Hz)"};
    if (piano_status_lbl_) {
        lv_label_set_text_fmt(piano_status_lbl_, "Nada: %s", kNoteNames[note_idx]);
    }
}

void NovaDisplay::SetPianoInstrument(int mode) {
    piano_instrument_ = mode;
    static const char* kInstNames[3] = {"Grand Piano", "8-Bit Chiptune", "Organ Vibrato"};
    for (int i = 0; i < 3; ++i) {
        if (!piano_inst_btns_[i])
            continue;
        if (i == mode) {
            lv_obj_set_style_bg_color(piano_inst_btns_[i], C_CYAN, LV_PART_MAIN);
            lv_obj_t* lbl = lv_obj_get_child(piano_inst_btns_[i], 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, C_DARK, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(piano_inst_btns_[i], C_PANEL, LV_PART_MAIN);
            lv_obj_t* lbl = lv_obj_get_child(piano_inst_btns_[i], 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, C_MUTED, LV_PART_MAIN);
        }
    }
    if (piano_status_lbl_) {
        lv_label_set_text_fmt(piano_status_lbl_, "Instrumen: %s", kInstNames[mode % 3]);
    }
    if (callbacks_.trigger_click)
        callbacks_.trigger_click();
}

void NovaDisplay::BuildPiano() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MINI PIANO", true);
    lv_obj_clear_flag(root, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                            LV_OBJ_FLAG_SCROLL_CHAIN_VER));

    bool is_land = (width_ > 240);
    // Header height is 36px. Provide clean margin below header (bar_y = 42 for portrait, 40 for
    // landscape)
    int bar_y = is_land ? 40 : 42;
    int bar_h = is_land ? 22 : 24;
    int btn_w = is_land ? 66 : 70;
    int btn_h = bar_h;
    int btn_gap = 6;
    int start_x = is_land ? 10 : 9;

    static const char* kInstTitles[3] = {"Piano", "8-Bit", "Organ"};
    for (int i = 0; i < 3; ++i) {
        piano_inst_btns_[i] = lv_btn_create(root);
        lv_obj_set_pos(piano_inst_btns_[i], start_x + i * (btn_w + btn_gap), bar_y);
        lv_obj_set_size(piano_inst_btns_[i], btn_w, btn_h);
        StyleButton(piano_inst_btns_[i], (i == piano_instrument_) ? C_CYAN : C_PANEL, C_BORDER, 6);
        lv_obj_clear_flag(piano_inst_btns_[i],
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                          LV_OBJ_FLAG_SCROLL_CHAIN_VER));
        lv_obj_t* lbl = MakeCenteredLabel(piano_inst_btns_[i], kInstTitles[i], 0, 0, btn_w, btn_h,
                                          FONT_10, (i == piano_instrument_) ? C_DARK : C_MUTED);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(
            piano_inst_btns_[i],
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                int mode = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                self->SetPianoInstrument(mode);
            },
            LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(piano_inst_btns_[i], (void*)(intptr_t)i);
    }

    int lbl_y = is_land ? 40 : 70;
    int lbl_x = is_land ? 228 : 8;
    int lbl_w = is_land ? 84 : 224;
    piano_status_lbl_ =
        MakeCenteredLabel(root, "Sentuh tuts untuk main", lbl_x, lbl_y, lbl_w, 18, FONT_10, C_CYAN);

    // Keys Container - Fixed, static, non-scrollable piano keyboard
    int keys_x = 4;
    int keys_y = is_land ? 66 : 92;
    int keys_w = is_land ? 312 : 232;
    int keys_h = is_land ? 138 : 192;

    lv_obj_t* keys_cont = lv_obj_create(root);
    lv_obj_set_pos(keys_cont, keys_x, keys_y);
    lv_obj_set_size(keys_cont, keys_w, keys_h);
    ClearDecoration(keys_cont);
    lv_obj_set_style_bg_color(keys_cont, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_radius(keys_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(keys_cont, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keys_cont, 1, LV_PART_MAIN);
    lv_obj_clear_flag(keys_cont,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                      LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                      LV_OBJ_FLAG_SCROLL_CHAIN_VER));
    lv_obj_set_scrollbar_mode(keys_cont, LV_SCROLLBAR_MODE_OFF);

    // 8 White Keys: C4, D4, E4, F4, G4, A4, B4, C5
    int wk_w = keys_w / 8;
    int wk_h = keys_h;
    static const char* kWkLabels[8] = {"C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"};

    for (int i = 0; i < 8; ++i) {
        piano_white_keys_[i] = lv_btn_create(keys_cont);
        lv_obj_set_pos(piano_white_keys_[i], i * wk_w, 0);
        lv_obj_set_size(piano_white_keys_[i], wk_w - 1, wk_h);
        lv_obj_set_style_bg_color(piano_white_keys_[i], lv_color_hex(0xF8FAFC), LV_PART_MAIN);
        lv_obj_set_style_bg_color(piano_white_keys_[i], lv_color_hex(0xBAE6FD), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(piano_white_keys_[i], lv_color_hex(0x64748B), LV_PART_MAIN);
        lv_obj_set_style_border_width(piano_white_keys_[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(piano_white_keys_[i], 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(piano_white_keys_[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(piano_white_keys_[i],
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                          LV_OBJ_FLAG_SCROLL_CHAIN_VER));

        lv_obj_t* klbl = lv_label_create(piano_white_keys_[i]);
        lv_label_set_text(klbl, kWkLabels[i]);
        lv_obj_set_style_text_font(klbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(klbl, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_align(klbl, LV_ALIGN_BOTTOM_MID, 0, -4);

        lv_obj_set_user_data(piano_white_keys_[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(
            piano_white_keys_[i],
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                int n = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                self->PlayPianoNote(n);
            },
            LV_EVENT_PRESSED, this);
    }

    // 5 Black Keys: C#4 (note 8), D#4 (note 9), F#4 (note 10), G#4 (note 11), A#4 (note 12)
    int bk_w = is_land ? 22 : 18;
    int bk_h = is_land ? 88 : 112;
    static const int kBkWhiteNeighbors[5] = {1, 2, 4, 5, 6};
    static const char* kBkLabels[5] = {"C#", "D#", "F#", "G#", "A#"};

    for (int i = 0; i < 5; ++i) {
        int note_idx = 8 + i;
        int target_x = kBkWhiteNeighbors[i] * wk_w - (bk_w / 2);

        piano_black_keys_[i] = lv_btn_create(keys_cont);
        lv_obj_set_pos(piano_black_keys_[i], target_x, 0);
        lv_obj_set_size(piano_black_keys_[i], bk_w, bk_h);
        lv_obj_set_style_bg_color(piano_black_keys_[i], lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(piano_black_keys_[i], lv_color_hex(0x0284C7), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(piano_black_keys_[i], lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(piano_black_keys_[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(piano_black_keys_[i], 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(piano_black_keys_[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(piano_black_keys_[i],
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR |
                                          LV_OBJ_FLAG_SCROLL_CHAIN_VER));

        lv_obj_t* klbl = lv_label_create(piano_black_keys_[i]);
        lv_label_set_text(klbl, kBkLabels[i]);
        lv_obj_set_style_text_font(klbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(klbl, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_align(klbl, LV_ALIGN_BOTTOM_MID, 0, -3);

        lv_obj_set_user_data(piano_black_keys_[i], (void*)(intptr_t)note_idx);
        lv_obj_add_event_cb(
            piano_black_keys_[i],
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                int n = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                self->PlayPianoNote(n);
            },
            LV_EVENT_PRESSED, this);
    }

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// TOUCH SKETCHPAD (PAPAN GAMBAR)
// ============================================================================

void NovaDisplay::ClearSketchpad() {
    if (sketch_canvas_ && sketch_buf_) {
        lv_canvas_fill_bg(sketch_canvas_, lv_color_hex(0x0F172A), LV_OPA_COVER);
        if (callbacks_.trigger_click)
            callbacks_.trigger_click();
    }
}

void NovaDisplay::SetSketchColor(lv_color_t color) {
    sketch_color_ = color;
    sketch_eraser_ = false;
    if (sketch_tool_lbl_) {
        lv_label_set_text(sketch_tool_lbl_, "Kuas");
        lv_obj_set_style_text_color(sketch_tool_lbl_, sketch_color_, LV_PART_MAIN);
    }
}

void NovaDisplay::SetSketchBrushSize(int sz) {
    sketch_brush_size_ = sz;
    if (sketch_size_btn_) {
        lv_obj_t* lbl = lv_obj_get_child(sketch_size_btn_, 0);
        if (lbl)
            lv_label_set_text_fmt(lbl, "%dpx", sz);
    }
}

void NovaDisplay::ToggleSketchEraser() {
    sketch_eraser_ = !sketch_eraser_;
    if (sketch_tool_lbl_) {
        if (sketch_eraser_) {
            lv_label_set_text(sketch_tool_lbl_, "Hapus");
            lv_obj_set_style_text_color(sketch_tool_lbl_, C_AMBER, LV_PART_MAIN);
        } else {
            lv_label_set_text(sketch_tool_lbl_, "Kuas");
            lv_obj_set_style_text_color(sketch_tool_lbl_, sketch_color_, LV_PART_MAIN);
        }
    }
    if (callbacks_.trigger_click)
        callbacks_.trigger_click();
}

static void SketchCanvasDrawLine(lv_obj_t* canvas, int buf_w, int buf_h, int x0, int y0, int x1,
                                 int y1, lv_color_t color, int sz) {
    if (!canvas)
        return;
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int r = sz / 2;

    while (true) {
        for (int oy = -r; oy <= r; ++oy) {
            for (int ox = -r; ox <= r; ++ox) {
                int px = x0 + ox;
                int py = y0 + oy;
                if (px >= 0 && px < buf_w && py >= 0 && py < buf_h) {
                    lv_canvas_set_px(canvas, px, py, color, LV_OPA_COVER);
                }
            }
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void NovaDisplay::BuildSketch() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "TOUCH SKETCHPAD", true);

    bool is_land = (width_ > 240);
    int bar_y = is_land ? 34 : 36;
    int bar_h = is_land ? 24 : 28;

    // Palette Colors: White, Red, Yellow, Green, Cyan, Violet
    static const lv_color_t kPalette[6] = {lv_color_hex(0xFFFFFF), lv_color_hex(0xEF4444),
                                           lv_color_hex(0xFACC15), lv_color_hex(0x22C55E),
                                           lv_color_hex(0x06B6D4), lv_color_hex(0xA855F7)};
    int swatch_sz = is_land ? 18 : 20;
    int swatch_gap = is_land ? 4 : 5;
    int start_x = is_land ? 8 : 6;

    for (int i = 0; i < 6; ++i) {
        lv_obj_t* sw = lv_btn_create(root);
        lv_obj_set_pos(sw, start_x + i * (swatch_sz + swatch_gap), bar_y + (bar_h - swatch_sz) / 2);
        lv_obj_set_size(sw, swatch_sz, swatch_sz);
        lv_obj_set_style_bg_color(sw, kPalette[i], LV_PART_MAIN);
        lv_obj_set_style_radius(sw, swatch_sz / 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(sw, lv_color_hex(0x475569), LV_PART_MAIN);
        lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);

        lv_obj_set_user_data(sw, (void*)(intptr_t)i);
        lv_obj_add_event_cb(
            sw,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                static const lv_color_t kP[6] = {lv_color_hex(0xFFFFFF), lv_color_hex(0xEF4444),
                                                 lv_color_hex(0xFACC15), lv_color_hex(0x22C55E),
                                                 lv_color_hex(0x06B6D4), lv_color_hex(0xA855F7)};
                self->SetSketchColor(kP[idx % 6]);
                if (self->callbacks_.trigger_click)
                    self->callbacks_.trigger_click();
            },
            LV_EVENT_CLICKED, this);
    }

    int right_x = start_x + 6 * (swatch_sz + swatch_gap) + 4;

    // Size Toggle Button
    sketch_size_btn_ = lv_btn_create(root);
    lv_obj_set_pos(sketch_size_btn_, right_x, bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(sketch_size_btn_, 32, 22);
    StyleButton(sketch_size_btn_, C_PANEL, C_BORDER, 4);
    lv_obj_t* sz_lbl = MakeCenteredLabel(sketch_size_btn_, "2px", 0, 0, 32, 22, FONT_10, C_TEXT);
    lv_obj_center(sz_lbl);
    lv_obj_add_event_cb(
        sketch_size_btn_,
        [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            int next_sz =
                (self->sketch_brush_size_ == 2) ? 4 : ((self->sketch_brush_size_ == 4) ? 6 : 2);
            self->SetSketchBrushSize(next_sz);
            if (self->callbacks_.trigger_click)
                self->callbacks_.trigger_click();
        },
        LV_EVENT_CLICKED, this);

    // Eraser Button
    lv_obj_t* eraser_btn = lv_btn_create(root);
    lv_obj_set_pos(eraser_btn, right_x + 36, bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(eraser_btn, is_land ? 32 : 36, 22);
    StyleButton(eraser_btn, C_PANEL, C_BORDER, 4);
    lv_obj_t* er_lbl =
        MakeCenteredLabel(eraser_btn, "Hps", 0, 0, is_land ? 32 : 36, 22, FONT_10, C_AMBER);
    lv_obj_center(er_lbl);
    lv_obj_add_event_cb(
        eraser_btn,
        [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            self->ToggleSketchEraser();
        },
        LV_EVENT_CLICKED, this);

    // Clear Canvas Button
    lv_obj_t* clear_btn = lv_btn_create(root);
    lv_obj_set_pos(clear_btn, right_x + (is_land ? 72 : 76), bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(clear_btn, is_land ? 32 : 34, 22);
    StyleButton(clear_btn, C_PANEL, C_BORDER, 4);
    lv_obj_t* cl_lbl =
        MakeCenteredLabel(clear_btn, "Clr", 0, 0, is_land ? 32 : 34, 22, FONT_10, C_RED);
    lv_obj_center(cl_lbl);
    lv_obj_add_event_cb(
        clear_btn,
        [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            self->ClearSketchpad();
        },
        LV_EVENT_CLICKED, this);

    // Canvas Dimensions: leave header and bottom nav safe
    int canvas_x = 4;
    int canvas_y = is_land ? 62 : 68;
    sketch_buf_w_ = is_land ? 312 : 232;
    sketch_buf_h_ = is_land ? 144 : 216;

    if (sketch_buf_) {
        free(sketch_buf_);
        sketch_buf_ = nullptr;
    }
    size_t bytes = (size_t)sketch_buf_w_ * sketch_buf_h_ * sizeof(uint16_t) + 64;
    sketch_buf_ = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sketch_buf_) {
        sketch_buf_ = (lv_color_t*)malloc(bytes);
    }

    sketch_canvas_ = lv_canvas_create(root);
    lv_obj_set_pos(sketch_canvas_, canvas_x, canvas_y);
    lv_obj_set_size(sketch_canvas_, sketch_buf_w_, sketch_buf_h_);
    if (sketch_buf_) {
        lv_canvas_set_buffer(sketch_canvas_, sketch_buf_, sketch_buf_w_, sketch_buf_h_,
                             LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(sketch_canvas_, lv_color_hex(0x0F172A), LV_OPA_COVER);
    }
    lv_obj_set_style_border_color(sketch_canvas_, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sketch_canvas_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(sketch_canvas_, 6, LV_PART_MAIN);
    lv_obj_add_flag(sketch_canvas_, LV_OBJ_FLAG_CLICKABLE);

    sketch_prev_pt_ = {-1, -1};

    // Canvas touch handler for drawing
    auto touch_cb = [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self || !self->sketch_canvas_ || !self->sketch_buf_)
            return;
        lv_event_code_t code = lv_event_get_code(e);

        lv_indev_t* indev = lv_indev_active();
        if (!indev)
            return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);

        lv_area_t a;
        lv_obj_get_coords(self->sketch_canvas_, &a);
        int cx = pt.x - a.x1;
        int cy = pt.y - a.y1;
        cx = std::max(0, std::min(cx, self->sketch_buf_w_ - 1));
        cy = std::max(0, std::min(cy, self->sketch_buf_h_ - 1));

        lv_color_t draw_col = self->sketch_eraser_ ? lv_color_hex(0x0F172A) : self->sketch_color_;
        int sz = self->sketch_brush_size_;

        if (code == LV_EVENT_PRESSED) {
            self->sketch_prev_pt_ = {cx, cy};
            SketchCanvasDrawLine(self->sketch_canvas_, self->sketch_buf_w_, self->sketch_buf_h_, cx,
                                 cy, cx, cy, draw_col, sz);
        } else if (code == LV_EVENT_PRESSING) {
            if (self->sketch_prev_pt_.x >= 0) {
                SketchCanvasDrawLine(self->sketch_canvas_, self->sketch_buf_w_, self->sketch_buf_h_,
                                     self->sketch_prev_pt_.x, self->sketch_prev_pt_.y, cx, cy,
                                     draw_col, sz);
            }
            self->sketch_prev_pt_ = {cx, cy};
        } else if (code == LV_EVENT_RELEASED) {
            self->sketch_prev_pt_ = {-1, -1};
        }
    };

    lv_obj_add_event_cb(sketch_canvas_, touch_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(sketch_canvas_, touch_cb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(sketch_canvas_, touch_cb, LV_EVENT_RELEASED, this);

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// RESOURCE & TASK MONITOR (ESP32-S3 DASHBOARD)
// ============================================================================

void NovaDisplay::MonitorTick() {
    // 1. Uptime
    int64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    int up_h = (int)(uptime_sec / 3600);
    int up_m = (int)((uptime_sec % 3600) / 60);
    int up_s = (int)(uptime_sec % 60);
    if (mon_uptime_lbl_) {
        lv_label_set_text_fmt(mon_uptime_lbl_, "Uptime: %02d:%02d:%02d", up_h, up_m, up_s);
    }

    // 2. Chip Temperature
    if (mon_temp_lbl_) {
        float t = callbacks_.get_temperature ? callbacks_.get_temperature() : 0.0f;
        lv_label_set_text_fmt(mon_temp_lbl_, "%.1f C", t);
        if (t < 50.0f) {
            lv_obj_set_style_text_color(mon_temp_lbl_, C_MINT, LV_PART_MAIN);
        } else if (t < 65.0f) {
            lv_obj_set_style_text_color(mon_temp_lbl_, C_AMBER, LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_color(mon_temp_lbl_, C_RED, LV_PART_MAIN);
        }
    }

    // 3. Internal SRAM
    size_t free_sram = esp_get_free_internal_heap_size();
    int total_sram_kb = 384;
    int free_sram_kb = (int)(free_sram / 1024);
    int used_sram_kb = std::max(0, total_sram_kb - free_sram_kb);
    int sram_pct = std::max(0, std::min(100, (used_sram_kb * 100) / total_sram_kb));
    if (mon_sram_bar_) {
        lv_bar_set_value(mon_sram_bar_, sram_pct, LV_ANIM_OFF);
    }
    if (mon_sram_lbl_) {
        lv_label_set_text_fmt(mon_sram_lbl_, "%d KB Sisa / %d%% Terpakai", free_sram_kb, sram_pct);
    }

    // 4. PSRAM (8 MB)
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    float free_psram_mb = (float)free_psram / (1024.0f * 1024.0f);
    int psram_pct = (int)((1.0f - (free_psram_mb / 8.0f)) * 100.0f);
    psram_pct = std::max(0, std::min(100, psram_pct));
    if (mon_psram_bar_) {
        lv_bar_set_value(mon_psram_bar_, psram_pct, LV_ANIM_OFF);
    }
    if (mon_psram_lbl_) {
        lv_label_set_text_fmt(mon_psram_lbl_, "%.2f MB Sisa / 8 MB (%d%%)", free_psram_mb,
                              psram_pct);
    }

    // 5. Wi-Fi & IP
    if (mon_wifi_lbl_) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            esp_netif_ip_info_t ip_info;
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            char s_ip[24] = "0.0.0.0";
            if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK &&
                ip_info.ip.addr != 0) {
                esp_ip4addr_ntoa(&ip_info.ip, s_ip, sizeof(s_ip));
            }
            lv_label_set_text_fmt(mon_wifi_lbl_, "%s (%d dBm)  |  IP: %s", (char*)ap.ssid, ap.rssi,
                                  s_ip);
        } else {
            lv_label_set_text(mon_wifi_lbl_, "Wi-Fi: Terputus / Mode Hotspot");
        }
    }
}

void NovaDisplay::BuildMonitor() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "RESOURCE MONITOR", true);

    bool is_land = (width_ > 240);

    if (is_land) {
        // Landscape: 2 Column Layout
        // Left Column: CPU & SRAM
        lv_obj_t* col1 = MakePanel(root, 8, 36, 148, 168, C_PANEL, C_BORDER, 8);
        MakeLabel(col1, "ESP32-S3 SoC", 8, 6, 132, 16, FONT_12, C_CYAN);
        mon_cpu_lbl_ = MakeLabel(col1, "Dual-Core LX7 @ 240MHz", 8, 24, 132, 14, FONT_10, C_MUTED);

        MakeLabel(col1, "Suhu Chip:", 8, 44, 70, 14, FONT_10, C_MUTED);
        mon_temp_lbl_ = MakeLabel(col1, "-- C", 80, 44, 60, 14, FONT_12, C_MINT);

        mon_uptime_lbl_ = MakeLabel(col1, "Uptime: 00:00:00", 8, 62, 132, 14, FONT_10, C_MUTED);

        MakeLabel(col1, "Internal SRAM (384KB)", 8, 86, 132, 14, FONT_10, C_TEXT);
        mon_sram_bar_ = lv_bar_create(col1);
        lv_obj_set_pos(mon_sram_bar_, 8, 104);
        lv_obj_set_size(mon_sram_bar_, 132, 8);
        lv_obj_set_style_bg_color(mon_sram_bar_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(mon_sram_bar_, C_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_radius(mon_sram_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(mon_sram_bar_, 4, LV_PART_INDICATOR);

        mon_sram_lbl_ = MakeLabel(col1, "-- KB Sisa", 8, 116, 132, 14, FONT_10, C_MUTED);

        // Right Column: PSRAM & Network
        lv_obj_t* col2 = MakePanel(root, 164, 36, 148, 168, C_PANEL, C_BORDER, 8);
        MakeLabel(col2, "Memori PSRAM (8 MB)", 8, 6, 132, 16, FONT_12, C_VIOLET);
        mon_psram_bar_ = lv_bar_create(col2);
        lv_obj_set_pos(mon_psram_bar_, 8, 28);
        lv_obj_set_size(mon_psram_bar_, 132, 8);
        lv_obj_set_style_bg_color(mon_psram_bar_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(mon_psram_bar_, C_VIOLET, LV_PART_INDICATOR);
        lv_obj_set_style_radius(mon_psram_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(mon_psram_bar_, 4, LV_PART_INDICATOR);

        mon_psram_lbl_ = MakeLabel(col2, "-- MB Sisa", 8, 40, 132, 14, FONT_10, C_MUTED);

        MakeLabel(col2, "Jaringan Wi-Fi", 8, 66, 132, 16, FONT_12, C_AMBER);
        mon_wifi_lbl_ = MakeLabel(col2, "Memeriksa sinyal...", 8, 88, 132, 32, FONT_10, C_MUTED);
        lv_label_set_long_mode(mon_wifi_lbl_, LV_LABEL_LONG_WRAP);

        lv_obj_t* rf_btn = lv_btn_create(col2);
        lv_obj_set_pos(rf_btn, 8, 128);
        lv_obj_set_size(rf_btn, 132, 26);
        StyleButton(rf_btn, C_PANEL_2, C_BORDER, 6);
        lv_obj_t* rfl = MakeCenteredLabel(rf_btn, "Segarkan", 0, 0, 132, 26, FONT_10, C_TEXT);
        lv_obj_center(rfl);
        lv_obj_add_event_cb(
            rf_btn,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->MonitorTick();
            },
            LV_EVENT_CLICKED, this);
    } else {
        // Portrait: Stacked Cards (width 224, centered x=8, Y=36..286)
        // Card 1: SoC & Temp
        lv_obj_t* c1 = MakePanel(root, 8, 36, 224, 60, C_PANEL, C_BORDER, 8);
        MakeLabel(c1, "ESP32-S3 SoC (240MHz)", 8, 4, 140, 16, FONT_12, C_CYAN);
        mon_uptime_lbl_ = MakeLabel(c1, "Uptime: 00:00:00", 8, 22, 140, 14, FONT_10, C_MUTED);

        MakeLabel(c1, "Suhu", 164, 4, 52, 14, FONT_10, C_MUTED);
        mon_temp_lbl_ = MakeCenteredLabel(c1, "-- C", 154, 20, 62, 18, FONT_14, C_MINT);

        // Card 2: SRAM (384KB)
        lv_obj_t* c2 = MakePanel(root, 8, 100, 224, 54, C_PANEL, C_BORDER, 8);
        MakeLabel(c2, "Internal SRAM (384 KB)", 8, 4, 208, 14, FONT_10, C_TEXT);
        mon_sram_bar_ = lv_bar_create(c2);
        lv_obj_set_pos(mon_sram_bar_, 8, 22);
        lv_obj_set_size(mon_sram_bar_, 208, 8);
        lv_obj_set_style_bg_color(mon_sram_bar_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(mon_sram_bar_, C_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_radius(mon_sram_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(mon_sram_bar_, 4, LV_PART_INDICATOR);
        mon_sram_lbl_ = MakeLabel(c2, "-- KB Sisa", 8, 34, 208, 14, FONT_10, C_MUTED);

        // Card 3: PSRAM (8 MB)
        lv_obj_t* c3 = MakePanel(root, 8, 158, 224, 54, C_PANEL, C_BORDER, 8);
        MakeLabel(c3, "Memori Eksternal PSRAM (8 MB)", 8, 4, 208, 14, FONT_10, C_VIOLET);
        mon_psram_bar_ = lv_bar_create(c3);
        lv_obj_set_pos(mon_psram_bar_, 8, 22);
        lv_obj_set_size(mon_psram_bar_, 208, 8);
        lv_obj_set_style_bg_color(mon_psram_bar_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(mon_psram_bar_, C_VIOLET, LV_PART_INDICATOR);
        lv_obj_set_style_radius(mon_psram_bar_, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(mon_psram_bar_, 4, LV_PART_INDICATOR);
        mon_psram_lbl_ = MakeLabel(c3, "-- MB Sisa", 8, 34, 208, 14, FONT_10, C_MUTED);

        // Card 4: Wi-Fi STA
        lv_obj_t* c4 = MakePanel(root, 8, 216, 224, 66, C_PANEL, C_BORDER, 8);
        MakeLabel(c4, "Status Jaringan Wi-Fi", 8, 4, 208, 14, FONT_10, C_AMBER);
        mon_wifi_lbl_ = MakeLabel(c4, "Memeriksa koneksi...", 8, 22, 208, 38, FONT_10, C_MUTED);
        lv_label_set_long_mode(mon_wifi_lbl_, LV_LABEL_LONG_WRAP);
    }

    MonitorTick();

    if (monitor_timer_) {
        lv_timer_del(monitor_timer_);
        monitor_timer_ = nullptr;
    }
    monitor_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
        if (self) self->MonitorTick();
    }, 1000, this);

    MakeBottomNav(root, PAGE_APPS);
}





// ============================================================================
// CHRONOS INTEGRATION & NATIVE NOVA OS BLUETOOTH FEATURES
// ============================================================================
