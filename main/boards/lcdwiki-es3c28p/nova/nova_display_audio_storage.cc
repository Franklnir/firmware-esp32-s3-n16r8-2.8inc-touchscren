#include "nova_display_internal.h"

void NovaDisplay::BuildAudio() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "AUDIO & SUARA");

    int vol = callbacks_.get_volume ? callbacks_.get_volume() : 75;
    if (vol < 0)
        vol = 0;
    if (vol > 100)
        vol = 100;

    bool snd_enabled = IsNotifSoundEnabled();
    int cur_snd_idx = GetNotifSoundIndex();
    static const char* kSoundNames[6] = {"Chime Bell",   "Ding Dong",  "Whistle",
                                         "Crystal Drop", "Bubble Pop", "Radar Pulse"};

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 32);
        lv_obj_set_size(list, 320, height_ - 62);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 40, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        lv_obj_add_event_cb(
            list,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->audio_scroll_y_ = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);

        // Left Column (x=8, y=6, w=148, h=156): Volume Card
        lv_obj_t* card = MakePanel(list, 8, 6, 148, 156, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_AUDIO, 8, 8, C_AMBER, 20);
        MakeLabel(card, "Volume", 32, 8, 54, 16, FONT_12, C_TEXT);

        char vol_str[16];
        snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
        audio_volume_label_ =
            MakeLabel(card, vol_str, 88, 6, 50, 18, FONT_14, C_CYAN, LV_TEXT_ALIGN_RIGHT);

        audio_volume_slider_ = lv_slider_create(card);
        lv_obj_set_pos(audio_volume_slider_, 8, 32);
        lv_obj_set_size(audio_volume_slider_, 132, 14);
        lv_slider_set_range(audio_volume_slider_, 0, 100);
        lv_slider_set_value(audio_volume_slider_, vol, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(audio_volume_slider_, 7, LV_PART_MAIN);
        lv_obj_set_style_border_width(audio_volume_slider_, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(audio_volume_slider_, 7, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_TEXT, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(audio_volume_slider_, 4, LV_PART_KNOB);

        lv_obj_add_event_cb(
            audio_volume_slider_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->audio_volume_slider_)
                    return;
                int val = lv_slider_get_value(self->audio_volume_slider_);
                char txt[16];
                snprintf(txt, sizeof(txt), "%d%%", val);
                if (self->audio_volume_label_)
                    lv_label_set_text(self->audio_volume_label_, txt);
                if (self->callbacks_.set_volume) {
                    self->callbacks_.set_volume(val);
                }
                if (val > 0 && self->muted_) {
                    self->muted_ = false;
                    Settings prefs("nova", true);
                    prefs.SetInt("mute", 0);
                }
                if (self->top_mute_icon_) {
                    lv_label_set_text(self->top_mute_icon_, (val == 0)
                                                                ? MATERIAL_SYMBOLS_VOLUME_OFF
                                                                : MATERIAL_SYMBOLS_VOLUME_UP);
                    lv_obj_set_style_text_color(self->top_mute_icon_,
                                                (val == 0) ? C_MUTED : lv_color_hex(0x06B6D4),
                                                LV_PART_MAIN);
                }
            },
            LV_EVENT_VALUE_CHANGED, this);

        // Presets in 2x2 grid
        static const int presets[] = {25, 50, 75, 100};
        for (int i = 0; i < 4; ++i) {
            int p_val = presets[i];
            char p_txt[12];
            snprintf(p_txt, sizeof(p_txt), "%d%%", p_val);
            lv_obj_t* p_btn = lv_btn_create(card);
            int bx = (i % 2 == 0) ? 8 : 76;
            int by = (i < 2) ? 58 : 90;
            lv_obj_set_pos(p_btn, bx, by);
            lv_obj_set_size(p_btn, 60, 26);
            bool is_match = (vol == p_val);
            StyleButton(p_btn, is_match ? C_PANEL_2 : C_PANEL, is_match ? C_CYAN : C_BORDER, 6);
            lv_obj_t* lbl = lv_label_create(p_btn);
            lv_label_set_text(lbl, p_txt);
            lv_obj_set_style_text_font(lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, is_match ? C_CYAN : C_TEXT, LV_PART_MAIN);
            lv_obj_center(lbl);

            lv_obj_set_user_data(p_btn, (void*)(intptr_t)p_val);
            lv_obj_add_event_cb(
                p_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!b)
                        b = (lv_obj_t*)lv_event_get_target(e);
                    int val = (int)(intptr_t)lv_obj_get_user_data(b);
                    if (self->callbacks_.set_volume) {
                        self->callbacks_.set_volume(val);
                    }
                    if (val > 0 && self->muted_) {
                        self->muted_ = false;
                        Settings prefs("nova", true);
                        prefs.SetInt("mute", 0);
                    }
                    self->BuildCurrentPage();
                },
                LV_EVENT_CLICKED, this);
        }
        MakeLabel(card, "ES8311 + SC8002B", 8, 126, 132, 16, FONT_10, C_MUTED);

        // Right Column (x=164, w=148): Mute, Haptic, Notif Sound Toggle
        lv_obj_t* mute = lv_btn_create(list);
        lv_obj_set_pos(mute, 164, 6);
        lv_obj_set_size(mute, 148, 48);
        StyleButton(mute, C_PANEL, muted_ ? C_AMBER : C_BORDER, 8);
        MakeIcon(mute, ICON_AUDIO, 8, 14, muted_ ? C_AMBER : C_CYAN, 20);
        MakeLabel(mute, "Bisukan Speaker", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(mute, muted_ ? "MUTE" : "Suara Aktif", 34, 24, 80, 14, FONT_10,
                  muted_ ? C_AMBER : C_MUTED);
        MakeLabel(mute, muted_ ? "ON" : "OFF", 116, 14, 26, 16, FONT_10, muted_ ? C_AMBER : C_MINT,
                  LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(mute, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        bool haptic = callbacks_.get_haptic ? callbacks_.get_haptic() : true;
        lv_obj_t* haptic_btn = lv_btn_create(list);
        lv_obj_set_pos(haptic_btn, 164, 60);
        lv_obj_set_size(haptic_btn, 148, 48);
        StyleButton(haptic_btn, C_PANEL, haptic ? C_MINT : C_BORDER, 8);
        MakeIcon(haptic_btn, ICON_CHIP, 8, 14, haptic ? C_MINT : C_MUTED, 20);
        MakeLabel(haptic_btn, "Suara Haptik", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(haptic_btn, haptic ? "Klik Sentuh" : "Dimatikan", 34, 24, 80, 14, FONT_10,
                  C_MUTED);
        MakeLabel(haptic_btn, haptic ? "ON" : "OFF", 116, 14, 26, 16, FONT_10,
                  haptic ? C_MINT : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(
            haptic_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.set_haptic && self->callbacks_.get_haptic) {
                    bool cur = self->callbacks_.get_haptic();
                    self->callbacks_.set_haptic(!cur);
                    self->BuildCurrentPage();
                }
            },
            LV_EVENT_CLICKED, this);

        lv_obj_t* notif_tgl = lv_btn_create(list);
        lv_obj_set_pos(notif_tgl, 164, 114);
        lv_obj_set_size(notif_tgl, 148, 48);
        StyleButton(notif_tgl, C_PANEL, snd_enabled ? C_CYAN : C_BORDER, 8);
        MakeIcon(notif_tgl, ICON_CHAT, 8, 14, snd_enabled ? C_CYAN : C_MUTED, 20);
        MakeLabel(notif_tgl, "Suara Notif HP", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(notif_tgl, snd_enabled ? "Bunyi Aktif" : "Senyap", 34, 24, 80, 14, FONT_10,
                  snd_enabled ? C_CYAN : C_MUTED);
        MakeLabel(notif_tgl, snd_enabled ? "ON" : "OFF", 116, 14, 26, 16, FONT_10,
                  snd_enabled ? C_CYAN : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(
            notif_tgl,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    bool en = self->IsNotifSoundEnabled();
                    self->SetNotifSoundEnabled(!en);
                    if (!en)
                        self->PreviewNotifSound(self->GetNotifSoundIndex());
                    self->BuildCurrentPage();
                }
            },
            LV_EVENT_CLICKED, this);

        // Section: Pilihan Suara Notifikasi
        MakeLabel(list, "PILIHAN NADA SUARA NOTIFIKASI", 10, 172, 300, 16, FONT_10, C_MUTED);

        for (int i = 0; i < 6; ++i) {
            int col = i % 3;
            int row = i / 3;
            int bx = 8 + col * 102;
            int by = 192 + row * 34;
            bool is_cur = (cur_snd_idx == i);
            lv_obj_t* s_btn = lv_btn_create(list);
            lv_obj_set_pos(s_btn, bx, by);
            lv_obj_set_size(s_btn, 98, 28);
            StyleButton(s_btn, is_cur ? C_PANEL_2 : C_PANEL, is_cur ? C_CYAN : C_BORDER, 6);
            if (is_cur)
                lv_obj_set_style_border_width(s_btn, 2, 0);

            lv_obj_t* lbl = lv_label_create(s_btn);
            lv_label_set_text(lbl, kSoundNames[i]);
            lv_obj_set_style_text_font(lbl, FONT_10, 0);
            lv_obj_set_style_text_color(lbl, is_cur ? C_CYAN : C_TEXT, 0);
            lv_obj_center(lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                s_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!b)
                        b = (lv_obj_t*)lv_event_get_target(e);
                    int idx = (int)(intptr_t)lv_obj_get_user_data(b);
                    self->SetNotifSoundIndex(idx);
                    self->PreviewNotifSound(idx);
                    self->BuildCurrentPage();
                },
                LV_EVENT_CLICKED, this);
        }

        // Preview Button
        lv_obj_t* prev_btn = lv_btn_create(list);
        lv_obj_set_pos(prev_btn, 8, 268);
        lv_obj_set_size(prev_btn, 304, 30);
        StyleButton(prev_btn, C_PANEL_2, C_CYAN, 8);
        lv_obj_t* p_lbl = lv_label_create(prev_btn);
        char p_str[64];
        snprintf(p_str, sizeof(p_str), "▶ PREVIEW SUARA: %s", kSoundNames[cur_snd_idx % 6]);
        lv_label_set_text(p_lbl, p_str);
        lv_obj_set_style_text_font(p_lbl, FONT_10, 0);
        lv_obj_set_style_text_color(p_lbl, C_CYAN, 0);
        lv_obj_center(p_lbl);
        lv_obj_add_event_cb(
            prev_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->PreviewNotifSound(self->GetNotifSoundIndex());
            },
            LV_EVENT_CLICKED, this);

        MakeLabel(list, "Amplifier SC8002B onboard berdaya 1W.", 10, 306, 300, 16, FONT_10,
                  C_MUTED);

        if (audio_scroll_y_ > 0)
            lv_obj_scroll_to_y(list, audio_scroll_y_, LV_ANIM_OFF);

    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 60, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        lv_obj_add_event_cb(
            list,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->audio_scroll_y_ = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
            },
            LV_EVENT_SCROLL, this);

        // 1. Volume Card (y=6, h=98)
        lv_obj_t* card = MakePanel(list, 12, 6, 216, 98, C_PANEL, C_BORDER, 12);
        MakeIcon(card, ICON_AUDIO, 10, 10, C_AMBER, 22);
        MakeLabel(card, "Volume Speaker", 38, 10, 110, 18, FONT_14, C_TEXT);

        char vol_str[16];
        snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
        audio_volume_label_ =
            MakeLabel(card, vol_str, 150, 8, 54, 20, FONT_16, C_CYAN, LV_TEXT_ALIGN_RIGHT);

        // Volume Slider
        audio_volume_slider_ = lv_slider_create(card);
        lv_obj_set_pos(audio_volume_slider_, 10, 36);
        lv_obj_set_size(audio_volume_slider_, 196, 14);
        lv_slider_set_range(audio_volume_slider_, 0, 100);
        lv_slider_set_value(audio_volume_slider_, vol, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_PANEL_2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(audio_volume_slider_, 7, LV_PART_MAIN);
        lv_obj_set_style_border_width(audio_volume_slider_, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(audio_volume_slider_, 7, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(audio_volume_slider_, C_TEXT, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(audio_volume_slider_, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(audio_volume_slider_, 4, LV_PART_KNOB);

        lv_obj_add_event_cb(
            audio_volume_slider_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->audio_volume_slider_)
                    return;
                int val = lv_slider_get_value(self->audio_volume_slider_);
                char txt[16];
                snprintf(txt, sizeof(txt), "%d%%", val);
                if (self->audio_volume_label_)
                    lv_label_set_text(self->audio_volume_label_, txt);
                if (self->callbacks_.set_volume) {
                    self->callbacks_.set_volume(val);
                }
                if (val > 0 && self->muted_) {
                    self->muted_ = false;
                    Settings prefs("nova", true);
                    prefs.SetInt("mute", 0);
                }
                if (self->top_mute_icon_) {
                    lv_label_set_text(self->top_mute_icon_, (val == 0)
                                                                ? MATERIAL_SYMBOLS_VOLUME_OFF
                                                                : MATERIAL_SYMBOLS_VOLUME_UP);
                    lv_obj_set_style_text_color(self->top_mute_icon_,
                                                (val == 0) ? C_MUTED : lv_color_hex(0x06B6D4),
                                                LV_PART_MAIN);
                }
            },
            LV_EVENT_VALUE_CHANGED, this);

        // Presets: 25%, 50%, 75%, 100%
        static const int presets[] = {25, 50, 75, 100};
        for (int i = 0; i < 4; ++i) {
            int p_val = presets[i];
            char p_txt[12];
            snprintf(p_txt, sizeof(p_txt), "%d%%", p_val);
            lv_obj_t* p_btn = lv_btn_create(card);
            lv_obj_set_pos(p_btn, 10 + i * 50, 60);
            lv_obj_set_size(p_btn, 44, 26);
            bool is_match = (vol == p_val);
            StyleButton(p_btn, is_match ? C_PANEL_2 : C_PANEL, is_match ? C_CYAN : C_BORDER, 6);
            lv_obj_t* lbl = lv_label_create(p_btn);
            lv_label_set_text(lbl, p_txt);
            lv_obj_set_style_text_font(lbl, FONT_12, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, is_match ? C_CYAN : C_TEXT, LV_PART_MAIN);
            lv_obj_center(lbl);

            lv_obj_set_user_data(p_btn, (void*)(intptr_t)p_val);
            lv_obj_add_event_cb(
                p_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!b)
                        b = (lv_obj_t*)lv_event_get_target(e);
                    int val = (int)(intptr_t)lv_obj_get_user_data(b);
                    if (self->callbacks_.set_volume) {
                        self->callbacks_.set_volume(val);
                    }
                    if (val > 0 && self->muted_) {
                        self->muted_ = false;
                        Settings prefs("nova", true);
                        prefs.SetInt("mute", 0);
                    }
                    self->BuildCurrentPage();
                },
                LV_EVENT_CLICKED, this);
        }

        // Section: Kontrol Suara
        MakeLabel(list, "KONTROL SUARA", 14, 110, 212, 14, FONT_10, C_MUTED);

        // Mute Button
        lv_obj_t* mute = lv_btn_create(list);
        lv_obj_set_pos(mute, 12, 128);
        lv_obj_set_size(mute, 216, 40);
        StyleButton(mute, C_PANEL, C_BORDER, 10);
        MakeIcon(mute, ICON_AUDIO, 12, 8, muted_ ? C_MUTED : C_AMBER, 22);
        MakeLabel(mute, "Bisukan speaker", 44, 4, 120, 18, FONT_14, C_TEXT);
        MakeLabel(mute, muted_ ? "MUTE (Senyap)" : "AKTIF", 44, 22, 120, 14, FONT_10,
                  muted_ ? C_AMBER : C_MUTED);
        MakeLabel(mute, muted_ ? "ON" : "OFF", 169, 11, 34, 18, FONT_14, muted_ ? C_AMBER : C_CYAN,
                  LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(mute, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        // Haptic Button
        bool haptic = callbacks_.get_haptic ? callbacks_.get_haptic() : true;
        lv_obj_t* haptic_btn = lv_btn_create(list);
        lv_obj_set_pos(haptic_btn, 12, 174);
        lv_obj_set_size(haptic_btn, 216, 40);
        StyleButton(haptic_btn, C_PANEL, C_BORDER, 10);
        MakeIcon(haptic_btn, ICON_CHIP, 12, 8, haptic ? C_MINT : C_MUTED, 22);
        MakeLabel(haptic_btn, "Suara haptik sentuh", 44, 4, 130, 18, FONT_14, C_TEXT);
        MakeLabel(haptic_btn, haptic ? "Klik speaker saat disentuh" : "Haptik dimatikan", 44, 22,
                  130, 14, FONT_10, C_MUTED);
        MakeLabel(haptic_btn, haptic ? "ON" : "OFF", 169, 11, 34, 18, FONT_14,
                  haptic ? C_MINT : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(
            haptic_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.set_haptic && self->callbacks_.get_haptic) {
                    bool cur = self->callbacks_.get_haptic();
                    self->callbacks_.set_haptic(!cur);
                    self->BuildCurrentPage();
                }
            },
            LV_EVENT_CLICKED, this);

        // Section: Suara Notifikasi HP
        MakeLabel(list, "SUARA NOTIFIKASI HP", 14, 222, 212, 14, FONT_10, C_MUTED);

        // Notification Sound ON/OFF Toggle
        lv_obj_t* notif_tgl = lv_btn_create(list);
        lv_obj_set_pos(notif_tgl, 12, 238);
        lv_obj_set_size(notif_tgl, 216, 40);
        StyleButton(notif_tgl, C_PANEL, snd_enabled ? C_CYAN : C_BORDER, 10);
        MakeIcon(notif_tgl, ICON_CHAT, 12, 8, snd_enabled ? C_CYAN : C_MUTED, 22);
        MakeLabel(notif_tgl, "Bunyi notifikasi HP", 44, 4, 125, 18, FONT_14, C_TEXT);
        MakeLabel(notif_tgl, snd_enabled ? "Dering notif aktif" : "Senyap saat ada pesan", 44, 22,
                  125, 14, FONT_10, snd_enabled ? C_CYAN : C_MUTED);
        MakeLabel(notif_tgl, snd_enabled ? "ON" : "OFF", 169, 11, 34, 18, FONT_14,
                  snd_enabled ? C_CYAN : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(
            notif_tgl,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    bool en = self->IsNotifSoundEnabled();
                    self->SetNotifSoundEnabled(!en);
                    if (!en)
                        self->PreviewNotifSound(self->GetNotifSoundIndex());
                    self->BuildCurrentPage();
                }
            },
            LV_EVENT_CLICKED, this);

        // 6 Sound Choice Buttons (2 cols x 3 rows)
        for (int i = 0; i < 6; ++i) {
            int col = i % 2;
            int row = i / 2;
            int bx = 12 + col * 110;
            int by = 286 + row * 34;
            bool is_cur = (cur_snd_idx == i);

            lv_obj_t* s_btn = lv_btn_create(list);
            lv_obj_set_pos(s_btn, bx, by);
            lv_obj_set_size(s_btn, 106, 28);
            StyleButton(s_btn, is_cur ? C_PANEL_2 : C_PANEL, is_cur ? C_CYAN : C_BORDER, 6);
            if (is_cur)
                lv_obj_set_style_border_width(s_btn, 2, 0);

            lv_obj_t* lbl = lv_label_create(s_btn);
            lv_label_set_text(lbl, kSoundNames[i]);
            lv_obj_set_style_text_font(lbl, FONT_10, 0);
            lv_obj_set_style_text_color(lbl, is_cur ? C_CYAN : C_TEXT, 0);
            lv_obj_center(lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                s_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!b)
                        b = (lv_obj_t*)lv_event_get_target(e);
                    int idx = (int)(intptr_t)lv_obj_get_user_data(b);
                    self->SetNotifSoundIndex(idx);
                    self->PreviewNotifSound(idx);
                    self->BuildCurrentPage();
                },
                LV_EVENT_CLICKED, this);
        }

        // Preview Button
        lv_obj_t* prev_btn = lv_btn_create(list);
        lv_obj_set_pos(prev_btn, 12, 394);
        lv_obj_set_size(prev_btn, 216, 32);
        StyleButton(prev_btn, C_PANEL_2, C_CYAN, 8);
        lv_obj_t* p_lbl = lv_label_create(prev_btn);
        char p_str[64];
        snprintf(p_str, sizeof(p_str), "▶ PREVIEW: %s", kSoundNames[cur_snd_idx % 6]);
        lv_label_set_text(p_lbl, p_str);
        lv_obj_set_style_text_font(p_lbl, FONT_12, 0);
        lv_obj_set_style_text_color(p_lbl, C_CYAN, 0);
        lv_obj_center(p_lbl);
        lv_obj_add_event_cb(
            prev_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->PreviewNotifSound(self->GetNotifSoundIndex());
            },
            LV_EVENT_CLICKED, this);

        MakeLabel(list, "Amplifier SC8002B onboard berdaya 1W.", 14, 434, 212, 16, FONT_10,
                  C_MUTED);

        if (audio_scroll_y_ > 0)
            lv_obj_scroll_to_y(list, audio_scroll_y_, LV_ANIM_OFF);
    }

    MakeBottomNav(root, PAGE_AUDIO);
}

