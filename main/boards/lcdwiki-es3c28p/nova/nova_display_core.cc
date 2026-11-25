#include "nova_display_internal.h"

lv_color_t C_BG = lv_color_hex(0x0B1220);
lv_color_t C_PANEL = lv_color_hex(0x141F31);
lv_color_t C_PANEL_2 = lv_color_hex(0x1A2940);
lv_color_t C_BORDER = lv_color_hex(0x2B3B52);
lv_color_t C_CYAN = lv_color_hex(0x63DDF2);  // Synchronized Accent Color
lv_color_t C_MINT = lv_color_hex(0x7DE0B5);
lv_color_t C_AMBER = lv_color_hex(0xFFC477);
lv_color_t C_VIOLET = lv_color_hex(0xB8A4FF);
lv_color_t C_RED = lv_color_hex(0xFF8195);
lv_color_t C_TEXT = lv_color_hex(0xF1F5FA);
lv_color_t C_MUTED = lv_color_hex(0x91A0B5);
lv_color_t C_DARK = lv_color_hex(0x0D1728);
NovaDisplay::NovaDisplay(const NovaCallbacks& callbacks) : callbacks_(callbacks) {
    Settings disp("display", true);
    header_icon_color_ = (uint32_t)disp.GetInt("hdr_icon_clr", 0xFFFFFF);
    int b_pct = disp.GetInt("brightness", -1);
    Settings prefs("nova", true);
    if (b_pct >= 0 && b_pct <= 100) {
        brightness_ = (uint8_t)((b_pct * 255) / 100);
    } else {
        brightness_ = (uint8_t)prefs.GetInt("bright", 210);
    }
    muted_ = prefs.GetInt("mute", 0) != 0;
    use_24hour_ = prefs.GetInt("24hour", 1) != 0;
    bat_style_ = (uint8_t)prefs.GetInt("bat_style", BAT_STYLE_ICON_PERCENT);
    if (bat_style_ > BAT_STYLE_PERCENT_ONLY)
        bat_style_ = BAT_STYLE_ICON_PERCENT;
    icon_color_mode_ = (uint8_t)prefs.GetInt("icon_mode", ICON_COLOR_MIXED);
    if (icon_color_mode_ > ICON_COLOR_THEME)
        icon_color_mode_ = ICON_COLOR_MIXED;

    Settings sys("system", true);
    language_idx_ = sys.GetInt("lang_idx", 0);
    timezone_idx_ = sys.GetInt("tz_idx", 0);

    contrast_level_ = GetCurrentContrastLevel();
    sharpness_level_ = GetCurrentSharpnessLevel();
    text_weight_ = GetCurrentTextWeightLevel();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        ApplyTheme(synced->theme_name, false);
    }
    const auto* synced_bg = GetCurrentSyncedBg();
    if (synced_bg) {
        ApplyBackground(synced_bg->id, false);
    }
    UpdateResolvedTextColor();

    if (callbacks_.get_volume) {
        saved_volume_ = callbacks_.get_volume();
        if (saved_volume_ <= 0)
            saved_volume_ = 75;
    }

    nav_icon_buf_ = (uint8_t*)heap_caps_malloc(48 * 48 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nav_icon_buf_)
        nav_icon_buf_ = (uint8_t*)malloc(48 * 48 * 2);
    if (nav_icon_buf_) {
        memset(nav_icon_buf_, 0, 48 * 48 * 2);
        memset(&nav_icon_dsc_, 0, sizeof(nav_icon_dsc_));
        nav_icon_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        nav_icon_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        nav_icon_dsc_.header.w = 48;
        nav_icon_dsc_.header.h = 48;
        nav_icon_dsc_.header.stride = 48 * 2;
        nav_icon_dsc_.data_size = 48 * 48 * 2;
        nav_icon_dsc_.data = nav_icon_buf_;
    }
}

void NovaDisplay::ApplyTheme(const std::string& theme_name, bool persist) {
    const auto* th = FindThemeByName(theme_name);
    if (!th)
        return;

    uint32_t eff_accent = ApplySharpnessToAccent(th->accent_color, sharpness_level_);
    C_CYAN = lv_color_hex(eff_accent);

    // If custom background was selected by user, keep it; otherwise use theme default
    Settings nova_s("nova", true);
    std::string saved_bg = nova_s.GetString("bg_id", "");
    uint32_t raw_bg = th->bg_color;
    uint32_t raw_panel = th->panel_color;
    uint32_t raw_panel2 = th->panel2_color;
    uint32_t raw_border = th->border_color;

    if (!saved_bg.empty()) {
        const auto* bg = FindBgById(saved_bg);
        if (bg) {
            raw_bg = bg->bg_color;
            raw_panel = bg->panel_color;
            raw_panel2 = bg->panel2_color;
            raw_border = bg->border_color;
        }
    }

    uint32_t eff_bg, eff_panel, eff_panel2, eff_border;
    ApplyContrastToThemeColors(raw_bg, raw_panel, raw_panel2, raw_border, contrast_level_, eff_bg,
                               eff_panel, eff_panel2, eff_border);

    C_BG = lv_color_hex(eff_bg);
    C_PANEL = lv_color_hex(eff_panel);
    C_PANEL_2 = lv_color_hex(eff_panel2);
    C_BORDER = lv_color_hex(eff_border);
    C_DARK = lv_color_hex(eff_bg);

    if (persist) {
        SyncThemeGlobal(th->theme_name);
    }

    UpdateResolvedTextColor();

    if (current_page_ != PAGE_SPLASH && width_ > 0) {
        BuildCurrentPage();
    }
}

void NovaDisplay::ApplyBackground(const std::string& bg_id, bool persist) {
    const auto* bg = FindBgById(bg_id);
    if (!bg)
        return;

    uint32_t eff_bg, eff_panel, eff_panel2, eff_border;
    ApplyContrastToThemeColors(bg->bg_color, bg->panel_color, bg->panel2_color, bg->border_color,
                               contrast_level_, eff_bg, eff_panel, eff_panel2, eff_border);

    C_BG = lv_color_hex(eff_bg);
    C_PANEL = lv_color_hex(eff_panel);
    C_PANEL_2 = lv_color_hex(eff_panel2);
    C_BORDER = lv_color_hex(eff_border);
    C_DARK = lv_color_hex(eff_bg);

    if (persist) {
        SyncBgGlobal(bg->id);
    }

    UpdateResolvedTextColor();

    if (current_page_ != PAGE_SPLASH && width_ > 0) {
        BuildCurrentPage();
    }
}

void NovaDisplay::ApplyContrast(int contrast, bool persist) {
    contrast_level_ = contrast;
    if (persist) {
        SetCurrentContrastLevel(contrast);
    }
    const auto* cur_bg = GetCurrentSyncedBg();
    if (cur_bg) {
        ApplyBackground(cur_bg->id, false);
    } else {
        ApplyTheme(GetCurrentThemeName(), false);
    }
}

int NovaDisplay::GetCurrentContrast() const { return contrast_level_; }

void NovaDisplay::ApplySharpness(int sharpness, bool persist) {
    sharpness_level_ = sharpness;
    if (persist) {
        SetCurrentSharpnessLevel(sharpness);
    }
    ApplyTheme(GetCurrentThemeName(), false);
}

int NovaDisplay::GetCurrentSharpness() const { return sharpness_level_; }

void NovaDisplay::ApplyTextWeight(int weight, bool persist) {
    text_weight_ = weight;
    if (persist) {
        SetCurrentTextWeightLevel(weight);
    }
    if (current_page_ != PAGE_SPLASH && width_ > 0) {
        BuildCurrentPage();
    }
}

int NovaDisplay::GetCurrentTextWeight() const { return text_weight_; }

static void TextWeightDrawTaskCb(lv_event_t* e) {
    lv_draw_task_t* t = (lv_draw_task_t*)lv_event_get_param(e);
    if (!t || lv_draw_task_get_type(t) != LV_DRAW_TASK_TYPE_LABEL)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
    if (!self || self->GetCurrentTextWeight() <= 0)
        return;

    int weight = self->GetCurrentTextWeight();
    lv_draw_label_dsc_t* dsc = lv_draw_task_get_label_dsc(t);
    if (!dsc || !dsc->font || !dsc->text)
        return;

    // Do not apply faux bold to symbol/icon fonts
    if (dsc->font == FONT_ICON || dsc->font == &font_material_symbols_20_4 ||
        dsc->font == &font_material_symbols_30_4) {
        return;
    }

    lv_layer_t* layer = dsc->base.layer;
    if (!layer)
        layer = t->target_layer;
    if (!layer)
        return;
    lv_area_t coords = t->area;

    // Weight 1 (SEDANG / MEDIUM): duplicate shifted +1px horizontal (70% opacity)
    if (weight >= 1) {
        lv_draw_label_dsc_t dsc2 = *dsc;
        dsc2.base.obj = nullptr;
        dsc2.hint = nullptr;
        dsc2.ofs_x += 1;
        if (weight == 1)
            dsc2.opa = (dsc->opa * 7) / 10;
        lv_draw_label(layer, &dsc2, &coords);
    }
    // Weight 2 (TEBAL / BOLD): duplicate shifted +1px vertical (70% opacity for 2D boldness)
    if (weight >= 2) {
        lv_draw_label_dsc_t dsc3 = *dsc;
        dsc3.base.obj = nullptr;
        dsc3.hint = nullptr;
        dsc3.ofs_y += 1;
        if (weight == 2)
            dsc3.opa = (dsc->opa * 7) / 10;
        lv_draw_label(layer, &dsc3, &coords);
    }
    // Weight 3 (EKSTRA / HEAVY): duplicate shifted +1px diagonal (+1, +1)
    if (weight >= 3) {
        lv_draw_label_dsc_t dsc4 = *dsc;
        dsc4.base.obj = nullptr;
        dsc4.hint = nullptr;
        dsc4.ofs_x += 1;
        dsc4.ofs_y += 1;
        dsc4.opa = dsc->opa;
        lv_draw_label(layer, &dsc4, &coords);
    }
}

void NovaDisplay::ApplyTextWeightToObj(lv_obj_t* obj, lv_color_t color) {
    if (!obj)
        return;
    (void)color;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(obj, TextWeightDrawTaskCb, LV_EVENT_DRAW_TASK_ADDED, this);
}

std::string NovaDisplay::GetCurrentBackgroundId() const {
    const auto* bg = GetCurrentSyncedBg();
    return bg ? bg->id : "midnight";
}

void NovaDisplay::UpdateResolvedTextColor() {
    uint32_t text_hex = 0xF1F5FA;
    uint32_t muted_hex = 0x91A0B5;
    ResolveEffectiveTextColor(lv_color_to_int(C_BG), text_hex, muted_hex);
    C_TEXT = lv_color_hex(text_hex);
    C_MUTED = lv_color_hex(muted_hex);
}

void NovaDisplay::ApplyHeaderIconColor(uint32_t color_hex, bool persist) {
    if (header_icon_color_ == color_hex && !persist)
        return;
    header_icon_color_ = color_hex;
    if (persist) {
        Settings disp("display", true);
        disp.SetInt("hdr_icon_clr", (int)color_hex);
        if (callbacks_.on_header_icon_color_changed) {
            callbacks_.on_header_icon_color_changed(color_hex);
        }
    }
    lv_color_t c = lv_color_hex(header_icon_color_);
    if (top_back_icon_)
        lv_obj_set_style_text_color(top_back_icon_, c, LV_PART_MAIN);
    if (top_rot_icon_)
        lv_obj_set_style_text_color(top_rot_icon_, c, LV_PART_MAIN);
    if (top_br_icon_)
        lv_obj_set_style_text_color(top_br_icon_, c, LV_PART_MAIN);
    if (top_bluetooth_icon_)
        lv_obj_set_style_text_color(top_bluetooth_icon_, c, LV_PART_MAIN);
    if (top_notif_icon_)
        lv_obj_set_style_text_color(top_notif_icon_, c, LV_PART_MAIN);
    if (top_notif_label_)
        lv_obj_set_style_text_color(top_notif_label_, c, LV_PART_MAIN);
    if (top_mute_icon_)
        lv_obj_set_style_text_color(top_mute_icon_, c, LV_PART_MAIN);
    if (top_wifi_icon_)
        lv_obj_set_style_text_color(top_wifi_icon_, c, LV_PART_MAIN);
    if (top_battery_icon_)
        lv_obj_set_style_text_color(top_battery_icon_, c, LV_PART_MAIN);
    if (top_battery_label_)
        lv_obj_set_style_text_color(top_battery_label_, c, LV_PART_MAIN);
    UpdateHeaderColorModalPreview();
}