void NovaDisplay::BuildFiles() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "FILE HUB & STORAGE");

    SdCardStatus sd;
    bool has_sd = false;
    if (callbacks_.get_sd_status) {
        sd = callbacks_.get_sd_status();
        has_sd = sd.mounted;
    }

    // Pisahkan berkas Musik (/sdcard/music) dan Berkas lainnya
    std::vector<std::pair<std::string, size_t>> music_files;
    std::vector<std::pair<std::string, size_t>> root_files;
    if (has_sd && sd.mounted) {
        for (const auto& f : sd.files) {
            std::string ext = "";
            size_t dot_pos = f.first.rfind('.');
            if (dot_pos != std::string::npos) {
                ext = f.first.substr(dot_pos);
                for (auto& c : ext)
                    c = tolower(c);
            }
            if (f.first.find("music/") == 0 || ext == ".wav" || ext == ".mp3" || ext == ".pcm" ||
                ext == ".m4a") {
                music_files.push_back(f);
            } else {
                root_files.push_back(f);
            }
        }
    }

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN LAYOUT (320x240 - 100% Fit On 1 Screen - No Scroll) ===
        if (has_sd && sd.mounted) {
            // Left Column (x=8, y=38, w=148, h=166): MicroSD Status & Tabs
            lv_obj_t* card = MakePanel(root, 8, 38, 148, 166, C_PANEL, C_BORDER, 10);
            MakeIcon(card, ICON_FILES, 8, 8, C_MINT, 20);
            MakeLabel(card, sd.card_name[0] ? sd.card_name : "MicroSD Card", 32, 6, 110, 18,
                      FONT_12, C_TEXT);
            MakeLabel(card, "SDIO 4-Bit Bus", 32, 22, 110, 14, FONT_10, C_MINT);

            uint64_t used_mb = (sd.total_mb > sd.free_mb) ? (sd.total_mb - sd.free_mb) : 0;
            int pct = (sd.total_mb > 0) ? (int)((used_mb * 100ULL) / sd.total_mb) : 0;
            if (pct > 100)
                pct = 100;

            lv_obj_t* bar_bg = MakePanel(card, 8, 42, 132, 6, C_PANEL_2, C_BORDER, 3);
            int fill_w = (132 * pct) / 100;
            if (fill_w < 3 && pct > 0)
                fill_w = 3;
            IconShape(bar_bg, 0, 0, fill_w, 6, C_CYAN, 3, true);

            char cap_buf[48];
            double total_gb = (double)sd.total_mb / 1024.0;
            double free_gb = (double)sd.free_mb / 1024.0;
            if (total_gb >= 1.0) {
                snprintf(cap_buf, sizeof(cap_buf), "Total: %.1f GB | Sisa: %.1f GB", total_gb,
                         free_gb);
            } else {
                snprintf(cap_buf, sizeof(cap_buf), "Total: %llu MB | Sisa: %llu MB",
                         (unsigned long long)sd.total_mb, (unsigned long long)sd.free_mb);
            }
            MakeLabel(card, cap_buf, 8, 52, 132, 14, FONT_10, C_MUTED);

            char file_count_str[48];
            snprintf(file_count_str, sizeof(file_count_str), "%u Berkas Terindeks",
                     (unsigned)sd.files.size());
            MakeLabel(card, file_count_str, 8, 68, 132, 14, FONT_10, C_CYAN);

            MakeButton(
                card, "SEGARKAN", 8, 86, 64, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            MakeButton(
                card, "FORMAT", 76, 86, 64, 24, lv_color_hex(0x27141A), lv_color_hex(0xF43F5E),
                lv_color_hex(0xF43F5E), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            char music_tab_str[32];
            snprintf(music_tab_str, sizeof(music_tab_str), "MUSIK (%u)",
                     (unsigned)music_files.size());
            MakeButton(
                card, music_tab_str, 8, 118, 64, 34,
                (file_hub_filter_ == 1) ? lv_color_hex(0x13382C) : C_PANEL_2,
                (file_hub_filter_ == 1) ? C_MINT : C_BORDER,
                (file_hub_filter_ == 1) ? C_MINT : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 1;
                        self->BuildCurrentPage();
                    }
                },
                this);

            char all_tab_str[32];
            snprintf(all_tab_str, sizeof(all_tab_str), "SEMUA (%u)", (unsigned)sd.files.size());
            MakeButton(
                card, all_tab_str, 76, 118, 64, 34,
                (file_hub_filter_ == 0) ? lv_color_hex(0x132C38) : C_PANEL_2,
                (file_hub_filter_ == 0) ? C_CYAN : C_BORDER,
                (file_hub_filter_ == 0) ? C_CYAN : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 0;
                        self->BuildCurrentPage();
                    }
                },
                this);

            // Right Column (x=160, y=38, w=152, h=166): Dedicated Scrollable File List
            lv_obj_t* list = lv_obj_create(root);
            lv_obj_set_pos(list, 160, 38);
            lv_obj_set_size(list, 152, 166);
            lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(list, 36, LV_PART_MAIN);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);

            if (file_hub_filter_ == 1) {
                if (music_files.empty()) {
                    lv_obj_t* empty_box = MakePanel(list, 0, 4, 144, 60, C_PANEL, C_BORDER, 8);
                    MakeIcon(empty_box, ICON_AUDIO, 6, 16, C_MINT, 20);
                    MakeLabel(empty_box, "Folder Kosong", 32, 10, 106, 16, FONT_12, C_MINT);
                    MakeLabel(empty_box, "Belum ada lagu", 32, 30, 106, 16, FONT_10, C_MUTED);
                } else {
                    for (size_t i = 0; i < music_files.size() && i < 30; ++i) {
                        std::string full_name = music_files[i].first;
                        std::string clean_title = full_name;
                        if (clean_title.find("music/") == 0)
                            clean_title = clean_title.substr(6);

                        lv_obj_t* frow = lv_btn_create(list);
                        lv_obj_set_pos(frow, 0, (int)i * 36);
                        lv_obj_set_size(frow, 144, 32);
                        StyleButton(frow, C_PANEL, C_BORDER, 6);

                        lv_obj_t* icn = lv_label_create(frow);
                        lv_obj_set_style_text_font(icn, FONT_ICON, LV_PART_MAIN);
                        lv_label_set_text(icn, MATERIAL_SYMBOLS_MUSIC_NOTE);
                        lv_obj_set_style_text_color(icn, C_MINT, LV_PART_MAIN);
                        lv_obj_set_pos(icn, 6, 7);

                        char item_str[64];
                        if (music_files[i].second >= 1024 * 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%.1fMB)", clean_title.c_str(),
                                     (double)music_files[i].second / (1024.0 * 1024.0));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%uKB)", clean_title.c_str(),
                                     (unsigned)(music_files[i].second / 1024));
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 26, 7, 112, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx {
                            NovaDisplay* self;
                            std::string fname;
                            size_t size;
                        };
                        auto* ctx = new FileClickCtx{this, full_name, music_files[i].second};
                        lv_obj_add_event_cb(
                            frow,
                            [](lv_event_t* e) {
                                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                                    return;
                                auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                                if (c && c->self)
                                    c->self->ShowFileModal(c->fname, c->size);
                            },
                            LV_EVENT_CLICKED, ctx);
                    }
                }
            } else {
                const auto& active_list = root_files.empty() ? sd.files : root_files;
                if (active_list.empty()) {
                    lv_obj_t* empty_box = MakePanel(list, 0, 4, 144, 60, C_PANEL, C_BORDER, 8);
                    MakeLabel(empty_box, "Berkas Kosong", 8, 18, 128, 20, FONT_10, C_MUTED,
                              LV_TEXT_ALIGN_CENTER);
                } else {
                    for (size_t i = 0; i < active_list.size() && i < 30; ++i) {
                        std::string fname = active_list[i].first;
                        std::string ext = "";
                        size_t dot_pos = fname.rfind('.');
                        if (dot_pos != std::string::npos) {
                            ext = fname.substr(dot_pos);
                            for (auto& c : ext)
                                c = tolower(c);
                        }

                        const char* f_icon = MATERIAL_SYMBOLS_EDIT_SQUARE;
                        lv_color_t f_color = C_CYAN;
                        if (ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                            f_icon = MATERIAL_SYMBOLS_IMAGE;
                            f_color = C_AMBER;
                        } else if (ext == ".wav" || ext == ".mp3" || ext == ".pcm" ||
                                   ext == ".m4a") {
                            f_icon = MATERIAL_SYMBOLS_MUSIC_NOTE;
                            f_color = C_MINT;
                        }

                        lv_obj_t* frow = lv_btn_create(list);
                        lv_obj_set_pos(frow, 0, (int)i * 36);
                        lv_obj_set_size(frow, 144, 32);
                        StyleButton(frow, C_PANEL, C_BORDER, 6);

                        lv_obj_t* icn = lv_label_create(frow);
                        lv_obj_set_style_text_font(icn, FONT_ICON, LV_PART_MAIN);
                        lv_label_set_text(icn, f_icon);
                        lv_obj_set_style_text_color(icn, f_color, LV_PART_MAIN);
                        lv_obj_set_pos(icn, 6, 7);

                        char item_str[64];
                        if (active_list[i].second >= 1024 * 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%.1fMB)", fname.c_str(),
                                     (double)active_list[i].second / (1024.0 * 1024.0));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%uKB)", fname.c_str(),
                                     (unsigned)(active_list[i].second / 1024));
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 26, 7, 112, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx {
                            NovaDisplay* self;
                            std::string fname;
                            size_t size;
                        };
                        auto* ctx = new FileClickCtx{this, fname, active_list[i].second};
                        lv_obj_add_event_cb(
                            frow,
                            [](lv_event_t* e) {
                                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                                    return;
                                auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                                if (c && c->self)
                                    c->self->ShowFileModal(c->fname, c->size);
                            },
                            LV_EVENT_CLICKED, ctx);
                    }
                }
            }
        } else {
            // SD Card Not Mounted: Left Card for Slot & Actions, Right Card for Internal Flash
            lv_obj_t* card = MakePanel(root, 8, 38, 148, 166, C_PANEL, C_BORDER, 10);
            MakeIcon(card, ICON_FILES, 8, 8, C_AMBER, 22);
            MakeLabel(card, "Slot MicroSD SDIO", 34, 6, 108, 18, FONT_12, C_TEXT);
            MakeLabel(card, "Status: Belum Pasang", 34, 24, 108, 14, FONT_10, C_AMBER);
            MakeLabel(card, "SDIO 4-Bit (IO 38-41,47-48)\nPastikan kartu terpasang rapi.", 8, 44,
                      132, 28, FONT_10, C_MUTED);

            MakeButton(
                card, "DETEKSI & PASANG", 8, 80, 132, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            MakeButton(
                card, "FORMAT FAT32", 8, 116, 132, 28, lv_color_hex(0xF43F5E),
                lv_color_hex(0xBE123C), lv_color_white(), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            // Right Column: Flash Memory
            lv_obj_t* iflash = MakePanel(root, 164, 38, 148, 166, C_PANEL, C_BORDER, 10);
            MakeIcon(iflash, ICON_CHIP, 8, 8, C_VIOLET, 22);
            MakeLabel(iflash, "Flash 16 MB QSPI", 34, 6, 108, 18, FONT_12, C_TEXT);
            MakeLabel(iflash, "Firmware: 4 MB (OTA)", 8, 32, 132, 16, FONT_10, C_MUTED);
            MakeLabel(iflash, "Aset Audio: 8 MB", 8, 52, 132, 16, FONT_10, C_MUTED);
            MakeLabel(iflash, "NVS Config: Terpasang", 8, 72, 132, 16, FONT_10, C_MINT);
            MakeLabel(iflash, "Partisi Penyimpanan Aman", 8, 96, 132, 28, FONT_10, C_CYAN);
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 8, 36);
        lv_obj_set_size(list, 224, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 52, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        if (has_sd && sd.mounted) {
            lv_obj_t* card = MakePanel(list, 4, 4, 208, 126, C_PANEL, C_BORDER, 10);
            MakeIcon(card, ICON_FILES, 8, 8, C_MINT, 24);
            MakeLabel(card, sd.card_name[0] ? sd.card_name : "MicroSD Card", 40, 6, 160, 18,
                      FONT_14, C_TEXT);
            MakeLabel(card, "SDIO 4-Bit Bus (38-41, 47, 48)", 40, 24, 160, 14, FONT_10, C_MINT);

            uint64_t used_mb = (sd.total_mb > sd.free_mb) ? (sd.total_mb - sd.free_mb) : 0;
            int pct = (sd.total_mb > 0) ? (int)((used_mb * 100ULL) / sd.total_mb) : 0;
            if (pct > 100)
                pct = 100;

            lv_obj_t* bar_bg = MakePanel(card, 8, 48, 192, 8, C_PANEL_2, C_BORDER, 4);
            int fill_w = (192 * pct) / 100;
            if (fill_w < 4 && pct > 0)
                fill_w = 4;
            IconShape(bar_bg, 0, 0, fill_w, 8, C_CYAN, 4, true);

            char cap_buf[64];
            double total_gb = (double)sd.total_mb / 1024.0;
            double free_gb = (double)sd.free_mb / 1024.0;
            if (total_gb >= 1.0) {
                snprintf(cap_buf, sizeof(cap_buf), "Kapasitas: %.2f GB (Bebas: %.2f GB)", total_gb,
                         free_gb);
            } else {
                snprintf(cap_buf, sizeof(cap_buf), "Kapasitas: %llu MB (Bebas: %llu MB)",
                         (unsigned long long)sd.total_mb, (unsigned long long)sd.free_mb);
            }
            MakeLabel(card, cap_buf, 8, 62, 192, 16, FONT_10, C_MUTED);

            char file_count_str[64];
            snprintf(file_count_str, sizeof(file_count_str),
                     "Status: %u berkas terindeks di /sdcard", (unsigned)sd.files.size());
            MakeLabel(card, file_count_str, 8, 78, 192, 16, FONT_10, C_CYAN);

            MakeButton(
                card, "SEGARKAN", 8, 96, 92, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            MakeButton(
                card, "FORMAT FAT32", 108, 96, 92, 24, lv_color_hex(0x27141A),
                lv_color_hex(0xF43F5E), lv_color_hex(0xF43F5E), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            int tab_y = 132;
            char music_tab_str[48];
            snprintf(music_tab_str, sizeof(music_tab_str), "FOLDER MUSIK (%u)",
                     (unsigned)music_files.size());
            MakeButton(
                list, music_tab_str, 4, tab_y, 102, 28,
                (file_hub_filter_ == 1) ? lv_color_hex(0x13382C) : C_PANEL_2,
                (file_hub_filter_ == 1) ? C_MINT : C_BORDER,
                (file_hub_filter_ == 1) ? C_MINT : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 1;
                        self->BuildCurrentPage();
                    }
                },
                this);

            char all_tab_str[48];
            snprintf(all_tab_str, sizeof(all_tab_str), "SEMUA BERKAS (%u)",
                     (unsigned)sd.files.size());
            MakeButton(
                list, all_tab_str, 110, tab_y, 102, 28,
                (file_hub_filter_ == 0) ? lv_color_hex(0x132C38) : C_PANEL_2,
                (file_hub_filter_ == 0) ? C_CYAN : C_BORDER,
                (file_hub_filter_ == 0) ? C_CYAN : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 0;
                        self->BuildCurrentPage();
                    }
                },
                this);

            int start_y = tab_y + 34;

            if (file_hub_filter_ == 1) {
                MakeLabel(list, "KOLEKSI LAGU DI /sdcard/music (Ketuk untuk Putar)", 8, start_y,
                          208, 16, FONT_10, C_MINT);
                start_y += 18;

                if (music_files.empty()) {
                    lv_obj_t* empty_box =
                        MakePanel(list, 4, start_y, 208, 56, C_PANEL, C_BORDER, 8);
                    MakeIcon(empty_box, ICON_AUDIO, 8, 14, C_MINT, 24);
                    MakeLabel(empty_box, "Folder Musik Kosong", 38, 8, 160, 16, FONT_12, C_MINT);
                    MakeLabel(empty_box, "Minta XiaoZhi unduh lagu YouTube!", 38, 28, 160, 18,
                              FONT_10, C_MUTED);
                    start_y += 62;
                } else {
                    for (size_t i = 0; i < music_files.size() && i < 30; ++i) {
                        std::string full_name = music_files[i].first;
                        std::string clean_title = full_name;
                        if (clean_title.find("music/") == 0)
                            clean_title = clean_title.substr(6);

                        lv_obj_t* frow = lv_btn_create(list);
                        lv_obj_set_pos(frow, 4, start_y);
                        lv_obj_set_size(frow, 208, 32);
                        StyleButton(frow, C_PANEL, C_BORDER, 6);

                        lv_obj_t* icn = lv_label_create(frow);
                        lv_obj_set_style_text_font(icn, FONT_ICON, LV_PART_MAIN);
                        lv_label_set_text(icn, MATERIAL_SYMBOLS_MUSIC_NOTE);
                        lv_obj_set_style_text_color(icn, C_MINT, LV_PART_MAIN);
                        lv_obj_set_pos(icn, 6, 7);

                        char item_str[64];
                        if (music_files[i].second >= 1024 * 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%.1f MB)",
                                     clean_title.c_str(),
                                     (double)music_files[i].second / (1024.0 * 1024.0));
                        } else if (music_files[i].second >= 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%u KB)", clean_title.c_str(),
                                     (unsigned)(music_files[i].second / 1024));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%u B)", clean_title.c_str(),
                                     (unsigned)music_files[i].second);
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 28, 7, 172, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx {
                            NovaDisplay* self;
                            std::string fname;
                            size_t size;
                        };
                        auto* ctx = new FileClickCtx{this, full_name, music_files[i].second};
                        lv_obj_add_event_cb(
                            frow,
                            [](lv_event_t* e) {
                                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                                    return;
                                auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                                if (c && c->self)
                                    c->self->ShowFileModal(c->fname, c->size);
                            },
                            LV_EVENT_CLICKED, ctx);

                        start_y += 36;
                    }
                }
            } else {
                MakeLabel(list, "DAFTAR SEMUA BERKAS /sdcard (Ketuk Buka)", 8, start_y, 208, 16,
                          FONT_10, C_CYAN);
                start_y += 18;

                if (music_files.size() > 0) {
                    lv_obj_t* music_folder_row = lv_btn_create(list);
                    lv_obj_set_pos(music_folder_row, 4, start_y);
                    lv_obj_set_size(music_folder_row, 208, 32);
                    StyleButton(music_folder_row, lv_color_hex(0x13382C), C_MINT, 6);

                    lv_obj_t* f_icn = lv_label_create(music_folder_row);
                    lv_obj_set_style_text_font(f_icn, FONT_ICON, LV_PART_MAIN);
                    lv_label_set_text(f_icn, MATERIAL_SYMBOLS_SD_CARD);
                    lv_obj_set_style_text_color(f_icn, C_MINT, LV_PART_MAIN);
                    lv_obj_set_pos(f_icn, 6, 7);

                    char folder_title[64];
                    snprintf(folder_title, sizeof(folder_title),
                             "📁 music/ (Folder Musik - %u Lagu)", (unsigned)music_files.size());
                    lv_obj_t* f_lbl =
                        MakeLabel(music_folder_row, folder_title, 28, 7, 172, 16, FONT_10, C_MINT);
                    lv_label_set_long_mode(f_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                    lv_obj_add_event_cb(
                        music_folder_row,
                        [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                                return;
                            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                            if (self) {
                                self->file_hub_filter_ = 1;
                                self->BuildCurrentPage();
                            }
                        },
                        LV_EVENT_CLICKED, this);
                    start_y += 36;
                }

                if (root_files.empty() && music_files.empty()) {
                    lv_obj_t* empty_box =
                        MakePanel(list, 4, start_y, 208, 36, C_PANEL, C_BORDER, 8);
                    MakeLabel(empty_box, "Folder /sdcard kosong (Belum ada file).", 10, 10, 188, 16,
                              FONT_10, C_MUTED);
                    start_y += 42;
                } else {
                    for (size_t i = 0; i < root_files.size() && i < 20; ++i) {
                        std::string fname = root_files[i].first;
                        std::string ext = "";
                        size_t dot_pos = fname.rfind('.');
                        if (dot_pos != std::string::npos) {
                            ext = fname.substr(dot_pos);
                            for (auto& c : ext)
                                c = tolower(c);
                        }

                        const char* f_icon = MATERIAL_SYMBOLS_EDIT_SQUARE;
                        lv_color_t f_color = C_CYAN;
                        if (ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                            f_icon = MATERIAL_SYMBOLS_IMAGE;
                            f_color = C_AMBER;
                        }

                        lv_obj_t* frow = lv_btn_create(list);
                        lv_obj_set_pos(frow, 4, start_y);
                        lv_obj_set_size(frow, 208, 30);
                        StyleButton(frow, C_PANEL, C_BORDER, 6);

                        lv_obj_t* icn = lv_label_create(frow);
                        lv_obj_set_style_text_font(icn, FONT_ICON, LV_PART_MAIN);
                        lv_label_set_text(icn, f_icon);
                        lv_obj_set_style_text_color(icn, f_color, LV_PART_MAIN);
                        lv_obj_set_pos(icn, 6, 6);

                        char item_str[64];
                        if (root_files[i].second >= 1024 * 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%.1f MB)", fname.c_str(),
                                     (double)root_files[i].second / (1024.0 * 1024.0));
                        } else if (root_files[i].second >= 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%u KB)", fname.c_str(),
                                     (unsigned)(root_files[i].second / 1024));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%u B)", fname.c_str(),
                                     (unsigned)root_files[i].second);
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 28, 7, 172, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx {
                            NovaDisplay* self;
                            std::string fname;
                            size_t size;
                        };
                        auto* ctx = new FileClickCtx{this, fname, root_files[i].second};
                        lv_obj_add_event_cb(
                            frow,
                            [](lv_event_t* e) {
                                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                                    return;
                                auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                                if (c && c->self)
                                    c->self->ShowFileModal(c->fname, c->size);
                            },
                            LV_EVENT_CLICKED, ctx);

                        start_y += 34;
                    }
                }
            }

            MakeLabel(list, "PENYIMPANAN INTERNAL FLASH", 8, start_y + 4, 200, 16, FONT_10,
                      C_MUTED);
            lv_obj_t* iflash = MakePanel(list, 4, start_y + 22, 208, 68, C_PANEL, C_BORDER, 10);
            MakeIcon(iflash, ICON_CHIP, 8, 10, C_VIOLET, 22);
            MakeLabel(iflash, "16 MB External SPI Flash", 36, 8, 160, 18, FONT_12, C_TEXT);
            MakeLabel(iflash, "App: 4 MB | Aset Audio: 8 MB", 36, 26, 160, 15, FONT_10, C_MUTED);
            MakeLabel(iflash, "NVS Settings & Config: 64 KB", 36, 43, 160, 15, FONT_10, C_CYAN);
        } else {
            lv_obj_t* card = MakePanel(list, 4, 4, 208, 142, C_PANEL, C_BORDER, 10);
            MakeIcon(card, ICON_FILES, 8, 8, C_AMBER, 26);
            MakeLabel(card, "Slot MicroSD SDIO", 40, 6, 160, 18, FONT_14, C_TEXT);
            MakeLabel(card, "Status: Belum Terpasang", 40, 24, 160, 14, FONT_10, C_AMBER);

            MakeLabel(card,
                      "Pinout Hardware SDIO 4-Bit:\nCLK:38  CMD:40  D0:39\nD1:41  D2:48  D3:47", 10,
                      44, 188, 38, FONT_10, C_MUTED);

            MakeButton(
                card, "DETEKSI & PASANG", 8, 86, 92, 24, C_CYAN, C_CYAN, C_BG, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            MakeButton(
                card, "FORMAT FAT32", 108, 86, 92, 24, lv_color_hex(0xF43F5E),
                lv_color_hex(0xBE123C), lv_color_white(), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                },
                this);

            MakeLabel(card, "*Format menghapus isi kartu & ubah ke FAT32", 8, 114, 192, 14, FONT_10,
                      C_MUTED);

            MakeLabel(list, "PENYIMPANAN INTERNAL FLASH", 8, 154, 200, 16, FONT_10, C_CYAN);
            lv_obj_t* iflash = MakePanel(list, 4, 172, 208, 76, C_PANEL, C_BORDER, 10);
            MakeIcon(iflash, ICON_CHIP, 8, 10, C_VIOLET, 24);
            MakeLabel(iflash, "16 MB External SPI Flash", 38, 8, 160, 18, FONT_12, C_TEXT);
            MakeLabel(iflash, "Partisi Aplikasi Firmware: 4 MB", 38, 28, 160, 15, FONT_10, C_MUTED);
            MakeLabel(iflash, "Partisi Asset Audio & Font: 8 MB", 38, 43, 160, 15, FONT_10,
                      C_MUTED);
            MakeLabel(iflash, "NVS Configuration: Terpasang", 38, 58, 160, 15, FONT_10, C_MINT);
        }
    }

    MakeBottomNav(root, PAGE_FILES);
}