void NovaDisplay::ApplyTextColor(const std::string& text_color_id, bool persist) {
    const auto* tc = FindTextColorById(text_color_id);
    if (!tc)
        return;

    if (persist) {
        SyncTextColorGlobal(tc->id);
    }

    UpdateResolvedTextColor();

    if (current_page_ != PAGE_SPLASH && width_ > 0) {
        BuildCurrentPage();
    }
}

std::string NovaDisplay::GetCurrentTextColorId() const {
    const auto* tc = GetCurrentSyncedTextColor();
    return tc ? tc->id : "auto";
}

void NovaDisplay::CycleTheme() {
    size_t count = 0;
    const auto* table = GetThemeSyncTable(&count);
    const auto* cur = GetCurrentSyncedTheme();
    size_t cur_idx = 0;
    for (size_t i = 0; i < count; ++i) {
        if (cur && std::string(cur->theme_name) == table[i].theme_name) {
            cur_idx = i;
            break;
        }
    }
    size_t next_idx = (cur_idx + 1) % count;
    ApplyTheme(table[next_idx].theme_name, true);
}

std::string NovaDisplay::GetCurrentThemeName() const {
    const auto* cur = GetCurrentSyncedTheme();
    return cur ? cur->display_name : "Stabilo Hijau Neon";
}

bool NovaDisplay::IsNotifSoundEnabled() const {
    Settings audio_s("audio", true);
    return audio_s.GetInt("notif_snd_en", 1) != 0;
}

void NovaDisplay::SetNotifSoundEnabled(bool en) {
    Settings audio_s("audio", true);
    audio_s.SetInt("notif_snd_en", en ? 1 : 0);
}

int NovaDisplay::GetNotifSoundIndex() const {
    Settings audio_s("audio", true);
    return audio_s.GetInt("notif_snd_idx", 0);
}

void NovaDisplay::SetNotifSoundIndex(int idx) {
    if (idx < 0)
        idx = 0;
    if (idx > 5)
        idx = 5;
    Settings audio_s("audio", true);
    audio_s.SetInt("notif_snd_idx", idx);
}

void NovaDisplay::PreviewNotifSound(int idx) {
    if (idx < 0)
        idx = 0;
    if (idx > 5)
        idx = 5;
    if (callbacks_.play_sound) {
        callbacks_.play_sound(201 + idx);
    }
}

NovaDisplay::~NovaDisplay() {
    CloseDownloadModal(false);
    if (nav_icon_buf_) {
        free(nav_icon_buf_);
        nav_icon_buf_ = nullptr;
    }
}

void NovaDisplay::Init(int width, int height) {
    width_ = width;
    height_ = height;
    ESP_LOGI(TAG, "Initializing NOVA MINI OS on %dx%d display", width_, height_);

    static const char* kTimezonesEnv[] = {"WIB-7", "WITA-8", "WIT-9",
                                          "GMT0",  "JST-9",  "EST5EDT,M3.2.0,M11.1.0"};
    const char* tz = kTimezonesEnv[(timezone_idx_ >= 0 && timezone_idx_ < 6) ? timezone_idx_ : 0];
    setenv("TZ", tz, 1);
    tzset();

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "id.pool.ntp.org");
        esp_sntp_setservername(1, "pool.ntp.org");
        esp_sntp_setservername(2, "time.google.com");
        esp_sntp_init();
        ntp_started_ = true;
    }

    ApplyBrightness(brightness_, false);

    current_page_ = PAGE_HOME;
    BuildCurrentPage();
}

void NovaDisplay::ClearDecoration(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

lv_obj_t* NovaDisplay::MakePanel(lv_obj_t* parent, int x, int y, int width, int height,
                                 lv_color_t fill, lv_color_t border, int radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_clear_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(obj, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, (lv_color_to_int(border) == lv_color_to_int(fill)) ? 0 : 1,
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    return obj;
}

lv_obj_t* NovaDisplay::MakeLabel(lv_obj_t* parent, const char* text, int x, int y, int width,
                                 int height, const lv_font_t* font, lv_color_t color,
                                 lv_text_align_t align) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    ApplyTextWeightToObj(label, color);
    lv_obj_clear_flag(label, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return label;
}

lv_obj_t* NovaDisplay::MakeCenteredLabel(lv_obj_t* parent, const char* text, int x, int y,
                                         int width, int height, const lv_font_t* font,
                                         lv_color_t color) {
    return MakeLabel(parent, text, x, y, width, height, font, color, LV_TEXT_ALIGN_CENTER);
}

void NovaDisplay::StyleButton(lv_obj_t* button, lv_color_t fill, lv_color_t border, int radius) {
    lv_obj_set_style_bg_color(button, fill, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, C_PANEL_2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, border, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
    lv_obj_clear_flag(button,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS));
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_CHAIN);
}

void NovaDisplay::StyleKeyboard(lv_obj_t* kb) {
    if (!kb)
        return;
    // Main container styling
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x18181B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(kb, lv_color_hex(0x27272A), LV_PART_MAIN);
    lv_obj_set_style_radius(kb, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 2, LV_PART_MAIN);

    // Key button items (LV_PART_ITEMS)
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x27272A), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(0x3F3F46), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, FONT_12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);

    // Pressed state (Vibrant Blue accent feedback)
    lv_style_selector_t sel_pressed =
        (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2563EB), sel_pressed);
    lv_obj_set_style_border_color(kb, lv_color_hex(0x60A5FA), sel_pressed);
    lv_obj_set_style_text_color(kb, lv_color_white(), sel_pressed);
}

lv_obj_t* NovaDisplay::MakeButton(lv_obj_t* parent, const char* text, int x, int y, int width,
                                  int height, lv_color_t fill, lv_color_t border,
                                  lv_color_t text_color, const lv_font_t* font,
                                  lv_event_cb_t callback, void* user_data) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    StyleButton(button, fill, border, 12);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
    ApplyTextWeightToObj(label, text_color);
    lv_obj_center(label);
    if (callback) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

lv_obj_t* NovaDisplay::IconShape(lv_obj_t* parent, int x, int y, int width, int height,
                                 lv_color_t color, int radius, bool outline, int outline_width) {
    lv_obj_t* shape = lv_obj_create(parent);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_clear_flag(shape, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(shape, radius, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(shape, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(shape, 0, LV_PART_MAIN);
    if (outline) {
        lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(shape, color, LV_PART_MAIN);
        lv_obj_set_style_border_width(shape, outline_width, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(shape, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(shape, 0, LV_PART_MAIN);
    }
    return shape;
}

lv_obj_t* NovaDisplay::MakeIcon(lv_obj_t* parent, IconKind kind, int x, int y, lv_color_t color,
                                int size) {
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_pos(root, x, y);
    lv_obj_set_size(root, size, size);
    lv_obj_clear_flag(root, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    ClearDecoration(root);

    lv_obj_t* lbl = lv_label_create(root);
    lv_obj_set_style_text_font(lbl, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_center(lbl);

    const char* sym = MATERIAL_SYMBOLS_SETTINGS;
    switch (kind) {
        case ICON_AI:
            sym = MATERIAL_SYMBOLS_ROBOT_2;
            break;
        case ICON_WIFI:
            sym = MATERIAL_SYMBOLS_WIFI;
            break;
        case ICON_AUDIO:
            sym = MATERIAL_SYMBOLS_VOLUME_UP;
            break;
        case ICON_FILES:
            sym = MATERIAL_SYMBOLS_SD_CARD;
            break;
        case ICON_CHIP:
            sym = MATERIAL_SYMBOLS_MEMORY;
            break;
        case ICON_CHRONOS:
            sym = MATERIAL_SYMBOLS_SCHEDULE;
            break;
        case ICON_BRIGHTNESS:
            sym = MATERIAL_SYMBOLS_BRIGHTNESS_6;
            break;
        case ICON_SETTINGS:
            sym = MATERIAL_SYMBOLS_SETTINGS;
            break;
        case ICON_HOME:
            sym = MATERIAL_SYMBOLS_HOME;
            break;
        case ICON_APPS:
            sym = MATERIAL_SYMBOLS_PLAY_ARROW;
            break;
        case ICON_MIC:
            sym = MATERIAL_SYMBOLS_MIC;
            break;
        case ICON_BATTERY:
            sym = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL;
            break;
        case ICON_REGION:
            sym = MATERIAL_SYMBOLS_LANGUAGE;
            break;
        case ICON_CALC:
            sym = MATERIAL_SYMBOLS_CALCULATE;
            break;
        case ICON_GAME:
            sym = MATERIAL_SYMBOLS_SPORTS_ESPORTS;
            break;
        case ICON_RADIO:
            sym = MATERIAL_SYMBOLS_HEADPHONES;
            break;
        case ICON_PIANO:
            sym = MATERIAL_SYMBOLS_MUSIC_NOTE;
            break;
        case ICON_DRAW:
            sym = MATERIAL_SYMBOLS_EDIT_SQUARE;
            break;
        case ICON_DASHBOARD:
            sym = MATERIAL_SYMBOLS_DEVICE_THERMOSTAT;
            break;
        case ICON_NAV:
            sym = MATERIAL_SYMBOLS_NAVIGATION;
            break;
        case ICON_CHAT:
            sym = MATERIAL_SYMBOLS_CHAT_BUBBLE;
            break;
        case ICON_WEATHER:
            sym = MATERIAL_SYMBOLS_DEVICE_THERMOSTAT;
            break;
        case ICON_PHONE:
            sym = MATERIAL_SYMBOLS_PHONE;
            break;
    }
    lv_label_set_text(lbl, sym);
    return root;
}

void OnNavClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    lv_indev_t* indev = lv_indev_active();
    if (indev && lv_indev_get_scroll_obj(indev) != nullptr)
        return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(event);
    if (!btn)
        btn = (lv_obj_t*)lv_event_get_target(event);
    if (btn && (lv_obj_is_scrolling(btn) ||
                (lv_obj_get_parent(btn) && lv_obj_is_scrolling(lv_obj_get_parent(btn)))))
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self)
        return;
    auto target = static_cast<NovaDisplay::Page>((intptr_t)lv_obj_get_user_data(btn));
    self->NavigateTo(target);
}

static void OnBackClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->GoBack();
}

static void OnHomeClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->NavigateRoot(NovaDisplay::PAGE_HOME);
}

static void OnRecentAppsClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->ShowRecentAppsModal();
}

void NovaDisplay::MakeHeader(lv_obj_t* root, const char* title, bool show_back) {
    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, width_, 36);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(bar, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);

    lv_color_t icon_clr = lv_color_hex(header_icon_color_);

    int title_x = 8;
    int title_w = 110;

    if (show_back) {
        lv_obj_t* back_btn = lv_btn_create(bar);
        lv_obj_set_pos(back_btn, 6, 6);
        lv_obj_set_size(back_btn, 24, 24);
        StyleButton(back_btn, C_PANEL, C_BORDER, 6);
        lv_obj_set_style_pad_all(back_btn, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(back_btn, 8);
        top_back_icon_ = lv_label_create(back_btn);
        lv_obj_clear_flag(top_back_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_back_icon_, MATERIAL_SYMBOLS_ARROW_BACK);
        lv_obj_set_style_text_font(top_back_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_back_icon_, icon_clr, LV_PART_MAIN);
        lv_obj_center(top_back_icon_);
        lv_obj_add_event_cb(back_btn, OnBackClicked, LV_EVENT_CLICKED, this);

        // Screen Rotation Button on subpages too (pos x = 6 + 24 + 5 = 35)
        lv_obj_t* rot_btn = lv_btn_create(bar);
        lv_obj_set_pos(rot_btn, 35, 6);
        lv_obj_set_size(rot_btn, 24, 24);
        StyleButton(rot_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(rot_btn, 8);
        top_rot_icon_ = lv_label_create(rot_btn);
        lv_obj_clear_flag(top_rot_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_rot_icon_, MATERIAL_SYMBOLS_REFRESH);
        lv_obj_set_style_text_font(top_rot_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_rot_icon_, icon_clr, LV_PART_MAIN);
        lv_obj_center(top_rot_icon_);
        lv_obj_add_event_cb(
            rot_btn,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            },
            LV_EVENT_CLICKED, nullptr);

        title_x = 65;
        title_w = 70;
    } else {
        // Screen Rotation Button (Rotates display by 90 degrees)
        lv_obj_t* rot_btn = lv_btn_create(bar);
        lv_obj_set_pos(rot_btn, 6, 6);
        lv_obj_set_size(rot_btn, 24, 24);
        StyleButton(rot_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(rot_btn, 8);
        top_rot_icon_ = lv_label_create(rot_btn);
        lv_obj_clear_flag(top_rot_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_rot_icon_, MATERIAL_SYMBOLS_REFRESH);
        lv_obj_set_style_text_font(top_rot_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_rot_icon_, icon_clr, LV_PART_MAIN);
        lv_obj_center(top_rot_icon_);
        lv_obj_add_event_cb(
            rot_btn,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            },
            LV_EVENT_CLICKED, nullptr);

        // XiaoZhi Quick Brightness Button (pos x = 35, size 26x24, ext_click 8)
        lv_obj_t* br_btn = lv_btn_create(bar);
        lv_obj_set_pos(br_btn, 35, 6);
        lv_obj_set_size(br_btn, 26, 24);
        StyleButton(br_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(br_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(br_btn, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(br_btn, 8);
        top_br_icon_ = lv_label_create(br_btn);
        lv_obj_clear_flag(top_br_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_br_icon_, MATERIAL_SYMBOLS_BRIGHTNESS_6);
        lv_obj_set_style_text_font(top_br_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_br_icon_, icon_clr, LV_PART_MAIN);
        lv_obj_center(top_br_icon_);
        lv_obj_add_event_cb(
            br_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->NavigateTo(PAGE_DISPLAY);
            },
            LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(
            br_btn,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self)
                        self->ShowHeaderColorModal();
                }
            },
            LV_EVENT_LONG_PRESSED, this);

        title_x = 65;
        title_w = 52;
    }

    // Dynamic right status bar layout based on bat_style_
    int bat_w = 52;
    if (bat_style_ == BAT_STYLE_ICON_ONLY) {
        bat_w = 24;
    } else if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
        bat_w = 38;
    }
    const int gap = 5;
    int bat_x = width_ - 6 - bat_w;
    int wifi_w = 24;
    int wifi_x = bat_x - gap - wifi_w;
    int vol_w = 24;
    int vol_x = wifi_x - gap - vol_w;
    int bt_w = 24;
    int bt_x = vol_x - gap - bt_w;

    if (current_page_ == PAGE_HOME) {
        // Homescreen: omits title text so status bar icons have clean, luxurious spacing without
        // overlap
        int notif_w = 24;
        int notif_x = bt_x - gap - notif_w;

        int notif_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        top_notif_btn_ = lv_btn_create(bar);
        lv_obj_set_pos(top_notif_btn_, notif_x, 6);
        lv_obj_set_size(top_notif_btn_, notif_w, 24);
        StyleButton(top_notif_btn_, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(top_notif_btn_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(top_notif_btn_, 0, LV_PART_MAIN);

        top_notif_icon_ = lv_label_create(top_notif_btn_);
        lv_obj_clear_flag(top_notif_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_notif_icon_, MATERIAL_SYMBOLS_MAIL);
        lv_obj_set_style_text_font(top_notif_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_notif_icon_, icon_clr, LV_PART_MAIN);
        lv_obj_center(top_notif_icon_);

        top_notif_label_ = lv_label_create(top_notif_btn_);
        lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(top_notif_label_, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_notif_label_, icon_clr, LV_PART_MAIN);
        lv_obj_set_pos(top_notif_label_, 14, 2);

        if (notif_cnt > 0) {
            char n_txt[16];
            snprintf(n_txt, sizeof(n_txt), "%d", notif_cnt);
            lv_label_set_text(top_notif_label_, n_txt);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_add_event_cb(
            top_notif_btn_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->NavigateTo(PAGE_NOTIF);
            },
            LV_EVENT_CLICKED, this);
    } else {
        // Subpages: Show page title with ample room
        title_w = bt_x - title_x - 2;
        if (title_w < 30)
            title_w = 30;
        lv_obj_t* title_lbl =
            MakeLabel(bar, title, title_x, 8, title_w, 20, FONT_14, C_TEXT, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
    }

    // Right Status Bar Icons: Bluetooth -> Volume -> Wi-Fi -> Battery
    // 1. Bluetooth BLE (Tap -> info dialog)
    bool ble_connected = false;
    std::string ble_name, ble_mac;
    if (callbacks_.get_ble_status) {
        callbacks_.get_ble_status(ble_connected, ble_name, ble_mac);
    }
    lv_obj_t* bt_btn = lv_btn_create(bar);
    lv_obj_set_pos(bt_btn, bt_x, 6);
    lv_obj_set_size(bt_btn, bt_w, 24);
    StyleButton(bt_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(bt_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bt_btn, 0, LV_PART_MAIN);
    top_bluetooth_icon_ = lv_label_create(bt_btn);
    lv_obj_clear_flag(top_bluetooth_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_bluetooth_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
    lv_obj_set_style_text_font(top_bluetooth_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_bluetooth_icon_,
                                ble_connected ? lv_color_hex(0x22C55E) : icon_clr, LV_PART_MAIN);
    lv_obj_set_style_text_opa(top_bluetooth_icon_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(top_bluetooth_icon_);
    lv_obj_add_event_cb(
        bt_btn,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self)
                self->ShowBluetoothModal();
        },
        LV_EVENT_CLICKED, this);

    // 2. Volume / Speaker (Tap -> PAGE_AUDIO)
    lv_obj_t* vol_btn = lv_btn_create(bar);
    lv_obj_set_pos(vol_btn, vol_x, 6);
    lv_obj_set_size(vol_btn, vol_w, 24);
    StyleButton(vol_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(vol_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(vol_btn, 0, LV_PART_MAIN);
    top_mute_icon_ = lv_label_create(vol_btn);
    lv_obj_clear_flag(top_mute_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_mute_icon_,
                      muted_ ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(top_mute_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_mute_icon_, icon_clr, LV_PART_MAIN);
    lv_obj_center(top_mute_icon_);
    lv_obj_set_user_data(vol_btn, (void*)(intptr_t)PAGE_AUDIO);
    lv_obj_add_event_cb(vol_btn, OnNavClicked, LV_EVENT_CLICKED, this);

    // 3. Wi-Fi (Tap -> PAGE_WIFI, reconnecting indicator if disconnected)
    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected =
        (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    lv_obj_t* wifi_btn = lv_btn_create(bar);
    lv_obj_set_pos(wifi_btn, wifi_x, 6);
    lv_obj_set_size(wifi_btn, wifi_w, 24);
    StyleButton(wifi_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(wifi_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_btn, 0, LV_PART_MAIN);
    top_wifi_icon_ = lv_label_create(wifi_btn);
    lv_obj_clear_flag(top_wifi_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_wifi_icon_,
                      wifi_connected ? MATERIAL_SYMBOLS_WIFI : MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(top_wifi_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_wifi_icon_, icon_clr, LV_PART_MAIN);
    lv_obj_center(top_wifi_icon_);
    lv_obj_set_user_data(wifi_btn, (void*)(intptr_t)PAGE_WIFI);
    lv_obj_add_event_cb(wifi_btn, OnNavClicked, LV_EVENT_CLICKED, this);

    // 4. Battery (Tap -> PAGE_BATTERY)
    int bat_lvl = 100, bat_mv = 0, bat_raw = 0;
    bool bat_chg = false;
    if (callbacks_.get_battery)
        callbacks_.get_battery(bat_lvl, bat_chg, bat_mv, bat_raw);

    lv_obj_t* bat_btn = lv_btn_create(bar);
    lv_obj_set_pos(bat_btn, bat_x, 6);
    lv_obj_set_size(bat_btn, bat_w, 24);
    StyleButton(bat_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(bat_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bat_btn, 0, LV_PART_MAIN);

    top_battery_icon_ = lv_label_create(bat_btn);
    lv_obj_clear_flag(top_battery_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_battery_icon_, NovaGetBatterySymbol(bat_lvl, bat_chg));
    lv_obj_set_style_text_font(top_battery_icon_, FONT_ICON, LV_PART_MAIN);
    lv_color_t bat_clr = bat_chg ? C_MINT : (bat_lvl <= 20 ? lv_color_hex(0xEF4444) : icon_clr);
    lv_obj_set_style_text_color(top_battery_icon_, bat_clr, LV_PART_MAIN);

    top_battery_label_ = lv_label_create(bat_btn);
    lv_obj_clear_flag(top_battery_label_, LV_OBJ_FLAG_CLICKABLE);
    char b_txt[12];
    snprintf(b_txt, sizeof(b_txt), "%d%%", bat_lvl);
    lv_label_set_text(top_battery_label_, b_txt);
    lv_obj_set_style_text_font(top_battery_label_, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_battery_label_, bat_clr, LV_PART_MAIN);

    if (bat_style_ == BAT_STYLE_ICON_ONLY) {
        lv_obj_center(top_battery_icon_);
        lv_obj_add_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
    } else if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
        lv_obj_add_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(top_battery_label_);
    } else {  // BAT_STYLE_ICON_PERCENT
        lv_obj_set_pos(top_battery_icon_, 2, 2);
        lv_obj_set_pos(top_battery_label_, 22, 3);
    }

    lv_obj_set_user_data(bat_btn, (void*)(intptr_t)PAGE_BATTERY);
    lv_obj_add_event_cb(bat_btn, OnNavClicked, LV_EVENT_CLICKED, this);
}

void NovaDisplay::MakeBottomNav(lv_obj_t* root, Page active) {
    lv_obj_t* bar = lv_obj_create(root);
    lv_obj_set_pos(bar, 0, height_ - 30);
    lv_obj_set_size(bar, width_, 30);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(bar, C_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // 1. Smartphone Back Button (◀)
    lv_obj_t* back_btn = lv_btn_create(bar);
    lv_obj_set_height(back_btn, 26);
    lv_obj_set_flex_grow(back_btn, 1);
    StyleButton(back_btn, C_DARK, C_DARK, 6);
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(back_lbl, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_lbl, C_MUTED, LV_PART_MAIN);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, OnBackClicked, LV_EVENT_CLICKED, this);

    // 2. Smartphone Home Button (●)
    lv_obj_t* home_btn = lv_btn_create(bar);
    lv_obj_set_height(home_btn, 26);
    lv_obj_set_flex_grow(home_btn, 1);
    StyleButton(home_btn, C_DARK, C_DARK, 6);
    lv_obj_set_style_border_width(home_btn, 0, LV_PART_MAIN);
    lv_obj_t* home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, MATERIAL_SYMBOLS_HOME);
    lv_obj_set_style_text_font(home_lbl, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(home_lbl, (active == PAGE_HOME) ? C_CYAN : C_TEXT, LV_PART_MAIN);
    lv_obj_center(home_lbl);
    lv_obj_add_event_cb(home_btn, OnHomeClicked, LV_EVENT_CLICKED, this);

    // 3. Smartphone Recent Apps Switcher Button (▢)
    lv_obj_t* recent_btn = lv_btn_create(bar);
    lv_obj_set_height(recent_btn, 26);
    lv_obj_set_flex_grow(recent_btn, 1);
    StyleButton(recent_btn, C_DARK, C_DARK, 6);
    lv_obj_set_style_border_width(recent_btn, 0, LV_PART_MAIN);
    lv_obj_t* sq_shape = IconShape(recent_btn, 0, 0, 14, 14, C_MUTED, 3, true, 2);
    lv_obj_center(sq_shape);
    lv_obj_add_event_cb(recent_btn, OnRecentAppsClicked, LV_EVENT_CLICKED, this);
}

void NovaDisplay::CloseRecentAppsModal() {
    if (recent_apps_modal_) {
        lv_obj_del(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
}

void NovaDisplay::CloseAllModals() {
    DismissOtpHighlight();
    DismissNotificationBanner();
    if (wifi_modal_) {
        lv_obj_del(wifi_modal_);
        wifi_modal_ = nullptr;
        wifi_password_input_ = nullptr;
        wifi_ssid_input_ = nullptr;
        wifi_keyboard_ = nullptr;
        wifi_modal_open_ = false;
    }
    CloseBluetoothModal();
    if (file_modal_) {
        lv_obj_del(file_modal_);
        file_modal_ = nullptr;
    }
    if (online_modal_) {
        lv_obj_del(online_modal_);
        online_modal_ = nullptr;
    }
    if (dl_modal_) {
        lv_obj_del(dl_modal_);
        dl_modal_ = nullptr;
    }
    if (recent_apps_modal_) {
        lv_obj_del(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
    CloseRgbMixerModal();
    CloseGamesModal();
    CloseHeaderColorModal();
}

void NovaDisplay::ShowRecentAppsModal() {
    CloseRecentAppsModal();

    recent_apps_modal_ = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(recent_apps_modal_, 0, 0);
    lv_obj_set_size(recent_apps_modal_, width_, height_);
    lv_obj_clear_flag(recent_apps_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(recent_apps_modal_, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(recent_apps_modal_, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(recent_apps_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(recent_apps_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(recent_apps_modal_, 0, LV_PART_MAIN);

    // Centered Floating Modal Card
    int card_h = (height_ < 260) ? (height_ - 16) : 252;
    int card_w = (width_ > 240) ? 280 : 226;
    lv_obj_t* card = lv_obj_create(recent_apps_modal_);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header Drawer
    MakeLabel(card, "APLIKASI TERBUKA", 8, 4, 150, 18, FONT_14, C_TEXT);
    MakeLabel(card, "Ketuk aplikasi untuk beralih instan", 8, 22, 170, 14, FONT_10, C_MUTED);

    // Close button (X)
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_pos(close_btn, card_w - 36, 2);
    lv_obj_set_size(close_btn, 26, 26);
    StyleButton(close_btn, C_PANEL_2, C_BORDER, 8);
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
                self->CloseRecentAppsModal();
        },
        LV_EVENT_CLICKED, this);

    // Scrollable container for App Cards
    lv_obj_t* list = lv_obj_create(card);
    lv_obj_set_pos(list, 4, 38);
    lv_obj_set_size(list, card_w - 18, card_h - 86);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    struct AppCardItem {
        const char* name;
        const char* desc;
        const char* icon;
        lv_color_t color;
        int action_mode;  // -1 = navigate to page, >= 0 switch mode
        Page page;
    };

    const AppCardItem items[] = {
        {"Radio Streaming", "Stasiun Live Musik & Berita", MATERIAL_SYMBOLS_HEADPHONES,
         GetAppIconColor(1, ICON_RADIO), -1, PAGE_RADIO},
        {"Xiaozhi AI", "Percakapan Suara & Musik LLM", MATERIAL_SYMBOLS_ROBOT_2,
         GetAppIconColor(0, ICON_AI), 0, PAGE_AI},
        {"Navigasi Maps", "Petunjuk Arah & Jarak Belokan", MATERIAL_SYMBOLS_NAVIGATION,
         GetAppIconColor(2, ICON_NAV), -1, PAGE_NAV},
        {"Beranda OS", "Dashboard Utama & Widget", MATERIAL_SYMBOLS_HOME,
         GetAppIconColor(2, ICON_HOME), -1, PAGE_HOME},
        {"Chronos Pomodoro", "Timer Fokus 25 Menit", MATERIAL_SYMBOLS_SCHEDULE,
         GetAppIconColor(11, ICON_CHRONOS), -1, PAGE_CHRONOS},
        {"Wi-Fi & Jaringan", "Pemindai SSID & Status IP", MATERIAL_SYMBOLS_WIFI,
         GetAppIconColor(8, ICON_WIFI), -1, PAGE_WIFI},
        {"Tampilan & Layar", "Pengaturan Kecerahan Backlight", MATERIAL_SYMBOLS_BRIGHTNESS_6,
         GetAppIconColor(12, ICON_BRIGHTNESS), -1, PAGE_DISPLAY},
        {"Audio & Suara", "Driver ES8311 & Amplifier", MATERIAL_SYMBOLS_VOLUME_UP,
         GetAppIconColor(9, ICON_AUDIO), -1, PAGE_AUDIO},
        {"Baterai & Daya", "Pemantau ADC & Voltase TP4054",
         MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL, GetAppIconColor(13, ICON_BATTERY), -1,
         PAGE_BATTERY},
        {"File Hub", "16MB Flash Storage & Partisi", MATERIAL_SYMBOLS_SD_CARD,
         GetAppIconColor(10, ICON_FILES), -1, PAGE_FILES},
        {"Pengaturan Sistem", "Preferensi & Informasi Chip", MATERIAL_SYMBOLS_SETTINGS,
         GetAppIconColor(14, ICON_SETTINGS), -1, PAGE_SETTINGS},
        {"Pin Lab", "Hardware GPIO Reference Map", MATERIAL_SYMBOLS_MEMORY,
         GetAppIconColor(17, ICON_CHIP), -1, PAGE_PINLAB},
        {"Kalkulator Sentuh", "Operasi Aritmatika & Desimal", MATERIAL_SYMBOLS_CALCULATE,
         GetAppIconColor(15, ICON_CALC), -1, PAGE_CALCULATOR},
        {"Game Arcade", "Koleksi Game Retro", MATERIAL_SYMBOLS_SPORTS_ESPORTS,
         GetAppIconColor(16, ICON_GAME), -1, PAGE_APPS}};

    const int total_items = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < total_items; ++i) {
        lv_obj_t* app_card = lv_btn_create(list);
        lv_obj_set_pos(app_card, 0, i * 44);
        lv_obj_set_size(app_card, card_w - 26, 38);
        StyleButton(app_card, C_PANEL_2, C_BORDER, 10);

        // App Icon
        lv_obj_t* icon_lbl = lv_label_create(app_card);
        lv_label_set_text(icon_lbl, items[i].icon);
        lv_obj_set_style_text_font(icon_lbl, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon_lbl, items[i].color, LV_PART_MAIN);
        lv_obj_set_pos(icon_lbl, 8, 9);

        // Titles
        lv_obj_t* title_lbl =
            MakeLabel(app_card, items[i].name, 36, 4, card_w - 92, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* desc_lbl =
            MakeLabel(app_card, items[i].desc, 36, 20, card_w - 92, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Right arrow
        MakeLabel(app_card, ">", card_w - 48, 10, 16, 18, FONT_12, items[i].color,
                  LV_TEXT_ALIGN_CENTER);

        // Callback data
        lv_obj_set_user_data(app_card, (void*)(intptr_t)i);
        lv_obj_add_event_cb(
            app_card,
            [](lv_event_t* ev) {
                if (lv_event_get_code(ev) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                if (!self)
                    return;
                int idx =
                    (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(ev));
                self->CloseRecentAppsModal();

                if (idx == 0) {
                    self->NavigateTo(PAGE_RADIO);
                } else if (idx == 1) {
                    if (self->GetCallbacks().switch_mode)
                        self->GetCallbacks().switch_mode(0);
                } else if (idx == 2) {
                    if (self->GetCallbacks().switch_mode)
                        self->GetCallbacks().switch_mode(1);
                } else if (idx == 13) {
                    self->ShowGamesModal();
                } else {
                    Page pages[] = {PAGE_RADIO,      PAGE_AI,    PAGE_HOME,     PAGE_HOME,
                                    PAGE_CHRONOS,    PAGE_WIFI,  PAGE_DISPLAY,  PAGE_AUDIO,
                                    PAGE_BATTERY,    PAGE_FILES, PAGE_SETTINGS, PAGE_PINLAB,
                                    PAGE_CALCULATOR, PAGE_APPS};
                    if (idx < (int)(sizeof(pages) / sizeof(pages[0]))) {
                        if (pages[idx] == PAGE_HOME)
                            self->NavigateRoot(PAGE_HOME);
                        else
                            self->NavigateTo(pages[idx]);
                    }
                }
            },
            LV_EVENT_CLICKED, this);
    }

    // Bottom close / back button
    MakeButton(
        card, "KEMBALI KE BERANDA", 4, card_h - 42, card_w - 20, 30, C_PANEL_2, C_CYAN, C_TEXT,
        FONT_12,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                self->CloseRecentAppsModal();
                self->NavigateRoot(PAGE_HOME);
            }
        },
        this);
}

lv_obj_t* NovaDisplay::NewRoot() {
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    }

    if (flappy_timer_) {
        lv_timer_del(flappy_timer_);
        flappy_timer_ = nullptr;
    }
    if (snake_timer_) {
        lv_timer_del(snake_timer_);
        snake_timer_ = nullptr;
    }
    if (brick_timer_) {
        lv_timer_del(brick_timer_);
        brick_timer_ = nullptr;
    }
    if (mine_timer_) {
        lv_timer_del(mine_timer_);
        mine_timer_ = nullptr;
    }
    if (racer_timer_) {
        lv_timer_del(racer_timer_);
        racer_timer_ = nullptr;
    }
    if (monitor_timer_) {
        lv_timer_del(monitor_timer_);
        monitor_timer_ = nullptr;
    }
    if (sketch_buf_) {
        free(sketch_buf_);
        sketch_buf_ = nullptr;
        sketch_canvas_ = nullptr;
    }
    CloseAllModals();

    top_back_icon_ = nullptr;
    top_rot_icon_ = nullptr;
    top_br_icon_ = nullptr;
    top_wifi_icon_ = nullptr;
    top_bluetooth_icon_ = nullptr;
    top_notif_btn_ = nullptr;
    top_notif_icon_ = nullptr;
    top_notif_label_ = nullptr;
    top_mute_icon_ = nullptr;
    top_battery_icon_ = nullptr;
    top_battery_label_ = nullptr;

    piano_status_lbl_ = nullptr;
    for (int i = 0; i < 3; ++i)
        piano_inst_btns_[i] = nullptr;
    for (int i = 0; i < 8; ++i)
        piano_white_keys_[i] = nullptr;
    for (int i = 0; i < 5; ++i)
        piano_black_keys_[i] = nullptr;
    sketch_tool_lbl_ = sketch_size_btn_ = nullptr;
    mon_cpu_lbl_ = mon_sram_bar_ = mon_sram_lbl_ = mon_psram_bar_ = mon_psram_lbl_ = nullptr;
    mon_temp_lbl_ = mon_wifi_lbl_ = mon_uptime_lbl_ = nullptr;
    flappy_score_lbl_ = flappy_best_lbl_ = flappy_card_ = nullptr;

    lv_anim_delete_all();

    lv_obj_t* root = lv_scr_act();
    lv_obj_clean(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);

    clock_label_ = date_label_ = timer_label_ = nullptr;
    brightness_value_label_ = battery_raw_label_ = battery_percent_label_ = nullptr;
    battery_volt_label_ = battery_state_label_ = battery_card_icon_ = wifi_status_label_ = nullptr;
    audio_volume_label_ = audio_volume_slider_ = nullptr;
    return root;
}

void NovaDisplay::NavigateTo(Page next) {
    if (next == current_page_)
        return;
    if (next == PAGE_DISPLAY && current_page_ != PAGE_DISPLAY) {
        display_scroll_y_ = 0;
        display_pal_scroll_x_ = 0;
        display_bg_scroll_x_ = 0;
        display_tc_scroll_x_ = 0;
        display_hdr_scroll_x_ = 0;
    }
    if (next == PAGE_AUDIO && current_page_ != PAGE_AUDIO) {
        audio_scroll_y_ = 0;
    }
    if (current_page_ != PAGE_SPLASH) {
        if (history_depth_ < HISTORY_CAPACITY) {
            page_history_[history_depth_++] = current_page_;
        } else {
            memmove(page_history_, page_history_ + 1,
                    (HISTORY_CAPACITY - 1) * sizeof(page_history_[0]));
            page_history_[HISTORY_CAPACITY - 1] = current_page_;
        }
    }
    current_page_ = next;
    BuildCurrentPage();
}

void NovaDisplay::GoBack() {
    if (history_depth_ > 0) {
        current_page_ = page_history_[--history_depth_];
    } else {
        current_page_ = PAGE_HOME;
    }
    BuildCurrentPage();
}

void NovaDisplay::NavigateRoot(Page target) {
    history_depth_ = 0;
    current_page_ = target;
    BuildCurrentPage();
}

void NovaDisplay::BuildSplash() {
    lv_obj_t* root = NewRoot();
    MakeCenteredLabel(root, "NOVA MINI OS", 16, 104, 208, 40, FONT_28, C_TEXT);
    MakeCenteredLabel(root, "PERSONAL DEVICE INTERFACE", 12, 151, 216, 20, FONT_12, C_MUTED);
    lv_obj_t* track = MakePanel(root, 48, 215, 144, 5, C_PANEL_2, C_PANEL_2, 3);
    IconShape(track, 0, 0, 90, 5, C_CYAN, 3, false);
    MakeCenteredLabel(root, "Starting up", 16, 235, 208, 20, FONT_14, C_CYAN);
}