void NovaDisplay::BuildBattery() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "BATERAI & DAYA");

    int level = 100, mv = 0, raw = 0;
    bool chg = false;
    if (callbacks_.get_battery) {
        callbacks_.get_battery(level, chg, mv, raw);
    }

    bool is_land = (width_ > 240);
    const char* style_labels[] = {"Icon Saja", "Icon + %", "% Saja"};

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=10, w=146): Status Card + Style Switcher
        lv_obj_t* card = MakePanel(root, 10, 38, 146, 76, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_BATTERY, 8, 8, C_MINT, 22);
        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%d%%", level);
        battery_percent_label_ =
            MakeLabel(card, pct_str, 36, 5, 54, 20, FONT_16, chg ? C_CYAN : C_TEXT);

        char state_str[48];
        snprintf(state_str, sizeof(state_str), "%s", chg ? "Isi Daya" : "Baterai Aktif");
        battery_state_label_ =
            MakeLabel(card, state_str, 36, 25, 104, 14, FONT_10, chg ? C_MINT : C_MUTED);

        char volt_str[48];
        float vf = mv / 1000.0f;
        snprintf(volt_str, sizeof(volt_str), "Tegangan: %d mV (%.2fV)", mv, vf);
        battery_volt_label_ = MakeLabel(card, volt_str, 8, 43, 130, 16, FONT_10, C_CYAN);
        lv_label_set_long_mode(battery_volt_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        char raw_str[48];
        snprintf(raw_str, sizeof(raw_str), "ADC Mentah: %d", raw);
        battery_raw_label_ = MakeLabel(card, raw_str, 8, 58, 130, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(battery_raw_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(root, "GAYA STATUS BAR", 12, 120, 142, 14, FONT_10, C_MUTED);
        for (int i = 0; i < 3; ++i) {
            bool selected = (bat_style_ == i);
            lv_obj_t* s_btn = lv_btn_create(root);
            lv_obj_set_pos(s_btn, 10 + i * 50, 138);
            lv_obj_set_size(s_btn, 46, 32);
            StyleButton(s_btn, selected ? C_PANEL_2 : C_PANEL, selected ? C_CYAN : C_BORDER, 6);
            if (selected) {
                lv_obj_set_style_border_width(s_btn, 2, LV_PART_MAIN);
            }

            lv_obj_t* btn_lbl = lv_label_create(s_btn);
            lv_label_set_text(btn_lbl, style_labels[i]);
            lv_obj_set_style_text_font(btn_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(btn_lbl, selected ? C_CYAN : C_MUTED, LV_PART_MAIN);
            lv_obj_center(btn_lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                s_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!btn)
                        btn = (lv_obj_t*)lv_event_get_target(e);
                    uint8_t style = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
                    self->SetBatteryStyle(style);
                },
                LV_EVENT_CLICKED, this);
        }

        // Right Column (x=164, w=146, h=164): Hardware info card
        lv_obj_t* hcard = MakePanel(root, 164, 38, 146, 164, C_PANEL, C_BORDER, 10);
        MakeLabel(hcard, "INFORMASI RANGKAIAN", 8, 8, 130, 14, FONT_10, C_CYAN);

        lv_obj_t* l1 = MakeLabel(hcard, "Sensor: GPIO 9 (ADC Divider 1:1) • Monitoring Real-Time",
                                 8, 28, 130, 16, FONT_10, C_TEXT);
        lv_label_set_long_mode(l1, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* l2 = MakeLabel(hcard, "IC Pengisi Daya: TP4054 Linear Charger CC/CV ~300mA", 8,
                                 52, 130, 16, FONT_10, C_MUTED);
        lv_label_set_long_mode(l2, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* l3 = MakeLabel(hcard, "Baterai: Li-ion 3.7V Standar (Tegangan Maksimal 4.2V)", 8,
                                 76, 130, 16, FONT_10, C_MUTED);
        lv_label_set_long_mode(l3, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* l4 = MakeLabel(hcard, "Proteksi: Hemat Daya & Dynamic Clock Scaling Aktif", 8,
                                 100, 130, 16, FONT_10, C_MINT);
        lv_label_set_long_mode(l4, LV_LABEL_LONG_SCROLL_CIRCULAR);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "STATUS BATERAI", 14, 38, 212, 13, FONT_10, C_MUTED);

        lv_obj_t* card = MakePanel(root, 12, 54, 216, 78, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_BATTERY, 8, 8, C_MINT, 24);
        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%d%%", level);
        battery_percent_label_ =
            MakeLabel(card, pct_str, 38, 4, 76, 22, FONT_16, chg ? C_CYAN : C_TEXT);

        char state_str[48];
        snprintf(state_str, sizeof(state_str), "%s",
                 chg ? "Sedang Mengisi Daya" : "Menggunakan Baterai");
        battery_state_label_ =
            MakeLabel(card, state_str, 38, 26, 164, 14, FONT_10, chg ? C_MINT : C_MUTED);

        char volt_str[48];
        float vf = mv / 1000.0f;
        snprintf(volt_str, sizeof(volt_str), "Tegangan: %d mV (%.2f V)", mv, vf);
        battery_volt_label_ = MakeLabel(card, volt_str, 8, 44, 200, 16, FONT_10, C_CYAN);
        lv_label_set_long_mode(battery_volt_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        char raw_str[48];
        snprintf(raw_str, sizeof(raw_str), "Sensor ADC Mentah: %d (Divider 1:1)", raw);
        battery_raw_label_ = MakeLabel(card, raw_str, 8, 59, 200, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(battery_raw_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(root, "GAYA TAMPILAN STATUS BAR", 14, 138, 212, 13, FONT_10, C_MUTED);

        for (int i = 0; i < 3; ++i) {
            bool selected = (bat_style_ == i);
            lv_obj_t* s_btn = lv_btn_create(root);
            lv_obj_set_pos(s_btn, 12 + i * 72, 154);
            lv_obj_set_size(s_btn, 68, 28);
            StyleButton(s_btn, selected ? C_PANEL_2 : C_PANEL, selected ? C_CYAN : C_BORDER, 8);
            if (selected) {
                lv_obj_set_style_border_width(s_btn, 2, LV_PART_MAIN);
            }

            lv_obj_t* btn_lbl = lv_label_create(s_btn);
            lv_label_set_text(btn_lbl, style_labels[i]);
            lv_obj_set_style_text_font(btn_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(btn_lbl, selected ? C_CYAN : C_MUTED, LV_PART_MAIN);
            lv_obj_center(btn_lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(
                s_btn,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                        return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self)
                        return;
                    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                    if (!btn)
                        btn = (lv_obj_t*)lv_event_get_target(e);
                    uint8_t style = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
                    self->SetBatteryStyle(style);
                },
                LV_EVENT_CLICKED, this);
        }

        // Dedicated "INFORMASI RANGKAIAN" Card Panel
        // Starts at y=188, height=88 -> ends at 276 (Bottom Nav starts at 284, perfectly visible
        // and spacious!)
        lv_obj_t* icard = MakePanel(root, 12, 188, 216, 88, C_PANEL, C_BORDER, 10);
        MakeLabel(icard, "INFORMASI RANGKAIAN & DAYA", 8, 5, 200, 14, FONT_10, C_CYAN);

        lv_obj_t* cl1 =
            MakeLabel(icard, "Sensor: GPIO 9 (ADC Divider R40/R41 1:1) • Monitoring Real-Time", 8,
                      22, 200, 15, FONT_10, C_TEXT);
        lv_label_set_long_mode(cl1, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* cl2 =
            MakeLabel(icard, "IC Pengisi Daya: TP4054 Linear Li-ion Charger CC/CV ~300mA Auto-Cut",
                      8, 38, 200, 15, FONT_10, C_MUTED);
        lv_label_set_long_mode(cl2, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* cl3 =
            MakeLabel(icard, "Baterai: Li-ion 3.7V Standar (Tegangan Maksimal 4.2V, Cutoff 3.3V)",
                      8, 54, 200, 15, FONT_10, C_MUTED);
        lv_label_set_long_mode(cl3, LV_LABEL_LONG_SCROLL_CIRCULAR);

        lv_obj_t* cl4 =
            MakeLabel(icard, "Proteksi: Hemat Daya Cerdas & Dynamic Clock Scaling Aktif", 8, 70,
                      200, 15, FONT_10, C_MINT);
        lv_label_set_long_mode(cl4, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    MakeBottomNav(root, PAGE_BATTERY);
}

const char* NovaDisplay::GetLanguageName() const {
    static const char* kLangs[] = {"Bahasa Indonesia (ID)", "English (US)"};
    return kLangs[(language_idx_ >= 0 && language_idx_ < 2) ? language_idx_ : 0];
}

const char* NovaDisplay::GetTimezoneName() const {
    static const char* kTzNames[] = {"WIB (UTC+7) - Barat", "WITA (UTC+8) - Tengah",
                                     "WIT (UTC+9) - Timur", "GMT (UTC+0) - London",
                                     "JST (UTC+9) - Tokyo", "EST (UTC-5) - New York"};
    return kTzNames[(timezone_idx_ >= 0 && timezone_idx_ < 6) ? timezone_idx_ : 0];
}

void NovaDisplay::CycleLanguage() {
    language_idx_ = (language_idx_ + 1) % 2;
    Settings sys("system", true);
    sys.SetInt("lang_idx", language_idx_);
    BuildCurrentPage();
}

void NovaDisplay::CycleTimezone() {
    timezone_idx_ = (timezone_idx_ + 1) % 6;
    Settings sys("system", true);
    sys.SetInt("tz_idx", timezone_idx_);

    static const char* kTimezonesEnv[] = {"WIB-7", "WITA-8", "WIT-9",
                                          "GMT0",  "JST-9",  "EST5EDT,M3.2.0,M11.1.0"};
    const char* tz = kTimezonesEnv[timezone_idx_];
    setenv("TZ", tz, 1);
    tzset();

    BuildCurrentPage();
}

void NovaDisplay::SyncNtpNow() {
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "id.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_setservername(2, "time.google.com");
    esp_sntp_init();
    ntp_started_ = true;
    BuildCurrentPage();
}
