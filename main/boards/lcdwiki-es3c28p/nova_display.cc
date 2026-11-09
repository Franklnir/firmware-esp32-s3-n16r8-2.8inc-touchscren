
#include <cstdint>
#include <cstddef>

enum qrcodegen_Ecc {
    qrcodegen_Ecc_LOW = 0,
    qrcodegen_Ecc_MEDIUM,
    qrcodegen_Ecc_QUARTILE,
    qrcodegen_Ecc_HIGH
};
enum qrcodegen_Mask {
    qrcodegen_Mask_AUTO = -1,
    qrcodegen_Mask_0 = 0,
    qrcodegen_Mask_1,
    qrcodegen_Mask_2,
    qrcodegen_Mask_3,
    qrcodegen_Mask_4,
    qrcodegen_Mask_5,
    qrcodegen_Mask_6,
    qrcodegen_Mask_7
};
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n)  ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)
extern "C" {
bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          enum qrcodegen_Mask mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

#include "nova_display.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#if CONFIG_SPIRAM
#include <esp_psram.h>
#endif
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <settings.h>
#include <material_symbols.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <board.h>

#include <ctime>
#include <cmath>
#include <cstring>
#include <algorithm>

#define TAG "NovaDisplay"

LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(font_material_symbols_20_4);

#define FONT_10 (&lv_font_montserrat_10)
#define FONT_12 (&lv_font_montserrat_12)
#define FONT_14 (&lv_font_montserrat_14)
#define FONT_16 (&lv_font_montserrat_14)
#define FONT_20 (&lv_font_montserrat_24)
#define FONT_24 (&lv_font_montserrat_24)
#define FONT_28 (&lv_font_montserrat_24)
#define FONT_36 (&lv_font_montserrat_24)
#define FONT_ICON (&font_material_symbols_20_4)

static lv_color_t C_BG       = lv_color_hex(0x0B1220);
static lv_color_t C_PANEL    = lv_color_hex(0x141F31);
static lv_color_t C_PANEL_2  = lv_color_hex(0x1A2940);
static lv_color_t C_BORDER   = lv_color_hex(0x2B3B52);
static lv_color_t C_CYAN     = lv_color_hex(0x63DDF2); // Synchronized Accent Color
static lv_color_t C_MINT     = lv_color_hex(0x7DE0B5);
static lv_color_t C_AMBER    = lv_color_hex(0xFFC477);
static lv_color_t C_VIOLET   = lv_color_hex(0xB8A4FF);
static lv_color_t C_RED      = lv_color_hex(0xFF8195);
static lv_color_t C_TEXT     = lv_color_hex(0xF1F5FA);
static lv_color_t C_MUTED    = lv_color_hex(0x91A0B5);
static lv_color_t C_DARK     = lv_color_hex(0x0D1728);

NovaDisplay::NovaDisplay(const NovaCallbacks& callbacks)
    : callbacks_(callbacks) {
    Settings disp("display", true);
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
    if (bat_style_ > BAT_STYLE_PERCENT_ONLY) bat_style_ = BAT_STYLE_ICON_PERCENT;

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
        if (saved_volume_ <= 0) saved_volume_ = 75;
    }

    nav_icon_buf_ = (uint8_t*)heap_caps_malloc(48 * 48 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nav_icon_buf_) nav_icon_buf_ = (uint8_t*)malloc(48 * 48 * 2);
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
    if (!th) return;

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
    ApplyContrastToThemeColors(raw_bg, raw_panel, raw_panel2, raw_border, contrast_level_,
                               eff_bg, eff_panel, eff_panel2, eff_border);

    C_BG      = lv_color_hex(eff_bg);
    C_PANEL   = lv_color_hex(eff_panel);
    C_PANEL_2 = lv_color_hex(eff_panel2);
    C_BORDER  = lv_color_hex(eff_border);
    C_DARK    = lv_color_hex(eff_bg);

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
    if (!bg) return;

    uint32_t eff_bg, eff_panel, eff_panel2, eff_border;
    ApplyContrastToThemeColors(bg->bg_color, bg->panel_color, bg->panel2_color, bg->border_color, contrast_level_,
                               eff_bg, eff_panel, eff_panel2, eff_border);

    C_BG      = lv_color_hex(eff_bg);
    C_PANEL   = lv_color_hex(eff_panel);
    C_PANEL_2 = lv_color_hex(eff_panel2);
    C_BORDER  = lv_color_hex(eff_border);
    C_DARK    = lv_color_hex(eff_bg);

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

int NovaDisplay::GetCurrentContrast() const {
    return contrast_level_;
}

void NovaDisplay::ApplySharpness(int sharpness, bool persist) {
    sharpness_level_ = sharpness;
    if (persist) {
        SetCurrentSharpnessLevel(sharpness);
    }
    ApplyTheme(GetCurrentThemeName(), false);
}

int NovaDisplay::GetCurrentSharpness() const {
    return sharpness_level_;
}

void NovaDisplay::ApplyTextWeight(int weight, bool persist) {
    text_weight_ = weight;
    if (persist) {
        SetCurrentTextWeightLevel(weight);
    }
    if (current_page_ != PAGE_SPLASH && width_ > 0) {
        BuildCurrentPage();
    }
}

int NovaDisplay::GetCurrentTextWeight() const {
    return text_weight_;
}

void NovaDisplay::ApplyTextWeightToObj(lv_obj_t* obj, lv_color_t color) {
    if (!obj) return;
    if (text_weight_ == 1) { // Sedang / Medium
        lv_obj_set_style_text_outline_stroke_color(obj, color, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_opa(obj, LV_OPA_60, LV_PART_MAIN);
    } else if (text_weight_ == 2) { // Tebal / Bold
        lv_obj_set_style_text_outline_stroke_color(obj, color, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    } else if (text_weight_ >= 3) { // Ekstra Tebal / Heavy
        lv_obj_set_style_text_outline_stroke_color(obj, color, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_width(obj, 2, LV_PART_MAIN);
        lv_obj_set_style_text_outline_stroke_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    } else { // Normal (0)
        lv_obj_set_style_text_outline_stroke_width(obj, 0, LV_PART_MAIN);
    }
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

void NovaDisplay::ApplyTextColor(const std::string& text_color_id, bool persist) {
    const auto* tc = FindTextColorById(text_color_id);
    if (!tc) return;

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
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    Settings audio_s("audio", true);
    audio_s.SetInt("notif_snd_idx", idx);
}

void NovaDisplay::PreviewNotifSound(int idx) {
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
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

    static const char* kTimezonesEnv[] = {
        "WIB-7", "WITA-8", "WIT-9", "GMT0", "JST-9", "EST5EDT,M3.2.0,M11.1.0"
    };
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
    lv_obj_set_style_border_width(obj, (lv_color_to_int(border) == lv_color_to_int(fill)) ? 0 : 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    return obj;
}

lv_obj_t* NovaDisplay::MakeLabel(lv_obj_t* parent, const char* text,
                                int x, int y, int width, int height,
                                const lv_font_t* font, lv_color_t color,
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

lv_obj_t* NovaDisplay::MakeCenteredLabel(lv_obj_t* parent, const char* text,
                                         int x, int y, int width, int height,
                                         const lv_font_t* font, lv_color_t color) {
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
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* NovaDisplay::MakeButton(lv_obj_t* parent, const char* text,
                                  int x, int y, int width, int height,
                                  lv_color_t fill, lv_color_t border, lv_color_t text_color,
                                  const lv_font_t* font,
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

lv_obj_t* NovaDisplay::MakeIcon(lv_obj_t* parent, IconKind kind, int x, int y, lv_color_t color, int size) {
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
        case ICON_AI:         sym = MATERIAL_SYMBOLS_ROBOT_2; break;
        case ICON_WIFI:       sym = MATERIAL_SYMBOLS_WIFI; break;
        case ICON_AUDIO:      sym = MATERIAL_SYMBOLS_VOLUME_UP; break;
        case ICON_FILES:      sym = MATERIAL_SYMBOLS_SD_CARD; break;
        case ICON_CHIP:       sym = MATERIAL_SYMBOLS_MEMORY; break;
        case ICON_CHRONOS:    sym = MATERIAL_SYMBOLS_SCHEDULE; break;
        case ICON_BRIGHTNESS: sym = MATERIAL_SYMBOLS_BRIGHTNESS_6; break;
        case ICON_SETTINGS:   sym = MATERIAL_SYMBOLS_SETTINGS; break;
        case ICON_HOME:       sym = MATERIAL_SYMBOLS_HOME; break;
        case ICON_APPS:       sym = MATERIAL_SYMBOLS_PLAY_ARROW; break;
        case ICON_MIC:        sym = MATERIAL_SYMBOLS_MIC; break;
        case ICON_BATTERY:    sym = MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL; break;
        case ICON_REGION:     sym = MATERIAL_SYMBOLS_LANGUAGE; break;
        case ICON_CALC:       sym = MATERIAL_SYMBOLS_CALCULATE; break;
        case ICON_GAME:       sym = MATERIAL_SYMBOLS_SPORTS_ESPORTS; break;
        case ICON_RADIO:      sym = MATERIAL_SYMBOLS_HEADPHONES; break;
        case ICON_PIANO:      sym = MATERIAL_SYMBOLS_MUSIC_NOTE; break;
        case ICON_DRAW:       sym = MATERIAL_SYMBOLS_EDIT_SQUARE; break;
        case ICON_DASHBOARD:  sym = MATERIAL_SYMBOLS_DEVICE_THERMOSTAT; break;
        case ICON_NAV:        sym = MATERIAL_SYMBOLS_NAVIGATION; break;
        case ICON_CHAT:       sym = MATERIAL_SYMBOLS_CHAT_BUBBLE; break;
        case ICON_WEATHER:    sym = MATERIAL_SYMBOLS_DEVICE_THERMOSTAT; break;
        case ICON_PHONE:      sym = MATERIAL_SYMBOLS_PHONE; break;
    }
    lv_label_set_text(lbl, sym);
    return root;
}

static void OnNavClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(event);
    if (!btn) btn = (lv_obj_t*)lv_event_get_target(event);
    auto target = static_cast<NovaDisplay::Page>((intptr_t)lv_obj_get_user_data(btn));
    self->NavigateTo(target);
}

static void OnBackClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self) self->GoBack();
}

static void OnHomeClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self) self->NavigateRoot(NovaDisplay::PAGE_HOME);
}

static void OnRecentAppsClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self) self->ShowRecentAppsModal();
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

    int title_x = 8;
    int title_w = 110;

    if (show_back) {
        lv_obj_t* back_btn = lv_btn_create(bar);
        lv_obj_set_pos(back_btn, 4, 3);
        lv_obj_set_size(back_btn, 28, 28);
        StyleButton(back_btn, C_PANEL, C_BORDER, 8);
        lv_obj_t* back_icon = lv_label_create(back_btn);
        lv_label_set_text(back_icon, MATERIAL_SYMBOLS_ARROW_BACK);
        lv_obj_set_style_text_font(back_icon, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(back_icon, C_CYAN, LV_PART_MAIN);
        lv_obj_center(back_icon);
        lv_obj_add_event_cb(back_btn, OnBackClicked, LV_EVENT_CLICKED, this);

        // Screen Rotation Button on subpages too
        lv_obj_t* rot_btn = lv_btn_create(bar);
        lv_obj_set_pos(rot_btn, 34, 4);
        lv_obj_set_size(rot_btn, 24, 26);
        StyleButton(rot_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(rot_btn, 0, LV_PART_MAIN);
        lv_obj_t* rot_icon = lv_label_create(rot_btn);
        lv_label_set_text(rot_icon, MATERIAL_SYMBOLS_REFRESH);
        lv_obj_set_style_text_font(rot_icon, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(rot_icon, C_CYAN, LV_PART_MAIN);
        lv_obj_center(rot_icon);
        lv_obj_add_event_cb(rot_btn, [](lv_event_t* e) {
            (void)e;
            Board::GetInstance().RotateDisplay();
        }, LV_EVENT_CLICKED, nullptr);

        title_x = 62;
        title_w = 70;
    } else {
        // Screen Rotation Button (Rotates display by 90 degrees)
        lv_obj_t* rot_btn = lv_btn_create(bar);
        lv_obj_set_pos(rot_btn, 6, 4);
        lv_obj_set_size(rot_btn, 24, 26);
        StyleButton(rot_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(rot_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(rot_btn, 0, LV_PART_MAIN);
        lv_obj_t* rot_icon = lv_label_create(rot_btn);
        lv_label_set_text(rot_icon, MATERIAL_SYMBOLS_REFRESH);
        lv_obj_set_style_text_font(rot_icon, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(rot_icon, C_CYAN, LV_PART_MAIN);
        lv_obj_center(rot_icon);
        lv_obj_add_event_cb(rot_btn, [](lv_event_t* e) {
            (void)e;
            Board::GetInstance().RotateDisplay();
        }, LV_EVENT_CLICKED, nullptr);

        // XiaoZhi Quick Brightness Button
        lv_obj_t* br_btn = lv_btn_create(bar);
        lv_obj_set_pos(br_btn, 32, 4);
        lv_obj_set_size(br_btn, 24, 26);
        StyleButton(br_btn, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(br_btn, 0, LV_PART_MAIN);
        lv_obj_t* br_icon = lv_label_create(br_btn);
        lv_label_set_text(br_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
        lv_obj_set_style_text_font(br_icon, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), LV_PART_MAIN);
        lv_obj_center(br_icon);
        lv_obj_set_user_data(br_btn, (void*)(intptr_t)PAGE_DISPLAY);
        lv_obj_add_event_cb(br_btn, OnNavClicked, LV_EVENT_CLICKED, this);

        title_x = 58;
        title_w = 52;
    }

    // Dynamic right status bar layout based on bat_style_
    int bat_w = 52;
    if (bat_style_ == BAT_STYLE_ICON_ONLY) {
        bat_w = 28;
    } else if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
        bat_w = 42;
    }
    int bat_x = width_ - 4 - bat_w;
    int wifi_w = 24;
    int wifi_x = bat_x - wifi_w - 4;
    int vol_w = 24;
    int vol_x = wifi_x - vol_w - 4;
    int bt_w = 22;
    int bt_x = vol_x - bt_w - 4;

    if (current_page_ == PAGE_HOME) {
        // Homescreen: omits title text so status bar icons have clean, luxurious spacing without overlap
        int notif_w = 32;
        int notif_x = bt_x - notif_w - 4;

        int notif_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        top_notif_btn_ = lv_btn_create(bar);
        lv_obj_set_pos(top_notif_btn_, notif_x, 4);
        lv_obj_set_size(top_notif_btn_, notif_w, 26);
        StyleButton(top_notif_btn_, C_BG, C_BG, 6);
        lv_obj_set_style_border_width(top_notif_btn_, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(top_notif_btn_, 0, LV_PART_MAIN);

        top_notif_icon_ = lv_label_create(top_notif_btn_);
        lv_obj_clear_flag(top_notif_icon_, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(top_notif_icon_, MATERIAL_SYMBOLS_MAIL);
        lv_obj_set_style_text_font(top_notif_icon_, FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_set_pos(top_notif_icon_, 1, 3);

        top_notif_label_ = lv_label_create(top_notif_btn_);
        lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(top_notif_label_, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(top_notif_label_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_pos(top_notif_label_, 18, 5);

        if (notif_cnt > 0) {
            char n_txt[16];
            snprintf(n_txt, sizeof(n_txt), "%d", notif_cnt);
            lv_label_set_text(top_notif_label_, n_txt);
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0x64748B), LV_PART_MAIN);
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_add_event_cb(top_notif_btn_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->NavigateTo(PAGE_NOTIF);
        }, LV_EVENT_CLICKED, this);
    } else {
        // Subpages: Show page title with ample room
        title_w = bt_x - title_x - 2;
        if (title_w < 30) title_w = 30;
        lv_obj_t* title_lbl = MakeLabel(bar, title, title_x, 7, title_w, 20, FONT_14, C_TEXT, LV_TEXT_ALIGN_LEFT);
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
    lv_obj_set_pos(bt_btn, bt_x, 4);
    lv_obj_set_size(bt_btn, bt_w, 26);
    StyleButton(bt_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(bt_btn, 0, LV_PART_MAIN);
    top_bluetooth_icon_ = lv_label_create(bt_btn);
    lv_obj_clear_flag(top_bluetooth_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_bluetooth_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
    lv_obj_set_style_text_font(top_bluetooth_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_bluetooth_icon_, ble_connected ? lv_color_hex(0x38BDF8) : lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_center(top_bluetooth_icon_);
    lv_obj_add_event_cb(bt_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->ShowBluetoothModal();
    }, LV_EVENT_CLICKED, this);

    // 2. Volume / Speaker (Tap -> PAGE_AUDIO)
    lv_obj_t* vol_btn = lv_btn_create(bar);
    lv_obj_set_pos(vol_btn, vol_x, 4);
    lv_obj_set_size(vol_btn, vol_w, 26);
    StyleButton(vol_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(vol_btn, 0, LV_PART_MAIN);
    top_mute_icon_ = lv_label_create(vol_btn);
    lv_obj_clear_flag(top_mute_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_mute_icon_, muted_ ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(top_mute_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_mute_icon_, muted_ ? C_MUTED : lv_color_hex(0x06B6D4), LV_PART_MAIN);
    lv_obj_center(top_mute_icon_);
    lv_obj_set_user_data(vol_btn, (void*)(intptr_t)PAGE_AUDIO);
    lv_obj_add_event_cb(vol_btn, OnNavClicked, LV_EVENT_CLICKED, this);

    // 3. Wi-Fi (Tap -> PAGE_WIFI, reconnecting indicator if disconnected)
    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    lv_obj_t* wifi_btn = lv_btn_create(bar);
    lv_obj_set_pos(wifi_btn, wifi_x, 4);
    lv_obj_set_size(wifi_btn, wifi_w, 26);
    StyleButton(wifi_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(wifi_btn, 0, LV_PART_MAIN);
    top_wifi_icon_ = lv_label_create(wifi_btn);
    lv_obj_clear_flag(top_wifi_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_wifi_icon_, wifi_connected ? MATERIAL_SYMBOLS_WIFI : MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(top_wifi_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_wifi_icon_, wifi_connected ? lv_color_hex(0x22C55E) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
    lv_obj_center(top_wifi_icon_);
    lv_obj_set_user_data(wifi_btn, (void*)(intptr_t)PAGE_WIFI);
    lv_obj_add_event_cb(wifi_btn, OnNavClicked, LV_EVENT_CLICKED, this);

    // 4. Battery (Tap -> PAGE_BATTERY)
    int bat_lvl = 100, bat_mv = 0, bat_raw = 0;
    bool bat_chg = false;
    if (callbacks_.get_battery) callbacks_.get_battery(bat_lvl, bat_chg, bat_mv, bat_raw);

    lv_obj_t* bat_btn = lv_btn_create(bar);
    lv_obj_set_pos(bat_btn, bat_x, 4);
    lv_obj_set_size(bat_btn, bat_w, 26);
    StyleButton(bat_btn, C_BG, C_BG, 6);
    lv_obj_set_style_border_width(bat_btn, 0, LV_PART_MAIN);

    top_battery_icon_ = lv_label_create(bat_btn);
    lv_obj_clear_flag(top_battery_icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(top_battery_icon_, bat_chg ? MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT : MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
    lv_obj_set_style_text_font(top_battery_icon_, FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_battery_icon_, bat_chg ? lv_color_hex(0xF59E0B) : (bat_lvl < 20 ? lv_color_hex(0xFF8195) : lv_color_hex(0x7DE0B5)), LV_PART_MAIN);

    top_battery_label_ = lv_label_create(bat_btn);
    lv_obj_clear_flag(top_battery_label_, LV_OBJ_FLAG_CLICKABLE);
    char b_txt[12];
    snprintf(b_txt, sizeof(b_txt), "%d%%", bat_lvl);
    lv_label_set_text(top_battery_label_, b_txt);
    lv_obj_set_style_text_font(top_battery_label_, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(top_battery_label_, C_TEXT, LV_PART_MAIN);

    if (bat_style_ == BAT_STYLE_ICON_ONLY) {
        lv_obj_center(top_battery_icon_);
        lv_obj_add_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
    } else if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
        lv_obj_add_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(top_battery_label_);
    } else { // BAT_STYLE_ICON_PERCENT
        lv_obj_set_pos(top_battery_icon_, 2, 3);
        lv_obj_set_pos(top_battery_label_, 22, 4);
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
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
    if (bt_modal_) {
        lv_obj_del(bt_modal_);
        bt_modal_ = nullptr;
    }
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
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->CloseRecentAppsModal();
    }, LV_EVENT_CLICKED, this);

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
        int action_mode; // -1 = navigate to page, >= 0 switch mode
        Page page;
    };

    const AppCardItem items[] = {
        {"Radio Streaming", "Stasiun Live Musik & Berita", MATERIAL_SYMBOLS_HEADPHONES, C_MINT, -1, PAGE_RADIO},
        {"Xiaozhi AI", "Percakapan Suara & Musik LLM", MATERIAL_SYMBOLS_ROBOT_2, C_CYAN, 0, PAGE_AI},
        {"Chronchi Watch", "Smartwatch, Kalender & Timer", MATERIAL_SYMBOLS_WATCH, C_AMBER, 1, PAGE_HOME},
        {"Beranda OS", "Dashboard Utama & Widget", MATERIAL_SYMBOLS_HOME, C_CYAN, -1, PAGE_HOME},
        {"Chronos Pomodoro", "Timer Fokus 25 Menit", MATERIAL_SYMBOLS_SCHEDULE, C_AMBER, -1, PAGE_CHRONOS},
        {"Wi-Fi & Jaringan", "Pemindai SSID & Status IP", MATERIAL_SYMBOLS_WIFI, C_MINT, -1, PAGE_WIFI},
        {"Tampilan & Layar", "Pengaturan Kecerahan Backlight", MATERIAL_SYMBOLS_BRIGHTNESS_6, C_AMBER, -1, PAGE_DISPLAY},
        {"Audio & Suara", "Driver ES8311 & Amplifier", MATERIAL_SYMBOLS_VOLUME_UP, C_CYAN, -1, PAGE_AUDIO},
        {"Baterai & Daya", "Pemantau ADC & Voltase TP4054", MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL, C_MINT, -1, PAGE_BATTERY},
        {"File Hub", "16MB Flash Storage & Partisi", MATERIAL_SYMBOLS_SD_CARD, C_VIOLET, -1, PAGE_FILES},
        {"Pengaturan Sistem", "Preferensi & Informasi Chip", MATERIAL_SYMBOLS_SETTINGS, C_VIOLET, -1, PAGE_SETTINGS},
        {"Pin Lab", "Hardware GPIO Reference Map", MATERIAL_SYMBOLS_MEMORY, C_AMBER, -1, PAGE_PINLAB},
        {"Kalkulator Sentuh", "Operasi Aritmatika & Desimal", MATERIAL_SYMBOLS_CALCULATE, C_CYAN, -1, PAGE_CALCULATOR},
        {"Game Arcade", "Koleksi Game Retro", MATERIAL_SYMBOLS_SPORTS_ESPORTS, C_AMBER, -1, PAGE_APPS}
    };

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
        lv_obj_t* title_lbl = MakeLabel(app_card, items[i].name, 36, 4, card_w - 92, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* desc_lbl = MakeLabel(app_card, items[i].desc, 36, 20, card_w - 92, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Right arrow
        MakeLabel(app_card, ">", card_w - 48, 10, 16, 18, FONT_12, items[i].color, LV_TEXT_ALIGN_CENTER);

        // Callback data
        lv_obj_set_user_data(app_card, (void*)(intptr_t)i);
        lv_obj_add_event_cb(app_card, [](lv_event_t* ev) {
            if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
            if (!self) return;
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(ev));
            self->CloseRecentAppsModal();

            if (idx == 0) {
                self->NavigateTo(PAGE_RADIO);
            } else if (idx == 1) {
                if (self->GetCallbacks().switch_mode) self->GetCallbacks().switch_mode(0);
            } else if (idx == 2) {
                if (self->GetCallbacks().switch_mode) self->GetCallbacks().switch_mode(1);
            } else if (idx == 13) {
                self->ShowGamesModal();
            } else {
                Page pages[] = {PAGE_RADIO, PAGE_AI, PAGE_HOME, PAGE_HOME, PAGE_CHRONOS, PAGE_WIFI,
                                PAGE_DISPLAY, PAGE_AUDIO, PAGE_BATTERY, PAGE_FILES,
                                PAGE_SETTINGS, PAGE_PINLAB, PAGE_CALCULATOR, PAGE_APPS};
                if (idx < (int)(sizeof(pages) / sizeof(pages[0]))) {
                    if (pages[idx] == PAGE_HOME) self->NavigateRoot(PAGE_HOME);
                    else self->NavigateTo(pages[idx]);
                }
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Bottom close / back button
    MakeButton(card, "KEMBALI KE BERANDA", 4, card_h - 42, card_w - 20, 30, C_PANEL_2, C_CYAN, C_TEXT, FONT_12,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                self->CloseRecentAppsModal();
                self->NavigateRoot(PAGE_HOME);
            }
        }, this);
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

    top_wifi_icon_ = nullptr;
    top_bluetooth_icon_ = nullptr;
    top_notif_btn_ = nullptr;
    top_notif_icon_ = nullptr;
    top_notif_label_ = nullptr;
    top_mute_icon_ = nullptr;
    top_battery_icon_ = nullptr;
    top_battery_label_ = nullptr;

    piano_status_lbl_ = nullptr;
    for (int i = 0; i < 3; ++i) piano_inst_btns_[i] = nullptr;
    for (int i = 0; i < 8; ++i) piano_white_keys_[i] = nullptr;
    for (int i = 0; i < 5; ++i) piano_black_keys_[i] = nullptr;
    sketch_tool_lbl_ = sketch_size_btn_ = nullptr;
    mon_cpu_lbl_ = mon_sram_bar_ = mon_sram_lbl_ = mon_psram_bar_ = mon_psram_lbl_ = nullptr;
    mon_temp_lbl_ = mon_wifi_lbl_ = mon_uptime_lbl_ = nullptr;
    flappy_score_lbl_ = flappy_best_lbl_ = flappy_card_ = nullptr;

    lv_obj_t* root = lv_scr_act();
    lv_obj_clean(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, C_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);

    clock_label_ = date_label_ = timer_label_ = nullptr;
    brightness_value_label_ = battery_raw_label_ = battery_percent_label_ = nullptr;
    battery_volt_label_ = battery_state_label_ = wifi_status_label_ = nullptr;
    audio_volume_label_ = audio_volume_slider_ = nullptr;
    return root;
}

void NovaDisplay::NavigateTo(Page next) {
    if (next == current_page_) return;
    if (next == PAGE_DISPLAY && current_page_ != PAGE_DISPLAY) {
        display_scroll_y_ = 0;
        display_pal_scroll_x_ = 0;
        display_bg_scroll_x_ = 0;
        display_tc_scroll_x_ = 0;
    }
    if (next == PAGE_AUDIO && current_page_ != PAGE_AUDIO) {
        audio_scroll_y_ = 0;
    }
    if (current_page_ != PAGE_SPLASH) {
        if (history_depth_ < HISTORY_CAPACITY) {
            page_history_[history_depth_++] = current_page_;
        } else {
            memmove(page_history_, page_history_ + 1, (HISTORY_CAPACITY - 1) * sizeof(page_history_[0]));
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

static void OnLaunchAiVoice(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self && self->GetCallbacks().switch_mode) {
        ESP_LOGI(TAG, "Launching XiaoZhi AI Mode (Mode 0)...");
        self->GetCallbacks().switch_mode(0);
    }
}

static void OnLaunchChronchi(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
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
            if (h == 0) h = 12;
            snprintf(time_text, sizeof(time_text), "%d:%02d %s", h, info.tm_min, info.tm_hour >= 12 ? "PM" : "AM");
        }
        static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};
        snprintf(date_text, sizeof(date_text), "%02d %s %04d", info.tm_mday, months[info.tm_mon], info.tm_year + 1900);
    }

    if (width_ > 240) {
        // Landscape Mode (320x240): Tata letak seimbang dan terpusat (Center Aligned)
        // Baris Atas (Y=48, H=72): Kartu Jam & Tanggal (Kiri) dan Kartu Xiaozhi AI (Kanan)
        lv_obj_t* time_card = MakePanel(root, 12, 48, 142, 72, C_PANEL, C_BORDER, 12);
        clock_label_ = MakeLabel(time_card, time_text, 12, 6, 120, 28, FONT_28, C_TEXT);
        MakeLabel(time_card, "WAKTU LOKAL", 12, 34, 120, 14, FONT_10, C_CYAN);
        date_label_ = MakeLabel(time_card, date_text, 12, 48, 120, 16, FONT_10, C_MUTED);

        lv_obj_t* ai_card = MakePanel(root, 166, 48, 142, 72, C_PANEL, C_BORDER, 12);
        MakeIcon(ai_card, ICON_AI, 8, 14, C_CYAN, 24);
        MakeLabel(ai_card, "Xiaozhi AI", 36, 6, 100, 18, FONT_14, C_TEXT);
        MakeLabel(ai_card, "Asisten suara LLM", 36, 24, 100, 14, FONT_10, C_MUTED);
        MakeButton(ai_card, "BUKA ASISTEN", 36, 43, 98, 22, C_CYAN, C_CYAN, C_BG, FONT_10, OnLaunchAiVoice, this);

        // Baris Bawah (Y=134, H=56): 3 Tile Aplikasi presisi di tengah layar (margin kiri/kanan 12px)
        const int tile_x[] = {12, 114, 216};
        const char* tile_names[] = {"Wi-Fi", "Watch", "Apps"};
        const IconKind tile_icons[] = {ICON_WIFI, ICON_CHRONOS, ICON_APPS};
        const lv_color_t tile_colors[] = {C_CYAN, C_AMBER, C_VIOLET};
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
        // Portrait Mode (240x320): Jam saja diturunkan 1mm (ke Y=56), teks waktu/tanggal tetap di posisi semula
        clock_label_ = MakeLabel(root, time_text, 15, 56, 210, 32, FONT_36, C_TEXT);
        MakeLabel(root, "WAKTU LOKAL", 17, 91, 100, 15, FONT_12, C_CYAN);
        date_label_ = MakeLabel(root, date_text, 17, 106, 207, 16, FONT_12, C_MUTED);

        lv_obj_t* ai_card = MakePanel(root, 12, 126, 216, 74, C_PANEL, C_BORDER, 14);
        MakeIcon(ai_card, ICON_AI, 12, 15, C_CYAN, 28);
        MakeLabel(ai_card, "Xiaozhi AI", 48, 7, 153, 22, FONT_14, C_TEXT);
        MakeLabel(ai_card, "Asisten suara LLM", 48, 28, 153, 16, FONT_10, C_MUTED);
        MakeButton(ai_card, "BUKA ASISTEN", 48, 47, 154, 22, C_CYAN, C_CYAN, C_BG, FONT_10, OnLaunchAiVoice, this);

        const int tile_x[] = {12, 88, 164};
        const char* tile_names[] = {"Radio", "Watch", "Apps"};
        const IconKind tile_icons[] = {ICON_RADIO, ICON_CHRONOS, ICON_APPS};
        const lv_color_t tile_colors[] = {C_MINT, C_AMBER, C_VIOLET};
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
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    int screen = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(event));
    if (!self) return;
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
                              const char* description, IconKind icon, lv_color_t accent, int screen) {
    lv_obj_t* tile = lv_btn_create(root);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 102, 54);
    StyleButton(tile, C_PANEL, C_BORDER, 12);
    MakeIcon(tile, icon, 8, 13, accent, 26);
    lv_obj_t* name_lbl = MakeLabel(tile, name, 40, 7, 58, 19, FONT_14, C_TEXT);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t* desc_lbl = MakeLabel(tile, description, 40, 27, 58, 18, FONT_10, C_MUTED);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_user_data(tile, (void*)(intptr_t)screen);
    lv_obj_add_event_cb(tile, OnLaunchChronchiScreen, LV_EVENT_CLICKED, this);
}

void NovaDisplay::AppTile(lv_obj_t* root, int x, int y, const char* name,
                          const char* description, IconKind icon, lv_color_t accent, Page destination) {
    lv_obj_t* tile = lv_btn_create(root);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 102, 54);
    StyleButton(tile, C_PANEL, C_BORDER, 12);
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
            int chronchi_screen; // -1: normal page, >= 0: Chronchi screen
            bool is_games_modal;
        };

        static const AppGridItem kApps[] = {
            {"Xiaozhi",    ICON_AI,         C_CYAN,                  PAGE_AI,          -1, false},
            {"Navigasi",   ICON_NAV,        C_MINT,                  PAGE_NAV,         -1, false},
            {"Musik HP",   ICON_AUDIO,      C_AMBER,                 PAGE_BT_MUSIC,    -1, false},
            {"Notif HP",   ICON_CHAT,       C_CYAN,                  PAGE_NOTIF,       -1, false},
            {"Cuaca HP",   ICON_WEATHER,    C_AMBER,                 PAGE_WEATHER,     -1, false},
            {"Cari HP",    ICON_PHONE,      C_MINT,                  PAGE_PHONE,       -1, false},
            {"Smartwatch", ICON_CHRONOS,    C_AMBER,                 PAGE_HOME,         0, false},
            {"Radio",      ICON_RADIO,      C_MINT,                  PAGE_RADIO,       -1, false},
            {"Wi-Fi",      ICON_WIFI,       C_CYAN,                  PAGE_WIFI,        -1, false},
            {"Audio",      ICON_AUDIO,      C_AMBER,                 PAGE_AUDIO,       -1, false},
            {"File Hub",   ICON_FILES,      C_VIOLET,                PAGE_FILES,       -1, false},
            {"Chronos",    ICON_CHRONOS,    C_AMBER,                 PAGE_CHRONOS,     -1, false},
            {"Tampilan",   ICON_BRIGHTNESS, C_AMBER,                 PAGE_DISPLAY,     -1, false},
            {"Baterai",    ICON_BATTERY,    C_MINT,                  PAGE_BATTERY,     -1, false},
            {"Atur",       ICON_SETTINGS,   C_VIOLET,                PAGE_SETTINGS,    -1, false},
            {"Kalkulator", ICON_CALC,       C_CYAN,                  PAGE_CALCULATOR,  -1, false},
            {"Game",       ICON_GAME,       C_AMBER,                 PAGE_APPS,        -1, true},
            {"Pin Lab",    ICON_CHIP,       C_CYAN,                  PAGE_PINLAB,      -1, false},
            {"Pintasan",   ICON_SETTINGS,   C_CYAN,                  PAGE_QUICK,       -1, false},
            {"Piano",      ICON_PIANO,      C_CYAN,                  PAGE_PIANO,       -1, false},
            {"Sketch",     ICON_DRAW,       C_VIOLET,                PAGE_SKETCH,      -1, false},
            {"Monitor",    ICON_DASHBOARD,  C_MINT,                  PAGE_MONITOR,     -1, false}
        };

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

            MakeIcon(tile, kApps[i].icon, 11, 5, kApps[i].color, 24);
            lv_obj_t* lbl = MakeCenteredLabel(tile, kApps[i].name, 0, 32, tile_w, 14, FONT_10, C_TEXT);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            if (kApps[i].chronchi_screen >= 0) {
                lv_obj_set_user_data(tile, (void*)(intptr_t)kApps[i].chronchi_screen);
                lv_obj_add_event_cb(tile, OnLaunchChronchiScreen, LV_EVENT_CLICKED, this);
            } else if (kApps[i].is_games_modal) {
                lv_obj_add_event_cb(tile, [](lv_event_t* e) {
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) self->ShowGamesModal();
                }, LV_EVENT_CLICKED, this);
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
        AppTile(list, 12, 24, "Xiaozhi", "Asisten AI", ICON_AI, C_CYAN, PAGE_AI);
        AppTile(list, 126, 24, "Radio Live", "Online Stream", ICON_RADIO, C_MINT, PAGE_RADIO);

        // Row 2: Navigasi Google Maps & Musik HP
        AppTile(list, 12, 84, "Navigasi", "Google Maps", ICON_NAV, C_MINT, PAGE_NAV);
        AppTile(list, 126, 84, "Musik HP", "Remote Media", ICON_AUDIO, C_AMBER, PAGE_BT_MUSIC);

        // Row 3: Notifikasi HP & Cuaca HP
        AppTile(list, 12, 144, "Notif HP", "Pusat Pesan", ICON_CHAT, C_CYAN, PAGE_NOTIF);
        AppTile(list, 126, 144, "Cuaca HP", "Info Cuaca", ICON_WEATHER, C_AMBER, PAGE_WEATHER);

        // Row 4: Cari HP & Smartwatch Chronos
        AppTile(list, 12, 204, "Cari HP", "Deringkan HP", ICON_PHONE, C_MINT, PAGE_PHONE);
        ChronchiTile(list, 126, 204, "Smartwatch", "Jam & Wajah", ICON_CHRONOS, C_AMBER, 0);

        // Row 5
        AppTile(list, 12, 264, "Wi-Fi", "Jaringan", ICON_WIFI, C_CYAN, PAGE_WIFI);
        AppTile(list, 126, 264, "Audio", "Suara I/O", ICON_AUDIO, C_AMBER, PAGE_AUDIO);

        // Row 6
        AppTile(list, 12, 324, "File Hub", "Berkas", ICON_FILES, C_VIOLET, PAGE_FILES);
        AppTile(list, 126, 324, "Chronos", "Jam / fokus", ICON_CHRONOS, C_AMBER, PAGE_CHRONOS);

        // Row 7
        AppTile(list, 12, 384, "Tampilan", "Kecerahan", ICON_BRIGHTNESS, C_AMBER, PAGE_DISPLAY);
        AppTile(list, 126, 384, "Baterai", "Status daya", ICON_BATTERY, C_MINT, PAGE_BATTERY);

        // Row 8
        AppTile(list, 12, 444, "Atur", "Sistem", ICON_SETTINGS, C_VIOLET, PAGE_SETTINGS);
        AppTile(list, 126, 444, "Kalkulator", "Hitung cepat", ICON_CALC, C_CYAN, PAGE_CALCULATOR);

        // Row 9
        lv_obj_t* game_tile = lv_btn_create(list);
        lv_obj_set_pos(game_tile, 12, 504);
        lv_obj_set_size(game_tile, 102, 54);
        StyleButton(game_tile, C_PANEL, C_BORDER, 12);
        MakeIcon(game_tile, ICON_GAME, 8, 13, C_AMBER, 26);
        lv_obj_t* g_name = MakeLabel(game_tile, "Game", 40, 7, 58, 19, FONT_14, C_TEXT);
        lv_label_set_long_mode(g_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* g_desc = MakeLabel(game_tile, "Koleksi Game", 40, 27, 58, 18, FONT_10, C_MUTED);
        lv_label_set_long_mode(g_desc, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_event_cb(game_tile, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->ShowGamesModal();
        }, LV_EVENT_CLICKED, this);

        AppTile(list, 126, 504, "Pin Lab", "Hardware", ICON_CHIP, C_CYAN, PAGE_PINLAB);

        // Row 10
        AppTile(list, 12, 564, "Pintasan", "Aksi Cepat", ICON_SETTINGS, C_CYAN, PAGE_QUICK);
        AppTile(list, 126, 564, "Mini Piano", "Synthesizer", ICON_PIANO, C_CYAN, PAGE_PIANO);

        // Row 11
        AppTile(list, 12, 624, "Sketchpad", "Papan Gambar", ICON_DRAW, C_VIOLET, PAGE_SKETCH);
        AppTile(list, 126, 624, "Monitor", "Task & CPU", ICON_DASHBOARD, C_MINT, PAGE_MONITOR);
    }

    MakeBottomNav(root, PAGE_APPS);
}

static void OnBrightnessSliderEvent(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self) return;
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
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);
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
                if (cur > 0) saved_volume_ = cur;
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
    if (style > BAT_STYLE_PERCENT_ONLY) style = BAT_STYLE_ICON_PERCENT;
    bat_style_ = style;
    Settings prefs("nova", true);
    prefs.SetInt("bat_style", bat_style_);
    BuildCurrentPage();
}

static void OnMuteTileClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self) self->ToggleMute();
}

void NovaDisplay::BuildQuick() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "KONTROL CEPAT");

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool wifi_connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=10, w=146): Quick toggles & Brightness slider
        lv_obj_t* wifi_tile = lv_btn_create(root);
        lv_obj_set_pos(wifi_tile, 10, 40); lv_obj_set_size(wifi_tile, 70, 44);
        StyleButton(wifi_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(wifi_tile, ICON_WIFI, 6, 10, C_CYAN, 20);
        MakeLabel(wifi_tile, "Wi-Fi", 28, 6, 40, 16, FONT_12, C_TEXT);
        MakeLabel(wifi_tile, wifi_connected ? "ON" : "OFF", 28, 22, 40, 14, FONT_10, wifi_connected ? C_MINT : C_MUTED);
        lv_obj_set_user_data(wifi_tile, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wifi_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* audio_tile = lv_btn_create(root);
        lv_obj_set_pos(audio_tile, 84, 40); lv_obj_set_size(audio_tile, 72, 44);
        StyleButton(audio_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(audio_tile, ICON_AUDIO, 6, 10, muted_ ? C_MUTED : C_AMBER, 20);
        MakeLabel(audio_tile, "Audio", 28, 6, 42, 16, FONT_12, C_TEXT);
        MakeLabel(audio_tile, muted_ ? "MUTE" : "ON", 28, 22, 42, 14, FONT_10, muted_ ? C_AMBER : C_MINT);
        lv_obj_add_event_cb(audio_tile, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* disp_tile = lv_btn_create(root);
        lv_obj_set_pos(disp_tile, 10, 88); lv_obj_set_size(disp_tile, 70, 44);
        StyleButton(disp_tile, C_PANEL, C_BORDER, 8);
        MakeIcon(disp_tile, ICON_BRIGHTNESS, 6, 10, C_AMBER, 20);
        MakeLabel(disp_tile, "Layar", 28, 6, 40, 16, FONT_12, C_TEXT);
        MakeLabel(disp_tile, "Terang", 28, 22, 40, 14, FONT_10, C_MUTED);
        lv_obj_set_user_data(disp_tile, (void*)(intptr_t)PAGE_DISPLAY);
        lv_obj_add_event_cb(disp_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* set_tile = lv_btn_create(root);
        lv_obj_set_pos(set_tile, 84, 88); lv_obj_set_size(set_tile, 72, 44);
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
        brightness_value_label_ = MakeLabel(b_card, b_txt, 90, 6, 48, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(b_card, 8, 30, 130);

        // Right Column (x=164, w=146): Big mode switch cards
        lv_obj_t* ai_btn = lv_btn_create(root);
        lv_obj_set_pos(ai_btn, 164, 40); lv_obj_set_size(ai_btn, 146, 76);
        StyleButton(ai_btn, C_PANEL, C_CYAN, 12);
        MakeIcon(ai_btn, ICON_AI, 10, 20, C_CYAN, 28);
        MakeLabel(ai_btn, "Xiaozhi AI", 42, 12, 98, 20, FONT_14, C_TEXT);
        MakeLabel(ai_btn, "Asisten Suara LLM", 42, 32, 98, 14, FONT_10, C_MUTED);
        MakeLabel(ai_btn, "BUKA ASISTEN >", 42, 50, 98, 14, FONT_10, C_CYAN);
        lv_obj_add_event_cb(ai_btn, OnLaunchAiVoice, LV_EVENT_CLICKED, this);

        lv_obj_t* chr_btn = lv_btn_create(root);
        lv_obj_set_pos(chr_btn, 164, 124); lv_obj_set_size(chr_btn, 146, 76);
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
        lv_obj_set_pos(wifi_tile, 12, 60); lv_obj_set_size(wifi_tile, 102, 50);
        StyleButton(wifi_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(wifi_tile, ICON_WIFI, 10, 12, C_CYAN, 24);
        lv_obj_t* w_name = MakeLabel(wifi_tile, "Wi-Fi", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(w_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* w_sub = MakeLabel(wifi_tile, wifi_connected ? "Terhubung" : "Buka menu", 42, 26, 56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(w_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_user_data(wifi_tile, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wifi_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* audio_tile = lv_btn_create(root);
        lv_obj_set_pos(audio_tile, 126, 60); lv_obj_set_size(audio_tile, 102, 50);
        StyleButton(audio_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(audio_tile, ICON_AUDIO, 10, 12, muted_ ? C_MUTED : C_AMBER, 24);
        lv_obj_t* a_name = MakeLabel(audio_tile, "Audio", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(a_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* a_sub = MakeLabel(audio_tile, muted_ ? "UI senyap" : "UI aktif", 42, 26, 56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(a_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_event_cb(audio_tile, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* disp_tile = lv_btn_create(root);
        lv_obj_set_pos(disp_tile, 12, 114); lv_obj_set_size(disp_tile, 102, 50);
        StyleButton(disp_tile, C_PANEL, C_BORDER, 10);
        MakeIcon(disp_tile, ICON_BRIGHTNESS, 10, 12, C_AMBER, 24);
        lv_obj_t* d_name = MakeLabel(disp_tile, "Tampilan", 42, 7, 56, 17, FONT_14, C_TEXT);
        lv_label_set_long_mode(d_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_t* d_sub = MakeLabel(disp_tile, "Brightness", 42, 26, 56, 17, FONT_10, C_MUTED);
        lv_label_set_long_mode(d_sub, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_user_data(disp_tile, (void*)(intptr_t)PAGE_DISPLAY);
        lv_obj_add_event_cb(disp_tile, OnNavClicked, LV_EVENT_CLICKED, this);

        lv_obj_t* set_tile = lv_btn_create(root);
        lv_obj_set_pos(set_tile, 126, 114); lv_obj_set_size(set_tile, 102, 50);
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
        brightness_value_label_ = MakeLabel(b_card, b_txt, 140, 6, 62, 16, FONT_12, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(b_card, 12, 28, 192);

        // Launcher buttons for Modes
        MakeButton(root, "XIAOZHI AI", 12, 226, 102, 48, C_PANEL, C_CYAN, C_CYAN, FONT_12, OnLaunchAiVoice, this);
        MakeButton(root, "CHRONCHI", 126, 226, 102, 48, C_PANEL, C_AMBER, C_AMBER, FONT_12, OnLaunchChronchi, this);
    }

    MakeBottomNav(root, PAGE_QUICK);
}

void NovaDisplay::StartWifiScan() {
    scan_state_ = 0;
    wifi_message_ = "Sedang memindai jaringan...";
    scanned_aps_.clear();
    BuildCurrentPage();

    xTaskCreate([](void* arg) {
        auto* self = static_cast<NovaDisplay*>(arg);
        esp_wifi_set_ps(WIFI_PS_NONE);
        wifi_scan_config_t scan_cfg = {};
        scan_cfg.show_hidden = false;
        esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);

        uint16_t ap_count = 0;
        if (err == ESP_OK) {
            esp_wifi_scan_get_ap_num(&ap_count);
        }
        std::vector<ScannedAp> results;
        if (ap_count > 0) {
            std::vector<wifi_ap_record_t> records(ap_count);
            esp_wifi_scan_get_ap_records(&ap_count, records.data());
            for (uint16_t i = 0; i < ap_count && i < 20; ++i) {
                if (records[i].ssid[0] != '\0') {
                    ScannedAp ap;
                    ap.ssid = (char*)records[i].ssid;
                    ap.rssi = records[i].rssi;
                    ap.open = (records[i].authmode == WIFI_AUTH_OPEN);
                    results.push_back(ap);
                }
            }
        }

        // Tampilkan juga jaringan yang tersimpan di SsidManager agar mudah dipilih
        const auto& saved_list = SsidManager::GetInstance().GetSsidList();
        for (const auto& item : saved_list) {
            bool exists = false;
            for (const auto& r : results) {
                if (r.ssid == item.ssid) { exists = true; break; }
            }
            if (!exists && !item.ssid.empty()) {
                ScannedAp saved_ap;
                saved_ap.ssid = item.ssid;
                saved_ap.rssi = -60; // indikasi sinyal tersimpan
                saved_ap.open = item.password.empty();
                results.push_back(saved_ap);
            }
        }

        lvgl_port_lock(0);
        self->scanned_aps_ = std::move(results);
        self->scan_state_ = 1;
        self->wifi_message_ = self->scanned_aps_.empty() ? "Tidak ada jaringan ditemukan." : "Ketuk jaringan untuk memilih.";
        if (self->current_page_ == PAGE_WIFI) {
            self->BuildCurrentPage();
        }
        lvgl_port_unlock();

        vTaskDelete(NULL);
    }, "nova_wifi_scan", 4096, this, 3, NULL);
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

    // 1. Simpan ke SsidManager NVS (Kunci utama agar tersimpan lintas mode XiaoZhi / Chronchi / Nova OS)
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
    if (ap_name.empty()) ap_name = "Xiaozhi-BE55";

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

    lv_obj_t* card = MakePanel(modal, 12, 50, 216, 216, lv_color_hex(0x18181B), lv_color_hex(0x38BDF8), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    MakeLabel(card, "HOTSPOT WEB AKTIF", 12, 12, 192, 20, FONT_14, lv_color_hex(0x38BDF8));
    MakeLabel(card, "1. Sambungkan HP ke Wi-Fi:", 12, 36, 192, 16, FONT_10, C_MUTED);
    MakeLabel(card, ap_name.c_str(), 12, 52, 192, 18, FONT_12, lv_color_hex(0xFDE047));
    MakeLabel(card, "2. Buka browser HP ke URL:", 12, 74, 192, 16, FONT_10, C_MUTED);
    MakeLabel(card, "http://192.168.4.1", 12, 90, 192, 18, FONT_12, lv_color_hex(0x22C55E));
    MakeLabel(card, "Tip: Matikan Data Seluler HP agar koneksi tidak keluar sendiri.", 12, 114, 192, 34, FONT_10, lv_color_hex(0x94A3B8));

    MakeButton(card, "TUTUP", 12, 162, 192, 34, lv_color_hex(0x2563EB), lv_color_hex(0x3B82F6), lv_color_white(), FONT_12,
        [](lv_event_t* e) {
            lv_obj_t* m = (lv_obj_t*)lv_event_get_user_data(e);
            if (m) lv_obj_del(m);
        }, modal);
}

void NovaDisplay::ShowBluetoothModal() {
    bool ble_conn = false;
    std::string bname = "Chronchi", bmac = "--:--:--:--:--:--";
    if (callbacks_.get_ble_status) {
        callbacks_.get_ble_status(ble_conn, bname, bmac);
    }

    if (bt_modal_) {
        lv_obj_del(bt_modal_);
        bt_modal_ = nullptr;
    }

    lv_obj_t* root = lv_layer_top();
    bt_modal_ = lv_obj_create(root);
    lv_obj_set_size(bt_modal_, width_, height_);
    lv_obj_clear_flag(bt_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bt_modal_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bt_modal_, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(bt_modal_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bt_modal_, 0, LV_PART_MAIN);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 300 : 224;
    int card_h = is_land ? 216 : 286;

    lv_obj_t* card = MakePanel(bt_modal_, 0, 0, card_w, card_h, lv_color_hex(0x18181B), lv_color_hex(0x38BDF8), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    // Header
    lv_obj_t* h_icon = lv_label_create(card);
    lv_obj_set_style_text_font(h_icon, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(h_icon, MATERIAL_SYMBOLS_BLUETOOTH);
    lv_obj_set_style_text_color(h_icon, ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_pos(h_icon, 10, 8);

    lv_obj_t* h_title = lv_label_create(card);
    lv_obj_set_style_text_font(h_title, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(h_title, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(h_title, "PAIRING APP CHRONOS");
    lv_obj_set_pos(h_title, 30, 8);

    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 24, 22);
    lv_obj_set_pos(close_btn, card_w - 30, 6);
    StyleButton(close_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, FONT_ICON, LV_PART_MAIN);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), LV_PART_MAIN);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->bt_modal_) {
            lv_obj_del(self->bt_modal_);
            self->bt_modal_ = nullptr;
        }
    }, LV_EVENT_CLICKED, this);

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

    if (qmd->img_buf) {
        for (int i = 0; i < qr_target_dim * qr_target_dim; i++) {
            qmd->img_buf[i] = 0xFFFF; // Pure white background
        }

        uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
        uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
        const char* qr_text = bmac.c_str();
        if (qrcodegen_encodeText(qr_text, tempBuffer, qrcode, qrcodegen_Ecc_LOW, 1, 10, qrcodegen_Mask_AUTO, true)) {
            int qr_size = qrcodegen_getSize(qrcode);
            int border = 2;
            int total_modules = qr_size + border * 2;
            int scale = qr_target_dim / total_modules;
            if (scale < 1) scale = 1;
            int offset_x = (qr_target_dim - (total_modules * scale)) / 2 + border * scale;
            int offset_y = (qr_target_dim - (total_modules * scale)) / 2 + border * scale;

            for (int y = 0; y < qr_size; y++) {
                for (int x = 0; x < qr_size; x++) {
                    if (qrcodegen_getModule(qrcode, x, y)) {
                        int px_start = offset_x + x * scale;
                        int py_start = offset_y + y * scale;
                        for (int dy = 0; dy < scale; dy++) {
                            int py = py_start + dy;
                            if (py < 0 || py >= qr_target_dim) continue;
                            for (int dx = 0; dx < scale; dx++) {
                                int px = px_start + dx;
                                if (px < 0 || px >= qr_target_dim) continue;
                                qmd->img_buf[py * qr_target_dim + px] = 0x0000; // Black module
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

        int qr_box_w = qr_target_dim + 8;
        int qr_box_h = qr_target_dim + 8;
        int qr_box_x = is_land ? 12 : (card_w - qr_box_w) / 2;
        int qr_box_y = is_land ? 38 : 32;

        lv_obj_t* qr_box = MakePanel(card, qr_box_x, qr_box_y, qr_box_w, qr_box_h, lv_color_white(), lv_color_hex(0x38BDF8), 8);
        lv_obj_clear_flag(qr_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* qr_img = lv_image_create(qr_box);
        lv_image_set_src(qr_img, &qmd->img_dsc);
        lv_obj_center(qr_img);

        lv_obj_add_event_cb(bt_modal_, [](lv_event_t* e) {
            auto* d = static_cast<QrModalData*>(lv_event_get_user_data(e));
            if (d) {
                if (d->img_buf) {
                    heap_caps_free(d->img_buf);
                }
                delete d;
            }
        }, LV_EVENT_DELETE, qmd);
    }

    if (!is_land) {
        // Portrait info rows
        char info_line[64];
        snprintf(info_line, sizeof(info_line), "%s • %s", bname.c_str(), bmac.c_str());
        MakeCenteredLabel(card, info_line, 0, 166, card_w - 20, 16, FONT_10, lv_color_hex(0xFDE047));

        const char* st_txt = ble_conn ? "● TERHUBUNG DENGAN CHRONOS" : "○ SIAP DI-SCAN APP CHRONOS";
        lv_color_t st_col = ble_conn ? lv_color_hex(0x22C55E) : lv_color_hex(0x38BDF8);
        MakeCenteredLabel(card, st_txt, 0, 186, card_w - 20, 16, FONT_10, st_col);

        MakeCenteredLabel(card, "Buka Chronos App di HP > Scan QR", 0, 204, card_w - 20, 16, FONT_10, C_MUTED);

        MakeButton(card, "BUKA CHRONCHI WATCH", 12, 244, card_w - 24, 32,
                   lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), lv_color_white(), FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self) {
                           if (self->bt_modal_) {
                               lv_obj_del(self->bt_modal_);
                               self->bt_modal_ = nullptr;
                           }
                           if (self->GetCallbacks().switch_mode) {
                               self->GetCallbacks().switch_mode(1);
                           }
                       }
                   }, this);
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
        MakeLabel(card, st_txt, rx, 76, rw, 16, FONT_12, st_col);

        MakeLabel(card, "Scan QR lewat Chronos App", rx, 96, rw, 32, FONT_10, lv_color_hex(0x93C5FD));

        MakeButton(card, "CHRONCHI WATCH", rx, 168, rw, 32,
                   lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), lv_color_white(), FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self) {
                           if (self->bt_modal_) {
                               lv_obj_del(self->bt_modal_);
                               self->bt_modal_ = nullptr;
                           }
                           if (self->GetCallbacks().switch_mode) {
                               self->GetCallbacks().switch_mode(1);
                           }
                       }
                   }, this);
    }
}

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
    if (rgb_slider_r_) mixer_r_ = (uint8_t)lv_slider_get_value(rgb_slider_r_);
    if (rgb_slider_g_) mixer_g_ = (uint8_t)lv_slider_get_value(rgb_slider_g_);
    if (rgb_slider_b_) mixer_b_ = (uint8_t)lv_slider_get_value(rgb_slider_b_);

    if (rgb_preview_box_ && lv_obj_is_valid(rgb_preview_box_)) {
        lv_obj_set_style_bg_color(rgb_preview_box_, lv_color_make(mixer_r_, mixer_g_, mixer_b_), 0);
    }
    if (rgb_hex_lbl_ && lv_obj_is_valid(rgb_hex_lbl_)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", mixer_r_, mixer_g_, mixer_b_);
        lv_label_set_text(rgb_hex_lbl_, buf);
    }
    if (rgb_val_r_ && lv_obj_is_valid(rgb_val_r_)) {
        char buf[16]; snprintf(buf, sizeof(buf), "%u", mixer_r_);
        lv_label_set_text(rgb_val_r_, buf);
    }
    if (rgb_val_g_ && lv_obj_is_valid(rgb_val_g_)) {
        char buf[16]; snprintf(buf, sizeof(buf), "%u", mixer_g_);
        lv_label_set_text(rgb_val_g_, buf);
    }
    if (rgb_val_b_ && lv_obj_is_valid(rgb_val_b_)) {
        char buf[16]; snprintf(buf, sizeof(buf), "%u", mixer_b_);
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
    MakeLabel(card, mixer_is_text_ ? "🎨 KUSTOM RGB WARNA TEKS" : "🎨 CUSTOM RGB MIXER (LATAR)", 12, 8, card_w - 24, 18, FONT_12, C_CYAN);

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
    auto make_slider_row = [&](int y, const char* label, lv_color_t color, uint8_t val, lv_obj_t** out_slider, lv_obj_t** out_val) {
        lv_obj_t* lbl = MakeLabel(card, label, 12, y, 16, 16, FONT_10, color);
        (void)lbl;
        char val_str[16]; snprintf(val_str, sizeof(val_str), "%u", val);
        *out_val = MakeLabel(card, val_str, card_w - 44, y, 32, 16, FONT_10, color, LV_TEXT_ALIGN_RIGHT);

        lv_obj_t* slider = lv_slider_create(card);
        lv_obj_set_pos(slider, 30, y + 2);
        lv_obj_set_size(slider, card_w - 76, 12);
        lv_slider_set_range(slider, 0, 255);
        lv_slider_set_value(slider, val, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x27272A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, color, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->UpdateRgbMixerPreview();
        }, LV_EVENT_VALUE_CHANGED, this);
        *out_slider = slider;
    };

    int base_y = is_land ? 62 : 66;
    int step_y = is_land ? 28 : 34;

    make_slider_row(base_y, "R", lv_color_hex(0xEF4444), mixer_r_, &rgb_slider_r_, &rgb_val_r_);
    make_slider_row(base_y + step_y, "G", lv_color_hex(0x22C55E), mixer_g_, &rgb_slider_g_, &rgb_val_g_);
    make_slider_row(base_y + step_y * 2, "B", lv_color_hex(0x3B82F6), mixer_b_, &rgb_slider_b_, &rgb_val_b_);

    int btn_y = is_land ? 162 : 188;
    int btn_w = (card_w - 32) / 2;

    // Apply button
    MakeButton(card, "TERAPKAN", 12, btn_y, btn_w, 32, C_PANEL, C_CYAN, C_CYAN, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            uint32_t custom_hex = ((uint32_t)self->mixer_r_ << 16) | ((uint32_t)self->mixer_g_ << 8) | (uint32_t)self->mixer_b_;
            if (self->mixer_is_text_) {
                SyncTextColorCustom(custom_hex);
                self->CloseRgbMixerModal();
                self->ApplyTextColor("custom", true);
            } else {
                SyncBgCustom(custom_hex);
                self->CloseRgbMixerModal();
                self->ApplyBackground("custom", true);
            }
            if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
        }, this);

    // Cancel button
    MakeButton(card, "BATAL", 12 + btn_w + 8, btn_y, btn_w, 32, C_PANEL, C_BORDER, C_MUTED, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->CloseRgbMixerModal();
        }, this);
}

void NovaDisplay::CloseGamesModal() {
    if (games_modal_) {
        lv_obj_del(games_modal_);
        games_modal_ = nullptr;
    }
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
    lv_obj_add_event_cb(games_modal_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && target == self->games_modal_) {
            self->CloseGamesModal();
        }
    }, LV_EVENT_CLICKED, this);

    struct GameItem {
        const char* name;
        const char* subtitle;
        const char* icon_sym;
        lv_color_t color;
        Page page;
        const char* tag;
    };

    static const GameItem kGames[] = {
        {"Flappy Bird",   "Ketuk layar & terbang",    MATERIAL_SYMBOLS_SPORTS_ESPORTS, lv_color_hex(0xF59E0B), PAGE_FLAPPY,      "ARCADE"},
        {"Game 2048",     "Puzzle geser angka",       MATERIAL_SYMBOLS_CALCULATE,      lv_color_hex(0x10B981), PAGE_2048,        "PUZZLE"},
        {"Retro Snake",   "Ular makan apel klasik",   MATERIAL_SYMBOLS_EXPLORE,        lv_color_hex(0x84CC16), PAGE_SNAKE,       "RETRO"},
        {"Brick Breaker", "Pantul bola & hancurkan",  MATERIAL_SYMBOLS_STAR,           lv_color_hex(0x06B6D4), PAGE_BRICK,       "ACTION"},
        {"Minesweeper",   "Hindari ranjau bom",       MATERIAL_SYMBOLS_WARNING,        lv_color_hex(0xEF4444), PAGE_MINESWEEPER, "LOGIC"},
        {"Highway Racer", "Balapan mobil jalan raya", MATERIAL_SYMBOLS_NAVIGATION,     lv_color_hex(0xF97316), PAGE_RACER,       "SPEED"}
    };

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
    MakeLabel(card, is_land ? "GAME ARCADE" : "KOLEKSI GAME", 36, 4, card_w - 76, 18, FONT_14, C_TEXT);
    MakeLabel(card, is_land ? "Koleksi game retro siap dimainkan" : "6 Game Retro Siap Dimainkan", 36, 22, card_w - 76, 14, FONT_10, C_MUTED);

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
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->CloseGamesModal();
    }, LV_EVENT_CLICKED, this);

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
            lv_obj_clear_flag(icon_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(icon_box);

            lv_obj_t* icon_lbl = lv_label_create(icon_box);
            lv_label_set_text(icon_lbl, kGames[i].icon_sym);
            lv_obj_set_style_text_font(icon_lbl, FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(icon_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(icon_lbl);

            // Category tag
            MakeLabel(btn, kGames[i].tag, 34, 10, tile_w - 40, 14, FONT_10, kGames[i].color, LV_TEXT_ALIGN_RIGHT);

            // Game Name
            lv_obj_t* name_lbl = MakeLabel(btn, kGames[i].name, 6, 36, tile_w - 12, 16, FONT_10, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Play badge
            MakeLabel(btn, "MAIN  ▶", 6, 54, tile_w - 12, 16, FONT_10, kGames[i].color);

            lv_obj_set_user_data(btn, (void*)(intptr_t)kGames[i].page);
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                Page target = (Page)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
                self->CloseGamesModal();
                self->NavigateTo(target);
            }, LV_EVENT_CLICKED, this);
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
            lv_obj_clear_flag(icon_box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(icon_box);

            lv_obj_t* icon_lbl = lv_label_create(icon_box);
            lv_label_set_text(icon_lbl, kGames[i].icon_sym);
            lv_obj_set_style_text_font(icon_lbl, FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(icon_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(icon_lbl);

            // Name & Subtitle
            lv_obj_t* name_lbl = MakeLabel(card_btn, kGames[i].name, 38, 3, 116, 16, FONT_12, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* sub_lbl = MakeLabel(card_btn, kGames[i].subtitle, 38, 19, 116, 14, FONT_10, C_MUTED);
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
            lv_obj_clear_flag(play_pill, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            ClearDecoration(play_pill);

            lv_obj_t* play_lbl = lv_label_create(play_pill);
            lv_label_set_text(play_lbl, "MAIN ▶");
            lv_obj_set_style_text_font(play_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(play_lbl, kGames[i].color, LV_PART_MAIN);
            lv_obj_center(play_lbl);

            lv_obj_set_user_data(card_btn, (void*)(intptr_t)kGames[i].page);
            lv_obj_add_event_cb(card_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                Page target = (Page)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
                self->CloseGamesModal();
                self->NavigateTo(target);
            }, LV_EVENT_CLICKED, this);
        }
    }
}

void NovaDisplay::ShowWifiPasswordDialog(const std::string& ssid) {
    ShowWifiInputDialog(ssid);
}

void NovaDisplay::ShowWifiInputDialog(const std::string& default_ssid) {
    if (wifi_modal_open_) return;
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
    int card_h = is_land ? (height_ - 8) : 304;

    // 2. Kartu Dialog Mengambang (Floating Card asli XiaoZhi)
    lv_obj_t* card = lv_obj_create(wifi_modal_);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18181B), LV_PART_MAIN); // Zinc 900
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2563EB), LV_PART_MAIN); // Vibrant Blue Border
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
        lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->wifi_modal_) {
                lv_obj_del(self->wifi_modal_);
                self->wifi_modal_ = nullptr;
                self->wifi_ssid_input_ = nullptr;
                self->wifi_password_input_ = nullptr;
                self->wifi_keyboard_ = nullptr;
                self->wifi_modal_open_ = false;
            }
        }, LV_EVENT_CLICKED, this);

        // Row 2: Inputs & Action buttons in one compact line (Y=24)
        if (!is_manual) {
            lv_obj_t* ssid_lbl = lv_label_create(card);
            lv_obj_set_pos(ssid_lbl, 2, 26);
            lv_obj_set_size(ssid_lbl, 96, 24);
            lv_obj_set_style_text_font(ssid_lbl, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFDE047), LV_PART_MAIN);
            lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text(ssid_lbl, default_ssid.c_str());

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 100, 24);
            lv_obj_set_size(wifi_password_input_, 112, 26);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 4, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_password_input_, 3, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 216, 24);
            lv_obj_set_size(eye_btn, 28, 26);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 4);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(eye_btn, [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->wifi_password_input_) return;
                self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                lv_textarea_set_password_mode(self->wifi_password_input_, !self->wifi_pwd_visible_);
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (lbl) {
                    lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
                    lv_obj_set_style_text_color(lbl, self->wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
                }
            }, LV_EVENT_CLICKED, this);

            MakeButton(card, "OK", 248, 24, 52, 26, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E), lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_password_input_) return;
                    std::string pass = lv_textarea_get_text(self->wifi_password_input_);
                    self->ConnectWifi(self->selected_ssid_, pass);
                }, this);
        } else {
            wifi_ssid_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_ssid_input_, 2, 24);
            lv_obj_set_size(wifi_ssid_input_, 106, 26);
            lv_textarea_set_one_line(wifi_ssid_input_, true);
            lv_textarea_set_placeholder_text(wifi_ssid_input_, "Nama SSID...");
            lv_obj_set_style_bg_color(wifi_ssid_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_ssid_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_ssid_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_ssid_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_ssid_input_, 4, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_ssid_input_, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_ssid_input_, 3, LV_PART_MAIN);

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 112, 24);
            lv_obj_set_size(wifi_password_input_, 100, 26);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 4, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_10, LV_PART_MAIN);
            lv_obj_set_style_pad_all(wifi_password_input_, 3, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 216, 24);
            lv_obj_set_size(eye_btn, 28, 26);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 4);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(eye_btn, [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->wifi_password_input_) return;
                self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                lv_textarea_set_password_mode(self->wifi_password_input_, !self->wifi_pwd_visible_);
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (lbl) {
                    lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
                    lv_obj_set_style_text_color(lbl, self->wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
                }
            }, LV_EVENT_CLICKED, this);

            MakeButton(card, "OK", 248, 24, 52, 26, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E), lv_color_white(), FONT_10,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_ssid_input_) return;
                    std::string s = lv_textarea_get_text(self->wifi_ssid_input_);
                    std::string p = self->wifi_password_input_ ? lv_textarea_get_text(self->wifi_password_input_) : "";
                    if (!s.empty()) {
                        self->ConnectWifi(s, p);
                    }
                }, this);
        }

        // Virtual Keyboard spanning full width of card
        wifi_keyboard_ = lv_keyboard_create(card);
        lv_obj_set_size(wifi_keyboard_, card_w - 6, card_h - 54);
        lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, -2);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // 3. Header Dialog
        lv_obj_t* header = lv_obj_create(card);
        lv_obj_set_size(header, 226, 30);
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
        lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->wifi_modal_) {
                lv_obj_del(self->wifi_modal_);
                self->wifi_modal_ = nullptr;
                self->wifi_ssid_input_ = nullptr;
                self->wifi_password_input_ = nullptr;
                self->wifi_keyboard_ = nullptr;
                self->wifi_modal_open_ = false;
            }
        }, LV_EVENT_CLICKED, this);

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
            lv_obj_set_pos(wifi_password_input_, 6, 52);
            lv_obj_set_size(wifi_password_input_, 174, 32);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata sandi...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_12, LV_PART_MAIN);

            // Tombol Intip Sandi
            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 184, 52);
            lv_obj_set_size(eye_btn, 40, 32);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(eye_btn, [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->wifi_password_input_) return;
                self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                lv_textarea_set_password_mode(self->wifi_password_input_, !self->wifi_pwd_visible_);
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (lbl) {
                    lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
                    lv_obj_set_style_text_color(lbl, self->wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
                }
            }, LV_EVENT_CLICKED, this);

            // Tombol Aksi: Batal & Hubungkan
            MakeButton(card, "BATAL", 6, 88, 80, 26, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                }, this);

            MakeButton(card, "HUBUNGKAN", 90, 88, 134, 26, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E), lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_password_input_) return;
                    std::string pass = lv_textarea_get_text(self->wifi_password_input_);
                    self->ConnectWifi(self->selected_ssid_, pass);
                }, this);
        } else {
            // Mode Manual: Input SSID Baru
            wifi_ssid_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_ssid_input_, 6, 30);
            lv_obj_set_size(wifi_ssid_input_, 218, 26);
            lv_textarea_set_one_line(wifi_ssid_input_, true);
            lv_textarea_set_placeholder_text(wifi_ssid_input_, "Nama SSID / Kode...");
            lv_obj_set_style_bg_color(wifi_ssid_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_ssid_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_ssid_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_ssid_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_ssid_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_ssid_input_, FONT_12, LV_PART_MAIN);

            wifi_password_input_ = lv_textarea_create(card);
            lv_obj_set_pos(wifi_password_input_, 6, 58);
            lv_obj_set_size(wifi_password_input_, 174, 26);
            lv_textarea_set_one_line(wifi_password_input_, true);
            lv_textarea_set_password_mode(wifi_password_input_, !wifi_pwd_visible_);
            lv_textarea_set_placeholder_text(wifi_password_input_, "Kata Sandi (opsional)...");
            lv_obj_set_style_bg_color(wifi_password_input_, lv_color_hex(0x27272A), LV_PART_MAIN);
            lv_obj_set_style_text_color(wifi_password_input_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_color(wifi_password_input_, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            lv_obj_set_style_border_width(wifi_password_input_, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(wifi_password_input_, 6, LV_PART_MAIN);
            lv_obj_set_style_text_font(wifi_password_input_, FONT_12, LV_PART_MAIN);

            lv_obj_t* eye_btn = lv_btn_create(card);
            lv_obj_set_pos(eye_btn, 184, 58);
            lv_obj_set_size(eye_btn, 40, 26);
            StyleButton(eye_btn, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), 6);
            lv_obj_t* eye_lbl = lv_label_create(eye_btn);
            lv_obj_set_style_text_font(eye_lbl, FONT_ICON, LV_PART_MAIN);
            lv_label_set_text(eye_lbl, wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(eye_lbl, wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
            lv_obj_center(eye_lbl);
            lv_obj_add_event_cb(eye_btn, [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self || !self->wifi_password_input_) return;
                self->wifi_pwd_visible_ = !self->wifi_pwd_visible_;
                lv_textarea_set_password_mode(self->wifi_password_input_, !self->wifi_pwd_visible_);
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (lbl) {
                    lv_label_set_text(lbl, self->wifi_pwd_visible_ ? MATERIAL_SYMBOLS_LOCK_OPEN : MATERIAL_SYMBOLS_LOCK);
                    lv_obj_set_style_text_color(lbl, self->wifi_pwd_visible_ ? lv_color_hex(0x10B981) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
                }
            }, LV_EVENT_CLICKED, this);

            MakeButton(card, "BATAL", 6, 86, 80, 24, lv_color_hex(0x3F3F46), lv_color_hex(0x52525B), lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (self && self->wifi_modal_) {
                        lv_obj_del(self->wifi_modal_);
                        self->wifi_modal_ = nullptr;
                        self->wifi_ssid_input_ = nullptr;
                        self->wifi_password_input_ = nullptr;
                        self->wifi_keyboard_ = nullptr;
                        self->wifi_modal_open_ = false;
                    }
                }, this);

            MakeButton(card, "HUBUNGKAN", 90, 86, 134, 24, lv_color_hex(0x16A34A), lv_color_hex(0x22C55E), lv_color_white(), FONT_12,
                [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self || !self->wifi_ssid_input_) return;
                    std::string s = lv_textarea_get_text(self->wifi_ssid_input_);
                    std::string p = self->wifi_password_input_ ? lv_textarea_get_text(self->wifi_password_input_) : "";
                    if (!s.empty()) {
                        self->ConnectWifi(s, p);
                    }
                }, this);
        }

        // 4. Keyboard Virtual Asli (Native LVGL XiaoZhi Keyboard)
        wifi_keyboard_ = lv_keyboard_create(card);
        lv_obj_set_size(wifi_keyboard_, 226, 174);
        lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    lv_keyboard_set_textarea(wifi_keyboard_, is_manual ? wifi_ssid_input_ : wifi_password_input_);
    lv_obj_add_state(is_manual ? wifi_ssid_input_ : wifi_password_input_, LV_STATE_FOCUSED);

    if (is_manual) {
        lv_obj_add_event_cb(wifi_ssid_input_, [](lv_event_t* e) {
            auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
            if (kb) lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_CLICKED, wifi_keyboard_);

        lv_obj_add_event_cb(wifi_password_input_, [](lv_event_t* e) {
            auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
            if (kb) lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_CLICKED, wifi_keyboard_);
    }

    // Event listener keyboard asli: LV_EVENT_READY & LV_EVENT_CANCEL
    lv_obj_add_event_cb(wifi_keyboard_, [](lv_event_t* e) {
        auto code = lv_event_get_code(e);
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        if (code == LV_EVENT_READY) {
            std::string s = self->selected_ssid_;
            if (self->wifi_ssid_input_) {
                const char* st = lv_textarea_get_text(self->wifi_ssid_input_);
                if (st && st[0] != '\0') s = st;
            }
            std::string p = self->wifi_password_input_ ? lv_textarea_get_text(self->wifi_password_input_) : "";
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
    }, LV_EVENT_ALL, this);
}

static void OnScanWifiBtn(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (self) self->StartWifiScan();
}

void NovaDisplay::BuildWifi() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "WI-FI & JARINGAN");

    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240) ===
        // Left Column (x=10, w=146): Status & Action Buttons
        lv_obj_t* scard = MakePanel(root, 10, 40, 146, 44, C_PANEL, C_BORDER, 8);
        MakeIcon(scard, ICON_WIFI, 8, 10, connected ? C_MINT : C_AMBER, 22);
        MakeLabel(scard, connected ? "Terhubung" : "Menghubungkan...", 36, 4, 104, 16, FONT_12, C_TEXT);

        if (connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid = (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + " | " + ip_str;
            wifi_status_label_ = MakeLabel(scard, details.c_str(), 36, 22, 104, 14, FONT_10, C_CYAN);
        } else {
            wifi_status_label_ = MakeLabel(scard, "Wi-Fi Terputus", 36, 22, 104, 14, FONT_10, lv_color_hex(0xF59E0B));
        }
        lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        const char* scan_txt = (scan_state_ == 0) ? "PINDAI..." : "PINDAI";
        MakeButton(root, scan_txt, 10, 88, 70, 26, C_CYAN, C_CYAN, C_BG, FONT_10, OnScanWifiBtn, this);
        MakeButton(root, "+ MANUAL", 84, 88, 72, 26, C_PANEL, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowWifiInputDialog("");
            }, this);

        MakeButton(root, "LUPAKAN", 10, 118, 70, 26, C_PANEL, lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ForgetWifi();
            }, this);

        MakeButton(root, "HOTSPOT", 84, 118, 72, 26, C_PANEL, lv_color_hex(0x38BDF8), lv_color_hex(0x38BDF8), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->StartWebHotspot();
            }, this);

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
            const char* empty_txt = (scan_state_ < 0) ? "Tekan PINDAI untuk mencari." : "Tidak ada Wi-Fi.";
            MakeLabel(list, empty_txt, 4, 8, 142, 42, FONT_10, C_MUTED);
        } else {
            for (size_t i = 0; i < scanned_aps_.size(); ++i) {
                const int y = (int)i * 32;
                lv_obj_t* row = lv_btn_create(list);
                lv_obj_set_pos(row, 0, y);
                lv_obj_set_size(row, 144, 28);
                StyleButton(row, C_PANEL, C_BORDER, 6);

                lv_obj_t* ssid_lbl = MakeLabel(row, scanned_aps_[i].ssid.c_str(), 6, 5, 90, 16, FONT_10, C_TEXT);
                lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", scanned_aps_[i].rssi);
                MakeLabel(row, rssi_str, 96, 5, 44, 16, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);

                lv_obj_set_user_data(row, (void*)(intptr_t)i);
                lv_obj_add_event_cb(row, [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self) return;
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
                    if (idx >= 0 && idx < (int)self->scanned_aps_.size()) {
                        const auto& ap = self->scanned_aps_[idx];
                        if (ap.open) {
                            self->ConnectWifi(ap.ssid, "");
                        } else {
                            self->ShowWifiPasswordDialog(ap.ssid);
                        }
                    }
                }, LV_EVENT_CLICKED, this);
            }
        }
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* scard = MakePanel(root, 12, 42, 216, 48, C_PANEL, C_BORDER, 10);
        MakeIcon(scard, ICON_WIFI, 10, 11, connected ? C_MINT : C_AMBER, 25);
        MakeLabel(scard, connected ? "Terhubung" : "Menghubungkan...", 44, 5, 157, 18, FONT_14, C_TEXT);

        if (connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid = (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + "  |  " + ip_str;
            wifi_status_label_ = MakeLabel(scard, details.c_str(), 44, 25, 160, 18, FONT_10, C_CYAN);
        } else {
            wifi_status_label_ = MakeLabel(scard, "Menghubungkan kembali...", 44, 25, 160, 18, FONT_10, lv_color_hex(0xF59E0B));
        }
        lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        const char* scan_txt = (scan_state_ == 0) ? "MEMINDAI..." : "PINDAI";
        MakeButton(root, scan_txt, 12, 92, 102, 26, C_CYAN, C_CYAN, C_BG, FONT_10, OnScanWifiBtn, this);
        MakeButton(root, "+ MANUAL", 126, 92, 102, 26, C_PANEL, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowWifiInputDialog("");
            }, this);

        MakeButton(root, "LUPAKAN", 12, 122, 102, 26, C_PANEL, lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ForgetWifi();
            }, this);

        MakeButton(root, "HOTSPOT", 126, 122, 102, 26, C_PANEL, lv_color_hex(0x38BDF8), lv_color_hex(0x38BDF8), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->StartWebHotspot();
            }, this);

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
            const char* empty_txt = (scan_state_ < 0) ? "Tekan pindai untuk mencari jaringan." : "Tidak ada jaringan. Coba pindai lagi.";
            MakeLabel(list, empty_txt, 4, 8, 204, 42, FONT_10, C_MUTED);
        } else {
            for (size_t i = 0; i < scanned_aps_.size(); ++i) {
                const int y = (int)i * 34;
                lv_obj_t* row = lv_btn_create(list);
                lv_obj_set_pos(row, 0, y);
                lv_obj_set_size(row, 202, 30);
                StyleButton(row, C_PANEL, C_BORDER, 8);

                lv_obj_t* ssid_lbl = MakeLabel(row, scanned_aps_[i].ssid.c_str(), 8, 6, 142, 18, FONT_10, C_TEXT);
                lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d dBm", scanned_aps_[i].rssi);
                MakeLabel(row, rssi_str, 149, 6, 47, 18, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);

                lv_obj_set_user_data(row, (void*)(intptr_t)i);
                lv_obj_add_event_cb(row, [](lv_event_t* ev) {
                    if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(ev));
                    if (!self) return;
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
                    if (idx >= 0 && idx < (int)self->scanned_aps_.size()) {
                        const auto& ap = self->scanned_aps_[idx];
                        if (ap.open) {
                            self->ConnectWifi(ap.ssid, "");
                        } else {
                            self->ShowWifiPasswordDialog(ap.ssid);
                        }
                    }
                }, LV_EVENT_CLICKED, this);
            }
        }
    }

    MakeBottomNav(root, PAGE_WIFI);
}

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
        MakeLabel(scard, "AI Voice siap. Tekan tombol kanan untuk mulai chat suara.", 8, 22, 128, 48, FONT_10, C_AMBER);

        // Right Column (x=164, w=144): Large Action Buttons
        lv_obj_t* ai_btn = lv_btn_create(root);
        lv_obj_set_pos(ai_btn, 164, 40); lv_obj_set_size(ai_btn, 144, 96);
        StyleButton(ai_btn, C_PANEL, C_CYAN, 12);
        MakeIcon(ai_btn, ICON_AI, 12, 30, C_CYAN, 32);
        MakeLabel(ai_btn, "BUKA ASISTEN", 48, 26, 90, 20, FONT_14, C_CYAN);
        MakeLabel(ai_btn, "Mulai dialog LLM", 48, 48, 90, 14, FONT_10, C_TEXT);
        lv_obj_add_event_cb(ai_btn, OnLaunchAiVoice, LV_EVENT_CLICKED, this);

        lv_obj_t* wbtn = MakeButton(root, "ATUR WI-FI", 164, 144, 144, 44, C_PANEL, C_BORDER, C_TEXT, FONT_12);
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
        MakeLabel(root, "Mode AI siap. Tekan tombol untuk membuka percakapan suara penuh.", 14, 164, 212, 30, FONT_10, C_AMBER);

        MakeButton(root, "BUKA ASISTEN SUARA", 12, 200, 216, 36, C_CYAN, C_CYAN, C_BG, FONT_12, OnLaunchAiVoice, this);

        lv_obj_t* wbtn = MakeButton(root, "ATUR KONEKSI WI-FI", 12, 242, 216, 28, C_PANEL, C_BORDER, C_TEXT, FONT_10);
        lv_obj_set_user_data(wbtn, (void*)(intptr_t)PAGE_WIFI);
        lv_obj_add_event_cb(wbtn, OnNavClicked, LV_EVENT_CLICKED, this);
    }

    MakeBottomNav(root, PAGE_AI);
}

void NovaDisplay::SettingsRow(lv_obj_t* root, int y, const char* name,
                             const char* detail, IconKind icon, lv_color_t accent, Page target) {
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
            {"Penyimpanan", "MicroSD & flash", ICON_FILES, C_VIOLET, PAGE_FILES}
        };

        static const SettingItem kRightItems[] = {
            {"Baterai & daya", "Status ADC TP4054", ICON_BATTERY, C_MINT, PAGE_BATTERY},
            {"Bahasa & wilayah", "Indonesia / WIB", ICON_REGION, C_CYAN, PAGE_REGION},
            {"Informasi sistem", "Chip ESP32-S3", ICON_CHIP, C_VIOLET, PAGE_SYSTEM},
            {"Akses Cepat", "Launcher pintasan", ICON_SETTINGS, C_CYAN, PAGE_QUICK}
        };

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

        SettingsRow(list, 24,  "Tampilan", "Kecerahan layar", ICON_BRIGHTNESS, C_AMBER, PAGE_DISPLAY);
        SettingsRow(list, 58,  "Jaringan", "Wi-Fi & koneksi", ICON_WIFI, C_CYAN, PAGE_WIFI);
        SettingsRow(list, 92,  "Audio & suara", "Volume, mute, haptik", ICON_AUDIO, C_AMBER, PAGE_AUDIO);
        SettingsRow(list, 126, "Penyimpanan", "Flash & sistem berkas", ICON_FILES, C_VIOLET, PAGE_FILES);
        SettingsRow(list, 160, "Baterai & daya", "Pembacaan ADC & status", ICON_BATTERY, C_MINT, PAGE_BATTERY);
        SettingsRow(list, 194, "Bahasa & wilayah", "Indonesia / WIB", ICON_REGION, C_CYAN, PAGE_REGION);
        SettingsRow(list, 228, "Informasi sistem", "Chip & memori", ICON_CHIP, C_VIOLET, PAGE_SYSTEM);
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
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        int w = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
        self->ApplyTextWeight(w, true);
        if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    };

    auto on_contrast_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        int c = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
        self->ApplyContrast(c, true);
        if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    };

    auto on_sharpness_click = [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        int s = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
        self->ApplySharpness(s, true);
        if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    };

    auto make_level_button = [&](lv_obj_t* p, const char* label, int x, int y, int w, int h, bool active, lv_event_cb_t cb, int val) {
        lv_color_t fill = active ? C_CYAN : C_PANEL;
        lv_color_t border = active ? C_CYAN : C_BORDER;
        lv_color_t txt_col = active ? C_BG : C_TEXT;
        lv_obj_t* btn = MakeButton(p, label, x, y, w, h, fill, border, txt_col, FONT_10, cb, this);
        lv_obj_set_user_data(btn, (void*)(intptr_t)val);
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
        brightness_value_label_ = MakeLabel(card, val, 76, 6, 60, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(card, 8, 26, 130);

        MakeButton(root, "REDUP", 8, 106, 45, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(55);
            }, this);

        MakeButton(root, "SEDANG", 58, 106, 45, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(145);
            }, this);

        MakeButton(root, "TERANG", 108, 106, 46, 26, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(235);
            }, this);

        MakeButton(root, "ROTASI LAYAR 90°", 8, 136, 146, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            }, nullptr);

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
        lv_obj_set_flex_align(pal_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(pal_box, C_PANEL, 0);
        lv_obj_set_style_border_color(pal_box, C_BORDER, 0);
        lv_obj_set_style_border_width(pal_box, 1, 0);
        lv_obj_set_style_radius(pal_box, 6, 0);
        lv_obj_set_style_pad_all(pal_box, 2, 0);
        lv_obj_set_style_pad_column(pal_box, 4, 0);
        lv_obj_set_scroll_dir(pal_box, LV_DIR_HOR);
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
            bool is_active = (lv_color_to_int(C_CYAN) == lv_color_to_int(lv_color_hex(themes[i].accent_color)));
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(themes[i].border_color), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* th_table = GetThemeSyncTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    self->ApplyTheme(th_table[idx].theme_name, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        // 2. Background Colors
        MakeLabel(right_list, "WARNA LATAR (25 PILIHAN)", 2, 48, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* bg_box = lv_obj_create(right_list);
        lv_obj_set_pos(bg_box, 2, 62);
        lv_obj_set_size(bg_box, 148, 28);
        lv_obj_set_flex_flow(bg_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bg_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(bg_box, C_PANEL, 0);
        lv_obj_set_style_border_color(bg_box, C_BORDER, 0);
        lv_obj_set_style_border_width(bg_box, 1, 0);
        lv_obj_set_style_radius(bg_box, 6, 0);
        lv_obj_set_style_pad_all(bg_box, 2, 0);
        lv_obj_set_style_pad_column(bg_box, 4, 0);
        lv_obj_set_scroll_dir(bg_box, LV_DIR_HOR);
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
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);
            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* bg_table = GetBackgroundTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    self->ApplyBackground(bg_table[idx].id, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        MakeButton(right_list, "🎨 RGB LATAR", 2, 94, 148, 22, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRgbMixerModal(false);
            }, this);

        // 3. Text Colors (Full Palette)
        MakeLabel(right_list, "WARNA TEKS (31 PILIHAN)", 2, 120, 148, 14, FONT_10, C_MUTED);
        lv_obj_t* tc_box = lv_obj_create(right_list);
        lv_obj_set_pos(tc_box, 2, 134);
        lv_obj_set_size(tc_box, 148, 28);
        lv_obj_set_flex_flow(tc_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tc_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(tc_box, C_PANEL, 0);
        lv_obj_set_style_border_color(tc_box, C_BORDER, 0);
        lv_obj_set_style_border_width(tc_box, 1, 0);
        lv_obj_set_style_radius(tc_box, 6, 0);
        lv_obj_set_style_pad_all(tc_box, 2, 0);
        lv_obj_set_style_pad_column(tc_box, 4, 0);
        lv_obj_set_scroll_dir(tc_box, LV_DIR_HOR);
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
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);

            if (strcmp(tcs[i].id, "auto") == 0) {
                lv_obj_t* albl = lv_label_create(dot);
                lv_label_set_text(albl, "A");
                lv_obj_set_style_text_font(albl, FONT_10, 0);
                lv_obj_set_style_text_color(albl, lv_color_black(), 0);
                lv_obj_center(albl);
            }

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* tc_table = GetTextColorTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    self->ApplyTextColor(tc_table[idx].id, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        MakeButton(right_list, "🎨 RGB TEKS", 2, 166, 148, 22, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRgbMixerModal(true);
            }, this);

        // 4. Text Weight (Ketebalan Teks)
        MakeLabel(right_list, "KETEBALAN TEKS", 2, 192, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "NORM", 2, 206, 35, 22, cur_weight == 0, on_weight_click, 0);
        make_level_button(right_list, "MDM", 39, 206, 35, 22, cur_weight == 1, on_weight_click, 1);
        make_level_button(right_list, "BOLD", 76, 206, 36, 22, cur_weight == 2, on_weight_click, 2);
        make_level_button(right_list, "XTRA", 114, 206, 36, 22, cur_weight == 3, on_weight_click, 3);

        // 5. Contrast (Kontras Tampilan)
        MakeLabel(right_list, "KONTRAS TAMPILAN", 2, 232, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "LMBT", 2, 246, 35, 22, cur_contrast == 0, on_contrast_click, 0);
        make_level_button(right_list, "NORM", 39, 246, 35, 22, cur_contrast == 1, on_contrast_click, 1);
        make_level_button(right_list, "TNGI", 76, 246, 36, 22, cur_contrast == 2, on_contrast_click, 2);
        make_level_button(right_list, "EKST", 114, 246, 36, 22, cur_contrast == 3, on_contrast_click, 3);

        // 6. Sharpness (Ketajaman Aksen Tema)
        MakeLabel(right_list, "KETAJAMAN WARNA", 2, 272, 148, 14, FONT_10, C_MUTED);
        make_level_button(right_list, "HLUS", 2, 286, 35, 22, cur_sharpness == 0, on_sharpness_click, 0);
        make_level_button(right_list, "STND", 39, 286, 35, 22, cur_sharpness == 1, on_sharpness_click, 1);
        make_level_button(right_list, "TAJM", 76, 286, 36, 22, cur_sharpness == 2, on_sharpness_click, 2);
        make_level_button(right_list, "EKST", 114, 286, 36, 22, cur_sharpness == 3, on_sharpness_click, 3);

    } else {
        // === PORTRAIT LAYOUT (240x320) WITH SMOOTH SCROLLING ===
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 0, 36);
        lv_obj_set_size(list, 240, height_ - 67);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 180, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        MakeLabel(list, "Kecerahan layar", 14, 6, 212, 20, FONT_14, C_TEXT);
        MakeLabel(list, "Atur intensitas backlight perangkat.", 14, 26, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* card = MakePanel(list, 12, 46, 216, 66, C_PANEL, C_BORDER, 10);
        char val[24];
        snprintf(val, sizeof(val), "%u / 255", brightness_);
        brightness_value_label_ = MakeLabel(card, val, 12, 6, 190, 16, FONT_12, C_CYAN, LV_TEXT_ALIGN_RIGHT);
        MakeBrightnessSlider(card, 12, 28, 190);

        MakeLabel(list, "PILIHAN KECERAHAN", 14, 118, 212, 14, FONT_10, C_MUTED);

        MakeButton(list, "REDUP", 12, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(55);
            }, this);

        MakeButton(list, "SEDANG", 88, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(145);
            }, this);

        MakeButton(list, "TERANG", 164, 134, 64, 30, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ApplyBrightness(235);
            }, this);

        // Section 1: Synchronized Theme Accent Colors (10 Themes)
        MakeLabel(list, "TEMA WARNA SINKRON (AKSEN)", 14, 172, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* pal_box = lv_obj_create(list);
        lv_obj_set_pos(pal_box, 12, 188);
        lv_obj_set_size(pal_box, 216, 34);
        lv_obj_set_flex_flow(pal_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pal_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(pal_box, C_PANEL, 0);
        lv_obj_set_style_border_color(pal_box, C_BORDER, 0);
        lv_obj_set_style_border_width(pal_box, 1, 0);
        lv_obj_set_style_radius(pal_box, 8, 0);
        lv_obj_set_style_pad_all(pal_box, 3, 0);
        lv_obj_set_style_pad_column(pal_box, 6, 0);
        lv_obj_set_scroll_dir(pal_box, LV_DIR_HOR);
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
            bool is_active = (lv_color_to_int(C_CYAN) == lv_color_to_int(lv_color_hex(themes[i].accent_color)));
            lv_obj_set_style_border_width(dot, is_active ? 2 : 1, 0);
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(themes[i].border_color), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* th_table = GetThemeSyncTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                    if (p_box) {
                        self->display_pal_scroll_x_ = lv_obj_get_scroll_x(p_box);
                        lv_obj_t* p_list = lv_obj_get_parent(p_box);
                        if (p_list) self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                    }
                    self->ApplyTheme(th_table[idx].theme_name, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        // Section 2: Custom Background Colors (25 Choices)
        MakeLabel(list, "WARNA BACKGROUND (25 PILIHAN)", 14, 228, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* bg_box = lv_obj_create(list);
        lv_obj_set_pos(bg_box, 12, 244);
        lv_obj_set_size(bg_box, 216, 34);
        lv_obj_set_flex_flow(bg_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bg_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(bg_box, C_PANEL, 0);
        lv_obj_set_style_border_color(bg_box, C_BORDER, 0);
        lv_obj_set_style_border_width(bg_box, 1, 0);
        lv_obj_set_style_radius(bg_box, 8, 0);
        lv_obj_set_style_pad_all(bg_box, 3, 0);
        lv_obj_set_style_pad_column(bg_box, 6, 0);
        lv_obj_set_scroll_dir(bg_box, LV_DIR_HOR);
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
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* bg_table = GetBackgroundTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                    if (p_box) {
                        self->display_bg_scroll_x_ = lv_obj_get_scroll_x(p_box);
                        lv_obj_t* p_list = lv_obj_get_parent(p_box);
                        if (p_list) self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                    }
                    self->ApplyBackground(bg_table[idx].id, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        // Custom RGB Mixer Button for Background
        MakeButton(list, "🎨 ATUR RGB LATAR SENDIRI", 12, 284, 216, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRgbMixerModal(false);
            }, this);

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
        lv_obj_set_flex_align(tc_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(tc_box, C_PANEL, 0);
        lv_obj_set_style_border_color(tc_box, C_BORDER, 0);
        lv_obj_set_style_border_width(tc_box, 1, 0);
        lv_obj_set_style_radius(tc_box, 8, 0);
        lv_obj_set_style_pad_all(tc_box, 3, 0);
        lv_obj_set_style_pad_column(tc_box, 6, 0);
        lv_obj_set_scroll_dir(tc_box, LV_DIR_HOR);
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
            lv_obj_set_style_border_color(dot, is_active ? lv_color_white() : lv_color_hex(0x475569), 0);
            lv_obj_set_style_pad_all(dot, 0, 0);

            if (strcmp(tcs[i].id, "auto") == 0) {
                lv_obj_t* albl = lv_label_create(dot);
                lv_label_set_text(albl, "A");
                lv_obj_set_style_text_font(albl, FONT_10, 0);
                lv_obj_set_style_text_color(albl, lv_color_black(), 0);
                lv_obj_center(albl);
            }

            lv_obj_set_user_data(dot, (void*)(intptr_t)i);
            lv_obj_add_event_cb(dot, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                size_t count = 0;
                const auto* tc_table = GetTextColorTable(&count);
                if (idx >= 0 && idx < (int)count) {
                    lv_obj_t* target_dot = (lv_obj_t*)lv_event_get_target(e);
                    lv_obj_t* p_box = lv_obj_get_parent(target_dot);
                    if (p_box) {
                        self->display_tc_scroll_x_ = lv_obj_get_scroll_x(p_box);
                        lv_obj_t* p_list = lv_obj_get_parent(p_box);
                        if (p_list) self->display_scroll_y_ = lv_obj_get_scroll_y(p_list);
                    }
                    self->ApplyTextColor(tc_table[idx].id, true);
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                }
            }, LV_EVENT_CLICKED, this);
        }

        // Custom RGB Button for Text Color
        MakeButton(list, "🎨 ATUR RGB TEKS SENDIRI", 12, 388, 216, 28, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRgbMixerModal(true);
            }, this);

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
        make_level_button(list, "TINGGI", 124, 508, 48, 28, cur_contrast == 2, on_contrast_click, 2);
        make_level_button(list, "EKSTREM", 180, 508, 48, 28, cur_contrast == 3, on_contrast_click, 3);

        // Section 6: Sharpness (Ketajaman & Saturasi Tema - 4 Level)
        MakeLabel(list, "KETAJAMAN WARNA TEMA", 14, 544, 212, 14, FONT_10, C_MUTED);
        make_level_button(list, "HALUS", 12, 560, 48, 28, cur_sharpness == 0, on_sharpness_click, 0);
        make_level_button(list, "STANDAR", 68, 560, 48, 28, cur_sharpness == 1, on_sharpness_click, 1);
        make_level_button(list, "TAJAM", 124, 560, 48, 28, cur_sharpness == 2, on_sharpness_click, 2);
        make_level_button(list, "EKSTREM", 180, 560, 48, 28, cur_sharpness == 3, on_sharpness_click, 3);

        // Section 7: Screen Rotation Button
        MakeButton(list, "ROTASI LAYAR 90°", 12, 600, 216, 32, C_PANEL, C_CYAN, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                (void)e;
                Board::GetInstance().RotateDisplay();
            }, nullptr);

        lv_obj_add_event_cb(list, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->display_scroll_y_ = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(pal_box, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->display_pal_scroll_x_ = lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(bg_box, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->display_bg_scroll_x_ = lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(tc_box, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->display_tc_scroll_x_ = lv_obj_get_scroll_x((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);

        lv_obj_update_layout(root);
        if (display_scroll_y_ > 0)     lv_obj_scroll_to_y(list, display_scroll_y_, LV_ANIM_OFF);
        if (display_pal_scroll_x_ > 0) lv_obj_scroll_to_x(pal_box, display_pal_scroll_x_, LV_ANIM_OFF);
        if (display_bg_scroll_x_ > 0)  lv_obj_scroll_to_x(bg_box, display_bg_scroll_x_, LV_ANIM_OFF);
        if (display_tc_scroll_x_ > 0)  lv_obj_scroll_to_x(tc_box, display_tc_scroll_x_, LV_ANIM_OFF);
    }

    MakeBottomNav(root, PAGE_DISPLAY);
}

void NovaDisplay::BuildAudio() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "AUDIO & SUARA");

    int vol = callbacks_.get_volume ? callbacks_.get_volume() : 75;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    bool snd_enabled = IsNotifSoundEnabled();
    int cur_snd_idx = GetNotifSoundIndex();
    static const char* kSoundNames[6] = {
        "Chime Bell",
        "Ding Dong",
        "Whistle",
        "Crystal Drop",
        "Bubble Pop",
        "Radar Pulse"
    };

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

        lv_obj_add_event_cb(list, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->audio_scroll_y_ = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);

        // Left Column (x=8, y=6, w=148, h=156): Volume Card
        lv_obj_t* card = MakePanel(list, 8, 6, 148, 156, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_AUDIO, 8, 8, C_AMBER, 20);
        MakeLabel(card, "Volume", 32, 8, 54, 16, FONT_12, C_TEXT);

        char vol_str[16];
        snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
        audio_volume_label_ = MakeLabel(card, vol_str, 88, 6, 50, 18, FONT_14, C_CYAN, LV_TEXT_ALIGN_RIGHT);

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

        lv_obj_add_event_cb(audio_volume_slider_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self || !self->audio_volume_slider_) return;
            int val = lv_slider_get_value(self->audio_volume_slider_);
            char txt[16];
            snprintf(txt, sizeof(txt), "%d%%", val);
            if (self->audio_volume_label_) lv_label_set_text(self->audio_volume_label_, txt);
            if (self->callbacks_.set_volume) {
                self->callbacks_.set_volume(val);
            }
            if (val > 0 && self->muted_) {
                self->muted_ = false;
                Settings prefs("nova", true);
                prefs.SetInt("mute", 0);
            }
            if (self->top_mute_icon_) {
                lv_label_set_text(self->top_mute_icon_, (val == 0) ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
                lv_obj_set_style_text_color(self->top_mute_icon_, (val == 0) ? C_MUTED : lv_color_hex(0x06B6D4), LV_PART_MAIN);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

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
            lv_obj_add_event_cb(p_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                if (!b) b = (lv_obj_t*)lv_event_get_target(e);
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
            }, LV_EVENT_CLICKED, this);
        }
        MakeLabel(card, "ES8311 + SC8002B", 8, 126, 132, 16, FONT_10, C_MUTED);

        // Right Column (x=164, w=148): Mute, Haptic, Notif Sound Toggle
        lv_obj_t* mute = lv_btn_create(list);
        lv_obj_set_pos(mute, 164, 6); lv_obj_set_size(mute, 148, 48);
        StyleButton(mute, C_PANEL, muted_ ? C_AMBER : C_BORDER, 8);
        MakeIcon(mute, ICON_AUDIO, 8, 14, muted_ ? C_AMBER : C_CYAN, 20);
        MakeLabel(mute, "Bisukan Speaker", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(mute, muted_ ? "MUTE" : "Suara Aktif", 34, 24, 80, 14, FONT_10, muted_ ? C_AMBER : C_MUTED);
        MakeLabel(mute, muted_ ? "ON" : "OFF", 116, 14, 26, 16, FONT_10, muted_ ? C_AMBER : C_MINT, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(mute, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        bool haptic = callbacks_.get_haptic ? callbacks_.get_haptic() : true;
        lv_obj_t* haptic_btn = lv_btn_create(list);
        lv_obj_set_pos(haptic_btn, 164, 60); lv_obj_set_size(haptic_btn, 148, 48);
        StyleButton(haptic_btn, C_PANEL, haptic ? C_MINT : C_BORDER, 8);
        MakeIcon(haptic_btn, ICON_CHIP, 8, 14, haptic ? C_MINT : C_MUTED, 20);
        MakeLabel(haptic_btn, "Suara Haptik", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(haptic_btn, haptic ? "Klik Sentuh" : "Dimatikan", 34, 24, 80, 14, FONT_10, C_MUTED);
        MakeLabel(haptic_btn, haptic ? "ON" : "OFF", 116, 14, 26, 16, FONT_10, haptic ? C_MINT : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(haptic_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->callbacks_.set_haptic && self->callbacks_.get_haptic) {
                bool cur = self->callbacks_.get_haptic();
                self->callbacks_.set_haptic(!cur);
                self->BuildCurrentPage();
            }
        }, LV_EVENT_CLICKED, this);

        lv_obj_t* notif_tgl = lv_btn_create(list);
        lv_obj_set_pos(notif_tgl, 164, 114); lv_obj_set_size(notif_tgl, 148, 48);
        StyleButton(notif_tgl, C_PANEL, snd_enabled ? C_CYAN : C_BORDER, 8);
        MakeIcon(notif_tgl, ICON_CHAT, 8, 14, snd_enabled ? C_CYAN : C_MUTED, 20);
        MakeLabel(notif_tgl, "Suara Notif HP", 34, 6, 80, 16, FONT_10, C_TEXT);
        MakeLabel(notif_tgl, snd_enabled ? "Bunyi Aktif" : "Senyap", 34, 24, 80, 14, FONT_10, snd_enabled ? C_CYAN : C_MUTED);
        MakeLabel(notif_tgl, snd_enabled ? "ON" : "OFF", 116, 14, 26, 16, FONT_10, snd_enabled ? C_CYAN : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(notif_tgl, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                bool en = self->IsNotifSoundEnabled();
                self->SetNotifSoundEnabled(!en);
                if (!en) self->PreviewNotifSound(self->GetNotifSoundIndex());
                self->BuildCurrentPage();
            }
        }, LV_EVENT_CLICKED, this);

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
            if (is_cur) lv_obj_set_style_border_width(s_btn, 2, 0);

            lv_obj_t* lbl = lv_label_create(s_btn);
            lv_label_set_text(lbl, kSoundNames[i]);
            lv_obj_set_style_text_font(lbl, FONT_10, 0);
            lv_obj_set_style_text_color(lbl, is_cur ? C_CYAN : C_TEXT, 0);
            lv_obj_center(lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(s_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                if (!b) b = (lv_obj_t*)lv_event_get_target(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(b);
                self->SetNotifSoundIndex(idx);
                self->PreviewNotifSound(idx);
                self->BuildCurrentPage();
            }, LV_EVENT_CLICKED, this);
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
        lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->PreviewNotifSound(self->GetNotifSoundIndex());
        }, LV_EVENT_CLICKED, this);

        MakeLabel(list, "Amplifier SC8002B onboard berdaya 1W.", 10, 306, 300, 16, FONT_10, C_MUTED);

        if (audio_scroll_y_ > 0) lv_obj_scroll_to_y(list, audio_scroll_y_, LV_ANIM_OFF);

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

        lv_obj_add_event_cb(list, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->audio_scroll_y_ = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
        }, LV_EVENT_SCROLL, this);

        // 1. Volume Card (y=6, h=98)
        lv_obj_t* card = MakePanel(list, 12, 6, 216, 98, C_PANEL, C_BORDER, 12);
        MakeIcon(card, ICON_AUDIO, 10, 10, C_AMBER, 22);
        MakeLabel(card, "Volume Speaker", 38, 10, 110, 18, FONT_14, C_TEXT);

        char vol_str[16];
        snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
        audio_volume_label_ = MakeLabel(card, vol_str, 150, 8, 54, 20, FONT_16, C_CYAN, LV_TEXT_ALIGN_RIGHT);

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

        lv_obj_add_event_cb(audio_volume_slider_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self || !self->audio_volume_slider_) return;
            int val = lv_slider_get_value(self->audio_volume_slider_);
            char txt[16];
            snprintf(txt, sizeof(txt), "%d%%", val);
            if (self->audio_volume_label_) lv_label_set_text(self->audio_volume_label_, txt);
            if (self->callbacks_.set_volume) {
                self->callbacks_.set_volume(val);
            }
            if (val > 0 && self->muted_) {
                self->muted_ = false;
                Settings prefs("nova", true);
                prefs.SetInt("mute", 0);
            }
            if (self->top_mute_icon_) {
                lv_label_set_text(self->top_mute_icon_, (val == 0) ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
                lv_obj_set_style_text_color(self->top_mute_icon_, (val == 0) ? C_MUTED : lv_color_hex(0x06B6D4), LV_PART_MAIN);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

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
            lv_obj_add_event_cb(p_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                if (!b) b = (lv_obj_t*)lv_event_get_target(e);
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
            }, LV_EVENT_CLICKED, this);
        }

        // Section: Kontrol Suara
        MakeLabel(list, "KONTROL SUARA", 14, 110, 212, 14, FONT_10, C_MUTED);

        // Mute Button
        lv_obj_t* mute = lv_btn_create(list);
        lv_obj_set_pos(mute, 12, 128); lv_obj_set_size(mute, 216, 40);
        StyleButton(mute, C_PANEL, C_BORDER, 10);
        MakeIcon(mute, ICON_AUDIO, 12, 8, muted_ ? C_MUTED : C_AMBER, 22);
        MakeLabel(mute, "Bisukan speaker", 44, 4, 120, 18, FONT_14, C_TEXT);
        MakeLabel(mute, muted_ ? "MUTE (Senyap)" : "AKTIF", 44, 22, 120, 14, FONT_10, muted_ ? C_AMBER : C_MUTED);
        MakeLabel(mute, muted_ ? "ON" : "OFF", 169, 11, 34, 18, FONT_14, muted_ ? C_AMBER : C_CYAN, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(mute, OnMuteTileClicked, LV_EVENT_CLICKED, this);

        // Haptic Button
        bool haptic = callbacks_.get_haptic ? callbacks_.get_haptic() : true;
        lv_obj_t* haptic_btn = lv_btn_create(list);
        lv_obj_set_pos(haptic_btn, 12, 174); lv_obj_set_size(haptic_btn, 216, 40);
        StyleButton(haptic_btn, C_PANEL, C_BORDER, 10);
        MakeIcon(haptic_btn, ICON_CHIP, 12, 8, haptic ? C_MINT : C_MUTED, 22);
        MakeLabel(haptic_btn, "Suara haptik sentuh", 44, 4, 130, 18, FONT_14, C_TEXT);
        MakeLabel(haptic_btn, haptic ? "Klik speaker saat disentuh" : "Haptik dimatikan", 44, 22, 130, 14, FONT_10, C_MUTED);
        MakeLabel(haptic_btn, haptic ? "ON" : "OFF", 169, 11, 34, 18, FONT_14, haptic ? C_MINT : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(haptic_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->callbacks_.set_haptic && self->callbacks_.get_haptic) {
                bool cur = self->callbacks_.get_haptic();
                self->callbacks_.set_haptic(!cur);
                self->BuildCurrentPage();
            }
        }, LV_EVENT_CLICKED, this);

        // Section: Suara Notifikasi HP
        MakeLabel(list, "SUARA NOTIFIKASI HP", 14, 222, 212, 14, FONT_10, C_MUTED);

        // Notification Sound ON/OFF Toggle
        lv_obj_t* notif_tgl = lv_btn_create(list);
        lv_obj_set_pos(notif_tgl, 12, 238); lv_obj_set_size(notif_tgl, 216, 40);
        StyleButton(notif_tgl, C_PANEL, snd_enabled ? C_CYAN : C_BORDER, 10);
        MakeIcon(notif_tgl, ICON_CHAT, 12, 8, snd_enabled ? C_CYAN : C_MUTED, 22);
        MakeLabel(notif_tgl, "Bunyi notifikasi HP", 44, 4, 125, 18, FONT_14, C_TEXT);
        MakeLabel(notif_tgl, snd_enabled ? "Dering notif aktif" : "Senyap saat ada pesan", 44, 22, 125, 14, FONT_10, snd_enabled ? C_CYAN : C_MUTED);
        MakeLabel(notif_tgl, snd_enabled ? "ON" : "OFF", 169, 11, 34, 18, FONT_14, snd_enabled ? C_CYAN : C_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_obj_add_event_cb(notif_tgl, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                bool en = self->IsNotifSoundEnabled();
                self->SetNotifSoundEnabled(!en);
                if (!en) self->PreviewNotifSound(self->GetNotifSoundIndex());
                self->BuildCurrentPage();
            }
        }, LV_EVENT_CLICKED, this);

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
            if (is_cur) lv_obj_set_style_border_width(s_btn, 2, 0);

            lv_obj_t* lbl = lv_label_create(s_btn);
            lv_label_set_text(lbl, kSoundNames[i]);
            lv_obj_set_style_text_font(lbl, FONT_10, 0);
            lv_obj_set_style_text_color(lbl, is_cur ? C_CYAN : C_TEXT, 0);
            lv_obj_center(lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(s_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
                if (!b) b = (lv_obj_t*)lv_event_get_target(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(b);
                self->SetNotifSoundIndex(idx);
                self->PreviewNotifSound(idx);
                self->BuildCurrentPage();
            }, LV_EVENT_CLICKED, this);
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
        lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->PreviewNotifSound(self->GetNotifSoundIndex());
        }, LV_EVENT_CLICKED, this);

        MakeLabel(list, "Amplifier SC8002B onboard berdaya 1W.", 14, 434, 212, 16, FONT_10, C_MUTED);

        if (audio_scroll_y_ > 0) lv_obj_scroll_to_y(list, audio_scroll_y_, LV_ANIM_OFF);
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
                for (auto &c : ext) c = tolower(c);
            }
            if (f.first.find("music/") == 0 || ext == ".wav" || ext == ".mp3" || ext == ".pcm" || ext == ".m4a") {
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
            MakeLabel(card, sd.card_name[0] ? sd.card_name : "MicroSD Card", 32, 6, 110, 18, FONT_12, C_TEXT);
            MakeLabel(card, "SDIO 4-Bit Bus", 32, 22, 110, 14, FONT_10, C_MINT);

            uint64_t used_mb = (sd.total_mb > sd.free_mb) ? (sd.total_mb - sd.free_mb) : 0;
            int pct = (sd.total_mb > 0) ? (int)((used_mb * 100ULL) / sd.total_mb) : 0;
            if (pct > 100) pct = 100;

            lv_obj_t* bar_bg = MakePanel(card, 8, 42, 132, 6, C_PANEL_2, C_BORDER, 3);
            int fill_w = (132 * pct) / 100;
            if (fill_w < 3 && pct > 0) fill_w = 3;
            IconShape(bar_bg, 0, 0, fill_w, 6, C_CYAN, 3, true);

            char cap_buf[48];
            double total_gb = (double)sd.total_mb / 1024.0;
            double free_gb = (double)sd.free_mb / 1024.0;
            if (total_gb >= 1.0) {
                snprintf(cap_buf, sizeof(cap_buf), "Total: %.1f GB | Sisa: %.1f GB", total_gb, free_gb);
            } else {
                snprintf(cap_buf, sizeof(cap_buf), "Total: %llu MB | Sisa: %llu MB", (unsigned long long)sd.total_mb, (unsigned long long)sd.free_mb);
            }
            MakeLabel(card, cap_buf, 8, 52, 132, 14, FONT_10, C_MUTED);

            char file_count_str[48];
            snprintf(file_count_str, sizeof(file_count_str), "%u Berkas Terindeks", (unsigned)sd.files.size());
            MakeLabel(card, file_count_str, 8, 68, 132, 14, FONT_10, C_CYAN);

            MakeButton(card, "SEGARKAN", 8, 86, 64, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            MakeButton(card, "FORMAT", 76, 86, 64, 24, lv_color_hex(0x27141A), lv_color_hex(0xF43F5E), lv_color_hex(0xF43F5E), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            char music_tab_str[32];
            snprintf(music_tab_str, sizeof(music_tab_str), "MUSIK (%u)", (unsigned)music_files.size());
            MakeButton(card, music_tab_str, 8, 118, 64, 34,
                (file_hub_filter_ == 1) ? lv_color_hex(0x13382C) : C_PANEL_2,
                (file_hub_filter_ == 1) ? C_MINT : C_BORDER,
                (file_hub_filter_ == 1) ? C_MINT : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 1;
                        self->BuildCurrentPage();
                    }
                }, this);

            char all_tab_str[32];
            snprintf(all_tab_str, sizeof(all_tab_str), "SEMUA (%u)", (unsigned)sd.files.size());
            MakeButton(card, all_tab_str, 76, 118, 64, 34,
                (file_hub_filter_ == 0) ? lv_color_hex(0x132C38) : C_PANEL_2,
                (file_hub_filter_ == 0) ? C_CYAN : C_BORDER,
                (file_hub_filter_ == 0) ? C_CYAN : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 0;
                        self->BuildCurrentPage();
                    }
                }, this);

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
                        if (clean_title.find("music/") == 0) clean_title = clean_title.substr(6);

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
                            snprintf(item_str, sizeof(item_str), "%s (%.1fMB)", clean_title.c_str(), (double)music_files[i].second / (1024.0 * 1024.0));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%uKB)", clean_title.c_str(), (unsigned)(music_files[i].second / 1024));
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 26, 7, 112, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx { NovaDisplay* self; std::string fname; size_t size; };
                        auto* ctx = new FileClickCtx{this, full_name, music_files[i].second};
                        lv_obj_add_event_cb(frow, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                            if (c && c->self) c->self->ShowFileModal(c->fname, c->size);
                        }, LV_EVENT_CLICKED, ctx);
                    }
                }
            } else {
                const auto& active_list = root_files.empty() ? sd.files : root_files;
                if (active_list.empty()) {
                    lv_obj_t* empty_box = MakePanel(list, 0, 4, 144, 60, C_PANEL, C_BORDER, 8);
                    MakeLabel(empty_box, "Berkas Kosong", 8, 18, 128, 20, FONT_10, C_MUTED, LV_TEXT_ALIGN_CENTER);
                } else {
                    for (size_t i = 0; i < active_list.size() && i < 30; ++i) {
                        std::string fname = active_list[i].first;
                        std::string ext = "";
                        size_t dot_pos = fname.rfind('.');
                        if (dot_pos != std::string::npos) {
                            ext = fname.substr(dot_pos);
                            for (auto &c : ext) c = tolower(c);
                        }

                        const char* f_icon = MATERIAL_SYMBOLS_EDIT_SQUARE;
                        lv_color_t f_color = C_CYAN;
                        if (ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                            f_icon = MATERIAL_SYMBOLS_IMAGE;
                            f_color = C_AMBER;
                        } else if (ext == ".wav" || ext == ".mp3" || ext == ".pcm" || ext == ".m4a") {
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
                            snprintf(item_str, sizeof(item_str), "%s (%.1fMB)", fname.c_str(), (double)active_list[i].second / (1024.0 * 1024.0));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%uKB)", fname.c_str(), (unsigned)(active_list[i].second / 1024));
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 26, 7, 112, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx { NovaDisplay* self; std::string fname; size_t size; };
                        auto* ctx = new FileClickCtx{this, fname, active_list[i].second};
                        lv_obj_add_event_cb(frow, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                            if (c && c->self) c->self->ShowFileModal(c->fname, c->size);
                        }, LV_EVENT_CLICKED, ctx);
                    }
                }
            }
        } else {
            // SD Card Not Mounted: Left Card for Slot & Actions, Right Card for Internal Flash
            lv_obj_t* card = MakePanel(root, 8, 38, 148, 166, C_PANEL, C_BORDER, 10);
            MakeIcon(card, ICON_FILES, 8, 8, C_AMBER, 22);
            MakeLabel(card, "Slot MicroSD SDIO", 34, 6, 108, 18, FONT_12, C_TEXT);
            MakeLabel(card, "Status: Belum Pasang", 34, 24, 108, 14, FONT_10, C_AMBER);
            MakeLabel(card, "SDIO 4-Bit (IO 38-41,47-48)\nPastikan kartu terpasang rapi.", 8, 44, 132, 28, FONT_10, C_MUTED);

            MakeButton(card, "DETEKSI & PASANG", 8, 80, 132, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            MakeButton(card, "FORMAT FAT32", 8, 116, 132, 28, lv_color_hex(0xF43F5E), lv_color_hex(0xBE123C), lv_color_white(), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

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
            MakeLabel(card, sd.card_name[0] ? sd.card_name : "MicroSD Card", 40, 6, 160, 18, FONT_14, C_TEXT);
            MakeLabel(card, "SDIO 4-Bit Bus (38-41, 47, 48)", 40, 24, 160, 14, FONT_10, C_MINT);

            uint64_t used_mb = (sd.total_mb > sd.free_mb) ? (sd.total_mb - sd.free_mb) : 0;
            int pct = (sd.total_mb > 0) ? (int)((used_mb * 100ULL) / sd.total_mb) : 0;
            if (pct > 100) pct = 100;

            lv_obj_t* bar_bg = MakePanel(card, 8, 48, 192, 8, C_PANEL_2, C_BORDER, 4);
            int fill_w = (192 * pct) / 100;
            if (fill_w < 4 && pct > 0) fill_w = 4;
            IconShape(bar_bg, 0, 0, fill_w, 8, C_CYAN, 4, true);

            char cap_buf[64];
            double total_gb = (double)sd.total_mb / 1024.0;
            double free_gb = (double)sd.free_mb / 1024.0;
            if (total_gb >= 1.0) {
                snprintf(cap_buf, sizeof(cap_buf), "Kapasitas: %.2f GB (Bebas: %.2f GB)", total_gb, free_gb);
            } else {
                snprintf(cap_buf, sizeof(cap_buf), "Kapasitas: %llu MB (Bebas: %llu MB)", (unsigned long long)sd.total_mb, (unsigned long long)sd.free_mb);
            }
            MakeLabel(card, cap_buf, 8, 62, 192, 16, FONT_10, C_MUTED);

            char file_count_str[64];
            snprintf(file_count_str, sizeof(file_count_str), "Status: %u berkas terindeks di /sdcard", (unsigned)sd.files.size());
            MakeLabel(card, file_count_str, 8, 78, 192, 16, FONT_10, C_CYAN);

            MakeButton(card, "SEGARKAN", 8, 96, 92, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            MakeButton(card, "FORMAT FAT32", 108, 96, 92, 24, lv_color_hex(0x27141A), lv_color_hex(0xF43F5E), lv_color_hex(0xF43F5E), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            int tab_y = 132;
            char music_tab_str[48];
            snprintf(music_tab_str, sizeof(music_tab_str), "FOLDER MUSIK (%u)", (unsigned)music_files.size());
            MakeButton(list, music_tab_str, 4, tab_y, 102, 28,
                (file_hub_filter_ == 1) ? lv_color_hex(0x13382C) : C_PANEL_2,
                (file_hub_filter_ == 1) ? C_MINT : C_BORDER,
                (file_hub_filter_ == 1) ? C_MINT : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 1;
                        self->BuildCurrentPage();
                    }
                }, this);

            char all_tab_str[48];
            snprintf(all_tab_str, sizeof(all_tab_str), "SEMUA BERKAS (%u)", (unsigned)sd.files.size());
            MakeButton(list, all_tab_str, 110, tab_y, 102, 28,
                (file_hub_filter_ == 0) ? lv_color_hex(0x132C38) : C_PANEL_2,
                (file_hub_filter_ == 0) ? C_CYAN : C_BORDER,
                (file_hub_filter_ == 0) ? C_CYAN : C_MUTED, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self) {
                        self->file_hub_filter_ = 0;
                        self->BuildCurrentPage();
                    }
                }, this);

            int start_y = tab_y + 34;

            if (file_hub_filter_ == 1) {
                MakeLabel(list, "KOLEKSI LAGU DI /sdcard/music (Ketuk untuk Putar)", 8, start_y, 208, 16, FONT_10, C_MINT);
                start_y += 18;

                if (music_files.empty()) {
                    lv_obj_t* empty_box = MakePanel(list, 4, start_y, 208, 56, C_PANEL, C_BORDER, 8);
                    MakeIcon(empty_box, ICON_AUDIO, 8, 14, C_MINT, 24);
                    MakeLabel(empty_box, "Folder Musik Kosong", 38, 8, 160, 16, FONT_12, C_MINT);
                    MakeLabel(empty_box, "Minta XiaoZhi unduh lagu YouTube!", 38, 28, 160, 18, FONT_10, C_MUTED);
                    start_y += 62;
                } else {
                    for (size_t i = 0; i < music_files.size() && i < 30; ++i) {
                        std::string full_name = music_files[i].first;
                        std::string clean_title = full_name;
                        if (clean_title.find("music/") == 0) clean_title = clean_title.substr(6);

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
                            snprintf(item_str, sizeof(item_str), "%s (%.1f MB)", clean_title.c_str(), (double)music_files[i].second / (1024.0 * 1024.0));
                        } else if (music_files[i].second >= 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%u KB)", clean_title.c_str(), (unsigned)(music_files[i].second / 1024));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%u B)", clean_title.c_str(), (unsigned)music_files[i].second);
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 28, 7, 172, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx { NovaDisplay* self; std::string fname; size_t size; };
                        auto* ctx = new FileClickCtx{this, full_name, music_files[i].second};
                        lv_obj_add_event_cb(frow, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                            if (c && c->self) c->self->ShowFileModal(c->fname, c->size);
                        }, LV_EVENT_CLICKED, ctx);

                        start_y += 36;
                    }
                }
            } else {
                MakeLabel(list, "DAFTAR SEMUA BERKAS /sdcard (Ketuk Buka)", 8, start_y, 208, 16, FONT_10, C_CYAN);
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
                    snprintf(folder_title, sizeof(folder_title), "📁 music/ (Folder Musik - %u Lagu)", (unsigned)music_files.size());
                    lv_obj_t* f_lbl = MakeLabel(music_folder_row, folder_title, 28, 7, 172, 16, FONT_10, C_MINT);
                    lv_label_set_long_mode(f_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                    lv_obj_add_event_cb(music_folder_row, [](lv_event_t* e) {
                        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                        if (self) {
                            self->file_hub_filter_ = 1;
                            self->BuildCurrentPage();
                        }
                    }, LV_EVENT_CLICKED, this);
                    start_y += 36;
                }

                if (root_files.empty() && music_files.empty()) {
                    lv_obj_t* empty_box = MakePanel(list, 4, start_y, 208, 36, C_PANEL, C_BORDER, 8);
                    MakeLabel(empty_box, "Folder /sdcard kosong (Belum ada file).", 10, 10, 188, 16, FONT_10, C_MUTED);
                    start_y += 42;
                } else {
                    for (size_t i = 0; i < root_files.size() && i < 20; ++i) {
                        std::string fname = root_files[i].first;
                        std::string ext = "";
                        size_t dot_pos = fname.rfind('.');
                        if (dot_pos != std::string::npos) {
                            ext = fname.substr(dot_pos);
                            for (auto &c : ext) c = tolower(c);
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
                            snprintf(item_str, sizeof(item_str), "%s (%.1f MB)", fname.c_str(), (double)root_files[i].second / (1024.0 * 1024.0));
                        } else if (root_files[i].second >= 1024) {
                            snprintf(item_str, sizeof(item_str), "%s (%u KB)", fname.c_str(), (unsigned)(root_files[i].second / 1024));
                        } else {
                            snprintf(item_str, sizeof(item_str), "%s (%u B)", fname.c_str(), (unsigned)root_files[i].second);
                        }
                        lv_obj_t* flbl = MakeLabel(frow, item_str, 28, 7, 172, 16, FONT_10, C_TEXT);
                        lv_label_set_long_mode(flbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

                        struct FileClickCtx { NovaDisplay* self; std::string fname; size_t size; };
                        auto* ctx = new FileClickCtx{this, fname, root_files[i].second};
                        lv_obj_add_event_cb(frow, [](lv_event_t* e) {
                            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                            auto* c = static_cast<FileClickCtx*>(lv_event_get_user_data(e));
                            if (c && c->self) c->self->ShowFileModal(c->fname, c->size);
                        }, LV_EVENT_CLICKED, ctx);

                        start_y += 34;
                    }
                }
            }

            MakeLabel(list, "PENYIMPANAN INTERNAL FLASH", 8, start_y + 4, 200, 16, FONT_10, C_MUTED);
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

            MakeLabel(card, "Pinout Hardware SDIO 4-Bit:\nCLK:38  CMD:40  D0:39\nD1:41  D2:48  D3:47", 10, 44, 188, 38, FONT_10, C_MUTED);

            MakeButton(card, "DETEKSI & PASANG", 8, 86, 92, 24, C_CYAN, C_CYAN, C_BG, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.remount_sd) {
                        self->callbacks_.remount_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            MakeButton(card, "FORMAT FAT32", 108, 86, 92, 24, lv_color_hex(0xF43F5E), lv_color_hex(0xBE123C), lv_color_white(), FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (self && self->callbacks_.format_sd) {
                        self->callbacks_.format_sd();
                        self->BuildCurrentPage();
                    }
                }, this);

            MakeLabel(card, "*Format menghapus isi kartu & ubah ke FAT32", 8, 114, 192, 14, FONT_10, C_MUTED);

            MakeLabel(list, "PENYIMPANAN INTERNAL FLASH", 8, 154, 200, 16, FONT_10, C_CYAN);
            lv_obj_t* iflash = MakePanel(list, 4, 172, 208, 76, C_PANEL, C_BORDER, 10);
            MakeIcon(iflash, ICON_CHIP, 8, 10, C_VIOLET, 24);
            MakeLabel(iflash, "16 MB External SPI Flash", 38, 8, 160, 18, FONT_12, C_TEXT);
            MakeLabel(iflash, "Partisi Aplikasi Firmware: 4 MB", 38, 28, 160, 15, FONT_10, C_MUTED);
            MakeLabel(iflash, "Partisi Asset Audio & Font: 8 MB", 38, 43, 160, 15, FONT_10, C_MUTED);
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
        battery_percent_label_ = MakeLabel(card, pct_str, 36, 5, 54, 20, FONT_16, chg ? C_CYAN : C_TEXT);

        char state_str[48];
        snprintf(state_str, sizeof(state_str), "%s", chg ? "Isi Daya" : "Baterai Aktif");
        battery_state_label_ = MakeLabel(card, state_str, 36, 25, 104, 14, FONT_10, chg ? C_MINT : C_MUTED);

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
            lv_obj_add_event_cb(s_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                if (!btn) btn = (lv_obj_t*)lv_event_get_target(e);
                uint8_t style = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
                self->SetBatteryStyle(style);
            }, LV_EVENT_CLICKED, this);
        }

        // Right Column (x=164, w=146, h=164): Hardware info card
        lv_obj_t* hcard = MakePanel(root, 164, 38, 146, 164, C_PANEL, C_BORDER, 10);
        MakeLabel(hcard, "INFORMASI RANGKAIAN", 8, 8, 130, 14, FONT_10, C_CYAN);
        MakeLabel(hcard, "Sensor: GPIO 9\nADC Divider 1:1", 8, 28, 130, 28, FONT_10, C_TEXT);
        MakeLabel(hcard, "IC Pengisi Daya:\nTP4054 CC/CV ~300mA", 8, 62, 130, 28, FONT_10, C_MUTED);
        MakeLabel(hcard, "Baterai Li-ion 3.7V\n(Tegangan Max 4.2V)", 8, 96, 130, 28, FONT_10, C_MUTED);
        MakeLabel(hcard, "Hemat Daya: Aktif", 8, 136, 130, 16, FONT_10, C_MINT);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "STATUS BATERAI", 14, 40, 212, 14, FONT_10, C_MUTED);

        lv_obj_t* card = MakePanel(root, 12, 56, 216, 86, C_PANEL, C_BORDER, 12);
        MakeIcon(card, ICON_BATTERY, 10, 10, C_MINT, 26);
        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%d%%", level);
        battery_percent_label_ = MakeLabel(card, pct_str, 44, 6, 76, 24, FONT_20, chg ? C_CYAN : C_TEXT);

        char state_str[48];
        snprintf(state_str, sizeof(state_str), "%s", chg ? "Sedang Mengisi Daya" : "Menggunakan Baterai");
        battery_state_label_ = MakeLabel(card, state_str, 44, 30, 160, 15, FONT_10, chg ? C_MINT : C_MUTED);

        char volt_str[48];
        float vf = mv / 1000.0f;
        snprintf(volt_str, sizeof(volt_str), "Tegangan: %d mV (%.2f V)", mv, vf);
        battery_volt_label_ = MakeLabel(card, volt_str, 10, 48, 196, 18, FONT_12, C_CYAN);
        lv_label_set_long_mode(battery_volt_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        char raw_str[48];
        snprintf(raw_str, sizeof(raw_str), "Sensor ADC Mentah: %d (Divider 1:1)", raw);
        battery_raw_label_ = MakeLabel(card, raw_str, 10, 67, 196, 14, FONT_10, C_MUTED);
        lv_label_set_long_mode(battery_raw_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(root, "GAYA TAMPILAN STATUS BAR", 14, 148, 212, 14, FONT_10, C_MUTED);

        for (int i = 0; i < 3; ++i) {
            bool selected = (bat_style_ == i);
            lv_obj_t* s_btn = lv_btn_create(root);
            lv_obj_set_pos(s_btn, 12 + i * 72, 166);
            lv_obj_set_size(s_btn, 68, 34);
            StyleButton(s_btn, selected ? C_PANEL_2 : C_PANEL, selected ? C_CYAN : C_BORDER, 8);
            if (selected) {
                lv_obj_set_style_border_width(s_btn, 2, LV_PART_MAIN);
            }

            lv_obj_t* btn_lbl = lv_label_create(s_btn);
            lv_label_set_text(btn_lbl, style_labels[i]);
            lv_obj_set_style_text_font(btn_lbl, FONT_12, LV_PART_MAIN);
            lv_obj_set_style_text_color(btn_lbl, selected ? C_CYAN : C_MUTED, LV_PART_MAIN);
            lv_obj_center(btn_lbl);

            lv_obj_set_user_data(s_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(s_btn, [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self) return;
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
                if (!btn) btn = (lv_obj_t*)lv_event_get_target(e);
                uint8_t style = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
                self->SetBatteryStyle(style);
            }, LV_EVENT_CLICKED, this);
        }

        MakeLabel(root, "INFORMASI RANGKAIAN", 14, 208, 212, 14, FONT_10, C_MUTED);
        MakeLabel(root, "Sensor: GPIO 9 (ADC Divider R40/R41 1:1)", 14, 224, 212, 16, FONT_10, C_TEXT);
        MakeLabel(root, "IC Pengisi Daya: TP4054 CC/CV ~300mA", 14, 240, 212, 16, FONT_10, C_MUTED);
        MakeLabel(root, "Baterai Li-ion 3.7V (Tegangan Maksimal 4.2V)", 14, 256, 212, 16, FONT_10, C_MUTED);
    }

    MakeBottomNav(root, PAGE_BATTERY);
}

const char* NovaDisplay::GetLanguageName() const {
    static const char* kLangs[] = {
        "Bahasa Indonesia (ID)",
        "English (US)"
    };
    return kLangs[(language_idx_ >= 0 && language_idx_ < 2) ? language_idx_ : 0];
}

const char* NovaDisplay::GetTimezoneName() const {
    static const char* kTzNames[] = {
        "WIB (UTC+7) - Barat",
        "WITA (UTC+8) - Tengah",
        "WIT (UTC+9) - Timur",
        "GMT (UTC+0) - London",
        "JST (UTC+9) - Tokyo",
        "EST (UTC-5) - New York"
    };
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

    static const char* kTimezonesEnv[] = {
        "WIB-7", "WITA-8", "WIT-9", "GMT0", "JST-9", "EST5EDT,M3.2.0,M11.1.0"
    };
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

void NovaDisplay::BuildRegion() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, (language_idx_ == 0) ? "BAHASA & WILAYAH" : "LANGUAGE & REGION");

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE LAYOUT (320x240 - 100% Fit On 1 Screen - No Scroll) ===
        // Left Column (x=10, w=146): Language, Timezone, 12/24H
        lv_obj_t* lang_card = MakePanel(root, 10, 36, 146, 48, C_PANEL, C_BORDER, 8);
        MakeIcon(lang_card, ICON_REGION, 6, 12, C_CYAN, 22);
        MakeLabel(lang_card, (language_idx_ == 0) ? "Bahasa" : "Language", 32, 6, 62, 14, FONT_10, C_MUTED);
        lv_obj_t* lang_lbl = MakeLabel(lang_card, (language_idx_ == 0) ? "Indonesia" : "English", 32, 22, 62, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(lang_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(lang_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 96, 10, 44, 26, C_PANEL_2, C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->CycleLanguage();
            }, this);

        lv_obj_t* tz_card = MakePanel(root, 10, 88, 146, 48, C_PANEL, C_BORDER, 8);
        MakeIcon(tz_card, ICON_CHRONOS, 6, 12, C_AMBER, 22);
        MakeLabel(tz_card, (language_idx_ == 0) ? "Zona Waktu" : "Timezone", 32, 6, 62, 14, FONT_10, C_MUTED);
        lv_obj_t* tz_lbl = MakeLabel(tz_card, GetTimezoneName(), 32, 22, 62, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(tz_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(tz_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 96, 10, 44, 26, C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->CycleTimezone();
            }, this);

        const char* fmt_txt = use_24hour_ ?
            ((language_idx_ == 0) ? "FORMAT 24 JAM [AKTIF]" : "24-HOUR FORMAT [ON]") :
            ((language_idx_ == 0) ? "FORMAT 12 JAM [AKTIF]" : "12-HOUR FORMAT [ON]");
        MakeButton(root, fmt_txt, 10, 140, 146, 32, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Toggle24Hour();
            }, this);

        MakeLabel(root, "Zona & Waktu Tersinkron", 12, 178, 142, 16, FONT_10, C_MUTED);

        // Right Column (x=164, w=146, h=166): Clock Preview & NTP Sync
        lv_obj_t* p_card = MakePanel(root, 164, 36, 146, 166, C_PANEL, C_BORDER, 10);
        MakeLabel(p_card, (language_idx_ == 0) ? "WAKTU REALTIME" : "REALTIME CLOCK", 8, 8, 130, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_CENTER);
        region_time_preview_ = MakeLabel(p_card, "--:--:--", 6, 26, 134, 32, FONT_24, C_TEXT, LV_TEXT_ALIGN_CENTER);
        region_date_preview_ = MakeLabel(p_card, "Menghubungkan...", 6, 62, 134, 15, FONT_10, C_MUTED, LV_TEXT_ALIGN_CENTER);
        region_ntp_status_ = MakeLabel(p_card, "NTP: id.pool.ntp.org", 6, 80, 134, 14, FONT_10, C_CYAN, LV_TEXT_ALIGN_CENTER);

        MakeButton(p_card, (language_idx_ == 0) ? "SINKRONISASI NTP" : "SYNC NTP NOW", 8, 104, 130, 26,
            C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->SyncNtpNow();
            }, this);

        MakeButton(p_card, (language_idx_ == 0) ? "ATUR WI-FI >" : "WIFI CONFIG >", 8, 134, 130, 24,
            C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->NavigateTo(PAGE_WIFI);
            }, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Card 1: Bahasa Antarmuka
        MakeLabel(root, (language_idx_ == 0) ? "BAHASA ANTARMUKA" : "INTERFACE LANGUAGE", 14, 40, 212, 14, FONT_10, C_MUTED);
        lv_obj_t* lang_card = MakePanel(root, 12, 54, 216, 40, C_PANEL, C_BORDER, 10);
        MakeIcon(lang_card, ICON_REGION, 8, 9, C_CYAN, 22);
        lv_obj_t* lang_lbl = MakeLabel(lang_card, GetLanguageName(), 36, 11, 116, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(lang_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(lang_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 154, 7, 54, 26, C_PANEL_2, C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->CycleLanguage();
            }, this);

        // Card 2: Zona Waktu
        MakeLabel(root, (language_idx_ == 0) ? "ZONA WAKTU LOKAL" : "LOCAL TIMEZONE", 14, 98, 212, 14, FONT_10, C_MUTED);
        lv_obj_t* tz_card = MakePanel(root, 12, 112, 216, 40, C_PANEL, C_BORDER, 10);
        MakeIcon(tz_card, ICON_CHRONOS, 8, 9, C_AMBER, 22);
        lv_obj_t* tz_lbl = MakeLabel(tz_card, GetTimezoneName(), 36, 11, 116, 18, FONT_10, C_TEXT);
        lv_label_set_long_mode(tz_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        MakeButton(tz_card, (language_idx_ == 0) ? "GANTI" : "SWITCH", 154, 7, 54, 26, C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->CycleTimezone();
            }, this);

        // Card 3: Format Jam
        MakeLabel(root, (language_idx_ == 0) ? "FORMAT WAKTU" : "TIME FORMAT", 14, 156, 212, 14, FONT_10, C_MUTED);
        const char* fmt_txt = use_24hour_ ?
            ((language_idx_ == 0) ? "FORMAT 24 JAM [AKTIF]" : "24-HOUR FORMAT [ACTIVE]") :
            ((language_idx_ == 0) ? "FORMAT 12 JAM AM/PM [AKTIF]" : "12-HOUR FORMAT AM/PM [ACTIVE]");
        MakeButton(root, fmt_txt, 12, 170, 216, 28, C_PANEL, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Toggle24Hour();
            }, this);

        // Card 4: Preview Jam & Sinkronisasi NTP
        lv_obj_t* p_card = MakePanel(root, 12, 202, 216, 78, C_PANEL, C_BORDER, 10);
        region_time_preview_ = MakeLabel(p_card, "--:--:--", 10, 3, 196, 22, FONT_20, C_CYAN, LV_TEXT_ALIGN_CENTER);
        region_date_preview_ = MakeLabel(p_card, "Menghubungkan Waktu...", 10, 24, 196, 15, FONT_10, C_TEXT, LV_TEXT_ALIGN_CENTER);
        region_ntp_status_ = MakeLabel(p_card, "NTP: id.pool.ntp.org", 10, 39, 196, 14, FONT_10, C_MUTED, LV_TEXT_ALIGN_CENTER);

        MakeButton(p_card, (language_idx_ == 0) ? "SINKRONISASI NTP" : "SYNC NTP NOW", 18, 55, 180, 18,
            C_PANEL_2, C_BORDER, C_AMBER, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->SyncNtpNow();
            }, this);
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
    MakeLabel(dialog, "Semua proses akan dihentikan dan sistem reboot.", 14, 38, 188, 28, FONT_10, C_MUTED);

    MakeButton(dialog, "BATAL", 12, 74, 90, 28, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
        [](lv_event_t* e2) {
            if (lv_event_get_code(e2) == LV_EVENT_CLICKED) {
                lv_obj_t* ov = static_cast<lv_obj_t*>(lv_event_get_user_data(e2));
                if (ov) lv_obj_del(ov);
            }
        }, overlay);

    MakeButton(dialog, "REBOOT", 114, 74, 90, 28, C_RED, C_RED, C_BG, FONT_12,
        [](lv_event_t* e2) {
            if (lv_event_get_code(e2) == LV_EVENT_CLICKED) esp_restart();
        });
}

void NovaDisplay::BuildSystem() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "INFORMASI SISTEM");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    char core_str[64];
    snprintf(core_str, sizeof(core_str), "Cores: %d | Silicon Rev: %d", chip_info.cores, chip_info.revision);

    uint32_t free_heap = esp_get_free_heap_size() / 1024;
    uint32_t min_heap = esp_get_minimum_free_heap_size() / 1024;
    char heap_str[64];
    snprintf(heap_str, sizeof(heap_str), "SRAM: 512 KB (Sisa: %lu KB)", (unsigned long)free_heap);

#if CONFIG_SPIRAM
    size_t psram_sz = esp_psram_get_size() / (1024 * 1024);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    char psram_str[64];
    snprintf(psram_str, sizeof(psram_str), "PSRAM: %u MB (Sisa: %lu KB)", (unsigned)psram_sz, (unsigned long)free_psram);
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
    snprintf(mac_txt, sizeof(mac_txt), "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    int up_h = (int)(uptime_sec / 3600);
    int up_m = (int)((uptime_sec % 3600) / 60);
    int up_s = (int)(uptime_sec % 60);
    char uptime_txt[64];
    snprintf(uptime_txt, sizeof(uptime_txt), "Uptime: %02dh %02dm %02ds", up_h, up_m, up_s);

    esp_reset_reason_t rst_reason = esp_reset_reason();
    const char* rst_str = "Power-on";
    if (rst_reason == ESP_RST_SW) rst_str = "Software Reset";
    else if (rst_reason == ESP_RST_PANIC) rst_str = "Exception Panic";
    else if (rst_reason == ESP_RST_INT_WDT || rst_reason == ESP_RST_TASK_WDT) rst_str = "Watchdog";
    else if (rst_reason == ESP_RST_DEEPSLEEP) rst_str = "Deep Sleep";

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

        MakeButton(netCard, "REBOOT SISTEM", 8, 58, 132, 22, C_PANEL_2, C_RED, C_RED, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRebootModal();
            }, this);
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
        snprintf(min_heap_str, sizeof(min_heap_str), "Min Free Heap: %lu KB", (unsigned long)min_heap);
        MakeLabel(memCard, min_heap_str, 10, 46, 188, 15, FONT_10, C_MUTED);
        MakeLabel(memCard, psram_str, 10, 62, 188, 15, FONT_10, C_MINT);
        MakeLabel(memCard, "SPI Flash: 16 MB External Quad-SPI", 10, 78, 188, 15, FONT_10, C_CYAN);
        cur_y += 102;

        // 3. Display & Peripherals Card
        lv_obj_t* periCard = MakePanel(list, 4, cur_y, 208, 114, C_PANEL, C_BORDER, 10);
        MakeIcon(periCard, ICON_SETTINGS, 8, 8, C_AMBER, 24);
        MakeLabel(periCard, "IC & Hardware Board", 38, 6, 160, 18, FONT_12, C_TEXT);
        MakeLabel(periCard, "Layar: 2.8\" IPS TFT 240x320 ILI9341V", 10, 28, 188, 15, FONT_10, C_TEXT);
        MakeLabel(periCard, "Touchscreen: FT6336G Capacitive (0x38)", 10, 44, 188, 15, FONT_10, C_TEXT);
        MakeLabel(periCard, "Audio Codec: ES8311 (I2S + I2C 0x18)", 10, 60, 188, 15, FONT_10, C_CYAN);
        MakeLabel(periCard, "Audio Amplifier: SC8002B Class-AB (IO1)", 10, 76, 188, 15, FONT_10, C_MUTED);
        MakeLabel(periCard, "Power/Bat: TP4054 Charger | LED: WS2812", 10, 92, 188, 15, FONT_10, C_MINT);
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
        MakeButton(list, "MULAI ULANG PERANGKAT", 4, cur_y + 20, 208, 32, C_PANEL, C_RED, C_RED, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->ShowRebootModal();
            }, this);
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
            if (h == 0) h = 12;
            snprintf(time_text, sizeof(time_text), "%d:%02d %s", h, info.tm_min, info.tm_hour >= 12 ? "PM" : "AM");
        }
        static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};
        snprintf(date_text, sizeof(date_text), "%02d %s %04d", info.tm_mday, months[info.tm_mon], info.tm_year + 1900);
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

        MakeButton(root, "⚡ BUKA SMARTWATCH", 10, 136, 144, 66,
            lv_color_hex(0x132238), lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), FONT_12,
            OnLaunchChronchi, this);

        // Right Column (w=150): Pomodoro Timer card
        lv_obj_t* tcard = MakePanel(root, 160, 38, 150, 164, C_PANEL, C_BORDER, 10);
        MakeIcon(tcard, ICON_CHRONOS, 8, 8, C_AMBER, 20);
        MakeLabel(tcard, "TIMER POMODORO", 32, 10, 110, 16, FONT_10, C_MUTED);
        timer_label_ = MakeCenteredLabel(tcard, timer_txt, 0, 32, 150, 36, FONT_28, C_CYAN);

        MakeButton(tcard, action_txt, 10, 74, 130, 28, C_CYAN, C_CYAN, C_BG, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->TimerAction();
            }, this);

        MakeButton(tcard, "ULANGI", 10, 108, 130, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->TimerReset();
            }, this);

        MakeCenteredLabel(tcard, timer_running_ ? "Sesi berjalan" : "Fokus 25 menit", 0, 140, 150, 14, FONT_10, C_MUTED);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        MakeLabel(root, "JAM LOKAL & WAKTU", 14, 40, 212, 14, FONT_10, C_CYAN);
        clock_label_ = MakeCenteredLabel(root, time_text, 8, 54, 224, 36, FONT_36, C_TEXT);
        date_label_ = MakeCenteredLabel(root, date_text, 8, 92, 224, 16, FONT_12, C_MUTED);

        lv_obj_t* tcard = MakePanel(root, 12, 114, 216, 104, C_PANEL, C_BORDER, 12);
        MakeIcon(tcard, ICON_CHRONOS, 10, 10, C_AMBER, 22);
        MakeLabel(tcard, "TIMER FOKUS POMODORO", 38, 12, 164, 16, FONT_10, C_MUTED);

        timer_label_ = MakeLabel(tcard, timer_txt, 12, 34, 111, 36, FONT_28, C_CYAN);

        MakeButton(tcard, action_txt, 130, 34, 74, 28, C_CYAN, C_CYAN, C_BG, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->TimerAction();
            }, this);

        MakeButton(tcard, "ULANGI", 130, 66, 74, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->TimerReset();
            }, this);

        MakeLabel(tcard, timer_running_ ? "Sesi fokus berjalan" : "Sesi fokus 25 menit", 12, 76, 110, 16, FONT_10, C_MUTED);

        // Card / Button to launch Chronchi Smartwatch mode directly
        MakeButton(root, "⚡ BUKA MODE SMARTWATCH", 12, 226, 216, 40,
            lv_color_hex(0x132238), lv_color_hex(0x0284C7), lv_color_hex(0x38BDF8), FONT_12,
            OnLaunchChronchi, this);
    }

    MakeBottomNav(root, PAGE_CHRONOS);
}

void NovaDisplay::BuildPinLab() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "PIN LAB & DIAGNOSTIK");

    int lvl = 0, mv = 0, raw = 0; bool chg = false;
    if (callbacks_.get_battery) callbacks_.get_battery(lvl, chg, mv, raw);
    int boot_val = gpio_get_level(GPIO_NUM_0);

    struct PinGroup { const char* title; const char* pins; lv_color_t color; };
    static const PinGroup pin_groups[] = {
        {"Audio ES8311 & PA", "MCLK:4 BCLK:5 DIN:6 WS:7 DOUT:8 PA:1", C_CYAN},
        {"LCD ILI9341V SPI", "CS:10 MOSI:11 SCK:12 MISO:13 DC:46 BL:45", C_AMBER},
        {"Touch FT6336G I2C", "SCL:15 SDA:16 INT:17 RST:18", C_MINT},
        {"MicroSD SDIO 4-Bit", "CLK:38 D0:39 CMD:40 D1:41 D2:48 D3:47", C_VIOLET},
        {"Sistem & Tombol", "BOOT:0 BATT:9 WS2812:42 UART:43,44", C_CYAN},
        {"Header Ekspansi IO", "GPIO 2, 3, 14, 21 (Available)", C_TEXT}
    };
    const size_t num_groups = sizeof(pin_groups) / sizeof(pin_groups[0]);

    if (width_ > 240) {
        // === RESPONSIVE LANDSCAPE 2-COLUMN PIN LAB (100% Fit On 1 Screen - No Scroll) ===
        // Left Column (x=8, w=148, y=38, h=166): Hardware Controls & Diagnostics Panel
        lv_obj_t* card = MakePanel(root, 8, 38, 148, 166, C_PANEL, C_BORDER, 10);
        MakeIcon(card, ICON_CHIP, 6, 8, C_CYAN, 20);
        MakeLabel(card, "Uji Hardware", 30, 6, 112, 16, FONT_12, C_TEXT);

        MakeLabel(card, "Backlight PWM (IO45):", 6, 26, 134, 14, FONT_10, C_MUTED);
        struct BrightPreset { const char* label; uint8_t val; };
        const BrightPreset bpresets[] = { {"25%", 64}, {"50%", 128}, {"75%", 192}, {"MAX", 255} };
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* bbtn = MakeButton(card, bpresets[i].label, 6 + i * 34, 42, 31, 22, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    uint8_t val = (uint8_t)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (self) self->ApplyBrightness(val);
                }, this);
            lv_obj_set_user_data(bbtn, (void*)(intptr_t)bpresets[i].val);
        }

        MakeButton(card, "TES KLIK AUDIO (IO1)", 6, 70, 134, 26, C_PANEL_2, C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
            }, this);

        char bat_sense_str[48];
        snprintf(bat_sense_str, sizeof(bat_sense_str), "Sensor IO9: %d mV (ADC:%d)", mv, raw);
        MakeLabel(card, bat_sense_str, 6, 102, 134, 14, FONT_10, C_MINT);

        char boot_str[48];
        snprintf(boot_str, sizeof(boot_str), "Tombol BOOT: %s", (boot_val == 0) ? "TEKAN (LOW)" : "LEPAS (HIGH)");
        MakeLabel(card, boot_str, 6, 118, 134, 14, FONT_10, C_MUTED);

        MakeLabel(card, "Board LCDWiki ES3C28P", 6, 140, 134, 14, FONT_10, C_CYAN);

        // Right Column (x=160, y=38, w=152, h=166): Dedicated Scrollable Pinout Map
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, 160, 38);
        lv_obj_set_size(list, 152, 166);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 36, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        for (size_t i = 0; i < num_groups; ++i) {
            lv_obj_t* prow = MakePanel(list, 0, (int)i * 40, 144, 36, C_PANEL, C_BORDER, 6);
            MakeLabel(prow, pin_groups[i].title, 6, 3, 132, 15, FONT_10, pin_groups[i].color);
            lv_obj_t* pins_lbl = MakeLabel(prow, pin_groups[i].pins, 6, 18, 132, 13, FONT_10, C_MUTED);
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
        lv_obj_set_style_pad_bottom(list, 52, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        int cur_y = 4;

        lv_obj_t* testCard = MakePanel(list, 4, cur_y, 208, 150, C_PANEL, C_BORDER, 10);
        MakeIcon(testCard, ICON_CHIP, 8, 8, C_CYAN, 22);
        MakeLabel(testCard, "Uji Live Hardware", 36, 6, 160, 18, FONT_14, C_TEXT);

        MakeLabel(testCard, "Kecerahan Backlight PWM (IO45):", 10, 28, 188, 15, FONT_10, C_MUTED);
        struct BrightPreset { const char* label; uint8_t val; };
        const BrightPreset bpresets[] = { {"25%", 64}, {"50%", 128}, {"75%", 192}, {"100%", 255} };
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* bbtn = MakeButton(testCard, bpresets[i].label, 8 + i * 48, 46, 44, 24, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
                [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    uint8_t val = (uint8_t)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    if (self) self->ApplyBrightness(val);
                }, this);
            lv_obj_set_user_data(bbtn, (void*)(intptr_t)bpresets[i].val);
        }

        MakeButton(testCard, "TES KLIK AUDIO PA (IO1)", 8, 76, 192, 26, C_PANEL_2, C_BORDER, C_CYAN, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
            }, this);

        char bat_sense_str[64];
        snprintf(bat_sense_str, sizeof(bat_sense_str), "Sensor Baterai (IO9): %d mV (ADC: %d)", mv, raw);
        MakeLabel(testCard, bat_sense_str, 10, 108, 188, 15, FONT_10, C_MINT);

        char boot_str[64];
        snprintf(boot_str, sizeof(boot_str), "Tombol BOOT (IO0): %s", (boot_val == 0) ? "DITEKAN (LOW)" : "LEPAS (HIGH)");
        MakeLabel(testCard, boot_str, 10, 126, 188, 15, FONT_10, C_MUTED);
        cur_y += 158;

        MakeLabel(list, "MAP PINOUT GPIO HARDWARE", 8, cur_y, 200, 16, FONT_10, C_CYAN);
        cur_y += 18;

        for (size_t i = 0; i < num_groups; ++i) {
            lv_obj_t* prow = MakePanel(list, 4, cur_y, 208, 38, C_PANEL, C_BORDER, 8);
            MakeLabel(prow, pin_groups[i].title, 8, 4, 192, 16, FONT_12, pin_groups[i].color);
            MakeLabel(prow, pin_groups[i].pins, 8, 20, 192, 14, FONT_10, C_MUTED);
            cur_y += 42;
        }
    }

    MakeBottomNav(root, PAGE_PINLAB);
}

void NovaDisplay::BuildPlaylist() {
    BuildRadio();
}



void NovaDisplay::BuildCurrentPage() {
    switch (current_page_) {
        case PAGE_SPLASH:   BuildSplash(); break;
        case PAGE_HOME:     BuildHome(); break;
        case PAGE_APPS:     BuildApps(); break;
        case PAGE_QUICK:    BuildQuick(); break;
        case PAGE_WIFI:     BuildWifi(); break;
        case PAGE_AI:       BuildAi(); break;
        case PAGE_SETTINGS: BuildSettings(); break;
        case PAGE_DISPLAY:  BuildDisplay(); break;
        case PAGE_AUDIO:    BuildAudio(); break;
        case PAGE_FILES:    BuildFiles(); break;
        case PAGE_BATTERY:  BuildBattery(); break;
        case PAGE_REGION:   BuildRegion(); break;
        case PAGE_SYSTEM:   BuildSystem(); break;
        case PAGE_CHRONOS:  BuildChronos(); break;
        case PAGE_PINLAB:     BuildPinLab(); break;
        case PAGE_PLAYLIST:   BuildRadio(); break;
        case PAGE_CALCULATOR: BuildCalculator(); break;
        case PAGE_FLAPPY:     BuildFlappy(); break;
        case PAGE_RADIO:      BuildRadio(); break;
        case PAGE_2048:       Build2048(); break;
        case PAGE_SNAKE:      BuildSnake(); break;
        case PAGE_BRICK:      BuildBrick(); break;
        case PAGE_MINESWEEPER: BuildMinesweeper(); break;
        case PAGE_RACER:      BuildRacer(); break;
        case PAGE_PIANO:      BuildPiano(); break;
        case PAGE_SKETCH:     BuildSketch(); break;
        case PAGE_MONITOR:    BuildMonitor(); break;
        case PAGE_NAV:        BuildNav(); break;
        case PAGE_BT_MUSIC:   BuildBtMusic(); break;
        case PAGE_NOTIF:      BuildNotif(); break;
        case PAGE_WEATHER:    BuildWeather(); break;
        case PAGE_PHONE:      BuildPhone(); break;
    }
}

void NovaDisplay::UpdateLiveLabels() {
    time_t now_sec = time(nullptr);
    struct tm info;
    localtime_r(&now_sec, &info);

    static const char* kDaysId[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    static const char* kMonthsId[] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun", "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};
    static const char* kDaysEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonthsEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char time_text[24] = "--:--", date_text[48] = "WAKTU BELUM TERSINKRON";
    char time_preview[32] = "--:--:--";
    bool valid_time = (info.tm_year > (2020 - 1900));

    if (valid_time) {
        if (use_24hour_) {
            snprintf(time_text, sizeof(time_text), "%02d:%02d", info.tm_hour, info.tm_min);
            snprintf(time_preview, sizeof(time_preview), "%02d:%02d:%02d", info.tm_hour, info.tm_min, info.tm_sec);
        } else {
            int h = info.tm_hour % 12;
            if (h == 0) h = 12;
            snprintf(time_text, sizeof(time_text), "%d:%02d %s", h, info.tm_min, info.tm_hour >= 12 ? "PM" : "AM");
            snprintf(time_preview, sizeof(time_preview), "%d:%02d:%02d %s", h, info.tm_min, info.tm_sec, info.tm_hour >= 12 ? "PM" : "AM");
        }

        if (language_idx_ == 0) {
            snprintf(date_text, sizeof(date_text), "%s, %02d %s %04d",
                     kDaysId[info.tm_wday % 7], info.tm_mday, kMonthsId[info.tm_mon % 12], info.tm_year + 1900);
        } else {
            snprintf(date_text, sizeof(date_text), "%s, %s %02d, %04d",
                     kDaysEn[info.tm_wday % 7], kMonthsEn[info.tm_mon % 12], info.tm_mday, info.tm_year + 1900);
        }
    }

    if (clock_label_) lv_label_set_text(clock_label_, time_text);
    if (date_label_) lv_label_set_text(date_label_, date_text);
    if (region_time_preview_) lv_label_set_text(region_time_preview_, time_preview);
    if (region_date_preview_) lv_label_set_text(region_date_preview_, date_text);
    if (region_ntp_status_) {
        lv_label_set_text(region_ntp_status_, valid_time ? "NTP: Tersinkron (Akurat)" : "NTP: Menunggu Koneksi Wi-Fi");
        lv_obj_set_style_text_color(region_ntp_status_, valid_time ? C_MINT : C_AMBER, LV_PART_MAIN);
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
    bool wifi_connected = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);

    if (top_bluetooth_icon_) {
        bool ble_conn = false;
        std::string bname, bmac;
        if (callbacks_.get_ble_status) {
            callbacks_.get_ble_status(ble_conn, bname, bmac);
        }
        lv_label_set_text(top_bluetooth_icon_, MATERIAL_SYMBOLS_BLUETOOTH);
        lv_obj_set_style_text_color(top_bluetooth_icon_, ble_conn ? lv_color_hex(0x38BDF8) : lv_color_hex(0x64748B), LV_PART_MAIN);
    }

    if (top_notif_btn_ && top_notif_icon_ && top_notif_label_) {
        int n_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        if (n_cnt > 0) {
            char n_buf[16];
            snprintf(n_buf, sizeof(n_buf), "%d", n_cnt);
            lv_label_set_text(top_notif_label_, n_buf);
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0x64748B), LV_PART_MAIN);
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(top_notif_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    if (top_mute_icon_) {
        lv_label_set_text(top_mute_icon_, muted_ ? MATERIAL_SYMBOLS_VOLUME_OFF : MATERIAL_SYMBOLS_VOLUME_UP);
        lv_obj_set_style_text_color(top_mute_icon_, muted_ ? C_MUTED : lv_color_hex(0x06B6D4), LV_PART_MAIN);
    }

    static uint32_t wifi_tick = 0;
    wifi_tick++;

    if (top_wifi_icon_) {
        if (wifi_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                int8_t rssi = ap_info.rssi;
                if (rssi >= -65) {
                    // Strong signal (3 bars): Green
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI);
                    lv_obj_set_style_text_color(top_wifi_icon_, lv_color_hex(0x22C55E), LV_PART_MAIN);
                } else if (rssi >= -78) {
                    // Medium signal (2 bars): Amber/Yellow
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI_2_BAR);
                    lv_obj_set_style_text_color(top_wifi_icon_, lv_color_hex(0xEAB308), LV_PART_MAIN);
                } else {
                    // Weak signal (1 bar): Red/Coral
                    lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI_1_BAR);
                    lv_obj_set_style_text_color(top_wifi_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
                }
            } else {
                lv_label_set_text(top_wifi_icon_, MATERIAL_SYMBOLS_WIFI);
                lv_obj_set_style_text_color(top_wifi_icon_, lv_color_hex(0x22C55E), LV_PART_MAIN);
            }
        } else {
            // Reconnecting indicator: alternating icon and amber color
            lv_label_set_text(top_wifi_icon_, (wifi_tick % 2 == 0) ? MATERIAL_SYMBOLS_REFRESH : MATERIAL_SYMBOLS_WIFI_OFF);
            lv_obj_set_style_text_color(top_wifi_icon_, lv_color_hex(0xF59E0B), LV_PART_MAIN);
        }
    }

    if (battery_percent_label_ || battery_volt_label_ || top_battery_icon_ || top_battery_label_) {
        int level = 100, mv = 0, raw = 0;
        bool chg = false;
        if (callbacks_.get_battery) callbacks_.get_battery(level, chg, mv, raw);

        if (top_battery_icon_) {
            lv_label_set_text(top_battery_icon_, chg ? MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT : MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL);
            lv_obj_set_style_text_color(top_battery_icon_, chg ? lv_color_hex(0xF59E0B) : (level < 20 ? lv_color_hex(0xFF8195) : lv_color_hex(0x7DE0B5)), LV_PART_MAIN);
            if (bat_style_ == BAT_STYLE_PERCENT_ONLY) {
                lv_obj_add_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(top_battery_icon_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (top_battery_label_) {
            char pct[16]; snprintf(pct, sizeof(pct), "%d%%", level);
            lv_label_set_text(top_battery_label_, pct);
            if (bat_style_ == BAT_STYLE_ICON_ONLY) {
                lv_obj_add_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(top_battery_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (battery_percent_label_) {
            char pct[16]; snprintf(pct, sizeof(pct), "%d%%", level);
            lv_label_set_text(battery_percent_label_, pct);
        }
        if (battery_volt_label_) {
            char v[48];
            float v_float = mv / 1000.0f;
            snprintf(v, sizeof(v), (width_ > 240) ? "Tegangan: %d mV (%.2fV)" : "Tegangan: %d mV (%.2f V)", mv, v_float);
            lv_label_set_text(battery_volt_label_, v);
        }
        if (battery_raw_label_) {
            char r[48];
            snprintf(r, sizeof(r), (width_ > 240) ? "ADC Mentah: %d" : "Sensor ADC Mentah: %d (Divider 1:1)", raw);
            lv_label_set_text(battery_raw_label_, r);
        }
    }

    if (wifi_status_label_) {
        if (wifi_connected) {
            char ip_str[32] = "";
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            wifi_ap_record_t ap_rec;
            std::string ssid = (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) ? (char*)ap_rec.ssid : "Wi-Fi";
            std::string details = ssid + "  |  " + ip_str;
            lv_label_set_text(wifi_status_label_, details.c_str());
            lv_obj_set_style_text_color(wifi_status_label_, C_CYAN, LV_PART_MAIN);
        } else {
            lv_label_set_text(wifi_status_label_, "Menghubungkan kembali...");
            lv_obj_set_style_text_color(wifi_status_label_, lv_color_hex(0xF59E0B), LV_PART_MAIN);
        }
    }
}

void NovaDisplay::Update() {
    UpdateLiveLabels();
}

void NovaDisplay::ShowFileModal(const std::string& filename, size_t size) {
    if (file_modal_) {
        lv_obj_del(file_modal_);
        file_modal_ = nullptr;
    }

    std::string ext = "";
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = filename.substr(dot_pos);
        for (auto &c : ext) c = tolower(c);
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
        snprintf(sz_str, sizeof(sz_str), "Ukuran: %.1f KB (%u B)", (double)size / 1024.0, (unsigned)size);
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
        std::string full_text_path = (filename.find("/sdcard/") == 0) ? filename : ("/sdcard/" + filename);
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
        lv_obj_t* info_box = MakePanel(card, 8, 44, card_w - 16, box_h, lv_color_hex(0x111827), lv_color_hex(0x1F2937), 8);
        MakeIcon(info_box, ICON_FILES, (card_w - 16 - 32) / 2, (box_h < 150) ? 10 : 16, C_AMBER, 32);

        MakeLabel(info_box, "Berkas Gambar / Biner", 6, (box_h < 150) ? 46 : 60, card_w - 28, 16, FONT_12, C_AMBER);
        MakeLabel(info_box, "Rekomendasi Format:\nBMP 240x320 24-bit / 16-bit\nAtau RAW RGB565 Image", 10, (box_h < 150) ? 66 : 82, card_w - 36, 42, FONT_10, C_MUTED);
    }

    MakeButton(card, "TUTUP", 8, card_h - 32, card_w - 16, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self && self->file_modal_) {
                if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                lv_obj_del(self->file_modal_);
                self->file_modal_ = nullptr;
            }
        }, this);
}

void NovaDisplay::ShowOnlinePlayerModal(const std::string& title, const std::string& video_id, const std::string& duration) {
    ShowAudioPlayerModal(title, video_id, true, 0, duration);
}

void NovaDisplay::ShowAudioPlayerModal(const std::string& title, const std::string& source_id_or_path, bool is_online, size_t file_size, const std::string& duration) {
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
    if (is_online) online_modal_ = modal; else file_modal_ = modal;

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

    MakeLabel(card, is_online ? "PEMUTAR AUDIO ONLINE" : "PEMUTAR MUSIK", 34, (height_ < 260 ? 4 : 6), card_w - 44, 14, FONT_10, C_CYAN);

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
            snprintf(sub_str, sizeof(sub_str), "SD Card • %.2f MB • WAV", (double)file_size / (1024.0 * 1024.0));
        } else if (file_size >= 1024) {
            snprintf(sub_str, sizeof(sub_str), "SD Card • %u KB • WAV", (unsigned)(file_size / 1024));
        } else {
            snprintf(sub_str, sizeof(sub_str), "SD Card • %u B • WAV", (unsigned)file_size);
        }
    }
    MakeLabel(card, sub_str, 10, (height_ < 260 ? 31 : 38), card_w - 20, 14, FONT_10, lv_color_hex(0x71717A));

    // Separator line
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_pos(sep, 8, (height_ < 260 ? 44 : 52));
    lv_obj_set_size(sep, card_w - 16, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x27272A), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);

    // Center Earphone Visualizer Box
    int anim_y = (height_ < 260) ? 48 : 58;
    int anim_h = (height_ < 260) ? 52 : 70;
    lv_obj_t* anim_box = MakePanel(card, 8, anim_y, card_w - 16, anim_h, lv_color_hex(0x181818), lv_color_hex(0x282828), 10);

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
    int bar_x[6] = { hp_x - 44, hp_x - 30, hp_x - 16, hp_x + hp_size + 12, hp_x + hp_size + 26, hp_x + hp_size + 40 };
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

    lv_obj_t* state_lbl = MakeLabel(anim_box,
        is_playing ? "● SEDANG MEMUTAR (ES8311)" : "○ SIAP DIPUTAR",
        4, (height_ < 260 ? 36 : 48), card_w - 24, 16, FONT_10, is_playing ? C_CYAN : lv_color_hex(0x71717A));
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
    auto* actx = new AnimCtx{this, {bars[0], bars[1], bars[2], bars[3], bars[4], bars[5]}, hp_icon, state_lbl, nullptr, 0, is_online};
    actx->timer = lv_timer_create([](lv_timer_t* t) {
        auto* c = static_cast<AnimCtx*>(lv_timer_get_user_data(t));
        if (!c || !c->self) return;
        bool playing = false;
        if (c->is_online) {
            playing = c->self->callbacks_.is_online_playing && c->self->callbacks_.is_online_playing();
        } else {
            playing = c->self->callbacks_.is_audio_playing && c->self->callbacks_.is_audio_playing();
        }
        c->phase++;
        if (playing) {
            for (int i = 0; i < 6; i++) {
                float s = sinf((float)c->phase * 0.35f + (float)i * 1.15f);
                float cs = cosf((float)c->phase * 0.22f - (float)i * 0.8f);
                int h = (int)(6 + (s * 0.5f + 0.5f) * 14.0f + (cs * 0.5f + 0.5f) * 10.0f);
                if (h < 4) h = 4;
                if (h > 26) h = 26;
                lv_obj_set_height(c->bars[i], h);
                lv_obj_set_y(c->bars[i], 24 - (h / 2));
            }
            lv_obj_set_style_text_color(c->hp_icon, ((c->phase / 4) % 2 == 0) ? C_CYAN : lv_color_hex(0xA5F3FC), LV_PART_MAIN);
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
    }, 80, actx);

    lv_obj_add_event_cb(anim_box, [](lv_event_t* e) {
        auto* c = static_cast<AnimCtx*>(lv_event_get_user_data(e));
        if (c) {
            if (c->timer) lv_timer_del(c->timer);
            delete c;
        }
    }, LV_EVENT_DELETE, actx);

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
    MakeButton(card, repeat_active ? "🔁 ULANG: TERUS (LOOP)" : "▶️ PUTAR: 1x (SEKALI)",
        8, rep_y, card_w - 16, rep_h,
        repeat_active ? C_CYAN : lv_color_hex(0x242424),
        repeat_active ? C_CYAN : lv_color_hex(0x3E3E3E),
        repeat_active ? lv_color_black() : lv_color_hex(0xD4D4D8), FONT_10,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
            if (!pc || !pc->self) return;
            if (pc->self->callbacks_.trigger_click) pc->self->callbacks_.trigger_click();
            bool cur = pc->self->callbacks_.get_repeat ? pc->self->callbacks_.get_repeat() : false;
            bool nxt = !cur;
            if (pc->self->callbacks_.set_repeat) pc->self->callbacks_.set_repeat(nxt);

            lv_obj_t* b = static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_obj_t* l = lv_obj_get_child(b, 0);
            if (l) {
                lv_label_set_text(l, nxt ? "🔁 ULANG: TERUS (LOOP)" : "▶️ PUTAR: 1x (SEKALI)");
                lv_obj_set_style_text_color(l, nxt ? lv_color_black() : lv_color_hex(0xD4D4D8), LV_PART_MAIN);
            }
            lv_obj_set_style_bg_color(b, nxt ? C_CYAN : lv_color_hex(0x242424), LV_PART_MAIN);
            lv_obj_set_style_border_color(b, nxt ? C_CYAN : lv_color_hex(0x3E3E3E), LV_PART_MAIN);
        }, pctx);

    // Play / Stop Button
    int play_y = (height_ < 260) ? 132 : 166;
    int play_h = (height_ < 260) ? 30 : 34;
    MakeButton(card, is_playing ? "⏹ HENTIKAN MUSIK" : "▶ PUTAR SEKARANG", 8, play_y, card_w - 16, play_h,
        is_playing ? lv_color_hex(0xE11D48) : C_CYAN,
        is_playing ? lv_color_hex(0xBE123C) : C_CYAN,
        is_playing ? lv_color_white() : lv_color_black(), FONT_12,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
            if (pc && pc->self) {
                if (pc->self->callbacks_.trigger_click) pc->self->callbacks_.trigger_click();
                if (pc->is_online) {
                    bool play = pc->self->callbacks_.is_online_playing && pc->self->callbacks_.is_online_playing();
                    if (play) {
                        if (pc->self->callbacks_.stop_online) pc->self->callbacks_.stop_online();
                    } else {
                        if (pc->self->callbacks_.play_online) pc->self->callbacks_.play_online(pc->source, pc->title, pc->duration);
                    }
                } else {
                    bool play = pc->self->callbacks_.is_audio_playing && pc->self->callbacks_.is_audio_playing();
                    if (play) {
                        if (pc->self->callbacks_.stop_audio) pc->self->callbacks_.stop_audio();
                    } else {
                        std::string p = (pc->source.find("/sdcard/") == 0) ? pc->source : ("/sdcard/" + pc->source);
                        if (pc->self->callbacks_.play_audio) pc->self->callbacks_.play_audio(p);
                    }
                }
                std::string t = pc->title;
                std::string s = pc->source;
                bool o = pc->is_online;
                size_t sz = pc->file_size;
                std::string dur = pc->duration;
                pc->self->ShowAudioPlayerModal(t, s, o, sz, dur);
            }
        }, pctx);

    int bot_y = (height_ < 260) ? 166 : 206;
    int bot_h = (height_ < 260) ? 24 : 28;
    if (is_online) {
        int half_w = (card_w - 24) / 2;
        MakeButton(card, "⬇ UNDUH SD", 8, bot_y, half_w, bot_h,
            lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* pc = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
                if (pc && pc->self) {
                    if (pc->self->callbacks_.trigger_click) pc->self->callbacks_.trigger_click();
                    if (pc->self->callbacks_.download_online) {
                        pc->self->callbacks_.download_online(pc->source, pc->title);
                    }
                    if (pc->self->online_modal_) {
                        lv_obj_del(pc->self->online_modal_);
                        pc->self->online_modal_ = nullptr;
                    }
                }
            }, pctx);

        MakeButton(card, "✕ TUTUP", 8 + half_w + 8, bot_y, half_w, bot_h,
            lv_color_hex(0x1E1E22), lv_color_hex(0x2D2D32), lv_color_hex(0xA1A1AA), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->online_modal_) {
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                    lv_obj_del(self->online_modal_);
                    self->online_modal_ = nullptr;
                }
            }, this);
    } else {
        MakeButton(card, "✕ KEMBALI KE FILE HUB", 8, bot_y, card_w - 16, bot_h,
            lv_color_hex(0x1E1E22), lv_color_hex(0x2D2D32), lv_color_hex(0xA1A1AA), FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self && self->file_modal_) {
                    if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                    lv_obj_del(self->file_modal_);
                    self->file_modal_ = nullptr;
                }
            }, this);
    }

    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        auto* c = static_cast<PlayerCtx*>(lv_event_get_user_data(e));
        if (c) delete c;
    }, LV_EVENT_DELETE, pctx);
}

void NovaDisplay::ShowDownloadModal(const std::string& title, int progress_pct, const std::string& status_text) {
    if (progress_pct < 0) progress_pct = 0;
    if (progress_pct > 100) progress_pct = 100;

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
        lv_label_set_text(dl_icon_label_, (progress_pct >= 100) ? MATERIAL_SYMBOLS_CHECK : MATERIAL_SYMBOLS_ARROW_DOWNWARD);
        lv_obj_set_style_text_color(dl_icon_label_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_MAIN);
        lv_obj_set_pos(dl_icon_label_, 12, (height_ < 260 ? 6 : 10));

        // Header Title
        dl_hdr_label_ = lv_label_create(dl_card_);
        lv_label_set_text(dl_hdr_label_, (progress_pct >= 100) ? "UNDUHAN SUKSES" : "MENGUNDUH KE FOLDER MUSIK");
        lv_obj_set_style_text_font(dl_hdr_label_, FONT_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_hdr_label_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_MAIN);
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
        lv_obj_set_style_text_font(dl_pct_label_, (height_ < 260 ? FONT_20 : FONT_24), LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_pct_label_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_MAIN);
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
        lv_obj_set_style_bg_color(dl_bar_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_radius(dl_bar_, 7, LV_PART_MAIN);
        lv_obj_set_style_radius(dl_bar_, 7, LV_PART_INDICATOR);

        // Status text label below bar
        dl_status_label_ = lv_label_create(dl_card_);
        std::string initial_status = status_text.empty() ? "Menyimpan ke /sdcard/music/..." : status_text;
        lv_label_set_text(dl_status_label_, initial_status.c_str());
        lv_obj_set_style_text_font(dl_status_label_, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(dl_status_label_, (progress_pct >= 100) ? C_MINT : C_MUTED, LV_PART_MAIN);
        lv_obj_set_pos(dl_status_label_, 10, (height_ < 260 ? 116 : 140));
        lv_obj_set_width(dl_status_label_, card_w - 20);
        lv_obj_set_style_text_align(dl_status_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(dl_status_label_, LV_LABEL_LONG_DOT);

        // Close / Cancel button
        int btn_y = (height_ < 260) ? 144 : 182;
        int btn_h = (height_ < 260) ? 26 : 32;
        MakeButton(dl_card_, (progress_pct >= 100) ? "SELESAI" : "TUTUP", 12, btn_y, card_w - 24, btn_h,
                   C_PANEL_2, C_BORDER, C_TEXT, (height_ < 260 ? FONT_10 : FONT_12),
                   [](lv_event_t* e) {
                       if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self) {
                           self->CloseDownloadModal(true);
                       }
                   }, this);
    } else {
        // Modal already exists, update dynamic fields
        if (dl_bar_) {
            lv_bar_set_value(dl_bar_, progress_pct, LV_ANIM_ON);
            lv_obj_set_style_bg_color(dl_bar_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_INDICATOR);
        }
        if (dl_pct_label_) {
            char pct_str[16];
            snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct);
            lv_label_set_text(dl_pct_label_, pct_str);
            lv_obj_set_style_text_color(dl_pct_label_, (progress_pct >= 100) ? C_MINT : C_CYAN, LV_PART_MAIN);
        }
        if (dl_title_label_ && !title.empty()) {
            lv_label_set_text(dl_title_label_, title.c_str());
        }
        if (dl_status_label_ && !status_text.empty()) {
            lv_label_set_text(dl_status_label_, status_text.c_str());
            lv_obj_set_style_text_color(dl_status_label_, (progress_pct >= 100) ? C_MINT : C_MUTED, LV_PART_MAIN);
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
            dl_timer_ = lv_timer_create([](lv_timer_t* t) {
                auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
                if (self) {
                    self->CloseDownloadModal(true);
                }
            }, 2500, this);
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

static void OnCalcBtnClicked(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(event));
    if (!self) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(event);
    const char* key = (const char*)lv_obj_get_user_data(target);
    if (key) self->OnCalcKey(key);
}

static std::string FormatCalcNumber(double val) {
    if (std::isnan(val) || std::isinf(val)) return "Error";
    if (std::abs(val) < 1e-12) return "0";

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
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

void NovaDisplay::OnCalcKey(const std::string& key) {
    if (callbacks_.trigger_click) callbacks_.trigger_click();

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
            if (calc_val_ == "-") calc_val_ = "0";
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
                if (calc_val_[0] == '-') calc_val_.erase(0, 1);
                else calc_val_ = "-" + calc_val_;
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
    } else if (key == "+" || key == "-" || key == "x" || key == "X" || key == "*" || key == "×" || key == "/" || key == "÷") {
        if (calc_val_ == "Error") {
            calc_val_ = "0";
            calc_expr_ = "";
            calc_has_result_ = false;
        }

        double current = atof(calc_val_.c_str());

        // If an operator was already pending and user entered another number before hitting operator again
        if (!calc_pending_op_.empty() && !calc_reset_next_) {
            bool div_zero = false;
            if (calc_pending_op_ == "+") {
                calc_accum_ += current;
            } else if (calc_pending_op_ == "-") {
                calc_accum_ -= current;
            } else if (calc_pending_op_ == "x" || calc_pending_op_ == "X" || calc_pending_op_ == "×" || calc_pending_op_ == "*") {
                calc_accum_ *= current;
            } else if (calc_pending_op_ == "/" || calc_pending_op_ == "÷") {
                if (std::abs(current) < 1e-12) div_zero = true;
                else calc_accum_ /= current;
            }

            if (div_zero) {
                calc_expr_ = "";
                calc_val_ = "Error";
                calc_pending_op_ = "";
                calc_reset_next_ = true;
                calc_has_result_ = true;
                if (calc_expr_lbl_) lv_label_set_text(calc_expr_lbl_, calc_expr_.c_str());
                if (calc_val_lbl_) lv_label_set_text(calc_val_lbl_, calc_val_.c_str());
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
            } else if (calc_pending_op_ == "x" || calc_pending_op_ == "X" || calc_pending_op_ == "×" || calc_pending_op_ == "*") {
                calc_accum_ *= current;
            } else if (calc_pending_op_ == "/" || calc_pending_op_ == "÷") {
                if (std::abs(current) < 1e-12) div_zero = true;
                else calc_accum_ /= current;
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

    if (calc_expr_lbl_) lv_label_set_text(calc_expr_lbl_, calc_expr_.c_str());
    if (calc_val_lbl_) lv_label_set_text(calc_val_lbl_, calc_val_.c_str());
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
    } kButtons[] = {
        {"C",   0, 0, 1, lv_color_hex(0x3B1D28), lv_color_hex(0xFF5A70)},
        {"+/-", 1, 0, 1, C_PANEL_2, C_TEXT},
        {"%",   2, 0, 1, C_PANEL_2, C_TEXT},
        {"/",   3, 0, 1, C_PANEL_2, C_AMBER},

        {"7",   0, 1, 1, C_PANEL, C_TEXT},
        {"8",   1, 1, 1, C_PANEL, C_TEXT},
        {"9",   2, 1, 1, C_PANEL, C_TEXT},
        {"x",   3, 1, 1, C_PANEL_2, C_AMBER},

        {"4",   0, 2, 1, C_PANEL, C_TEXT},
        {"5",   1, 2, 1, C_PANEL, C_TEXT},
        {"6",   2, 2, 1, C_PANEL, C_TEXT},
        {"-",   3, 2, 1, C_PANEL_2, C_AMBER},

        {"1",   0, 3, 1, C_PANEL, C_TEXT},
        {"2",   1, 3, 1, C_PANEL, C_TEXT},
        {"3",   2, 3, 1, C_PANEL, C_TEXT},
        {"+",   3, 3, 1, C_PANEL_2, C_AMBER},

        {"0",   0, 4, 2, C_PANEL, C_TEXT},
        {".",   2, 4, 1, C_PANEL, C_TEXT},
        {"=",   3, 4, 1, C_CYAN, C_BG}
    };

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Display Card on left (w=122, h=164)
        lv_obj_t* disp_card = MakePanel(root, 8, 38, 122, 164, C_PANEL, C_BORDER, 10);
        calc_expr_lbl_ = MakeLabel(disp_card, calc_expr_.c_str(), 6, 8, 110, 18, FONT_10, C_MUTED, LV_TEXT_ALIGN_RIGHT);
        calc_val_lbl_ = MakeLabel(disp_card, calc_val_.c_str(), 6, 36, 110, 40, FONT_24, C_CYAN, LV_TEXT_ALIGN_RIGHT);
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
        calc_expr_lbl_ = MakeLabel(disp_card, calc_expr_.c_str(), 8, 6, 204, 18, FONT_12, C_MUTED, LV_TEXT_ALIGN_RIGHT);
        calc_val_lbl_ = MakeLabel(disp_card, calc_val_.c_str(), 8, 24, 204, 34, FONT_28, C_CYAN, LV_TEXT_ALIGN_RIGHT);

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
            if (strcmp(b.key, "+") == 0 || strcmp(b.key, "-") == 0 ||
                strcmp(b.key, "x") == 0 || strcmp(b.key, "/") == 0 ||
                strcmp(b.key, "=") == 0 || strcmp(b.key, ".") == 0 ||
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

static void OnFlappyTouchStatic(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
    if (self) self->FlappyFlap();
}

static void OnFlappyTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->FlappyTick();
}

void NovaDisplay::FlappyReset() {
    flappy_state_ = FLAPPY_RUNNING;
    flappy_score_ = 0;
    flappy_bird_y_ = (height_ > 240) ? 95.0f : 55.0f;
    flappy_bird_vy_ = -2.2f;
    flappy_pipe_x_ = (width_ > 240) ? 300.0f : 230.0f;
    flappy_pipe_gap_y_ = (height_ > 240) ? 75.0f : 45.0f;

    if (flappy_card_) lv_obj_add_flag(flappy_card_, LV_OBJ_FLAG_HIDDEN);
    if (flappy_score_lbl_) lv_label_set_text(flappy_score_lbl_, "0");
}

void NovaDisplay::FlappyFlap() {
    if (callbacks_.trigger_click) callbacks_.trigger_click();

    if (flappy_state_ == FLAPPY_IDLE || flappy_state_ == FLAPPY_OVER) {
        FlappyReset();
        return;
    }

    if (flappy_state_ == FLAPPY_RUNNING) {
        flappy_bird_vy_ = -3.4f; // Lompatan pas dan terkontrol (tidak overshooting)
    }
}

void NovaDisplay::FlappyTick() {
    if (flappy_state_ != FLAPPY_RUNNING) return;

    // Bird gravity physics
    flappy_bird_y_ += flappy_bird_vy_;
    flappy_bird_vy_ += 0.28f; // Gravitasi halus dan stabil
    if (flappy_bird_vy_ > 4.5f) flappy_bird_vy_ = 4.5f;

    // Pipe movement
    flappy_pipe_x_ -= 2.6f;
    int ground_y = (height_ > 240) ? 222 : 148;
    int gap_h = (height_ > 240) ? 82 : 68;
    if (flappy_pipe_x_ < -36.0f) {
        flappy_pipe_x_ = (width_ > 240) ? (width_ - 24.0f) : 224.0f;
        int max_gap_rand = ground_y - gap_h - 30;
        if (max_gap_rand < 10) max_gap_rand = 10;
        flappy_pipe_gap_y_ = 15.0f + (esp_random() % max_gap_rand);
        flappy_score_++;
        if (flappy_score_lbl_) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", flappy_score_);
            lv_label_set_text(flappy_score_lbl_, buf);
        }
        if (flappy_score_ > flappy_best_) {
            flappy_best_ = flappy_score_;
            Settings s("flappy", true);
            s.SetInt("best", flappy_best_);
            if (flappy_best_lbl_) {
                char bbuf[32];
                snprintf(bbuf, sizeof(bbuf), "REKOR: %d", flappy_best_);
                lv_label_set_text(flappy_best_lbl_, bbuf);
            }
        }
    }

    // Update Bird pos
    int bird_x = 42;
    int bird_y = (int)flappy_bird_y_;
    const int bird_size = 20;

    if (flappy_bird_) {
        lv_obj_set_pos(flappy_bird_, bird_x, bird_y);
    }

    // Update Pipes pos & size
    const int pipe_w = 32;

    if (flappy_pipe_top_) {
        int top_h = (int)flappy_pipe_gap_y_;
        lv_obj_set_pos(flappy_pipe_top_, (int)flappy_pipe_x_, 0);
        lv_obj_set_size(flappy_pipe_top_, pipe_w, std::max(2, top_h));
    }
    if (flappy_pipe_bot_) {
        int bot_y = (int)(flappy_pipe_gap_y_ + gap_h);
        int bot_h = ground_y - bot_y;
        lv_obj_set_pos(flappy_pipe_bot_, (int)flappy_pipe_x_, bot_y);
        lv_obj_set_size(flappy_pipe_bot_, pipe_w, std::max(2, bot_h));
    }

    // Collision Check: Floor and Ceiling
    bool dead = false;
    if (bird_y <= 1 || bird_y + bird_size >= ground_y) {
        dead = true;
    }

    // Collision Check: Pipes
    int px = (int)flappy_pipe_x_;
    if (bird_x + bird_size >= px && bird_x <= px + pipe_w) {
        if (bird_y < flappy_pipe_gap_y_ || bird_y + bird_size > flappy_pipe_gap_y_ + gap_h) {
            dead = true;
        }
    }

    if (dead) {
        flappy_state_ = FLAPPY_OVER;
        if (flappy_card_) {
            lv_obj_clear_flag(flappy_card_, LV_OBJ_FLAG_HIDDEN);
            if (flappy_card_title_) lv_label_set_text(flappy_card_title_, "GAME OVER!");
            if (flappy_card_sub_) {
                char buf[96];
                snprintf(buf, sizeof(buf), "Skor: %d  |  Rekor: %d", flappy_score_, flappy_best_);
                lv_label_set_text(flappy_card_sub_, buf);
            }
            if (flappy_card_btn_) lv_label_set_text(flappy_card_btn_, "MAIN LAGI");
        }
    }
}

void NovaDisplay::BuildFlappy() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "FLAPPY BIRD");

    Settings s("flappy", true);
    flappy_best_ = s.GetInt("best", 0);
    flappy_state_ = FLAPPY_IDLE;
    flappy_score_ = 0;

    // Game Box Container
    int box_w = (width_ > 240) ? (width_ - 16) : 224;
    int box_h = (height_ > 240) ? 240 : 166;
    int ground_h = 18;
    int ground_y = box_h - ground_h;

    flappy_box_ = lv_obj_create(root);
    lv_obj_set_pos(flappy_box_, (width_ - box_w) / 2, 38);
    lv_obj_set_size(flappy_box_, box_w, box_h);
    lv_obj_set_style_bg_color(flappy_box_, lv_color_hex(0x0A0F1D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flappy_box_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_box_, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_box_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_box_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flappy_box_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_box_, LV_OBJ_FLAG_SCROLLABLE);

    // Ground Grass Line
    lv_obj_t* ground = lv_obj_create(flappy_box_);
    lv_obj_set_pos(ground, 0, ground_y);
    lv_obj_set_size(ground, box_w, ground_h);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0x15803D), LV_PART_MAIN);
    lv_obj_set_style_border_width(ground, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ground, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);

    // Top Pipe
    flappy_pipe_top_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_pipe_top_, 230, 0);
    lv_obj_set_size(flappy_pipe_top_, 32, 75);
    lv_obj_set_style_bg_color(flappy_pipe_top_, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_pipe_top_, lv_color_hex(0x166534), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_pipe_top_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_pipe_top_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_pipe_top_, LV_OBJ_FLAG_SCROLLABLE);

    // Bottom Pipe
    flappy_pipe_bot_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_pipe_bot_, 230, 150);
    lv_obj_set_size(flappy_pipe_bot_, 32, 72);
    lv_obj_set_style_bg_color(flappy_pipe_bot_, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_pipe_bot_, lv_color_hex(0x166534), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_pipe_bot_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_pipe_bot_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_pipe_bot_, LV_OBJ_FLAG_SCROLLABLE);

    // Yellow Bird
    flappy_bird_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_bird_, 42, 100);
    lv_obj_set_size(flappy_bird_, 20, 20);
    lv_obj_set_style_bg_color(flappy_bird_, lv_color_hex(0xFACC15), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_bird_, lv_color_hex(0xCA8A04), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_bird_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_bird_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_bird_, LV_OBJ_FLAG_SCROLLABLE);

    // Bird Eye Dot
    lv_obj_t* eye = lv_obj_create(flappy_bird_);
    lv_obj_set_pos(eye, 11, 4);
    lv_obj_set_size(eye, 4, 4);
    lv_obj_set_style_bg_color(eye, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

    // Big Score Label at Top Center
    flappy_score_lbl_ = MakeCenteredLabel(flappy_box_, "0", 0, 8, box_w, 32, FONT_24, C_TEXT);

    // Best Score indicator inside the game box itself (top right corner, does NOT touch OS header!)
    char best_buf[32];
    snprintf(best_buf, sizeof(best_buf), "REKOR: %d", flappy_best_);
    flappy_best_lbl_ = MakeLabel(flappy_box_, best_buf, box_w - 92, 8, 84, 18, FONT_10, C_AMBER, LV_TEXT_ALIGN_RIGHT);

    // Modal Card overlay for Start / Game Over
    flappy_card_ = lv_obj_create(flappy_box_);
    lv_obj_set_size(flappy_card_, 190, 116);
    lv_obj_align(flappy_card_, LV_ALIGN_CENTER, 0, 0);
    StyleButton(flappy_card_, C_PANEL_2, C_BORDER, 12);
    lv_obj_set_style_pad_all(flappy_card_, 8, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_card_, LV_OBJ_FLAG_SCROLLABLE);

    flappy_card_title_ = MakeCenteredLabel(flappy_card_, "FLAPPY BIRD", 0, 8, 174, 22, FONT_16, C_AMBER);
    flappy_card_sub_ = MakeCenteredLabel(flappy_card_, "Ketuk layar untuk lompat!", 0, 36, 174, 16, FONT_10, C_MUTED);

    lv_obj_t* btn_box = lv_btn_create(flappy_card_);
    lv_obj_set_pos(btn_box, 35, 66);
    lv_obj_set_size(btn_box, 120, 34);
    StyleButton(btn_box, C_CYAN, C_CYAN, 8);
    flappy_card_btn_ = lv_label_create(btn_box);
    lv_label_set_text(flappy_card_btn_, "MULAI MAIN");
    lv_obj_set_style_text_font(flappy_card_btn_, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(flappy_card_btn_, C_BG, LV_PART_MAIN);
    lv_obj_center(flappy_card_btn_);

    // Click handler on entire game box
    lv_obj_add_event_cb(flappy_box_, OnFlappyTouchStatic, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_box, OnFlappyTouchStatic, LV_EVENT_CLICKED, this);

    // Start 35ms physics timer
    flappy_timer_ = lv_timer_create(OnFlappyTickStatic, 35, this);

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// RADIO ONLINE STREAMING
// ============================================================================

struct RadioStation {
    const char* name;
    const char* genre;
    const char* video_id;
    const char* query;
    lv_color_t color;
};

static const RadioStation kRadioStations[] = {
    {"Prambors Radio", "102.2 FM - Top 40 Hits", "a9uVJphGruM", "Prambors Top 40 Hits Terbaru", lv_color_hex(0xF59E0B)},
    {"Gen FM Jakarta", "98.7 FM - Suara Musik Indo", "mmkuC9IgfkE", "Gen FM Hits Musik Indonesia", lv_color_hex(0xEC4899)},
    {"Lofi Chill Beats", "Lo-Fi Work / Study Music", "CLeZyIID9Bo", "Lofi Hip Hop Chill Beats", lv_color_hex(0x10B981)},
    {"Hard Rock FM", "87.6 FM - Rock & Lifestyle", "O1I8QOWQ2RU", "Hard Rock Hits Terbaik", lv_color_hex(0xEF4444)},
    {"Akustik Cafe", "Pop Akustik Santai Sore", "qIzOXW9-Zsw", "Akustik Cafe Indonesia Populer", lv_color_hex(0x8B5CF6)},
    {"Billboard Hot 100", "Top Global Billboard Hits", "AOYJ4Mx1jsQ", "Billboard Hot 100 Pop Hits", lv_color_hex(0x3B82F6)},
    {"I-Radio Jakarta", "89.6 FM - 100% Musik Indo", "PJ4jj_3S670", "I-Radio Musik Pop Indo Terbaik", lv_color_hex(0x06B6D4)}
};

static void OnRadioStationClick(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
    if (!self) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(target);
    self->OnRadioStationSelect(idx);
}

void NovaDisplay::OnRadioStationSelect(int idx) {
    if (radio_active_idx_ == idx) {
        // Stop playing
        if (callbacks_.stop_online) callbacks_.stop_online();
        radio_active_idx_ = -1;
    } else {
        // Hentikan stream / lagu sebelumnya dengan bersih
        if (callbacks_.stop_online) callbacks_.stop_online();
        radio_active_idx_ = idx;
        const auto& st = kRadioStations[idx];
        if (callbacks_.play_online) {
            std::string vid = (st.video_id != nullptr) ? st.video_id : "";
            std::string q = (st.query != nullptr) ? st.query : st.name;
            callbacks_.play_online(vid, q, "LIVE");
        }
    }
    if (callbacks_.trigger_click) callbacks_.trigger_click();
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
            StyleButton(card, is_active ? lv_color_hex(0x0C2B22) : C_PANEL, is_active ? C_MINT : C_BORDER, 8);

            MakeIcon(card, ICON_RADIO, 4, 7, is_active ? C_MINT : st.color, 22);

            lv_obj_t* name_lbl = MakeLabel(card, st.name, 28, 4, w - 74, 15, FONT_10, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* genre_lbl = MakeLabel(card, st.genre, 28, 19, w - 74, 13, FONT_10, is_active ? C_MINT : C_MUTED);
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
        lv_obj_set_style_text_color(s_lbl, (radio_active_idx_ >= 0) ? C_RED : C_MUTED, LV_PART_MAIN);
        lv_obj_center(s_lbl);
        lv_obj_add_event_cb(stop_btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) {
                if (self->callbacks_.stop_online) self->callbacks_.stop_online();
                self->radio_active_idx_ = -1;
                self->BuildCurrentPage();
            }
        }, LV_EVENT_CLICKED, this);
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
            StyleButton(card, is_active ? lv_color_hex(0x0C2B22) : C_PANEL, is_active ? C_MINT : C_BORDER, 10);

            // Station Icon
            MakeIcon(card, ICON_RADIO, 8, 10, is_active ? C_MINT : st.color, 24);

            // Station Name & Genre
            lv_obj_t* name_lbl = MakeLabel(card, st.name, 38, 5, 120, 18, FONT_12, C_TEXT);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_t* genre_lbl = MakeLabel(card, st.genre, 38, 22, 120, 15, FONT_10, is_active ? C_MINT : C_MUTED);
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

static lv_color_t Get2048Color(int val) {
    switch (val) {
        case 0:    return lv_color_hex(0x131B2A); // Empty
        case 2:    return lv_color_hex(0x1E2E42);
        case 4:    return lv_color_hex(0x273F5C);
        case 8:    return lv_color_hex(0xF59E0B); // Amber
        case 16:   return lv_color_hex(0xEA580C); // Orange
        case 32:   return lv_color_hex(0xDC2626); // Red
        case 64:   return lv_color_hex(0xE11D48); // Rose
        case 128:  return lv_color_hex(0x7C3AED); // Violet
        case 256:  return lv_color_hex(0x4F46E5); // Indigo
        case 512:  return lv_color_hex(0x0284C7); // Sky
        case 1024: return lv_color_hex(0x059669); // Emerald
        case 2048: return lv_color_hex(0xD97706); // Gold
        default:   return lv_color_hex(0xB45309);
    }
}

void NovaDisplay::Reset2048() {
    memset(board_2048_, 0, sizeof(board_2048_));
    score_2048_ = 0;
    game_over_2048_ = false;
    won_2048_ = false;

    Settings prefs("game_2048", true);
    best_2048_ = prefs.GetInt("best", 0);

    SpawnTile2048();
    SpawnTile2048();
    UpdateUI2048();
}

void NovaDisplay::SpawnTile2048() {
    struct EmptyCell { int r, c; };
    EmptyCell empties[16];
    int count = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (board_2048_[r][c] == 0) {
                empties[count++] = {r, c};
            }
        }
    }
    if (count == 0) return;
    int idx = rand() % count;
    board_2048_[empties[idx].r][empties[idx].c] = (rand() % 10 == 0) ? 4 : 2;
}

bool NovaDisplay::CanMove2048() {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (board_2048_[r][c] == 0) return true;
            if (c < 3 && board_2048_[r][c] == board_2048_[r][c + 1]) return true;
            if (r < 3 && board_2048_[r][c] == board_2048_[r + 1][c]) return true;
        }
    }
    return false;
}

bool NovaDisplay::Move2048(int dir) {
    if (game_over_2048_) return false;

    bool changed = false;

    auto slide_line = [this](int line[4]) -> bool {
        int temp[4] = {0};
        int t = 0;
        for (int i = 0; i < 4; ++i) {
            if (line[i] != 0) temp[t++] = line[i];
        }
        for (int i = 0; i < 3; ++i) {
            if (temp[i] != 0 && temp[i] == temp[i + 1]) {
                temp[i] *= 2;
                score_2048_ += temp[i];
                if (temp[i] == 2048 && !won_2048_) won_2048_ = true;
                temp[i + 1] = 0;
                i++;
            }
        }
        int res[4] = {0};
        t = 0;
        for (int i = 0; i < 4; ++i) {
            if (temp[i] != 0) res[t++] = temp[i];
        }
        bool line_changed = false;
        for (int i = 0; i < 4; ++i) {
            if (line[i] != res[i]) line_changed = true;
            line[i] = res[i];
        }
        return line_changed;
    };

    int prev_score = score_2048_;
    bool prev_won = won_2048_;

    if (dir == 0) { // UP
        for (int c = 0; c < 4; ++c) {
            int line[4] = {board_2048_[0][c], board_2048_[1][c], board_2048_[2][c], board_2048_[3][c]};
            if (slide_line(line)) changed = true;
            for (int r = 0; r < 4; ++r) board_2048_[r][c] = line[r];
        }
    } else if (dir == 1) { // DOWN
        for (int c = 0; c < 4; ++c) {
            int line[4] = {board_2048_[3][c], board_2048_[2][c], board_2048_[1][c], board_2048_[0][c]};
            if (slide_line(line)) changed = true;
            for (int r = 0; r < 4; ++r) board_2048_[3 - r][c] = line[r];
        }
    } else if (dir == 2) { // LEFT
        for (int r = 0; r < 4; ++r) {
            int line[4] = {board_2048_[r][0], board_2048_[r][1], board_2048_[r][2], board_2048_[r][3]};
            if (slide_line(line)) changed = true;
            for (int c = 0; c < 4; ++c) board_2048_[r][c] = line[c];
        }
    } else if (dir == 3) { // RIGHT
        for (int r = 0; r < 4; ++r) {
            int line[4] = {board_2048_[r][3], board_2048_[r][2], board_2048_[r][1], board_2048_[r][0]};
            if (slide_line(line)) changed = true;
            for (int c = 0; c < 4; ++c) board_2048_[r][3 - c] = line[c];
        }
    }

    if (changed) {
        if (score_2048_ > best_2048_) {
            best_2048_ = score_2048_;
            Settings prefs("game_2048", true);
            prefs.SetInt("best", best_2048_);
        }
        SpawnTile2048();
        if (!CanMove2048()) {
            game_over_2048_ = true;
        }

        if (game_over_2048_) {
            PlaySound(SOUND_LOSE_BALL);
        } else if (!prev_won && won_2048_) {
            PlaySound(SOUND_WIN);
        } else if (score_2048_ > prev_score) {
            PlaySound(SOUND_CAR_SCORE);
        } else {
            PlaySound(SOUND_PADDLE_HIT);
        }

        UpdateUI2048();
    }
    return changed;
}

void NovaDisplay::UpdateUI2048() {
    if (score_lbl_2048_) {
        char s_buf[32];
        snprintf(s_buf, sizeof(s_buf), "SKOR: %d", score_2048_);
        lv_label_set_text(score_lbl_2048_, s_buf);
    }
    if (best_lbl_2048_) {
        char b_buf[32];
        snprintf(b_buf, sizeof(b_buf), "REKOR: %d", best_2048_);
        lv_label_set_text(best_lbl_2048_, b_buf);
    }

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!tiles_2048_[r][c] || !tile_labels_2048_[r][c]) continue;
            int v = board_2048_[r][c];
            lv_color_t bg = Get2048Color(v);
            lv_obj_set_style_bg_color(tiles_2048_[r][c], bg, LV_PART_MAIN);

            if (v == 0) {
                lv_label_set_text(tile_labels_2048_[r][c], "");
            } else {
                char num_buf[16];
                snprintf(num_buf, sizeof(num_buf), "%d", v);
                lv_label_set_text(tile_labels_2048_[r][c], num_buf);
                const lv_font_t* font = (v >= 1024) ? FONT_10 : ((v >= 128) ? FONT_12 : FONT_14);
                lv_obj_set_style_text_font(tile_labels_2048_[r][c], font, LV_PART_MAIN);
                lv_obj_set_style_text_color(tile_labels_2048_[r][c], C_TEXT, LV_PART_MAIN);
            }
        }
    }

    if (card_2048_) {
        if (game_over_2048_) {
            lv_obj_clear_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
            if (card_title_2048_) lv_label_set_text(card_title_2048_, "GAME OVER!");
            if (card_sub_2048_) {
                char sub_buf[48];
                snprintf(sub_buf, sizeof(sub_buf), "Skor Akhir: %d", score_2048_);
                lv_label_set_text(card_sub_2048_, sub_buf);
            }
            if (card_btn_2048_) lv_label_set_text(card_btn_2048_, "MAIN LAGI");
        } else if (won_2048_) {
            lv_obj_clear_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
            if (card_title_2048_) lv_label_set_text(card_title_2048_, "2048 TERCAPAI!");
            if (card_sub_2048_) lv_label_set_text(card_sub_2048_, "Luar biasa! Ingin lanjut?");
            if (card_btn_2048_) lv_label_set_text(card_btn_2048_, "LANJUT MAIN");
        } else {
            lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NovaDisplay::Build2048() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "GAME 2048");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left: 170x170 Board at X=10, Y=36
        int board_w = 170;
        int tile_sz = 36;
        int gap = 4;
        int pad = 5;

        lv_obj_t* board_cont = lv_obj_create(root);
        lv_obj_set_pos(board_cont, 10, 36);
        lv_obj_set_size(board_cont, board_w, board_w);
        lv_obj_clear_flag(board_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(board_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(board_cont, lv_color_hex(0x0C1322), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(board_cont, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(board_cont, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(board_cont, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(board_cont, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(board_cont, 0, LV_PART_MAIN);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                tiles_2048_[r][c] = lv_obj_create(board_cont);
                lv_obj_set_pos(tiles_2048_[r][c], pad + c * (tile_sz + gap), pad + r * (tile_sz + gap));
                lv_obj_set_size(tiles_2048_[r][c], tile_sz, tile_sz);
                lv_obj_clear_flag(tiles_2048_[r][c], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                lv_obj_set_style_radius(tiles_2048_[r][c], 6, LV_PART_MAIN);
                lv_obj_set_style_border_width(tiles_2048_[r][c], 0, LV_PART_MAIN);

                tile_labels_2048_[r][c] = lv_label_create(tiles_2048_[r][c]);
                lv_obj_center(tile_labels_2048_[r][c]);
            }
        }

        // Gesture & Swipe event on board
        lv_obj_add_event_cb(board_cont, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
                lv_indev_get_point(lv_indev_active(), &self->touch_start_2048_);
            } else if (code == LV_EVENT_RELEASED) {
                lv_point_t pt;
                lv_indev_get_point(lv_indev_active(), &pt);
                int dx = pt.x - self->touch_start_2048_.x;
                int dy = pt.y - self->touch_start_2048_.y;
                if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                    int dir = (std::abs(dx) > std::abs(dy)) ? ((dx > 0) ? 3 : 2) : ((dy > 0) ? 1 : 0);
                    self->Move2048(dir);
                }
            } else if (code == LV_EVENT_GESTURE) {
                lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                if (gdir == LV_DIR_TOP) self->Move2048(0);
                else if (gdir == LV_DIR_BOTTOM) self->Move2048(1);
                else if (gdir == LV_DIR_LEFT) self->Move2048(2);
                else if (gdir == LV_DIR_RIGHT) self->Move2048(3);
            }
        }, LV_EVENT_ALL, this);

        // Right side: Status and Buttons (X=190, W=120)
        lv_obj_t* sc_panel = MakePanel(root, 190, 36, 120, 26, C_PANEL, C_BORDER, 6);
        score_lbl_2048_ = MakeCenteredLabel(sc_panel, "SKOR: 0", 0, 5, 120, 16, FONT_10, C_CYAN);

        lv_obj_t* best_panel = MakePanel(root, 190, 66, 120, 26, C_PANEL, C_BORDER, 6);
        best_lbl_2048_ = MakeCenteredLabel(best_panel, "REKOR: 0", 0, 5, 120, 16, FONT_10, C_AMBER);

        MakeButton(root, "RESET", 190, 96, 120, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Reset2048();
            }, this);

        // 4 D-Pad buttons for accessible navigation
        MakeButton(root, "^", 236, 128, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(0); }, this);
        MakeButton(root, "<", 202, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(2); }, this);
        MakeButton(root, "v", 236, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(1); }, this);
        MakeButton(root, ">", 270, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(3); }, this);

        // Game Over Overlay
        card_2048_ = MakePanel(root, 20, 60, 150, 100, C_PANEL, C_CYAN, 10);
        lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        card_title_2048_ = MakeCenteredLabel(card_2048_, "GAME OVER!", 0, 10, 150, 18, FONT_12, C_TEXT);
        card_sub_2048_ = MakeCenteredLabel(card_2048_, "Skor: 0", 0, 32, 150, 14, FONT_10, C_MUTED);
        lv_obj_t* btn = MakeButton(card_2048_, "MAIN LAGI", 25, 58, 100, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Reset2048();
            }, this);
        card_btn_2048_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status row (Y=36, H=28)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 70, 28, C_PANEL, C_BORDER, 8);
        score_lbl_2048_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 6, 70, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 85, 36, 85, 28, C_PANEL, C_BORDER, 8);
        best_lbl_2048_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 6, 85, 16, FONT_10, C_AMBER);

        MakeButton(root, "RESET", 175, 36, 55, 28, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Reset2048();
            }, this);

        // 200x200 Board at X=20, Y=68
        int board_w = 200;
        int tile_sz = 44;
        int gap = 4;
        int pad = 5;

        lv_obj_t* board_cont = lv_obj_create(root);
        lv_obj_set_pos(board_cont, 20, 68);
        lv_obj_set_size(board_cont, board_w, board_w);
        lv_obj_clear_flag(board_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(board_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(board_cont, lv_color_hex(0x0C1322), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(board_cont, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(board_cont, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(board_cont, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(board_cont, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(board_cont, 0, LV_PART_MAIN);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                tiles_2048_[r][c] = lv_obj_create(board_cont);
                lv_obj_set_pos(tiles_2048_[r][c], pad + c * (tile_sz + gap), pad + r * (tile_sz + gap));
                lv_obj_set_size(tiles_2048_[r][c], tile_sz, tile_sz);
                lv_obj_clear_flag(tiles_2048_[r][c], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                lv_obj_set_style_radius(tiles_2048_[r][c], 6, LV_PART_MAIN);
                lv_obj_set_style_border_width(tiles_2048_[r][c], 0, LV_PART_MAIN);

                tile_labels_2048_[r][c] = lv_label_create(tiles_2048_[r][c]);
                lv_obj_center(tile_labels_2048_[r][c]);
            }
        }

        // Gesture & Swipe event on board
        lv_obj_add_event_cb(board_cont, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
                lv_indev_get_point(lv_indev_active(), &self->touch_start_2048_);
            } else if (code == LV_EVENT_RELEASED) {
                lv_point_t pt;
                lv_indev_get_point(lv_indev_active(), &pt);
                int dx = pt.x - self->touch_start_2048_.x;
                int dy = pt.y - self->touch_start_2048_.y;
                if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                    int dir = (std::abs(dx) > std::abs(dy)) ? ((dx > 0) ? 3 : 2) : ((dy > 0) ? 1 : 0);
                    self->Move2048(dir);
                }
            } else if (code == LV_EVENT_GESTURE) {
                lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                if (gdir == LV_DIR_TOP) self->Move2048(0);
                else if (gdir == LV_DIR_BOTTOM) self->Move2048(1);
                else if (gdir == LV_DIR_LEFT) self->Move2048(2);
                else if (gdir == LV_DIR_RIGHT) self->Move2048(3);
            }
        }, LV_EVENT_ALL, this);

        MakeCenteredLabel(root, "Geser layar untuk menggabungkan angka", 0, 272, 240, 14, FONT_10, C_MUTED);

        // Game Over Overlay Card
        card_2048_ = MakePanel(root, 30, 110, 180, 110, C_PANEL, C_CYAN, 12);
        lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        card_title_2048_ = MakeCenteredLabel(card_2048_, "GAME OVER!", 0, 12, 180, 20, FONT_14, C_TEXT);
        card_sub_2048_ = MakeCenteredLabel(card_2048_, "Skor Akhir: 0", 0, 36, 180, 16, FONT_10, C_MUTED);
        lv_obj_t* btn = MakeButton(card_2048_, "MAIN LAGI", 35, 66, 110, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) self->Reset2048();
            }, this);
        card_btn_2048_ = lv_obj_get_child(btn, 0);
    }

    Reset2048();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- RETRO SNAKE IMPLEMENTATION ---
// ============================================================================

static void OnSnakeTimer(lv_timer_t* tmr) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(tmr));
    if (self) self->SnakeTick();
}

void NovaDisplay::ResetSnake() {
    snake_state_ = SNAKE_IDLE;
    snake_score_ = 0;
    snake_len_ = 3;
    snake_dir_ = SNAKE_RIGHT;
    snake_next_dir_ = SNAKE_RIGHT;

    Settings prefs("game_snake", true);
    snake_best_ = prefs.GetInt("best", 0);

    snake_body_[0] = {6, 7};
    snake_body_[1] = {5, 7};
    snake_body_[2] = {4, 7};

    SpawnSnakeFood();
    UpdateSnakeUI();
}

void NovaDisplay::StartSnake() {
    if (snake_state_ == SNAKE_RUNNING) return;
    snake_state_ = SNAKE_RUNNING;
    if (snake_card_) lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);

    if (snake_timer_) {
        lv_timer_del(snake_timer_);
    }
    snake_timer_ = lv_timer_create(OnSnakeTimer, 160, this);
}

void NovaDisplay::SpawnSnakeFood() {
    bool on_body = true;
    int tries = 0;
    while (on_body && tries < 100) {
        snake_food_.x = rand() % SNAKE_GRID_W;
        snake_food_.y = rand() % SNAKE_GRID_H;
        on_body = false;
        for (int i = 0; i < snake_len_; ++i) {
            if (snake_body_[i].x == snake_food_.x && snake_body_[i].y == snake_food_.y) {
                on_body = true;
                break;
            }
        }
        tries++;
    }
}

void NovaDisplay::ChangeSnakeDir(SnakeDir new_dir) {
    if ((new_dir == SNAKE_UP && snake_dir_ == SNAKE_DOWN) ||
        (new_dir == SNAKE_DOWN && snake_dir_ == SNAKE_UP) ||
        (new_dir == SNAKE_LEFT && snake_dir_ == SNAKE_RIGHT) ||
        (new_dir == SNAKE_RIGHT && snake_dir_ == SNAKE_LEFT)) {
        return;
    }
    snake_next_dir_ = new_dir;
    PlaySound(SOUND_CAR_STEER);
    if (snake_state_ == SNAKE_IDLE) {
        StartSnake();
    }
}

void NovaDisplay::SnakeTick() {
    if (snake_state_ != SNAKE_RUNNING) return;

    snake_dir_ = snake_next_dir_;
    SnakePoint head = snake_body_[0];

    if (snake_dir_ == SNAKE_UP) head.y--;
    else if (snake_dir_ == SNAKE_DOWN) head.y++;
    else if (snake_dir_ == SNAKE_LEFT) head.x--;
    else if (snake_dir_ == SNAKE_RIGHT) head.x++;

    // Wall collision
    if (head.x < 0 || head.x >= SNAKE_GRID_W || head.y < 0 || head.y >= SNAKE_GRID_H) {
        snake_state_ = SNAKE_OVER;
        if (snake_timer_) {
            lv_timer_del(snake_timer_);
            snake_timer_ = nullptr;
        }
        PlaySound(SOUND_CAR_CRASH);
        UpdateSnakeUI();
        return;
    }

    // Self bite collision
    for (int i = 0; i < snake_len_ - 1; ++i) {
        if (head.x == snake_body_[i].x && head.y == snake_body_[i].y) {
            snake_state_ = SNAKE_OVER;
            if (snake_timer_) {
                lv_timer_del(snake_timer_);
                snake_timer_ = nullptr;
            }
            PlaySound(SOUND_CAR_CRASH);
            UpdateSnakeUI();
            return;
        }
    }

    // Food collision
    bool ate = (head.x == snake_food_.x && head.y == snake_food_.y);
    if (ate) {
        snake_score_ += 10;
        PlaySound(SOUND_BRICK_HIT);
        if (snake_score_ > snake_best_) {
            snake_best_ = snake_score_;
            Settings prefs("game_snake", true);
            prefs.SetInt("best", snake_best_);
        }
        if (snake_len_ < SNAKE_MAX_SEGS) {
            snake_len_++;
        }
        SpawnSnakeFood();
    }

    for (int i = snake_len_ - 1; i > 0; --i) {
        snake_body_[i] = snake_body_[i - 1];
    }
    snake_body_[0] = head;

    UpdateSnakeUI();
}

void NovaDisplay::UpdateSnakeUI() {
    if (snake_score_lbl_) {
        char s_buf[32];
        snprintf(s_buf, sizeof(s_buf), "SKOR: %d", snake_score_);
        lv_label_set_text(snake_score_lbl_, s_buf);
    }
    if (snake_best_lbl_) {
        char b_buf[32];
        snprintf(b_buf, sizeof(b_buf), "REKOR: %d", snake_best_);
        lv_label_set_text(snake_best_lbl_, b_buf);
    }

    if (snake_food_obj_) {
        lv_obj_set_pos(snake_food_obj_, snake_food_.x * 10, snake_food_.y * 10);
    }

    for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
        if (!snake_segs_[i]) continue;
        if (i < snake_len_) {
            lv_obj_clear_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(snake_segs_[i], snake_body_[i].x * 10, snake_body_[i].y * 10);
            lv_color_t col = (i == 0) ? C_CYAN : C_MINT;
            lv_obj_set_style_bg_color(snake_segs_[i], col, LV_PART_MAIN);
        } else {
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (snake_card_) {
        if (snake_state_ == SNAKE_OVER) {
            lv_obj_clear_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
            if (snake_card_title_) lv_label_set_text(snake_card_title_, "GAME OVER!");
            if (snake_card_sub_) {
                char sub_buf[48];
                snprintf(sub_buf, sizeof(sub_buf), "Skor Akhir: %d", snake_score_);
                lv_label_set_text(snake_card_sub_, sub_buf);
            }
            if (snake_card_btn_) lv_label_set_text(snake_card_btn_, "MAIN LAGI");
        } else {
            lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NovaDisplay::BuildSnake() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "RETRO SNAKE");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left: 190x160 Arena at X=10, Y=36
        snake_arena_ = lv_obj_create(root);
        lv_obj_set_pos(snake_arena_, 10, 36);
        lv_obj_set_size(snake_arena_, 190, 160);
        lv_obj_clear_flag(snake_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(snake_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(snake_arena_, lv_color_hex(0x070B14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(snake_arena_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(snake_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(snake_arena_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(snake_arena_, 0, LV_PART_MAIN);

        // Food Object
        snake_food_obj_ = lv_obj_create(snake_arena_);
        lv_obj_set_size(snake_food_obj_, 10, 10);
        lv_obj_clear_flag(snake_food_obj_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_bg_color(snake_food_obj_, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_set_style_radius(snake_food_obj_, 5, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_food_obj_, 0, LV_PART_MAIN);

        // Pre-create body segments
        for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
            snake_segs_[i] = lv_obj_create(snake_arena_);
            lv_obj_set_size(snake_segs_[i], 10, 10);
            lv_obj_clear_flag(snake_segs_[i], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_radius(snake_segs_[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(snake_segs_[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(snake_segs_[i], (i == 0) ? C_CYAN : C_MINT, LV_PART_MAIN);
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Swipe & Gesture in Arena
        lv_obj_add_event_cb(snake_arena_, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
                lv_indev_get_point(lv_indev_active(), &self->touch_start_snake_);
            } else if (code == LV_EVENT_RELEASED) {
                lv_point_t pt;
                lv_indev_get_point(lv_indev_active(), &pt);
                int dx = pt.x - self->touch_start_snake_.x;
                int dy = pt.y - self->touch_start_snake_.y;
                if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                    if (std::abs(dx) > std::abs(dy)) {
                        self->ChangeSnakeDir((dx > 0) ? SNAKE_RIGHT : SNAKE_LEFT);
                    } else {
                        self->ChangeSnakeDir((dy > 0) ? SNAKE_DOWN : SNAKE_UP);
                    }
                }
            } else if (code == LV_EVENT_GESTURE) {
                lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                if (gdir == LV_DIR_TOP) self->ChangeSnakeDir(SNAKE_UP);
                else if (gdir == LV_DIR_BOTTOM) self->ChangeSnakeDir(SNAKE_DOWN);
                else if (gdir == LV_DIR_LEFT) self->ChangeSnakeDir(SNAKE_LEFT);
                else if (gdir == LV_DIR_RIGHT) self->ChangeSnakeDir(SNAKE_RIGHT);
            }
        }, LV_EVENT_ALL, this);

        // Right side: Status and D-Pad
        lv_obj_t* p1 = MakePanel(root, 210, 36, 100, 26, C_PANEL, C_BORDER, 6);
        snake_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 100, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 210, 66, 100, 26, C_PANEL, C_BORDER, 6);
        snake_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 100, 16, FONT_10, C_AMBER);

        MakeButton(root, "RESET", 210, 96, 100, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetSnake(); self->StartSnake(); }
            }, this);

        // D-Pad buttons
        MakeButton(root, "^", 244, 126, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_UP); }, this);
        MakeButton(root, "<", 208, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_LEFT); }, this);
        MakeButton(root, "v", 244, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_DOWN); }, this);
        MakeButton(root, ">", 280, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_RIGHT); }, this);

        // Game Over Card Overlay
        snake_card_ = MakePanel(root, 25, 66, 160, 96, C_PANEL, C_RED, 10);
        lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        snake_card_title_ = MakeCenteredLabel(snake_card_, "GAME OVER!", 0, 10, 160, 18, FONT_12, C_RED);
        snake_card_sub_ = MakeCenteredLabel(snake_card_, "Skor: 0", 0, 32, 160, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(snake_card_, "MAIN LAGI", 30, 56, 100, 26, C_RED, C_RED, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetSnake(); self->StartSnake(); }
            }, this);
        snake_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status row (Y=36, H=26)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 70, 26, C_PANEL, C_BORDER, 8);
        snake_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 70, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 85, 36, 85, 26, C_PANEL, C_BORDER, 8);
        snake_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 85, 15, FONT_10, C_AMBER);

        MakeButton(root, "RESET", 175, 36, 55, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetSnake(); self->StartSnake(); }
            }, this);

        // Arena: 210x150 at X=15, Y=64 (Grid: 21x15)
        snake_arena_ = lv_obj_create(root);
        lv_obj_set_pos(snake_arena_, 15, 64);
        lv_obj_set_size(snake_arena_, 210, 150);
        lv_obj_clear_flag(snake_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(snake_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(snake_arena_, lv_color_hex(0x070B14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(snake_arena_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(snake_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(snake_arena_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(snake_arena_, 0, LV_PART_MAIN);

        // Food Object
        snake_food_obj_ = lv_obj_create(snake_arena_);
        lv_obj_set_size(snake_food_obj_, 10, 10);
        lv_obj_clear_flag(snake_food_obj_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_bg_color(snake_food_obj_, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_set_style_radius(snake_food_obj_, 5, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_food_obj_, 0, LV_PART_MAIN);

        // Pre-create body segments
        for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
            snake_segs_[i] = lv_obj_create(snake_arena_);
            lv_obj_set_size(snake_segs_[i], 10, 10);
            lv_obj_clear_flag(snake_segs_[i], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_radius(snake_segs_[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(snake_segs_[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(snake_segs_[i], (i == 0) ? C_CYAN : C_MINT, LV_PART_MAIN);
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Swipe & Gesture in Arena
        lv_obj_add_event_cb(snake_arena_, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
                lv_indev_get_point(lv_indev_active(), &self->touch_start_snake_);
            } else if (code == LV_EVENT_RELEASED) {
                lv_point_t pt;
                lv_indev_get_point(lv_indev_active(), &pt);
                int dx = pt.x - self->touch_start_snake_.x;
                int dy = pt.y - self->touch_start_snake_.y;
                if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                    if (std::abs(dx) > std::abs(dy)) {
                        self->ChangeSnakeDir((dx > 0) ? SNAKE_RIGHT : SNAKE_LEFT);
                    } else {
                        self->ChangeSnakeDir((dy > 0) ? SNAKE_DOWN : SNAKE_UP);
                    }
                }
            } else if (code == LV_EVENT_GESTURE) {
                lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                if (gdir == LV_DIR_TOP) self->ChangeSnakeDir(SNAKE_UP);
                else if (gdir == LV_DIR_BOTTOM) self->ChangeSnakeDir(SNAKE_DOWN);
                else if (gdir == LV_DIR_LEFT) self->ChangeSnakeDir(SNAKE_LEFT);
                else if (gdir == LV_DIR_RIGHT) self->ChangeSnakeDir(SNAKE_RIGHT);
            }
        }, LV_EVENT_ALL, this);

        // Ergonomic D-Pad at Y=218 to 286
        MakeButton(root, "^", 102, 218, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_UP); }, this);
        MakeButton(root, "<", 62, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_LEFT); }, this);
        MakeButton(root, "v", 102, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_DOWN); }, this);
        MakeButton(root, ">", 142, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_RIGHT); }, this);

        // Game Over Card Overlay
        snake_card_ = MakePanel(root, 25, 88, 190, 102, C_PANEL, C_RED, 12);
        lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        snake_card_title_ = MakeCenteredLabel(snake_card_, "GAME OVER!", 0, 10, 190, 20, FONT_12, C_RED);
        snake_card_sub_ = MakeCenteredLabel(snake_card_, "Skor Akhir: 0", 0, 34, 190, 16, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(snake_card_, "MAIN LAGI", 45, 62, 100, 28, C_RED, C_RED, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetSnake(); self->StartSnake(); }
            }, this);
        snake_card_btn_ = lv_obj_get_child(btn, 0);
    }

    ResetSnake();
    StartSnake();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME BRICK BREAKER (ARKANOID) IMPLEMENTATION ---
// ============================================================================

static void OnBrickTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->BrickTick();
}

void NovaDisplay::ResetBrick() {
    brick_state_ = BRICK_IDLE;
    brick_score_ = 0;
    brick_lives_ = 3;
    bricks_left_ = 30;

    Settings prefs("game_brick", true);
    brick_best_ = prefs.GetInt("best", 0);

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            bricks_[r][c] = true;
            if (brick_objs_[r][c]) {
                lv_obj_clear_flag(brick_objs_[r][c], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    int arena_w = (width_ > 240) ? 200 : 220;
    int arena_h = (width_ > 240) ? 168 : 216;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    paddle_x_ = (arena_w - paddle_w) / 2.0f;
    ball_x_ = paddle_x_ + (paddle_w / 2.0f) - 4.0f;
    ball_y_ = arena_h - 26.0f;
    ball_vx_ = 2.4f;
    ball_vy_ = -3.0f;

    if (brick_card_) lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
    UpdateBrickUI();
}

void NovaDisplay::StartBrick() {
    brick_state_ = BRICK_RUNNING;
    if (brick_card_) lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
    if (!brick_timer_) {
        brick_timer_ = lv_timer_create(OnBrickTickStatic, 25, this);
    }
}

void NovaDisplay::SetPaddleX(float x) {
    int arena_w = (width_ > 240) ? 200 : 220;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    paddle_x_ = std::clamp(x, 0.0f, (float)(arena_w - paddle_w));
    if (brick_paddle_obj_) {
        lv_obj_set_x(brick_paddle_obj_, (int)paddle_x_);
    }
}

void NovaDisplay::BrickTick() {
    if (brick_state_ != BRICK_RUNNING) return;

    int arena_w = (width_ > 240) ? 200 : 220;
    int arena_h = (width_ > 240) ? 168 : 216;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    float paddle_h = 8.0f;
    float paddle_y = (width_ > 240) ? (168.0f - 14.0f) : (216.0f - 16.0f);

    ball_x_ += ball_vx_;
    ball_y_ += ball_vy_;

    // Wall bounce
    if (ball_x_ <= 0.0f) {
        ball_x_ = 0.0f;
        ball_vx_ = fabsf(ball_vx_);
        PlaySound(SOUND_WALL_HIT);
    } else if (ball_x_ >= arena_w - 8.0f) {
        ball_x_ = arena_w - 8.0f;
        ball_vx_ = -fabsf(ball_vx_);
        PlaySound(SOUND_WALL_HIT);
    }
    if (ball_y_ <= 0.0f) {
        ball_y_ = 0.0f;
        ball_vy_ = fabsf(ball_vy_);
        PlaySound(SOUND_WALL_HIT);
    }

    // Paddle collision
    if (ball_y_ + 8.0f >= paddle_y && ball_y_ <= paddle_y + paddle_h) {
        if (ball_x_ + 8.0f >= paddle_x_ && ball_x_ <= paddle_x_ + paddle_w) {
            if (ball_vy_ > 0.0f) {
                ball_vy_ = -fabsf(ball_vy_);
                float hit_offset = (ball_x_ + 4.0f) - (paddle_x_ + paddle_w / 2.0f);
                ball_vx_ = hit_offset * 0.18f;
                if (fabsf(ball_vx_) < 0.6f) ball_vx_ = (ball_vx_ >= 0) ? 1.5f : -1.5f;
                PlaySound(SOUND_PADDLE_HIT);
            }
        }
    }

    // Bricks collision
    int bw = (width_ > 240) ? 28 : 32;
    int bh = (width_ > 240) ? 9 : 10;
    int gx = 4, gy = 4;
    int start_y = (width_ > 240) ? 6 : 8;
    int total_grid_w = 6 * bw + 5 * gx;
    int start_x = (arena_w - total_grid_w) / 2;

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (!bricks_[r][c]) continue;
            int bx = start_x + c * (bw + gx);
            int by = start_y + r * (bh + gy);

            if (ball_x_ + 8.0f >= bx && ball_x_ <= bx + bw &&
                ball_y_ + 8.0f >= by && ball_y_ <= by + bh) {
                bricks_[r][c] = false;
                bricks_left_--;
                brick_score_ += 10;
                ball_vy_ = -ball_vy_;
                PlaySound(SOUND_BRICK_HIT);

                if (brick_objs_[r][c]) {
                    lv_obj_add_flag(brick_objs_[r][c], LV_OBJ_FLAG_HIDDEN);
                }

                if (brick_score_ > brick_best_) {
                    brick_best_ = brick_score_;
                    Settings prefs("game_brick", true);
                    prefs.SetInt("best", brick_best_);
                }

                if (bricks_left_ <= 0) {
                    brick_state_ = BRICK_WON;
                    if (brick_timer_) {
                        lv_timer_del(brick_timer_);
                        brick_timer_ = nullptr;
                    }
                    PlaySound(SOUND_WIN);
                    UpdateBrickUI();
                    return;
                }
                goto after_bricks;
            }
        }
    }
after_bricks:

    // Ball miss bottom
    if (ball_y_ > arena_h) {
        brick_lives_--;
        PlaySound(SOUND_LOSE_BALL);
        if (brick_lives_ > 0) {
            ball_x_ = paddle_x_ + paddle_w / 2.0f - 4.0f;
            ball_y_ = paddle_y - 12.0f;
            ball_vx_ = (esp_random() % 2 == 0) ? 2.4f : -2.4f;
            ball_vy_ = -3.0f;
        } else {
            brick_state_ = BRICK_OVER;
            if (brick_timer_) {
                lv_timer_del(brick_timer_);
                brick_timer_ = nullptr;
            }
            UpdateBrickUI();
            return;
        }
    }

    UpdateBrickUI();
}

void NovaDisplay::UpdateBrickUI() {
    if (brick_score_lbl_) {
        char s[32]; snprintf(s, sizeof(s), "SKOR: %d", brick_score_);
        lv_label_set_text(brick_score_lbl_, s);
    }
    if (brick_best_lbl_) {
        char s[32]; snprintf(s, sizeof(s), "REKOR: %d", brick_best_);
        lv_label_set_text(brick_best_lbl_, s);
    }
    if (brick_lives_lbl_) {
        std::string hearts = "";
        for (int i = 0; i < brick_lives_; ++i) hearts += "♥";
        if (hearts.empty()) hearts = "---";
        lv_label_set_text(brick_lives_lbl_, hearts.c_str());
    }
    if (brick_ball_obj_) {
        lv_obj_set_pos(brick_ball_obj_, (int)ball_x_, (int)ball_y_);
    }
    if (brick_paddle_obj_) {
        lv_obj_set_x(brick_paddle_obj_, (int)paddle_x_);
    }

    if (brick_card_) {
        if (brick_state_ == BRICK_OVER) {
            lv_obj_clear_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(brick_card_title_, "GAME OVER!");
            lv_obj_set_style_text_color(brick_card_title_, C_RED, LV_PART_MAIN);
            char sub[48]; snprintf(sub, sizeof(sub), "Skor Akhir: %d", brick_score_);
            lv_label_set_text(brick_card_sub_, sub);
            if (brick_card_btn_) lv_label_set_text(brick_card_btn_, "MAIN LAGI");
        } else if (brick_state_ == BRICK_WON) {
            lv_obj_clear_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(brick_card_title_, "MENANG!");
            lv_obj_set_style_text_color(brick_card_title_, C_MINT, LV_PART_MAIN);
            char sub[48]; snprintf(sub, sizeof(sub), "Hebat! Semua Bata Hancur!");
            lv_label_set_text(brick_card_sub_, sub);
            if (brick_card_btn_) lv_label_set_text(brick_card_btn_, "MAIN LAGI");
        }
    }
}

void NovaDisplay::BuildBrick() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "BRICK BREAKER");

    bool is_land = (width_ > 240);
    int arena_w = is_land ? 200 : 220;
    int arena_h = is_land ? 168 : 216;
    float paddle_w = is_land ? 40.0f : 44.0f;
    float paddle_h = 8.0f;
    float paddle_y = is_land ? (168.0f - 14.0f) : (216.0f - 16.0f);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Arena on left
        brick_arena_ = lv_obj_create(root);
        lv_obj_set_pos(brick_arena_, 8, 36);
        lv_obj_set_size(brick_arena_, arena_w, arena_h);
        lv_obj_clear_flag(brick_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(brick_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(brick_arena_, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(brick_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(brick_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(brick_arena_, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(brick_arena_, 0, LV_PART_MAIN);

        // Sidebar on right (w=98)
        lv_obj_t* p1 = MakePanel(root, 214, 36, 98, 26, C_PANEL, C_BORDER, 6);
        brick_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 98, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 214, 66, 98, 26, C_PANEL, C_BORDER, 6);
        brick_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 98, 16, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 214, 96, 98, 26, C_PANEL, C_BORDER, 6);
        brick_lives_lbl_ = MakeCenteredLabel(p3, "♥♥♥", 0, 5, 98, 16, FONT_10, C_RED);

        MakeButton(root, "RESET", 214, 126, 98, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetBrick(); self->StartBrick(); }
            }, this);

        // Overlay Card
        brick_card_ = MakePanel(brick_arena_, (arena_w - 160) / 2, (arena_h - 96) / 2, 160, 96, C_PANEL, C_BORDER, 10);
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
        brick_card_title_ = MakeCenteredLabel(brick_card_, "BRICK BREAKER", 0, 10, 160, 18, FONT_12, C_CYAN);
        brick_card_sub_ = MakeCenteredLabel(brick_card_, "Sentuh untuk mulai", 0, 32, 160, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(brick_card_, "MULAI", 30, 56, 100, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetBrick(); self->StartBrick(); }
            }, this);
        brick_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* p1 = MakePanel(root, 10, 36, 68, 26, C_PANEL, C_BORDER, 8);
        brick_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 68, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 82, 36, 68, 26, C_PANEL, C_BORDER, 8);
        brick_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 68, 15, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 154, 36, 42, 26, C_PANEL, C_BORDER, 8);
        brick_lives_lbl_ = MakeCenteredLabel(p3, "♥♥♥", 0, 5, 42, 15, FONT_10, C_RED);

        MakeButton(root, "RST", 200, 36, 30, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetBrick(); self->StartBrick(); }
            }, this);

        brick_arena_ = lv_obj_create(root);
        lv_obj_set_pos(brick_arena_, 10, 66);
        lv_obj_set_size(brick_arena_, arena_w, arena_h);
        lv_obj_clear_flag(brick_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(brick_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(brick_arena_, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(brick_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(brick_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(brick_arena_, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(brick_arena_, 0, LV_PART_MAIN);

        // Overlay Card
        brick_card_ = MakePanel(brick_arena_, (arena_w - 180) / 2, (arena_h - 100) / 2, 180, 100, C_PANEL, C_BORDER, 12);
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
        brick_card_title_ = MakeCenteredLabel(brick_card_, "BRICK BREAKER", 0, 10, 180, 20, FONT_12, C_CYAN);
        brick_card_sub_ = MakeCenteredLabel(brick_card_, "Geser layar untuk paddle", 0, 34, 180, 16, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(brick_card_, "MULAI", 40, 60, 100, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) { self->ResetBrick(); self->StartBrick(); }
            }, this);
        brick_card_btn_ = lv_obj_get_child(btn, 0);
    }

    // Paddle
    brick_paddle_obj_ = lv_obj_create(brick_arena_);
    lv_obj_set_pos(brick_paddle_obj_, (int)paddle_x_, (int)paddle_y);
    lv_obj_set_size(brick_paddle_obj_, (int)paddle_w, (int)paddle_h);
    lv_obj_clear_flag(brick_paddle_obj_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(brick_paddle_obj_, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_radius(brick_paddle_obj_, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(brick_paddle_obj_, 0, LV_PART_MAIN);

    // Ball
    brick_ball_obj_ = lv_obj_create(brick_arena_);
    lv_obj_set_pos(brick_ball_obj_, (int)ball_x_, (int)ball_y_);
    lv_obj_set_size(brick_ball_obj_, 8, 8);
    lv_obj_clear_flag(brick_ball_obj_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(brick_ball_obj_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_radius(brick_ball_obj_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(brick_ball_obj_, 0, LV_PART_MAIN);

    // Bricks Grid
    int bw = is_land ? 28 : 32;
    int bh = is_land ? 9 : 10;
    int gx = 4, gy = 4;
    int start_y = is_land ? 6 : 8;
    int total_grid_w = 6 * bw + 5 * gx;
    int start_x = (arena_w - total_grid_w) / 2;

    static const lv_color_t kRowColors[5] = {
        lv_color_hex(0xEF4444), // Red
        lv_color_hex(0xF97316), // Orange
        lv_color_hex(0xF59E0B), // Amber
        lv_color_hex(0x10B981), // Green
        lv_color_hex(0x06B6D4)  // Cyan
    };

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            lv_obj_t* brk = lv_obj_create(brick_arena_);
            lv_obj_set_pos(brk, start_x + c * (bw + gx), start_y + r * (bh + gy));
            lv_obj_set_size(brk, bw, bh);
            lv_obj_clear_flag(brk, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_bg_color(brk, kRowColors[r], LV_PART_MAIN);
            lv_obj_set_style_radius(brk, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(brk, 0, LV_PART_MAIN);
            brick_objs_[r][c] = brk;
        }
    }

    // Touch event handler for paddle dragging
    lv_obj_add_event_cb(brick_arena_, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
            if (self->brick_state_ == BRICK_IDLE) {
                self->StartBrick();
            }
            lv_point_t pt;
            lv_indev_get_point(lv_indev_active(), &pt);
            lv_area_t a;
            lv_obj_get_coords(self->brick_arena_, &a);
            int local_x = pt.x - a.x1;
            float paddle_w = (self->width_ > 240) ? 40.0f : 44.0f;
            self->SetPaddleX((float)local_x - paddle_w / 2.0f);
        }
    }, LV_EVENT_ALL, this);

    ResetBrick();
    StartBrick();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME MINESWEEPER IMPLEMENTATION ---
// ============================================================================

static void OnMineTimerTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->MineTimerTick();
}

void NovaDisplay::MineTimerTick() {
    if (mine_state_ == MINE_PLAYING && !mine_first_click_) {
        mine_time_sec_++;
        UpdateMinesweeperUI();
    }
}

void NovaDisplay::ResetMinesweeper() {
    mine_state_ = MINE_PLAYING;
    mine_first_click_ = true;
    mine_flag_mode_ = false;
    mine_flags_left_ = MINE_TOTAL_MINES;
    mine_time_sec_ = 0;

    if (mine_timer_) {
        lv_timer_del(mine_timer_);
        mine_timer_ = nullptr;
    }

    memset(mine_grid_, 0, sizeof(mine_grid_));
    memset(mine_adjacent_, 0, sizeof(mine_adjacent_));
    memset(mine_revealed_, 0, sizeof(mine_revealed_));
    memset(mine_flagged_, 0, sizeof(mine_flagged_));

    UpdateMinesweeperUI();
}

void NovaDisplay::ToggleMineMode() {
    mine_flag_mode_ = !mine_flag_mode_;
    PlaySound(SOUND_MINE_CLICK);
    UpdateMinesweeperUI();
}

void NovaDisplay::OnMineTileClick(int r, int c) {
    if (r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS) return;
    if (mine_state_ != MINE_PLAYING) return;

    if (mine_flag_mode_) {
        ToggleMineFlag(r, c);
        return;
    }

    if (mine_flagged_[r][c]) {
        return;
    }

    if (mine_first_click_) {
        // Place mines avoiding clicked cell and its neighbors
        int placed = 0;
        while (placed < MINE_TOTAL_MINES) {
            int mr = esp_random() % MINE_ROWS;
            int mc = esp_random() % MINE_COLS;
            if (abs(mr - r) <= 1 && abs(mc - c) <= 1) continue;
            if (!mine_grid_[mr][mc]) {
                mine_grid_[mr][mc] = true;
                placed++;
            }
        }

        // Calculate adjacent numbers
        for (int i = 0; i < MINE_ROWS; ++i) {
            for (int j = 0; j < MINE_COLS; ++j) {
                if (mine_grid_[i][j]) continue;
                int count = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        int nr = i + dr, nc = j + dc;
                        if (nr >= 0 && nr < MINE_ROWS && nc >= 0 && nc < MINE_COLS) {
                            if (mine_grid_[nr][nc]) count++;
                        }
                    }
                }
                mine_adjacent_[i][j] = (uint8_t)count;
            }
        }

        mine_first_click_ = false;
        if (!mine_timer_) {
            mine_timer_ = lv_timer_create(OnMineTimerTickStatic, 1000, this);
        }
    }

    if (mine_grid_[r][c]) {
        // Mine Detonated!
        mine_state_ = MINE_LOST;
        mine_revealed_[r][c] = true;
        if (mine_timer_) {
            lv_timer_del(mine_timer_);
            mine_timer_ = nullptr;
        }
        PlaySound(SOUND_MINE_EXPLODE);
        UpdateMinesweeperUI();
        return;
    }

    RevealMine(r, c);
    PlaySound(SOUND_MINE_CLICK);
    CheckMineWin();
    UpdateMinesweeperUI();
}

void NovaDisplay::ToggleMineFlag(int r, int c) {
    if (mine_revealed_[r][c]) return;
    mine_flagged_[r][c] = !mine_flagged_[r][c];
    if (mine_flagged_[r][c]) {
        mine_flags_left_--;
    } else {
        mine_flags_left_++;
    }
    PlaySound(SOUND_MINE_FLAG);
    CheckMineWin();
    UpdateMinesweeperUI();
}

void NovaDisplay::RevealMine(int r, int c) {
    if (r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS) return;
    if (mine_revealed_[r][c] || mine_flagged_[r][c]) return;

    mine_revealed_[r][c] = true;

    // Flood fill empty neighbors
    if (mine_adjacent_[r][c] == 0) {
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr != 0 || dc != 0) {
                    RevealMine(r + dr, c + dc);
                }
            }
        }
    }
}

void NovaDisplay::CheckMineWin() {
    int unrevealed_safe = 0;
    for (int r = 0; r < MINE_ROWS; ++r) {
        for (int c = 0; c < MINE_COLS; ++c) {
            if (!mine_grid_[r][c] && !mine_revealed_[r][c]) {
                unrevealed_safe++;
            }
        }
    }

    if (unrevealed_safe == 0 && mine_state_ == MINE_PLAYING) {
        mine_state_ = MINE_WON;
        if (mine_timer_) {
            lv_timer_del(mine_timer_);
            mine_timer_ = nullptr;
        }
        PlaySound(SOUND_WIN);
    }
}

void NovaDisplay::UpdateMinesweeperUI() {
    if (mine_count_lbl_) {
        char buf[16]; snprintf(buf, sizeof(buf), "💣 %02d", mine_flags_left_);
        lv_label_set_text(mine_count_lbl_, buf);
    }
    if (mine_timer_lbl_) {
        char buf[16]; snprintf(buf, sizeof(buf), "⏱ %03d", std::min(mine_time_sec_, 999));
        lv_label_set_text(mine_timer_lbl_, buf);
    }
    if (mine_face_lbl_) {
        if (mine_state_ == MINE_WON) lv_label_set_text(mine_face_lbl_, "😎");
        else if (mine_state_ == MINE_LOST) lv_label_set_text(mine_face_lbl_, "😵");
        else lv_label_set_text(mine_face_lbl_, "🙂");
    }
    if (mine_mode_btn_) {
        lv_obj_t* lbl = lv_obj_get_child(mine_mode_btn_, 0);
        if (lbl) {
            lv_label_set_text(lbl, mine_flag_mode_ ? "🚩 TANDAI" : "⛏ GALI");
            lv_obj_set_style_text_color(lbl, mine_flag_mode_ ? C_RED : C_CYAN, LV_PART_MAIN);
        }
        lv_obj_set_style_border_color(mine_mode_btn_, mine_flag_mode_ ? C_RED : C_CYAN, LV_PART_MAIN);
    }

    static const lv_color_t kNumColors[] = {
        lv_color_hex(0x06B6D4), // 1: Cyan
        lv_color_hex(0x10B981), // 2: Green
        lv_color_hex(0xEF4444), // 3: Red
        lv_color_hex(0x8B5CF6), // 4: Purple
        lv_color_hex(0xF59E0B), // 5: Amber
        lv_color_hex(0x14B8A6), // 6: Teal
        lv_color_hex(0xEC4899), // 7: Pink
        lv_color_hex(0x94A3B8)  // 8: Grey
    };

    for (int r = 0; r < MINE_ROWS; ++r) {
        for (int c = 0; c < MINE_COLS; ++c) {
            lv_obj_t* btn = mine_btns_[r][c];
            lv_obj_t* lbl = mine_lbls_[r][c];
            if (!btn || !lbl) continue;

            if (mine_state_ == MINE_LOST && mine_grid_[r][c]) {
                // Show exploded / unflagged mine
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x5A121A), LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_RED, LV_PART_MAIN);
                lv_label_set_text(lbl, "💣");
                lv_obj_set_style_text_color(lbl, C_RED, LV_PART_MAIN);
            } else if (mine_revealed_[r][c]) {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D1525), LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x1E293B), LV_PART_MAIN);
                int count = mine_adjacent_[r][c];
                if (count > 0 && count <= 8) {
                    char num_str[4]; snprintf(num_str, sizeof(num_str), "%d", count);
                    lv_label_set_text(lbl, num_str);
                    lv_obj_set_style_text_color(lbl, kNumColors[count - 1], LV_PART_MAIN);
                } else {
                    lv_label_set_text(lbl, "");
                }
            } else if (mine_flagged_[r][c]) {
                lv_obj_set_style_bg_color(btn, C_PANEL_2, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_RED, LV_PART_MAIN);
                lv_label_set_text(lbl, "🚩");
                lv_obj_set_style_text_color(lbl, C_RED, LV_PART_MAIN);
            } else {
                // Covered cell
                lv_obj_set_style_bg_color(btn, C_PANEL, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_BORDER, LV_PART_MAIN);
                lv_label_set_text(lbl, "");
            }
        }
    }
}

void NovaDisplay::BuildMinesweeper() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MINESWEEPER");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Board: 8 cols x 21px = 168px wide at X=10, Y=36
        const int tile_sz = 20;
        const int gap = 1;
        lv_obj_t* grid_box = lv_obj_create(root);
        lv_obj_set_pos(grid_box, 10, 36);
        lv_obj_set_size(grid_box, 172, 172);
        lv_obj_clear_flag(grid_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(grid_box, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(grid_box, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(grid_box, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(grid_box, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(grid_box, 2, LV_PART_MAIN);

        for (int r = 0; r < MINE_ROWS; ++r) {
            for (int c = 0; c < MINE_COLS; ++c) {
                lv_obj_t* btn = lv_btn_create(grid_box);
                lv_obj_set_pos(btn, c * (tile_sz + gap), r * (tile_sz + gap));
                lv_obj_set_size(btn, tile_sz, tile_sz);
                StyleButton(btn, C_PANEL, C_BORDER, 3);
                lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

                lv_obj_t* lbl = lv_label_create(btn);
                lv_label_set_text(lbl, "");
                lv_obj_set_style_text_font(lbl, FONT_10, LV_PART_MAIN);
                lv_obj_center(lbl);

                mine_btns_[r][c] = btn;
                mine_lbls_[r][c] = lbl;

                lv_obj_set_user_data(btn, (void*)(intptr_t)(r * 8 + c));
                lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self) return;
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    self->OnMineTileClick(idx / 8, idx % 8);
                }, LV_EVENT_CLICKED, this);
            }
        }

        // Right sidebar (w=122, X=188, Y=36)
        lv_obj_t* p1 = MakePanel(root, 188, 36, 122, 28, C_PANEL, C_BORDER, 8);
        mine_count_lbl_ = MakeCenteredLabel(p1, "💣 10", 0, 6, 122, 16, FONT_10, C_RED);

        lv_obj_t* p2 = MakePanel(root, 188, 68, 122, 28, C_PANEL, C_BORDER, 8);
        mine_timer_lbl_ = MakeCenteredLabel(p2, "⏱ 000", 0, 6, 122, 16, FONT_10, C_AMBER);

        mine_mode_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_mode_btn_, 188, 100);
        lv_obj_set_size(mine_mode_btn_, 122, 30);
        StyleButton(mine_mode_btn_, C_PANEL, C_CYAN, 8);
        lv_obj_t* m_lbl = lv_label_create(mine_mode_btn_);
        lv_label_set_text(m_lbl, "⛏ GALI");
        lv_obj_set_style_text_font(m_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_lbl, C_CYAN, LV_PART_MAIN);
        lv_obj_center(m_lbl);
        lv_obj_add_event_cb(mine_mode_btn_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->ToggleMineMode();
        }, LV_EVENT_CLICKED, this);

        mine_face_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_face_btn_, 188, 134);
        lv_obj_set_size(mine_face_btn_, 122, 30);
        StyleButton(mine_face_btn_, C_PANEL_2, C_BORDER, 8);
        mine_face_lbl_ = MakeCenteredLabel(mine_face_btn_, "🙂 RESET", 0, 6, 122, 18, FONT_12, C_AMBER);
        lv_obj_add_event_cb(mine_face_btn_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->ResetMinesweeper();
        }, LV_EVENT_CLICKED, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status bar (Y=36, H=30)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 54, 30, C_PANEL, C_BORDER, 8);
        mine_count_lbl_ = MakeCenteredLabel(p1, "💣 10", 0, 7, 54, 16, FONT_10, C_RED);

        mine_face_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_face_btn_, 68, 36);
        lv_obj_set_size(mine_face_btn_, 34, 30);
        StyleButton(mine_face_btn_, C_PANEL_2, C_BORDER, 8);
        mine_face_lbl_ = MakeCenteredLabel(mine_face_btn_, "🙂", 0, 5, 34, 18, FONT_14, C_AMBER);
        lv_obj_add_event_cb(mine_face_btn_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->ResetMinesweeper();
        }, LV_EVENT_CLICKED, this);

        lv_obj_t* p2 = MakePanel(root, 106, 36, 54, 30, C_PANEL, C_BORDER, 8);
        mine_timer_lbl_ = MakeCenteredLabel(p2, "⏱ 000", 0, 7, 54, 16, FONT_10, C_AMBER);

        mine_mode_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_mode_btn_, 164, 36);
        lv_obj_set_size(mine_mode_btn_, 66, 30);
        StyleButton(mine_mode_btn_, C_PANEL, C_CYAN, 8);
        lv_obj_t* m_lbl = lv_label_create(mine_mode_btn_);
        lv_label_set_text(m_lbl, "⛏ GALI");
        lv_obj_set_style_text_font(m_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_lbl, C_CYAN, LV_PART_MAIN);
        lv_obj_center(m_lbl);
        lv_obj_add_event_cb(mine_mode_btn_, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->ToggleMineMode();
        }, LV_EVENT_CLICKED, this);

        // Grid Container: 8x8 at X=16, Y=72 (208x208)
        const int tile_sz = 24;
        const int gap = 2;
        lv_obj_t* grid_box = lv_obj_create(root);
        lv_obj_set_pos(grid_box, 16, 72);
        lv_obj_set_size(grid_box, 208, 208);
        lv_obj_clear_flag(grid_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(grid_box, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(grid_box, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(grid_box, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(grid_box, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(grid_box, 1, LV_PART_MAIN);

        for (int r = 0; r < MINE_ROWS; ++r) {
            for (int c = 0; c < MINE_COLS; ++c) {
                lv_obj_t* btn = lv_btn_create(grid_box);
                lv_obj_set_pos(btn, c * (tile_sz + gap), r * (tile_sz + gap));
                lv_obj_set_size(btn, tile_sz, tile_sz);
                StyleButton(btn, C_PANEL, C_BORDER, 4);
                lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

                lv_obj_t* lbl = lv_label_create(btn);
                lv_label_set_text(lbl, "");
                lv_obj_set_style_text_font(lbl, FONT_10, LV_PART_MAIN);
                lv_obj_center(lbl);

                mine_btns_[r][c] = btn;
                mine_lbls_[r][c] = lbl;

                lv_obj_set_user_data(btn, (void*)(intptr_t)(r * 8 + c));
                lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                    if (!self) return;
                    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                    self->OnMineTileClick(idx / 8, idx % 8);
                }, LV_EVENT_CLICKED, this);
            }
        }
    }

    ResetMinesweeper();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME RETRO HIGHWAY RACER IMPLEMENTATION ---
// ============================================================================

static void OnRacerTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->RacerTick();
}

void NovaDisplay::SpawnTrafficCar(int i, float min_y) {
    // Check which lanes have cars near the spawn area
    bool lane_occupied[3] = {false, false, false};
    for (int j = 0; j < MAX_TRAFFIC; ++j) {
        if (j != i && traffic_[j].active) {
            if (fabsf(traffic_[j].y - min_y) < 75.0f) {
                if (traffic_[j].lane >= 0 && traffic_[j].lane < 3) {
                    lane_occupied[traffic_[j].lane] = true;
                }
            }
        }
    }

    // Pick from strictly free lanes to guarantee a clear passage
    int free_lanes[3];
    int free_count = 0;
    for (int l = 0; l < 3; ++l) {
        if (!lane_occupied[l]) {
            free_lanes[free_count++] = l;
        }
    }

    int chosen_lane = 0;
    if (free_count > 0) {
        chosen_lane = free_lanes[esp_random() % free_count];
    } else {
        chosen_lane = esp_random() % 3;
    }

    static const lv_color_t t_colors[] = {
        lv_color_hex(0xEF4444), // Sporty Red
        lv_color_hex(0xF59E0B), // Taxi Amber
        lv_color_hex(0x10B981), // Emerald Green
        lv_color_hex(0xA855F7)  // Violet Coupe
    };

    traffic_[i].lane = chosen_lane;
    traffic_[i].y = min_y;
    // Base speed: smooth 2.0f to 2.6f
    traffic_[i].speed = 2.0f + (esp_random() % 7) * 0.1f;
    traffic_[i].color = t_colors[esp_random() % 4];
    traffic_[i].active = (i < 2 || racer_score_ >= 6);
}

void NovaDisplay::ResetRacer() {
    racer_state_ = RACER_IDLE;
    racer_player_lane_ = 1;
    racer_score_ = 0;
    racer_speed_ = 80;
    road_stripe_y_ = 0.0f;

    Settings prefs("game_racer", true);
    racer_best_ = prefs.GetInt("best", 0);

    // Initial stagger: spaced 105px apart well above screen so player has time to react
    SpawnTrafficCar(0, -60.0f);
    SpawnTrafficCar(1, -165.0f);
    SpawnTrafficCar(2, -270.0f);

    if (racer_card_) {
        lv_obj_clear_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(racer_card_title_, "HIGHWAY RACER");
        lv_label_set_text(racer_card_sub_, "Ketuk jalan / tombol utk gas!");
        if (racer_card_btn_) lv_label_set_text(racer_card_btn_, "MULAI BALAPAN");
    }
    UpdateRacerUI();
}

void NovaDisplay::StartRacer() {
    if (racer_state_ == RACER_RUNNING) return;
    racer_state_ = RACER_RUNNING;
    if (racer_card_) lv_obj_add_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
    if (!racer_timer_) {
        racer_timer_ = lv_timer_create(OnRacerTickStatic, 30, this);
    }
}

void NovaDisplay::ChangeRacerLane(int delta) {
    if (racer_state_ == RACER_IDLE || racer_state_ == RACER_CRASHED) {
        ResetRacer();
        StartRacer();
        return;
    }
    if (racer_state_ != RACER_RUNNING) return;

    int new_lane = racer_player_lane_ + delta;
    if (new_lane >= 0 && new_lane <= 2) {
        racer_player_lane_ = new_lane;
        PlaySound(SOUND_CAR_STEER);
        UpdateRacerUI();
    }
}

void NovaDisplay::RacerTick() {
    if (racer_state_ != RACER_RUNNING) return;

    int road_h = (width_ > 240) ? 168 : 174;
    int car_h = (width_ > 240) ? 38 : 44;
    float player_y = (width_ > 240) ? (168.0f - car_h - 10.0f) : (174.0f - car_h - 10.0f);

    // Scroll road stripes smoothly
    road_stripe_y_ += 4.5f;
    if (road_stripe_y_ >= 48.0f) {
        road_stripe_y_ -= 48.0f;
    }

    // Traffic cars movement
    float extra_speed = std::min(1.4f, (racer_score_ / 8) * 0.15f);
    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        if (!traffic_[i].active) {
            if (i == 2 && racer_score_ >= 6) {
                float top_y = 0.0f;
                for (int j = 0; j < 2; ++j) {
                    if (traffic_[j].y < top_y) top_y = traffic_[j].y;
                }
                SpawnTrafficCar(2, std::min(-60.0f, top_y - 90.0f));
            }
            continue;
        }

        traffic_[i].y += traffic_[i].speed + extra_speed;

        // Passed screen bottom -> Score!
        if (traffic_[i].y > road_h + 10.0f) {
            racer_score_++;
            PlaySound(SOUND_CAR_SCORE);

            if (racer_score_ > racer_best_) {
                racer_best_ = racer_score_;
                Settings prefs("game_racer", true);
                prefs.SetInt("best", racer_best_);
            }

            float top_y = 0.0f;
            for (int j = 0; j < MAX_TRAFFIC; ++j) {
                if (j != i && traffic_[j].active && traffic_[j].y < top_y) {
                    top_y = traffic_[j].y;
                }
            }
            float spawn_y = std::min(-60.0f, top_y - 85.0f - (esp_random() % 35));
            SpawnTrafficCar(i, spawn_y);
        }

        // Fair Collision check (with 8px bumper tolerance)
        if (traffic_[i].active && traffic_[i].lane == racer_player_lane_) {
            if (traffic_[i].y + car_h - 8.0f > player_y + 8.0f &&
                traffic_[i].y + 8.0f < player_y + car_h - 8.0f) {
                racer_state_ = RACER_CRASHED;
                if (racer_timer_) {
                    lv_timer_del(racer_timer_);
                    racer_timer_ = nullptr;
                }
                PlaySound(SOUND_CAR_CRASH);
                UpdateRacerUI();
                return;
            }
        }
    }

    UpdateRacerUI();
}

void NovaDisplay::UpdateRacerUI() {
    if (racer_score_lbl_) {
        char s[32]; snprintf(s, sizeof(s), "SKOR: %d", racer_score_);
        lv_label_set_text(racer_score_lbl_, s);
    }
    if (racer_best_lbl_) {
        char s[32]; snprintf(s, sizeof(s), "REKOR: %d", racer_best_);
        lv_label_set_text(racer_best_lbl_, s);
    }
    if (racer_speed_lbl_) {
        int current_speed = racer_speed_ + std::min(120, racer_score_ * 3);
        char s[32]; snprintf(s, sizeof(s), (width_ > 240) ? "SPD: %d km/h" : "%d km/h", current_speed);
        lv_label_set_text(racer_speed_lbl_, s);
    }

    int road_w = 180;
    int lane_w = road_w / 3; // 60
    int car_w = (width_ > 240) ? 28 : 32;
    int car_h = (width_ > 240) ? 38 : 44;
    float player_y = (width_ > 240) ? (168.0f - car_h - 10.0f) : (174.0f - car_h - 10.0f);

    if (racer_player_car_) {
        int px = racer_player_lane_ * lane_w + (lane_w - car_w) / 2;
        lv_obj_set_pos(racer_player_car_, px, (int)player_y);
    }

    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        if (racer_traffic_cars_[i]) {
            if (!traffic_[i].active) {
                lv_obj_add_flag(racer_traffic_cars_[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(racer_traffic_cars_[i], LV_OBJ_FLAG_HIDDEN);
            int tx = traffic_[i].lane * lane_w + (lane_w - car_w) / 2;
            lv_obj_set_pos(racer_traffic_cars_[i], tx, (int)traffic_[i].y);
            lv_obj_set_style_bg_color(racer_traffic_cars_[i], traffic_[i].color, LV_PART_MAIN);
        }
    }

    // Road stripes (both lane dividers scroll simultaneously)
    for (int i = 0; i < 4; ++i) {
        int sy = (int)(road_stripe_y_ + i * 48.0f - 24.0f);
        if (racer_stripes_[i]) lv_obj_set_y(racer_stripes_[i], sy);
        if (racer_stripes_[i + 4]) lv_obj_set_y(racer_stripes_[i + 4], sy);
    }

    if (racer_card_) {
        if (racer_state_ == RACER_CRASHED) {
            lv_obj_clear_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(racer_card_title_, "TABRAKAN!!");
            char sub[48]; snprintf(sub, sizeof(sub), "Skor Balapan: %d", racer_score_);
            lv_label_set_text(racer_card_sub_, sub);
            if (racer_card_btn_) lv_label_set_text(racer_card_btn_, "BALAP LAGI");
        }
    }
}

void NovaDisplay::BuildRacer() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "HIGHWAY RACER");

    bool is_land = (width_ > 240);
    int road_w = 180;
    int road_h = is_land ? 168 : 174;
    int lane_w = road_w / 3;
    int car_w = is_land ? 28 : 32;
    int car_h = is_land ? 38 : 44;

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        racer_road_ = lv_obj_create(root);
        lv_obj_set_pos(racer_road_, 12, 36);
        lv_obj_set_size(racer_road_, road_w, road_h);
        lv_obj_clear_flag(racer_road_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(racer_road_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(racer_road_, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_border_color(racer_road_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_road_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(racer_road_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(racer_road_, 0, LV_PART_MAIN);

        // Sidebar on right (w=110, X=202)
        lv_obj_t* p1 = MakePanel(root, 202, 36, 110, 26, C_PANEL, C_BORDER, 6);
        racer_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 110, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 202, 66, 110, 26, C_PANEL, C_BORDER, 6);
        racer_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 110, 16, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 202, 96, 110, 26, C_PANEL, C_BORDER, 6);
        racer_speed_lbl_ = MakeCenteredLabel(p3, "SPD: 80 km/h", 0, 5, 110, 16, FONT_10, C_MINT);

        MakeButton(root, "<", 202, 128, 52, 34, C_PANEL, C_BORDER, C_CYAN, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(-1); }, this);
        MakeButton(root, ">", 260, 128, 52, 34, C_PANEL, C_BORDER, C_CYAN, FONT_14,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(1); }, this);

        // Start / Crash Card
        racer_card_ = MakePanel(racer_road_, (road_w - 150) / 2, (road_h - 96) / 2, 150, 96, C_PANEL, C_CYAN, 10);
        racer_card_title_ = MakeCenteredLabel(racer_card_, "HIGHWAY RACER", 0, 10, 150, 18, FONT_12, C_CYAN);
        racer_card_sub_ = MakeCenteredLabel(racer_card_, "Ketuk jalan utk gas!", 0, 32, 150, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(racer_card_, "MULAI BALAPAN", 20, 56, 110, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    if (self->racer_state_ == RACER_CRASHED) self->ResetRacer();
                    self->StartRacer();
                }
            }, this);
        racer_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* p1 = MakePanel(root, 10, 36, 68, 26, C_PANEL, C_BORDER, 8);
        racer_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 68, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 82, 36, 68, 26, C_PANEL, C_BORDER, 8);
        racer_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 68, 15, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 154, 36, 76, 26, C_PANEL, C_BORDER, 8);
        racer_speed_lbl_ = MakeCenteredLabel(p3, "80 km/h", 0, 5, 76, 15, FONT_10, C_MINT);

        racer_road_ = lv_obj_create(root);
        lv_obj_set_pos(racer_road_, 30, 66);
        lv_obj_set_size(racer_road_, road_w, road_h);
        lv_obj_clear_flag(racer_road_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(racer_road_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(racer_road_, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_border_color(racer_road_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_road_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(racer_road_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(racer_road_, 0, LV_PART_MAIN);

        // Steering buttons at Y=246
        MakeButton(root, "< KIRI", 30, 246, 85, 36, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(-1); }, this);
        MakeButton(root, "KANAN >", 125, 246, 85, 36, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(1); }, this);

        // Start / Crash Card
        racer_card_ = MakePanel(racer_road_, (road_w - 160) / 2, (road_h - 100) / 2, 160, 100, C_PANEL, C_CYAN, 12);
        racer_card_title_ = MakeCenteredLabel(racer_card_, "HIGHWAY RACER", 0, 10, 160, 20, FONT_12, C_CYAN);
        racer_card_sub_ = MakeCenteredLabel(racer_card_, "Ketuk jalan / tombol utk gas!", 0, 34, 160, 16, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(racer_card_, "MULAI BALAPAN", 25, 60, 110, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    if (self->racer_state_ == RACER_CRASHED) self->ResetRacer();
                    self->StartRacer();
                }
            }, this);
        racer_card_btn_ = lv_obj_get_child(btn, 0);
    }

    // Road Stripes (4 dashed dividers on each lane boundary: total 8 stripes)
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* s1 = lv_obj_create(racer_road_);
        lv_obj_set_pos(s1, lane_w - 1, i * 48);
        lv_obj_set_size(s1, 2, 24);
        lv_obj_set_style_bg_color(s1, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_border_width(s1, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s1, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        racer_stripes_[i] = s1;

        lv_obj_t* s2 = lv_obj_create(racer_road_);
        lv_obj_set_pos(s2, 2 * lane_w - 1, i * 48);
        lv_obj_set_size(s2, 2, 24);
        lv_obj_set_style_bg_color(s2, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_border_width(s2, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s2, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        racer_stripes_[i + 4] = s2;
    }

    // Traffic Cars
    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        racer_traffic_cars_[i] = lv_obj_create(racer_road_);
        lv_obj_set_size(racer_traffic_cars_[i], car_w, car_h);
        lv_obj_clear_flag(racer_traffic_cars_[i], (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_radius(racer_traffic_cars_[i], 6, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_traffic_cars_[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(racer_traffic_cars_[i], traffic_[i].color, LV_PART_MAIN);

        // Windshield on traffic car
        lv_obj_t* w = lv_obj_create(racer_traffic_cars_[i]);
        lv_obj_set_pos(w, (car_w - 20) / 2, 18);
        lv_obj_set_size(w, 20, 10);
        lv_obj_set_style_bg_color(w, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_radius(w, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(w, 0, LV_PART_MAIN);
        lv_obj_clear_flag(w, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    }

    // Player Car (Cyan Sports Car)
    racer_player_car_ = lv_obj_create(racer_road_);
    lv_obj_set_size(racer_player_car_, car_w, car_h);
    lv_obj_clear_flag(racer_player_car_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(racer_player_car_, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_radius(racer_player_car_, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(racer_player_car_, 0, LV_PART_MAIN);

    // Player Windshield
    lv_obj_t* pw = lv_obj_create(racer_player_car_);
    lv_obj_set_pos(pw, (car_w - 20) / 2, 10);
    lv_obj_set_size(pw, 20, 12);
    lv_obj_set_style_bg_color(pw, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_radius(pw, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(pw, 0, LV_PART_MAIN);
    lv_obj_clear_flag(pw, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Player Spoiler
    lv_obj_t* ps = lv_obj_create(racer_player_car_);
    lv_obj_set_pos(ps, 2, car_h - 6);
    lv_obj_set_size(ps, car_w - 4, 4);
    lv_obj_set_style_bg_color(ps, lv_color_hex(0x0891B2), LV_PART_MAIN);
    lv_obj_set_style_radius(ps, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(ps, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ps, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Intuitive Direct Lane Touch: Tap left lane -> lane 0, mid -> lane 1, right -> lane 2
    lv_obj_add_event_cb(racer_road_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;

        if (self->racer_state_ == RACER_IDLE || self->racer_state_ == RACER_CRASHED) {
            if (self->racer_state_ == RACER_CRASHED) self->ResetRacer();
            self->StartRacer();
            return;
        }

        lv_point_t pt;
        lv_indev_get_point(lv_indev_active(), &pt);
        lv_area_t a;
        lv_obj_get_coords(self->racer_road_, &a);
        int road_width = a.x2 - a.x1 + 1;
        int lane_width = road_width / 3;
        int rel_x = pt.x - a.x1;

        int target_lane = rel_x / lane_width;
        if (target_lane < 0) target_lane = 0;
        if (target_lane > 2) target_lane = 2;

        if (target_lane != self->racer_player_lane_) {
            self->racer_player_lane_ = target_lane;
            self->PlaySound(SOUND_CAR_STEER);
            self->UpdateRacerUI();
        }
    }, LV_EVENT_CLICKED, this);

    ResetRacer();
    MakeBottomNav(root, PAGE_APPS);
}


// ============================================================================
// MINI PIANO / TOUCH SYNTHESIZER
// ============================================================================

void NovaDisplay::PlayPianoNote(int note_idx) {
    if (note_idx < 0 || note_idx >= 13) return;
    int fx_id = 100 + piano_instrument_ * 20 + note_idx;
    if (callbacks_.play_sound) {
        callbacks_.play_sound(fx_id);
    }
    static const char* kNoteNames[13] = {
        "C4 (261.6 Hz)", "D4 (293.7 Hz)", "E4 (329.6 Hz)", "F4 (349.2 Hz)",
        "G4 (392.0 Hz)", "A4 (440.0 Hz)", "B4 (493.9 Hz)", "C5 (523.3 Hz)",
        "C#4 (277.2 Hz)", "D#4 (311.1 Hz)", "F#4 (370.0 Hz)", "G#4 (415.3 Hz)", "A#4 (466.2 Hz)"
    };
    if (piano_status_lbl_) {
        lv_label_set_text_fmt(piano_status_lbl_, "Nada: %s", kNoteNames[note_idx]);
    }
}

void NovaDisplay::SetPianoInstrument(int mode) {
    piano_instrument_ = mode;
    static const char* kInstNames[3] = {"Grand Piano", "8-Bit Chiptune", "Organ Vibrato"};
    for (int i = 0; i < 3; ++i) {
        if (!piano_inst_btns_[i]) continue;
        if (i == mode) {
            lv_obj_set_style_bg_color(piano_inst_btns_[i], C_CYAN, LV_PART_MAIN);
            lv_obj_t* lbl = lv_obj_get_child(piano_inst_btns_[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, C_DARK, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(piano_inst_btns_[i], C_PANEL, LV_PART_MAIN);
            lv_obj_t* lbl = lv_obj_get_child(piano_inst_btns_[i], 0);
            if (lbl) lv_obj_set_style_text_color(lbl, C_MUTED, LV_PART_MAIN);
        }
    }
    if (piano_status_lbl_) {
        lv_label_set_text_fmt(piano_status_lbl_, "Instrumen: %s", kInstNames[mode % 3]);
    }
    if (callbacks_.trigger_click) callbacks_.trigger_click();
}

void NovaDisplay::BuildPiano() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MINI PIANO", true);

    bool is_land = (width_ > 240);
    int bar_y = is_land ? 34 : 36;
    int bar_h = is_land ? 22 : 24;
    int btn_w = is_land ? 68 : 70;
    int btn_h = bar_h;
    int btn_gap = is_land ? 6 : 6;
    int start_x = is_land ? 10 : 8;

    static const char* kInstTitles[3] = {"Piano", "8-Bit", "Organ"};
    for (int i = 0; i < 3; ++i) {
        piano_inst_btns_[i] = lv_btn_create(root);
        lv_obj_set_pos(piano_inst_btns_[i], start_x + i * (btn_w + btn_gap), bar_y);
        lv_obj_set_size(piano_inst_btns_[i], btn_w, btn_h);
        StyleButton(piano_inst_btns_[i], (i == piano_instrument_) ? C_CYAN : C_PANEL, C_BORDER, 6);
        lv_obj_t* lbl = MakeCenteredLabel(piano_inst_btns_[i], kInstTitles[i], 0, 0, btn_w, btn_h, FONT_10,
                                          (i == piano_instrument_) ? C_DARK : C_MUTED);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(piano_inst_btns_[i], [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            int mode = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            self->SetPianoInstrument(mode);
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(piano_inst_btns_[i], (void*)(intptr_t)i);
    }

    int lbl_y = is_land ? 34 : 64;
    int lbl_x = is_land ? 232 : 8;
    int lbl_w = is_land ? 80 : 224;
    piano_status_lbl_ = MakeCenteredLabel(root, "Sentuh tuts untuk main", lbl_x, lbl_y, lbl_w, 20, FONT_10, C_CYAN);

    // Keys Container
    int keys_x = is_land ? 4 : 4;
    int keys_y = is_land ? 60 : 88;
    int keys_w = is_land ? 312 : 232;
    int keys_h = is_land ? 144 : 194;

    lv_obj_t* keys_cont = lv_obj_create(root);
    lv_obj_set_pos(keys_cont, keys_x, keys_y);
    lv_obj_set_size(keys_cont, keys_w, keys_h);
    ClearDecoration(keys_cont);
    lv_obj_set_style_bg_color(keys_cont, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_radius(keys_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(keys_cont, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keys_cont, 1, LV_PART_MAIN);

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

        lv_obj_t* klbl = lv_label_create(piano_white_keys_[i]);
        lv_label_set_text(klbl, kWkLabels[i]);
        lv_obj_set_style_text_font(klbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(klbl, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_align(klbl, LV_ALIGN_BOTTOM_MID, 0, -4);

        lv_obj_set_user_data(piano_white_keys_[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(piano_white_keys_[i], [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            int n = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            self->PlayPianoNote(n);
        }, LV_EVENT_PRESSED, this);
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

        lv_obj_t* klbl = lv_label_create(piano_black_keys_[i]);
        lv_label_set_text(klbl, kBkLabels[i]);
        lv_obj_set_style_text_font(klbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(klbl, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_align(klbl, LV_ALIGN_BOTTOM_MID, 0, -3);

        lv_obj_set_user_data(piano_black_keys_[i], (void*)(intptr_t)note_idx);
        lv_obj_add_event_cb(piano_black_keys_[i], [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            int n = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            self->PlayPianoNote(n);
        }, LV_EVENT_PRESSED, this);
    }

    MakeBottomNav(root, PAGE_APPS);
}


// ============================================================================
// TOUCH SKETCHPAD (PAPAN GAMBAR)
// ============================================================================

void NovaDisplay::ClearSketchpad() {
    if (sketch_canvas_ && sketch_buf_) {
        lv_canvas_fill_bg(sketch_canvas_, lv_color_hex(0x0F172A), LV_OPA_COVER);
        if (callbacks_.trigger_click) callbacks_.trigger_click();
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
        if (lbl) lv_label_set_text_fmt(lbl, "%dpx", sz);
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
    if (callbacks_.trigger_click) callbacks_.trigger_click();
}

static void SketchCanvasDrawLine(lv_obj_t* canvas, int buf_w, int buf_h, int x0, int y0, int x1, int y1, lv_color_t color, int sz) {
    if (!canvas) return;
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
        if (x0 == x1 && y0 == y1) break;
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
    static const lv_color_t kPalette[6] = {
        lv_color_hex(0xFFFFFF), lv_color_hex(0xEF4444), lv_color_hex(0xFACC15),
        lv_color_hex(0x22C55E), lv_color_hex(0x06B6D4), lv_color_hex(0xA855F7)
    };
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
        lv_obj_add_event_cb(sw, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self) return;
            int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            static const lv_color_t kP[6] = {
                lv_color_hex(0xFFFFFF), lv_color_hex(0xEF4444), lv_color_hex(0xFACC15),
                lv_color_hex(0x22C55E), lv_color_hex(0x06B6D4), lv_color_hex(0xA855F7)
            };
            self->SetSketchColor(kP[idx % 6]);
            if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
        }, LV_EVENT_CLICKED, this);
    }

    int right_x = start_x + 6 * (swatch_sz + swatch_gap) + 4;

    // Size Toggle Button
    sketch_size_btn_ = lv_btn_create(root);
    lv_obj_set_pos(sketch_size_btn_, right_x, bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(sketch_size_btn_, 32, 22);
    StyleButton(sketch_size_btn_, C_PANEL, C_BORDER, 4);
    lv_obj_t* sz_lbl = MakeCenteredLabel(sketch_size_btn_, "2px", 0, 0, 32, 22, FONT_10, C_TEXT);
    lv_obj_center(sz_lbl);
    lv_obj_add_event_cb(sketch_size_btn_, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        int next_sz = (self->sketch_brush_size_ == 2) ? 4 : ((self->sketch_brush_size_ == 4) ? 6 : 2);
        self->SetSketchBrushSize(next_sz);
        if (self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Eraser Button
    lv_obj_t* eraser_btn = lv_btn_create(root);
    lv_obj_set_pos(eraser_btn, right_x + 36, bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(eraser_btn, is_land ? 32 : 36, 22);
    StyleButton(eraser_btn, C_PANEL, C_BORDER, 4);
    lv_obj_t* er_lbl = MakeCenteredLabel(eraser_btn, "Hps", 0, 0, is_land ? 32 : 36, 22, FONT_10, C_AMBER);
    lv_obj_center(er_lbl);
    lv_obj_add_event_cb(eraser_btn, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        self->ToggleSketchEraser();
    }, LV_EVENT_CLICKED, this);

    // Clear Canvas Button
    lv_obj_t* clear_btn = lv_btn_create(root);
    lv_obj_set_pos(clear_btn, right_x + (is_land ? 72 : 76), bar_y + (bar_h - 22) / 2);
    lv_obj_set_size(clear_btn, is_land ? 32 : 34, 22);
    StyleButton(clear_btn, C_PANEL, C_BORDER, 4);
    lv_obj_t* cl_lbl = MakeCenteredLabel(clear_btn, "Clr", 0, 0, is_land ? 32 : 34, 22, FONT_10, C_RED);
    lv_obj_center(cl_lbl);
    lv_obj_add_event_cb(clear_btn, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (!self) return;
        self->ClearSketchpad();
    }, LV_EVENT_CLICKED, this);

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
        lv_canvas_set_buffer(sketch_canvas_, sketch_buf_, sketch_buf_w_, sketch_buf_h_, LV_COLOR_FORMAT_RGB565);
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
        if (!self || !self->sketch_canvas_ || !self->sketch_buf_) return;
        lv_event_code_t code = lv_event_get_code(e);

        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;
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
            SketchCanvasDrawLine(self->sketch_canvas_, self->sketch_buf_w_, self->sketch_buf_h_, cx, cy, cx, cy, draw_col, sz);
        } else if (code == LV_EVENT_PRESSING) {
            if (self->sketch_prev_pt_.x >= 0) {
                SketchCanvasDrawLine(self->sketch_canvas_, self->sketch_buf_w_, self->sketch_buf_h_, self->sketch_prev_pt_.x, self->sketch_prev_pt_.y, cx, cy, draw_col, sz);
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
        lv_label_set_text_fmt(mon_psram_lbl_, "%.2f MB Sisa / 8 MB (%d%%)", free_psram_mb, psram_pct);
    }

    // 5. Wi-Fi & IP
    if (mon_wifi_lbl_) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            esp_netif_ip_info_t ip_info;
            esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            char s_ip[24] = "0.0.0.0";
            if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                esp_ip4addr_ntoa(&ip_info.ip, s_ip, sizeof(s_ip));
            }
            lv_label_set_text_fmt(mon_wifi_lbl_, "%s (%d dBm)  |  IP: %s", (char*)ap.ssid, ap.rssi, s_ip);
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
        lv_obj_add_event_cb(rf_btn, [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (self) self->MonitorTick();
        }, LV_EVENT_CLICKED, this);
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

static bool CheckAndExtractOtp(const std::string& app, const std::string& title, const std::string& message,
                               std::string& out_service, std::string& out_otp_code) {
    if (message.empty() && title.empty()) return false;

    std::string combined = app + " " + title + " " + message;
    std::string lower = combined;
    for (char& c : lower) {
        c = (char)tolower((unsigned char)c);
    }

    bool has_otp_kw = false;
    static const char* const kKeywords[] = {
        "otp", "token", "kode verifikasi", "verification code", "verifikasi",
        "kode masuk", "login code", "kode konfirmasi", "security code",
        "kode keamanan", "passcode", "kode rahasia", "one time password", "autentikasi"
    };
    for (const char* kw : kKeywords) {
        if (lower.find(kw) != std::string::npos) {
            has_otp_kw = true;
            break;
        }
    }
    if (!has_otp_kw) return false;

    // Service identification
    static const struct { const char* key; const char* name; } kServices[] = {
        {"bca", "BCA BANK"},
        {"mandiri", "BANK MANDIRI"},
        {"bri", "BANK BRI"},
        {"bni", "BANK BNI"},
        {"bsi", "BANK BSI"},
        {"cimb", "CIMB NIAGA"},
        {"permata", "PERMATA BANK"},
        {"jenius", "JENIUS"},
        {"jago", "BANK JAGO"},
        {"dana", "DANA"},
        {"ovo", "OVO"},
        {"gopay", "GOPAY"},
        {"gojek", "GOJEK"},
        {"grab", "GRAB"},
        {"shopee", "SHOPEE"},
        {"tokopedia", "TOKOPEDIA"},
        {"blibli", "BLIBLI"},
        {"lazada", "LAZADA"},
        {"google", "GOOGLE"},
        {"whatsapp", "WHATSAPP"},
        {"telegram", "TELEGRAM"},
        {"tiktok", "TIKTOK"},
        {"facebook", "FACEBOOK"},
        {"instagram", "INSTAGRAM"},
        {"microsoft", "MICROSOFT"},
        {"apple", "APPLE ID"}
    };

    out_service.clear();
    for (const auto& s : kServices) {
        if (lower.find(s.key) != std::string::npos) {
            out_service = s.name;
            break;
        }
    }
    if (out_service.empty()) {
        if (!title.empty() && title.length() <= 20) {
            out_service = title;
        } else if (!app.empty()) {
            out_service = app;
        } else {
            out_service = "SMS BANKING";
        }
    }

    // 1. Check Google format: G-123456
    size_t g_pos = message.find("G-");
    if (g_pos != std::string::npos && g_pos + 8 <= message.length()) {
        bool all_num = true;
        for (int i = 0; i < 6; ++i) {
            if (!isdigit((unsigned char)message[g_pos + 2 + i])) {
                all_num = false;
                break;
            }
        }
        if (all_num) {
            out_otp_code = message.substr(g_pos, 8);
            return true;
        }
    }

    // 2. Scan after prefixes
    static const char* const kPrefixes[] = {
        "otp:", "otp :", "otp adalah", "otp anda", "kode:", "kode :", "kode anda",
        "code:", "code :", "code is", "token:", "token :", "adalah", "is"
    };
    for (const char* pfx : kPrefixes) {
        size_t idx = lower.find(pfx);
        if (idx != std::string::npos) {
            size_t s = idx + strlen(pfx);
            while (s < message.length() && (message[s] == ' ' || message[s] == ':' || message[s] == '=')) {
                s++;
            }
            std::string cand;
            while (s < message.length() && (isdigit((unsigned char)message[s]) || message[s] == '-')) {
                cand += message[s++];
            }
            int digits = 0;
            for (char c : cand) if (isdigit((unsigned char)c)) digits++;
            if (digits >= 4 && digits <= 8) {
                out_otp_code = cand;
                return true;
            }
        }
    }

    // 3. Scan standalone candidates
    for (size_t i = 0; i < message.length(); ) {
        if (isdigit((unsigned char)message[i])) {
            size_t start = i;
            while (i < message.length() && (isdigit((unsigned char)message[i]) || message[i] == '-')) {
                i++;
            }
            std::string cand = message.substr(start, i - start);
            int digits = 0;
            for (char c : cand) if (isdigit((unsigned char)c)) digits++;

            if (digits >= 4 && digits <= 8) {
                bool is_currency = false;
                if (start >= 2) {
                    size_t pre_start = (start >= 5) ? start - 5 : 0;
                    std::string pre = message.substr(pre_start, start - pre_start);
                    for (char& c : pre) c = (char)tolower((unsigned char)c);
                    if (pre.find("rp") != std::string::npos || pre.find("idr") != std::string::npos) {
                        is_currency = true;
                    }
                }
                if (digits == 4 && (cand == "2024" || cand == "2025" || cand == "2026" || cand == "2027")) {
                    is_currency = true;
                }
                if (!is_currency) {
                    out_otp_code = cand;
                    return true;
                }
            }
        } else {
            i++;
        }
    }

    return false;
}

void NovaDisplay::ShowNotificationBanner(const std::string& app, const std::string& title, const std::string& message) {
    // 1. Check if this is an OTP / SMS Banking token
    std::string otp_service, otp_code;
    if (CheckAndExtractOtp(app, title, message, otp_service, otp_code)) {
        ESP_LOGI(TAG, "Smart OTP Notification detected: service='%s', code='%s'",
                 otp_service.c_str(), otp_code.c_str());
        ShowOtpHighlight(otp_service, otp_code, message);
        return;
    }
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (notif_banner_) {
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    if (notif_banner_timer_) {
        lv_timer_del(notif_banner_timer_);
        notif_banner_timer_ = nullptr;
    }

    int banner_w = width_ - 16;
    int banner_h = 52;

    notif_banner_ = lv_obj_create(top);
    lv_obj_set_pos(notif_banner_, 8, 4);
    lv_obj_set_size(notif_banner_, banner_w, banner_h);
    lv_obj_set_style_radius(notif_banner_, 10, 0);
    lv_obj_set_style_bg_color(notif_banner_, lv_color_hex(0x090D16), 0);
    lv_obj_set_style_bg_opa(notif_banner_, LV_OPA_90, 0);

    uint32_t border_color = 0x38BDF8;
    if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
    else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
    else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
    else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

    lv_obj_set_style_border_color(notif_banner_, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(notif_banner_, 2, 0);
    lv_obj_set_style_pad_all(notif_banner_, 4, 0);
    lv_obj_set_style_pad_left(notif_banner_, 10, 0);
    lv_obj_set_style_pad_right(notif_banner_, 10, 0);
    lv_obj_clear_flag(notif_banner_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_lbl = lv_label_create(notif_banner_);
    std::string h_str = app.empty() ? "NOTIFIKASI" : app;
    if (!title.empty()) h_str += " - " + title;
    lv_label_set_text(title_lbl, h_str.c_str());
    lv_obj_set_style_text_font(title_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(border_color), 0);
    lv_obj_set_pos(title_lbl, 0, 2);
    lv_obj_set_width(title_lbl, banner_w - 24);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);

    lv_obj_t* msg_lbl = lv_label_create(notif_banner_);
    lv_label_set_text(msg_lbl, message.empty() ? "(Pesan baru)" : message.c_str());
    lv_obj_set_style_text_font(msg_lbl, FONT_14, 0);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(msg_lbl, 0, 22);
    lv_obj_set_width(msg_lbl, banner_w - 24);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_move_foreground(notif_banner_);
    lv_obj_add_flag(notif_banner_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(notif_banner_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) {
            self->DismissNotificationBanner();
            self->NavigateTo(PAGE_NOTIF);
        }
    }, LV_EVENT_CLICKED, this);

    notif_banner_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
        if (self) {
            self->notif_banner_timer_ = nullptr;
            self->DismissNotificationBanner();
        }
    }, 6000, this);
    lv_timer_set_repeat_count(notif_banner_timer_, 1);

    lvgl_port_unlock();
}

void NovaDisplay::DismissNotificationBanner() {
    lvgl_port_lock(0);
    if (notif_banner_timer_) {
        lv_timer_del(notif_banner_timer_);
        notif_banner_timer_ = nullptr;
    }
    if (notif_banner_) {
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::DismissOtpHighlight() {
    lvgl_port_lock(0);
    if (otp_timer_) {
        lv_timer_del(otp_timer_);
        otp_timer_ = nullptr;
    }
    if (otp_modal_) {
        lv_obj_del(otp_modal_);
        otp_modal_ = nullptr;
        otp_countdown_label_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowOtpHighlight(const std::string& service, const std::string& otp_code, const std::string& full_message) {
    lvgl_port_lock(0);
    if (notif_banner_) {
        if (notif_banner_timer_) {
            lv_timer_del(notif_banner_timer_);
            notif_banner_timer_ = nullptr;
        }
        lv_obj_del(notif_banner_);
        notif_banner_ = nullptr;
    }
    if (otp_timer_) {
        lv_timer_del(otp_timer_);
        otp_timer_ = nullptr;
    }
    if (otp_modal_) {
        lv_obj_del(otp_modal_);
        otp_modal_ = nullptr;
        otp_countdown_label_ = nullptr;
    }

    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    bool is_land = (width_ > 240);
    int modal_w = is_land ? 296 : 228;
    int modal_h = is_land ? 190 : 250;
    int modal_x = (width_ - modal_w) / 2;
    int modal_y = (height_ - modal_h) / 2;

    otp_modal_ = lv_obj_create(top);
    lv_obj_set_pos(otp_modal_, modal_x, modal_y);
    lv_obj_set_size(otp_modal_, modal_w, modal_h);
    lv_obj_set_style_radius(otp_modal_, 14, 0);
    lv_obj_set_style_bg_color(otp_modal_, lv_color_hex(0x0A0F1D), 0);
    lv_obj_set_style_bg_opa(otp_modal_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(otp_modal_, lv_color_hex(0xF59E0B), 0); // Amber gold glowing border
    lv_obj_set_style_border_width(otp_modal_, 2, 0);
    lv_obj_set_style_pad_all(otp_modal_, 8, 0);
    lv_obj_clear_flag(otp_modal_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Header: Lock Icon + Title + Close Button
    lv_obj_t* key_icon = lv_label_create(otp_modal_);
    lv_label_set_text(key_icon, MATERIAL_SYMBOLS_LOCK);
    lv_obj_set_style_text_font(key_icon, FONT_ICON, 0);
    lv_obj_set_style_text_color(key_icon, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_pos(key_icon, 4, 4);

    lv_obj_t* title_lbl = lv_label_create(otp_modal_);
    lv_label_set_text(title_lbl, "KODE OTP / TOKEN BANK");
    lv_obj_set_style_text_font(title_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_pos(title_lbl, 26, 6);

    // Close button [X]
    lv_obj_t* close_btn = lv_btn_create(otp_modal_);
    lv_obj_set_pos(close_btn, modal_w - 40, 2);
    lv_obj_set_size(close_btn, 24, 24);
    StyleButton(close_btn, lv_color_hex(0x1F2937), lv_color_hex(0x374151), 6);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(x_lbl, FONT_ICON, 0);
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissOtpHighlight();
    }, LV_EVENT_CLICKED, this);

    // 2. Service Badge Pill
    lv_obj_t* s_badge = lv_obj_create(otp_modal_);
    lv_obj_set_pos(s_badge, 6, 28);
    lv_obj_set_size(s_badge, modal_w - 28, 22);
    lv_obj_set_style_radius(s_badge, 6, 0);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x111827), 0);
    lv_obj_set_style_border_color(s_badge, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_border_width(s_badge, 1, 0);
    lv_obj_set_style_pad_all(s_badge, 2, 0);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* s_lbl = lv_label_create(s_badge);
    std::string s_text = "LAYANAN: " + service;
    lv_label_set_text(s_lbl, s_text.c_str());
    lv_obj_set_style_text_font(s_lbl, FONT_10, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(s_lbl);

    // 3. Central Huge OTP Box (Hero Element!)
    int box_y = 54;
    int box_h = is_land ? 58 : 68;
    lv_obj_t* otp_box = lv_obj_create(otp_modal_);
    lv_obj_set_pos(otp_box, 6, box_y);
    lv_obj_set_size(otp_box, modal_w - 28, box_h);
    lv_obj_set_style_radius(otp_box, 10, 0);
    lv_obj_set_style_bg_color(otp_box, lv_color_hex(0x030712), 0);
    lv_obj_set_style_border_color(otp_box, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_border_width(otp_box, 2, 0);
    lv_obj_set_style_pad_all(otp_box, 4, 0);
    lv_obj_clear_flag(otp_box, LV_OBJ_FLAG_SCROLLABLE);

    std::string spaced_code = "";
    for (size_t i = 0; i < otp_code.length(); ++i) {
        spaced_code += otp_code[i];
        if (i + 1 < otp_code.length() && otp_code[i] != '-' && otp_code[i+1] != '-') {
            spaced_code += " ";
        }
    }

    lv_obj_t* code_lbl = lv_label_create(otp_box);
    lv_label_set_text(code_lbl, spaced_code.c_str());
    lv_obj_set_style_text_font(code_lbl, FONT_36, 0);
    lv_obj_set_style_text_color(code_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(code_lbl);

    // 4. Message context preview (1 line)
    int msg_y = box_y + box_h + 4;
    lv_obj_t* msg_lbl = lv_label_create(otp_modal_);
    lv_label_set_text(msg_lbl, full_message.empty() ? "(Rahasiakan kode OTP Anda)" : full_message.c_str());
    lv_obj_set_style_text_font(msg_lbl, FONT_10, 0);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_pos(msg_lbl, 8, msg_y);
    lv_obj_set_width(msg_lbl, modal_w - 32);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_DOT);

    // 5. Countdown & Action footer (30 seconds)
    int foot_y = modal_h - 28;
    otp_countdown_label_ = lv_label_create(otp_modal_);
    otp_remaining_sec_ = 30;
    char cd_buf[96];
    snprintf(cd_buf, sizeof(cd_buf), "Menutup otomatis dalam %d dtk (Ketuk utk tutup)", otp_remaining_sec_);
    lv_label_set_text(otp_countdown_label_, cd_buf);
    lv_obj_set_style_text_font(otp_countdown_label_, FONT_10, 0);
    lv_obj_set_style_text_color(otp_countdown_label_, lv_color_hex(0xFCD34D), 0);
    lv_obj_set_pos(otp_countdown_label_, 8, foot_y);
    lv_obj_set_width(otp_countdown_label_, modal_w - 32);
    lv_obj_set_style_text_align(otp_countdown_label_, LV_TEXT_ALIGN_CENTER, 0);

    // Clicking anywhere on modal closes it
    lv_obj_add_flag(otp_modal_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(otp_modal_, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissOtpHighlight();
    }, LV_EVENT_CLICKED, this);

    // Timer: runs every 1000ms for 30 seconds
    otp_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(t));
        if (!self) return;
        self->otp_remaining_sec_--;
        if (self->otp_remaining_sec_ <= 0) {
            self->DismissOtpHighlight();
        } else if (self->otp_countdown_label_) {
            char buf[96];
            snprintf(buf, sizeof(buf), "Menutup otomatis dalam %d dtk (Ketuk utk tutup)", self->otp_remaining_sec_);
            lv_label_set_text(self->otp_countdown_label_, buf);
        }
    }, 1000, this);
    lv_timer_set_repeat_count(otp_timer_, 31);

    lv_obj_move_foreground(otp_modal_);
    lvgl_port_unlock();
}

void NovaDisplay::ShowBtMusicModal() {
    NavigateTo(PAGE_BT_MUSIC);
}

void NovaDisplay::ShowNavModal() {
    NavigateTo(PAGE_NAV);
}

void NovaDisplay::ShowNotifCenterModal() {
    NavigateTo(PAGE_NOTIF);
}

void NovaDisplay::ShowWeatherModal() {
    NavigateTo(PAGE_WEATHER);
}

void NovaDisplay::ShowPhoneModal() {
    NavigateTo(PAGE_PHONE);
}

// ---- NAVIGATION 48x48 ICON DRAWING HELPERS ----
static inline void nav_put_pixel(uint8_t* buf, int x, int y, uint16_t color) {
    if (x < 0 || x >= 48 || y < 0 || y >= 48) return;
    int idx = (y * 48 + x) * 2;
    buf[idx] = color & 0xFF;
    buf[idx + 1] = (color >> 8) & 0xFF;
}

static void nav_draw_filled_circle(uint8_t* buf, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                nav_put_pixel(buf, cx + dx, cy + dy, color);
            }
        }
    }
}

static void nav_draw_arrow_up(uint8_t* buf, uint16_t color) {
    for (int y = 24; y <= 42; y++)
        for (int x = 21; x <= 26; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 24, 42, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            nav_put_pixel(buf, x, 6 + i, color);
    }
}

static void nav_draw_arrow_left(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 12; x <= 34; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 34, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 6 + i, y, color);
    }
}

static void nav_draw_arrow_right(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 13; x <= 35; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 13, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 41 - i, y, color);
    }
}

static void nav_draw_arrow_slight_left(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 32 - i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            nav_put_pixel(buf, x + d, y, color);
            nav_put_pixel(buf, x, y + d, color);
        }
    }
    nav_draw_filled_circle(buf, 32, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 12 - i / 2, cy = 14 - i / 2;
        for (int d = -half; d <= half; d++)
            nav_put_pixel(buf, cx + d, cy, color);
    }
}

static void nav_draw_arrow_slight_right(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 15 + i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            nav_put_pixel(buf, x + d, y, color);
            nav_put_pixel(buf, x, y + d, color);
        }
    }
    nav_draw_filled_circle(buf, 15, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 35 + i / 2, cy = 14 - i / 2;
        for (int d = -half; d <= half; d++)
            nav_put_pixel(buf, cx + d, cy, color);
    }
}

static void nav_draw_arrow_uturn(uint8_t* buf, uint16_t color) {
    for (int y = 20; y <= 40; y++)
        for (int x = 27; x <= 32; x++)
            nav_put_pixel(buf, x, y, color);
    nav_draw_filled_circle(buf, 29, 40, 2, color);
    for (int a = 0; a <= 180; a += 3) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -2; t <= 2; t++) {
            int x = 20 + (int)((9 + t) * cosf(rad));
            int y = 20 - (int)((9 + t) * sinf(rad));
            nav_put_pixel(buf, x, y, color);
        }
    }
    for (int y = 20; y <= 32; y++)
        for (int x = 9; x <= 14; x++)
            nav_put_pixel(buf, x, y, color);
    for (int i = 0; i < 12; i++) {
        int half = 1 + (i * 7) / 12;
        for (int x = 11 - half; x <= 11 + half; x++)
            nav_put_pixel(buf, x, 32 + i, color);
    }
}

static void nav_draw_roundabout(uint8_t* buf, uint16_t color) {
    int r = 14;
    for (int a = 0; a < 360; a += 2) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -1; t <= 1; t++) {
            int x = 24 + (int)((r + t) * cosf(rad));
            int y = 24 + (int)((r + t) * sinf(rad));
            nav_put_pixel(buf, x, y, color);
        }
    }
    for (int y = 21; y <= 26; y++)
        for (int x = 30; x <= 42; x++)
            nav_put_pixel(buf, x, y, color);
    for (int i = 0; i < 8; i++) {
        int half = 1 + (i * 3) / 8;
        for (int y = 24 - half; y <= 24 + half; y++)
            nav_put_pixel(buf, 42 - i, y, color);
    }
}

static void nav_draw_arrive(uint8_t* buf, uint16_t color, uint16_t cutout) {
    nav_draw_filled_circle(buf, 24, 14, 9, color);
    nav_draw_filled_circle(buf, 24, 14, 4, cutout);
    for (int i = 0; i < 16; i++) {
        int half = 9 - (i * 6) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            nav_put_pixel(buf, x, 23 + i, color);
    }
    nav_put_pixel(buf, 24, 39, color);
    nav_put_pixel(buf, 24, 40, color);
}

static void nav_draw_car(uint8_t* buf, uint16_t color, uint16_t cutout) {
    for (int y = 24; y <= 36; y++)
        for (int x = 10; x <= 37; x++)
            nav_put_pixel(buf, x, y, color);
    for (int y = 16; y <= 24; y++)
        for (int x = 15; x <= 32; x++)
            nav_put_pixel(buf, x, y, color);
    for (int y = 17; y <= 23; y++)
        for (int x = 16; x <= 22; x++)
            nav_put_pixel(buf, x, y, cutout);
    for (int y = 17; y <= 23; y++)
        for (int x = 25; x <= 31; x++)
            nav_put_pixel(buf, x, y, cutout);
    nav_draw_filled_circle(buf, 15, 37, 4, color);
    nav_draw_filled_circle(buf, 32, 37, 4, color);
    nav_put_pixel(buf, 10, 28, 0xFFE0);
    nav_put_pixel(buf, 10, 29, 0xFFE0);
    nav_put_pixel(buf, 37, 28, 0xFFE0);
    nav_put_pixel(buf, 37, 29, 0xFFE0);
}

void NovaDisplay::RenderNavIcon(const std::string& maneuver, const std::string& directions, bool has_icon, const uint8_t* icon_data) {
    if (!nav_icon_buf_) return;

    uint32_t bg_rgb = lv_color_to_int(C_PANEL_2);
    uint16_t bg_color = (uint16_t)((((bg_rgb >> 16) & 0xF8) << 8) | (((bg_rgb >> 8) & 0xFC) << 3) | ((bg_rgb & 0xF8) >> 3));
    uint32_t fg_rgb = lv_color_to_int(C_CYAN);
    uint16_t fg_color = (uint16_t)((((fg_rgb >> 16) & 0xF8) << 8) | (((fg_rgb >> 8) & 0xFC) << 3) | ((fg_rgb & 0xF8) >> 3));

    if (has_icon && icon_data) {
        for (int y = 0; y < 48; y++) {
            for (int x = 0; x < 48; x++) {
                int byte_idx = (y * 48 + x) / 8;
                int bit_pos = 7 - (x % 8);
                bool on = (icon_data[byte_idx] >> bit_pos) & 0x01;
                uint16_t color = on ? fg_color : bg_color;
                int offset = (y * 48 + x) * 2;
                nav_icon_buf_[offset] = color & 0xFF;
                nav_icon_buf_[offset + 1] = (color >> 8) & 0xFF;
            }
        }
        return;
    }

    for (int i = 0; i < 48 * 48; i++) {
        nav_icon_buf_[i * 2] = bg_color & 0xFF;
        nav_icon_buf_[i * 2 + 1] = (bg_color >> 8) & 0xFF;
    }

    std::string m = maneuver;
    if (m.empty() && !directions.empty()) {
        std::string d = directions;
        for (auto& c : d) c = (char)tolower((unsigned char)c);
        if (d.find("slight left") != std::string::npos || d.find("serong kiri") != std::string::npos || d.find("agak ke kiri") != std::string::npos) {
            m = "SLIGHT_LEFT";
        } else if (d.find("slight right") != std::string::npos || d.find("serong kanan") != std::string::npos || d.find("agak ke kanan") != std::string::npos) {
            m = "SLIGHT_RIGHT";
        } else if (d.find("putar balik") != std::string::npos || d.find("u-turn") != std::string::npos) {
            m = "UTURN";
        } else if (d.find("kiri") != std::string::npos || d.find("left") != std::string::npos) {
            m = "LEFT";
        } else if (d.find("kanan") != std::string::npos || d.find("right") != std::string::npos) {
            m = "RIGHT";
        } else if (d.find("bundaran") != std::string::npos || d.find("roundabout") != std::string::npos) {
            m = "ROUNDABOUT";
        } else if (d.find("sampai") != std::string::npos || d.find("arrive") != std::string::npos || d.find("tujuan") != std::string::npos || d.find("tiba") != std::string::npos) {
            m = "ARRIVE";
        } else if (d.find("lurus") != std::string::npos || d.find("straight") != std::string::npos || d.find("maju") != std::string::npos) {
            m = "STRAIGHT";
        }
    }

    if (m == "LEFT") nav_draw_arrow_left(nav_icon_buf_, fg_color);
    else if (m == "RIGHT") nav_draw_arrow_right(nav_icon_buf_, fg_color);
    else if (m == "SLIGHT_LEFT") nav_draw_arrow_slight_left(nav_icon_buf_, fg_color);
    else if (m == "SLIGHT_RIGHT") nav_draw_arrow_slight_right(nav_icon_buf_, fg_color);
    else if (m == "UTURN") nav_draw_arrow_uturn(nav_icon_buf_, fg_color);
    else if (m == "ROUNDABOUT") nav_draw_roundabout(nav_icon_buf_, fg_color);
    else if (m == "ARRIVE") nav_draw_arrive(nav_icon_buf_, fg_color, bg_color);
    else if (m == "STRAIGHT") nav_draw_arrow_up(nav_icon_buf_, fg_color);
    else nav_draw_car(nav_icon_buf_, fg_color, bg_color);
}

void NovaDisplay::BuildNav() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "NAVIGASI GOOGLE MAPS");

    std::string dist = "--", dir = "Menunggu instruksi...", title = "Buka Google Maps di HP";
    std::string dur = "--", eta = "--:--", spd = "--";
    bool active = false;
    if (callbacks_.get_ble_nav) {
        callbacks_.get_ble_nav(dist, dir, title, dur, eta, spd, active);
    }

    std::string maneuver = last_nav_maneuver_;
    bool has_icon = last_nav_has_icon_;
    const uint8_t* icon_data = last_nav_has_icon_ ? last_nav_icon_data_ : nullptr;
    if (callbacks_.get_ble_nav_extra) {
        callbacks_.get_ble_nav_extra(maneuver, has_icon, icon_data);
    }
    RenderNavIcon(maneuver, dir, has_icon, icon_data);

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    if (!is_land) {
        // === PORTRAIT LAYOUT (240x320) ===
        // Tanpa header/pill "SIAP BUKA MAPS" agar tidak menghalangi dan luas!

        // 1. Kartu Manuver & Belokan Utama (y=36, h=146)
        lv_obj_t* card = MakePanel(root, content_x, 36, content_w, 146, C_PANEL, 
                                   active ? C_CYAN : C_BORDER, 12);
        lv_obj_set_style_border_width(card, 2, 0);

        // Icon Rute Gede & Rapih (54x54 Badge Container dengan Gambar 48x48)
        lv_obj_t* icon_badge = lv_obj_create(card);
        lv_obj_set_pos(icon_badge, 8, 8);
        lv_obj_set_size(icon_badge, 54, 54);
        lv_obj_set_style_radius(icon_badge, 12, 0);
        lv_obj_set_style_bg_color(icon_badge, C_PANEL_2, 0);
        lv_obj_set_style_border_color(icon_badge, C_CYAN, 0);
        lv_obj_set_style_border_width(icon_badge, 2, 0);
        lv_obj_set_style_pad_all(icon_badge, 0, 0);
        lv_obj_clear_flag(icon_badge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        if (nav_icon_buf_) {
            lv_obj_t* icon_img = lv_image_create(icon_badge);
            lv_image_set_src(icon_img, &nav_icon_dsc_);
            lv_obj_center(icon_img);
        }

        // Jarak Belokan Berikutnya
        std::string dist_str = dist.empty() ? "-- m" : dist;
        MakeLabel(card, dist_str.c_str(), 70, 6, content_w - 78, 30, FONT_28, C_CYAN);

        // Status GPS Aktif
        std::string status_txt = active ? "● Rute GPS Aktif" : "Menunggu Rute...";
        MakeLabel(card, status_txt.c_str(), 70, 36, content_w - 78, 18, FONT_10, active ? C_MINT : C_MUTED);

        // Instruksi Manuver (e.g. Belok Kanan)
        std::string dir_str = dir.empty() ? "Mulai rute navigasi di ponsel" : dir;
        lv_obj_t* dir_lbl = MakeLabel(card, dir_str.c_str(), 10, 68, content_w - 20, 24, FONT_14, C_TEXT);
        lv_label_set_long_mode(dir_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Nama Jalan Tujuan (e.g. ke Jl. Sudirman)
        std::string title_str = title.empty() ? "Menunggu tujuan..." : title;
        lv_obj_t* street_lbl = MakeLabel(card, title_str.c_str(), 10, 94, content_w - 20, 22, FONT_12, C_AMBER);
        lv_label_set_long_mode(street_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        // Catatan Info
        MakeLabel(card, active ? "Notifikasi diredam selama navigasi" : "Tersambung via Bluetooth Chronos", 
                  10, 120, content_w - 20, 16, FONT_10, C_MUTED);

        // 2. Kartu Telemetri (ETA, Durasi, Kecepatan) (y=190, h=76)
        lv_obj_t* tcard = MakePanel(root, content_x, 190, content_w, 76, C_PANEL, C_BORDER, 10);

        // Panel Kiri: Estimasi Tiba (ETA) & Sisa Waktu
        int half_w = (content_w - 16) / 2;
        lv_obj_t* p_left = MakePanel(tcard, 6, 6, half_w, 64, C_PANEL_2, C_BORDER, 8);
        MakeLabel(p_left, "ESTIMASI TIBA", 6, 4, half_w - 12, 14, FONT_10, C_MUTED);
        
        // Teks ETA dikecilkan (FONT_12) agar pas dan rapi tanpa terpotong
        std::string eta_clean = eta.empty() ? "--:--" : eta;
        std::string eta_txt = "ETA: " + eta_clean;
        MakeLabel(p_left, eta_txt.c_str(), 6, 20, half_w - 12, 18, FONT_12, C_AMBER);

        std::string dur_txt = dur.empty() ? "-- mnt" : dur;
        MakeLabel(p_left, dur_txt.c_str(), 6, 40, half_w - 12, 16, FONT_10, C_MINT);

        // Panel Kanan: Kecepatan & Status
        lv_obj_t* p_right = MakePanel(tcard, 10 + half_w, 6, half_w, 64, C_PANEL_2, C_BORDER, 8);
        MakeLabel(p_right, "KECEPATAN", 6, 4, half_w - 12, 14, FONT_10, C_MUTED);
        std::string spd_txt = spd.empty() ? "-- km/h" : spd;
        MakeLabel(p_right, spd_txt.c_str(), 6, 20, half_w - 12, 18, FONT_12, C_CYAN);
        MakeLabel(p_right, active ? "GPS Live" : "Standby", 6, 40, half_w - 12, 16, FONT_10, C_MUTED);

    } else {
        // === LANDSCAPE LAYOUT (320x240) ===

        // Left Column: Route & Distance (w=176, h=166)
        lv_obj_t* card = MakePanel(root, 8, 36, 176, 166, C_PANEL, active ? C_CYAN : C_BORDER, 12);
        lv_obj_set_style_border_width(card, 2, 0);

        // Large Turn Icon Badge
        lv_obj_t* icon_badge = lv_obj_create(card);
        lv_obj_set_pos(icon_badge, 8, 8);
        lv_obj_set_size(icon_badge, 54, 54);
        lv_obj_set_style_radius(icon_badge, 12, 0);
        lv_obj_set_style_bg_color(icon_badge, C_PANEL_2, 0);
        lv_obj_set_style_border_color(icon_badge, C_CYAN, 0);
        lv_obj_set_style_border_width(icon_badge, 2, 0);
        lv_obj_set_style_pad_all(icon_badge, 0, 0);
        lv_obj_clear_flag(icon_badge, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        if (nav_icon_buf_) {
            lv_obj_t* icon_img = lv_image_create(icon_badge);
            lv_image_set_src(icon_img, &nav_icon_dsc_);
            lv_obj_center(icon_img);
        }

        std::string dist_str = dist.empty() ? "-- m" : dist;
        lv_obj_t* dist_lbl = MakeLabel(card, dist_str.c_str(), 68, 6, 100, 30, FONT_24, C_TEXT);
        lv_obj_set_style_text_color(dist_lbl, C_CYAN, 0);

        std::string status_txt = active ? "● Rute GPS Aktif" : "Menunggu Rute...";
        MakeLabel(card, status_txt.c_str(), 68, 36, 100, 16, FONT_10, active ? C_MINT : C_MUTED);

        std::string dir_str = dir.empty() ? "Buka Google Maps di HP" : dir;
        lv_obj_t* dir_lbl = MakeLabel(card, dir_str.c_str(), 8, 68, 160, 24, FONT_12, C_TEXT);
        lv_label_set_long_mode(dir_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        std::string title_str = title.empty() ? "Menunggu rute..." : title;
        lv_obj_t* street_lbl = MakeLabel(card, title_str.c_str(), 8, 94, 160, 20, FONT_10, C_AMBER);
        lv_label_set_long_mode(street_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        MakeLabel(card, active ? "Notifikasi diredam" : "Bluetooth Chronos", 8, 118, 160, 16, FONT_10, C_MUTED);

        // Right Column: Telemetry Dashboard (w=124, h=166)
        lv_obj_t* tcard = MakePanel(root, 188, 36, 124, 166, C_PANEL, C_BORDER, 10);
        MakeLabel(tcard, "ESTIMASI TIBA", 8, 8, 108, 14, FONT_10, C_MUTED);
        std::string eta_txt = "ETA: " + (eta.empty() ? "--:--" : eta);
        MakeLabel(tcard, eta_txt.c_str(), 8, 24, 108, 18, FONT_12, C_AMBER);

        MakeLabel(tcard, "SISA WAKTU", 8, 54, 108, 14, FONT_10, C_MUTED);
        std::string dur_txt = dur.empty() ? "-- mnt" : dur;
        MakeLabel(tcard, dur_txt.c_str(), 8, 68, 108, 18, FONT_12, C_MINT);

        MakeLabel(tcard, "KECEPATAN", 8, 98, 108, 14, FONT_10, C_MUTED);
        std::string spd_txt = spd.empty() ? "-- km/h" : spd;
        MakeLabel(tcard, spd_txt.c_str(), 8, 112, 108, 18, FONT_12, C_CYAN);
    }

    MakeBottomNav(root, PAGE_NAV);
}

void NovaDisplay::BuildBtMusic() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MUSIK HP");

    std::string title = "--", artist = "--";
    bool playing = false;
    if (callbacks_.get_ble_music) {
        callbacks_.get_ble_music(title, artist, playing);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 300 : 216;
    int content_x = (width_ - content_w) / 2;

    int card_y = 38;
    int card_h = is_land ? 164 : 236;
    lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_AMBER, 12);

    // Album art / Disc icon
    int art_y = is_land ? 12 : 16;
    int art_x = is_land ? 16 : (content_w / 2 - 24);
    MakeIcon(card, ICON_AUDIO, art_x, art_y, C_AMBER, is_land ? 36 : 48);

    int text_x = is_land ? 70 : 10;
    int text_w = is_land ? (content_w - 80) : (content_w - 20);

    // Title & Artist
    lv_obj_t* t_lbl = MakeCenteredLabel(card, title.empty() ? "Tidak Ada Musik" : title.c_str(), 
                                        text_x, is_land ? 14 : 74, text_w, 22, FONT_14, C_TEXT);
    lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t* a_lbl = MakeCenteredLabel(card, artist.empty() ? "Buka Spotify/YouTube di HP" : artist.c_str(), 
                                        text_x, is_land ? 38 : 98, text_w, 18, FONT_12, C_MUTED);
    lv_label_set_long_mode(a_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Media Control Buttons (Prev, Play/Pause, Next) with clean 16px separation
    int btn_y = is_land ? 74 : 126;
    int btn_w = 42, btn_h = 36;
    int center_btn_x = content_w / 2;

    // Previous Button (left of play_btn with 16px gap)
    lv_obj_t* prev_btn = lv_btn_create(card);
    lv_obj_set_size(prev_btn, btn_w, btn_h);
    lv_obj_set_pos(prev_btn, center_btn_x - 84, btn_y);
    StyleButton(prev_btn, C_PANEL_2, C_BORDER, 8);
    lv_obj_t* p_icon = lv_label_create(prev_btn);
    lv_obj_set_style_text_font(p_icon, FONT_ICON, 0);
    lv_label_set_text(p_icon, MATERIAL_SYMBOLS_SKIP_PREVIOUS);
    lv_obj_set_style_text_color(p_icon, C_TEXT, 0);
    lv_obj_center(p_icon);
    lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(2); // PREV
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Play/Pause Button
    lv_obj_t* play_btn = lv_btn_create(card);
    lv_obj_set_size(play_btn, 52, 42);
    lv_obj_set_pos(play_btn, center_btn_x - 26, btn_y - 3);
    StyleButton(play_btn, C_AMBER, C_AMBER, 10);
    lv_obj_t* play_icon = lv_label_create(play_btn);
    lv_obj_set_style_text_font(play_icon, FONT_ICON, 0);
    lv_label_set_text(play_icon, playing ? MATERIAL_SYMBOLS_PAUSE : MATERIAL_SYMBOLS_PLAY_ARROW);
    lv_obj_set_style_text_color(play_icon, C_BG, 0);
    lv_obj_center(play_icon);
    lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(1); // TOGGLE PLAY/PAUSE
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Next Button (right of play_btn with 16px gap)
    lv_obj_t* next_btn = lv_btn_create(card);
    lv_obj_set_size(next_btn, btn_w, btn_h);
    lv_obj_set_pos(next_btn, center_btn_x + 42, btn_y);
    StyleButton(next_btn, C_PANEL_2, C_BORDER, 8);
    lv_obj_t* n_icon = lv_label_create(next_btn);
    lv_obj_set_style_text_font(n_icon, FONT_ICON, 0);
    lv_label_set_text(n_icon, MATERIAL_SYMBOLS_SKIP_NEXT);
    lv_obj_set_style_text_color(n_icon, C_TEXT, 0);
    lv_obj_center(n_icon);
    lv_obj_add_event_cb(next_btn, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(3); // NEXT
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, LV_EVENT_CLICKED, this);

    // Volume Buttons
    int vol_y = is_land ? 122 : 180;
    MakeButton(card, "VOL -", center_btn_x - 56, vol_y, 50, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(5); // VOL DOWN
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, this);

    MakeButton(card, "VOL +", center_btn_x + 6, vol_y, 50, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self && self->callbacks_.ble_music_control) self->callbacks_.ble_music_control(4); // VOL UP
        if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
    }, this);

    MakeBottomNav(root, PAGE_BT_MUSIC);
}

void NovaDisplay::BuildNotif() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "NOTIFIKASI HP");

    int notif_count = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    if (notif_count == 0) {
        // Empty state card
        int card_y = 38;
        int card_h = is_land ? 164 : 236;
        lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_CYAN, 12);
        MakeIcon(card, ICON_CHAT, is_land ? 20 : (content_w / 2 - 24), is_land ? 30 : 40, C_CYAN, 48);

        int text_x = is_land ? 80 : 10;
        int text_w = is_land ? (content_w - 90) : (content_w - 20);

        MakeCenteredLabel(card, "Tidak Ada Notifikasi", text_x, is_land ? 36 : 100, text_w, 24, FONT_16, C_TEXT);
        MakeCenteredLabel(card, "Notifikasi baru dari HP (WhatsApp, Telegram, SMS, dll)\nakan langsung tampil di sini dan di jidat layar.", 
                          text_x, is_land ? 66 : 130, text_w, 48, FONT_10, C_MUTED);
    } else {
        // Scrollable list container
        int list_y = 36;
        int list_h = height_ - 67;
        lv_obj_t* list = lv_obj_create(root);
        lv_obj_set_pos(list, content_x, list_y);
        lv_obj_set_size(list, content_w, list_h);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(list, 70, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);

        // Header action: Total count badge + "Hapus Semua" compact button
        lv_obj_t* hbar = lv_obj_create(list);
        lv_obj_set_pos(hbar, 0, 0);
        lv_obj_set_size(hbar, content_w, 28);
        lv_obj_set_style_bg_color(hbar, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_color(hbar, C_BORDER, 0);
        lv_obj_set_style_border_width(hbar, 1, 0);
        lv_obj_set_style_radius(hbar, 6, 0);
        lv_obj_set_style_pad_all(hbar, 2, 0);
        lv_obj_clear_flag(hbar, LV_OBJ_FLAG_SCROLLABLE);

        char cnt_str[32];
        snprintf(cnt_str, sizeof(cnt_str), "%d Notifikasi HP", notif_count);
        MakeLabel(hbar, cnt_str, 8, 4, content_w - 72, 18, FONT_12, C_CYAN);

        MakeButton(hbar, "HAPUS", content_w - 60, 3, 54, 20,
                   lv_color_hex(0x2A1215), lv_color_hex(0xEF4444), lv_color_hex(0xFCA5A5), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.clear_ble_notifs) {
                           self->callbacks_.clear_ble_notifs();
                           self->BuildNotif();
                       }
                   }, this);

        // Notification Cards
        int cur_y = 38;
        for (int i = 0; i < notif_count; ++i) {
            std::string app, title, msg, time;
            if (!callbacks_.get_ble_notif_at || !callbacks_.get_ble_notif_at(i, app, title, msg, time)) {
                continue;
            }

            uint32_t border_color = 0x38BDF8;
            if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
            else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
            else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
            else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

            int ncard_h = 88;
            lv_obj_t* ncard = MakePanel(list, 0, cur_y, content_w, ncard_h, C_PANEL, lv_color_hex(border_color), 10);
            lv_obj_clear_flag(ncard, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(ncard, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(ncard, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_bg_color(ncard, lv_color_hex(0x1E293B), LV_STATE_PRESSED);

            // Badge row
            MakeIcon(ncard, ICON_CHAT, 8, 8, lv_color_hex(border_color), 18);
            std::string a_str = app.empty() ? "Aplikasi" : app;
            MakeLabel(ncard, a_str.c_str(), 28, 8, content_w - 88, 16, FONT_12, lv_color_hex(border_color));
            if (!time.empty()) {
                MakeLabel(ncard, time.c_str(), content_w - 56, 8, 48, 16, FONT_10, C_MUTED);
            }

            // Sender / Subject
            lv_obj_t* s_lbl = MakeLabel(ncard, title.empty() ? "(Tanpa Subjek)" : title.c_str(), 10, 28, content_w - 20, 20, FONT_14, C_TEXT);
            lv_label_set_long_mode(s_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

            // Body
            lv_obj_t* b_lbl = MakeLabel(ncard, msg.empty() ? "(Pesan kosong)" : msg.c_str(), 10, 48, content_w - 76, 34, FONT_12, C_MUTED);
            lv_label_set_long_mode(b_lbl, LV_LABEL_LONG_DOT);

            // Tap cue to indicate clickability
            MakeLabel(ncard, "Detail >", content_w - 52, 64, 48, 14, FONT_10, lv_color_hex(0x38BDF8));

            struct NotifClickCtx {
                NovaDisplay* self;
                int idx;
            };
            auto* ctx = new NotifClickCtx{this, i};
            lv_obj_add_event_cb(ncard, [](lv_event_t* e) {
                auto* ctx = static_cast<NotifClickCtx*>(lv_event_get_user_data(e));
                if (ctx && ctx->self) {
                    ctx->self->ShowNotificationDetail(ctx->idx);
                }
            }, LV_EVENT_CLICKED, ctx);
            lv_obj_add_event_cb(ncard, [](lv_event_t* e) {
                auto* ctx = static_cast<NotifClickCtx*>(lv_event_get_user_data(e));
                delete ctx;
            }, LV_EVENT_DELETE, ctx);

            cur_y += ncard_h + 8;
        }
    }

    MakeBottomNav(root, PAGE_NOTIF);
}

void NovaDisplay::BuildWeather() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "CUACA HP");

    float temp = 0;
    std::string cond = "--";
    int hum = 0;
    std::string loc = "Lokasi HP";
    int high = 0, low = 0;
    bool has_w = false;
    if (callbacks_.get_ble_weather_full) {
        has_w = callbacks_.get_ble_weather_full(temp, cond, hum, loc, high, low);
    } else if (callbacks_.get_ble_weather) {
        has_w = callbacks_.get_ble_weather(temp, cond, hum);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    int card_y = 38;
    int card_h = is_land ? 164 : 236;
    lv_obj_t* card = MakePanel(root, content_x, card_y, content_w, card_h, C_PANEL, C_AMBER, 12);

    // Weather Icon
    MakeIcon(card, ICON_WEATHER, is_land ? 16 : (content_w / 2 - 24), is_land ? 16 : 14, C_AMBER, is_land ? 36 : 42);

    char temp_str[32];
    if (has_w) {
        snprintf(temp_str, sizeof(temp_str), "%.0f\xC2\xB0" "C", temp);
    } else {
        snprintf(temp_str, sizeof(temp_str), "--\xC2\xB0" "C");
    }

    int text_x = is_land ? 60 : 0;
    int text_w = is_land ? (content_w - 70) : content_w;

    // Location / City Name
    std::string loc_str = loc.empty() ? "Smartphone Lokasi" : loc;
    lv_obj_t* loc_lbl = MakeCenteredLabel(card, loc_str.c_str(), text_x, is_land ? 12 : 58, text_w, 20, FONT_14, C_CYAN);
    lv_label_set_long_mode(loc_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Big Temperature
    MakeCenteredLabel(card, temp_str, text_x, is_land ? 34 : 78, text_w, 36, FONT_36, C_AMBER);

    // Condition
    std::string c_str = cond.empty() ? "Menghubungkan ke HP..." : cond;
    lv_obj_t* cond_lbl = MakeCenteredLabel(card, c_str.c_str(), text_x, is_land ? 72 : 118, text_w, 20, FONT_14, C_TEXT);
    lv_label_set_long_mode(cond_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Temperature Range & Humidity Cards
    char stat_str[64];
    if (high > 0 || low > 0) {
        snprintf(stat_str, sizeof(stat_str), "Min %d\xC2\xB0 | Max %d\xC2\xB0  •  Kelembapan %d%%", low, high, hum);
    } else {
        snprintf(stat_str, sizeof(stat_str), "Kelembapan Udara: %d%%", hum);
    }
    MakeCenteredLabel(card, stat_str, 0, is_land ? 98 : 146, content_w, 18, FONT_12, C_CYAN);

    // Refresh Button
    MakeButton(card, "🔄 SINKRONISASI CUACA HP", (content_w - 180) / 2, is_land ? 124 : 180, 180, 32,
               lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_10,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self && self->callbacks_.ble_sync_request) {
                       self->callbacks_.ble_sync_request();
                   }
                   if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   if (self) self->BuildWeather();
               }, this);

    MakeBottomNav(root, PAGE_WEATHER);
}

void NovaDisplay::BuildPhone() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "STATUS & KONTROL HP");

    int bat = 0;
    bool chg = false;
    std::string net;
    bool has_p = false;
    if (callbacks_.get_ble_phone) {
        has_p = callbacks_.get_ble_phone(bat, chg, net);
    }

    bool is_land = (width_ > 240);
    int content_w = is_land ? 304 : 224;
    int content_x = (width_ - content_w) / 2;

    // 1. Phone Status Card (Battery & Network)
    lv_obj_t* scard = MakePanel(root, content_x, 38, content_w, is_land ? 62 : 72, C_PANEL, C_MINT, 12);
    MakeIcon(scard, ICON_PHONE, 10, is_land ? 10 : 14, C_MINT, 28);

    char bat_txt[64];
    if (has_p) {
        snprintf(bat_txt, sizeof(bat_txt), "Baterai HP: %d%% %s", bat, chg ? "(Mengisi Daya)" : "");
    } else {
        snprintf(bat_txt, sizeof(bat_txt), "Baterai HP: Tersambung via BLE");
    }
    MakeLabel(scard, bat_txt, 46, 10, content_w - 56, 18, FONT_14, C_TEXT);

    std::string net_txt = "Jaringan: " + (net.empty() ? "Aktif (Chronos Connected)" : net);
    MakeLabel(scard, net_txt.c_str(), 46, 32, content_w - 56, 16, FONT_12, C_CYAN);

    // 2. Actions Card (Dering HP, Remote Kamera, Sinkronisasi)
    int act_y = is_land ? 104 : 116;
    int act_h = is_land ? 100 : 166;
    lv_obj_t* acard = MakePanel(root, content_x, act_y, content_w, act_h, C_PANEL, lv_color_hex(0xEF4444), 12);

    static bool is_ringing = false;

    if (is_land) {
        // Landscape 2-column layout:
        int btn_w = (content_w - 24) / 2;

        // Col 1, Row 1: Bunyikan HP
        MakeButton(acard, is_ringing ? "BUNYIKAN LAGI" : "BUNYIKAN HP", 
                   8, 10, btn_w, 36,
                   lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = true;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(true);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Col 2, Row 1: Hentikan Dering
        MakeButton(acard, "HENTIKAN DERING", 
                   16 + btn_w, 10, btn_w, 36,
                   lv_color_hex(0x3B1219), lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = false;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(false);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Col 1, Row 2: Shutter Kamera
        MakeButton(acard, "📸 JEPRET FOTO HP", 
                   8, 52, btn_w, 36,
                   lv_color_hex(0x1E1B4B), lv_color_hex(0x818CF8), lv_color_hex(0x818CF8), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_capture_photo) self->callbacks_.ble_capture_photo();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   }, this);

        // Col 2, Row 2: Sinkronisasi
        MakeButton(acard, "🔄 SINKRON HP", 
                   16 + btn_w, 52, btn_w, 36,
                   lv_color_hex(0x164E63), C_CYAN, C_CYAN, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_sync_request) self->callbacks_.ble_sync_request();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);
    } else {
        // Portrait vertical layout:
        int btn_w = content_w - 20;

        // Button 1: BUNYIKAN HP
        MakeButton(acard, is_ringing ? "BUNYIKAN LAGI HP" : "BUNYIKAN / DERINGKAN HP", 
                   10, 10, btn_w, 34,
                   lv_color_hex(0x13382C), C_MINT, C_MINT, FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = true;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(true);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Button 2: HENTIKAN DERING
        MakeButton(acard, "HENTIKAN DERING", 
                   10, 48, btn_w, 32,
                   lv_color_hex(0x3B1219), lv_color_hex(0xEF4444), lv_color_hex(0xEF4444), FONT_12,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       is_ringing = false;
                       if (self && self->callbacks_.ble_find_phone) self->callbacks_.ble_find_phone(false);
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);

        // Button 3: SHUTTER KAMERA HP
        MakeButton(acard, "📸 AMBIL FOTO HP (SHUTTER)", 
                   10, 84, btn_w, 32,
                   lv_color_hex(0x1E1B4B), lv_color_hex(0x818CF8), lv_color_hex(0x818CF8), FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_capture_photo) self->callbacks_.ble_capture_photo();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                   }, this);

        // Button 4: SINKRONISASI HP
        MakeButton(acard, "🔄 SINKRONISASI DATA HP", 
                   10, 120, btn_w, 32,
                   lv_color_hex(0x164E63), C_CYAN, C_CYAN, FONT_10,
                   [](lv_event_t* e) {
                       auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                       if (self && self->callbacks_.ble_sync_request) self->callbacks_.ble_sync_request();
                       if (self && self->callbacks_.trigger_click) self->callbacks_.trigger_click();
                       if (self) self->BuildPhone();
                   }, this);
    }

    MakeBottomNav(root, PAGE_PHONE);
}

void NovaDisplay::UpdateNavData(const std::string& dist, const std::string& dir, const std::string& title,
                               const std::string& dur, const std::string& eta, const std::string& spd, bool active,
                               const std::string& maneuver, bool has_icon, const uint8_t* icon_data) {
    lvgl_port_lock(0);
    last_nav_maneuver_ = maneuver;
    last_nav_has_icon_ = has_icon;
    if (has_icon && icon_data) {
        memcpy(last_nav_icon_data_, icon_data, 288);
    }
    if (current_page_ == PAGE_NAV) {
        BuildNav();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdateBtMusicData(const std::string& title, const std::string& artist, bool playing) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_BT_MUSIC) {
        BuildBtMusic();
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowIncomingCallAlert(const std::string& caller, bool active) {
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (call_alert_modal_) {
        lv_obj_del(call_alert_modal_);
        call_alert_modal_ = nullptr;
    }

    if (!active) {
        lvgl_port_unlock();
        return;
    }

    int modal_w = width_ - 20;
    int modal_h = (height_ < 260) ? 96 : 110;
    call_alert_modal_ = lv_obj_create(top);
    lv_obj_set_pos(call_alert_modal_, 10, (height_ < 260) ? 10 : 20);
    lv_obj_set_size(call_alert_modal_, modal_w, modal_h);
    lv_obj_set_style_radius(call_alert_modal_, 14, 0);
    lv_obj_set_style_bg_color(call_alert_modal_, lv_color_hex(0x064E3B), 0);
    lv_obj_set_style_bg_opa(call_alert_modal_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(call_alert_modal_, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(call_alert_modal_, 3, 0);
    lv_obj_set_style_pad_all(call_alert_modal_, 8, 0);
    lv_obj_clear_flag(call_alert_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(call_alert_modal_);

    MakeIcon(call_alert_modal_, ICON_PHONE, 10, 10, lv_color_hex(0x10B981), 32);

    lv_obj_t* title = MakeLabel(call_alert_modal_, "📞 PANGGILAN TELEPON MASUK", 50, 8, modal_w - 60, 20, FONT_14, lv_color_hex(0x10B981));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    std::string cname = caller.empty() ? "Panggilan Suara" : caller;
    lv_obj_t* clbl = MakeLabel(call_alert_modal_, cname.c_str(), 50, 32, modal_w - 60, 24, FONT_16, C_TEXT);
    lv_label_set_long_mode(clbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    MakeButton(call_alert_modal_, "TUTUP PERINGATAN", 
               modal_w - 140, modal_h - 38, 130, 28,
               lv_color_hex(0x047857), lv_color_hex(0x10B981), C_TEXT, FONT_10,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) self->ShowIncomingCallAlert("", false);
               }, this);

    lvgl_port_unlock();
}

void NovaDisplay::ShowFindWatchAlert(bool active) {
    lvgl_port_lock(0);
    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    if (find_watch_modal_) {
        lv_obj_del(find_watch_modal_);
        find_watch_modal_ = nullptr;
    }

    if (!active) {
        lvgl_port_unlock();
        return;
    }

    bool is_land = (width_ > 240);
    int modal_w = is_land ? 280 : 220;
    int modal_h = is_land ? 160 : 176;

    find_watch_modal_ = lv_obj_create(top);
    lv_obj_set_size(find_watch_modal_, width_, height_);
    lv_obj_set_pos(find_watch_modal_, 0, 0);
    lv_obj_clear_flag(find_watch_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(find_watch_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(find_watch_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(find_watch_modal_, 0, 0);

    lv_obj_t* card = MakePanel(find_watch_modal_, 0, 0, modal_w, modal_h, lv_color_hex(0x270707), lv_color_hex(0xEF4444), 16);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Bell icon
    lv_obj_t* icon = lv_label_create(card);
    lv_obj_set_style_text_font(icon, FONT_ICON, 0);
    lv_label_set_text(icon, MATERIAL_SYMBOLS_NOTIFICATIONS);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xEF4444), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);

    // Single-line non-overlapping Title
    MakeCenteredLabel(card, "PENCARIAN SMARTPHONE", 0, 32, modal_w - 20, 20, FONT_12, lv_color_hex(0xEF4444));

    // Subtitle 1
    MakeCenteredLabel(card, "Perangkat ini sedang dicari dari HP", 0, 56, modal_w - 20, 16, FONT_10, lv_color_white());

    // Subtitle 2
    MakeCenteredLabel(card, "Tekan tombol untuk mematikan alarm", 0, 74, modal_w - 20, 16, FONT_10, C_MUTED);

    // Clear distinct button
    int btn_w = modal_w - 36;
    MakeButton(card, "SAYA DI SINI (HENTIKAN)", 18, modal_h - 48, btn_w, 36,
               lv_color_hex(0xDC2626), lv_color_hex(0xF87171), lv_color_white(), FONT_12,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) {
                       self->ShowFindWatchAlert(false);
                   }
               }, this);

    lvgl_port_unlock();
}

void NovaDisplay::UpdateWeatherFull(float temp, const std::string& cond, int hum, const std::string& loc, int high, int low) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_WEATHER) {
        BuildWeather();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdatePhoneStatus(int bat, bool chg, const std::string& net) {
    lvgl_port_lock(0);
    if (current_page_ == PAGE_PHONE) {
        BuildPhone();
    }
    lvgl_port_unlock();
}

void NovaDisplay::UpdateNotif() {
    lvgl_port_lock(0);
    if (top_notif_btn_ && top_notif_icon_ && top_notif_label_) {
        int n_cnt = callbacks_.get_ble_notif_count ? callbacks_.get_ble_notif_count() : 0;
        if (n_cnt > 0) {
            char n_buf[16];
            snprintf(n_buf, sizeof(n_buf), "%d", n_cnt);
            lv_label_set_text(top_notif_label_, n_buf);
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0xEF4444), LV_PART_MAIN);
            lv_obj_clear_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(top_notif_label_, "");
            lv_obj_set_style_text_color(top_notif_icon_, lv_color_hex(0x64748B), LV_PART_MAIN);
            lv_obj_add_flag(top_notif_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(top_notif_btn_, LV_OBJ_FLAG_HIDDEN);
    }
    if (current_page_ == PAGE_NOTIF) {
        BuildNotif();
    }
    lvgl_port_unlock();
}

void NovaDisplay::DismissNotificationDetail() {
    lvgl_port_lock(0);
    if (notif_detail_modal_) {
        lv_obj_del(notif_detail_modal_);
        notif_detail_modal_ = nullptr;
    }
    lvgl_port_unlock();
}

void NovaDisplay::ShowNotificationDetail(int notif_idx) {
    if (!callbacks_.get_ble_notif_at) return;

    std::string app, title, msg, time;
    if (!callbacks_.get_ble_notif_at(notif_idx, app, title, msg, time)) {
        return;
    }

    lvgl_port_lock(0);
    DismissNotificationDetail();

    lv_obj_t* top = lv_layer_top();
    if (!top) {
        lvgl_port_unlock();
        return;
    }

    notif_detail_modal_ = lv_obj_create(top);
    lv_obj_set_size(notif_detail_modal_, width_, height_);
    lv_obj_set_pos(notif_detail_modal_, 0, 0);
    lv_obj_clear_flag(notif_detail_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(notif_detail_modal_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(notif_detail_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(notif_detail_modal_, 0, 0);

    bool is_land = (width_ > 240);
    int card_w = is_land ? 296 : 226;
    int card_h = is_land ? 224 : 290;

    uint32_t border_color = lv_color_to_int(C_CYAN);
    if (app.find("WhatsApp") != std::string::npos || app.find("WA") != std::string::npos) border_color = 0x10B981;
    else if (app.find("Telegram") != std::string::npos) border_color = 0x0284C7;
    else if (app.find("Instagram") != std::string::npos) border_color = 0xEC4899;
    else if (app.find("YouTube") != std::string::npos) border_color = 0xEF4444;

    lv_obj_t* card = MakePanel(notif_detail_modal_, 0, 0, card_w, card_h, C_PANEL, lv_color_hex(border_color), 12);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header: App name + time + Close button
    MakeIcon(card, ICON_CHAT, 10, 10, lv_color_hex(border_color), 18);
    MakeLabel(card, app.empty() ? "Notifikasi" : app.c_str(), 32, 10, card_w - 90, 18, FONT_12, lv_color_hex(border_color));
    if (!time.empty()) {
        MakeLabel(card, time.c_str(), card_w - 74, 10, 40, 16, FONT_10, C_MUTED);
    }

    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 24, 22);
    lv_obj_set_pos(close_btn, card_w - 30, 8);
    StyleButton(close_btn, C_PANEL_2, C_BORDER, 4);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, FONT_ICON, 0);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
        if (self) self->DismissNotificationDetail();
    }, LV_EVENT_CLICKED, this);

    // Divider line
    lv_obj_t* div = lv_obj_create(card);
    lv_obj_set_pos(div, 8, 34);
    lv_obj_set_size(div, card_w - 16, 1);
    lv_obj_set_style_bg_color(div, C_BORDER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Sender / Title
    std::string sender_str = title.empty() ? "(Tanpa Subjek / Pengirim)" : title;
    MakeLabel(card, "PENGIRIM / JUDUL:", 10, 40, card_w - 20, 14, FONT_10, C_MUTED);
    lv_obj_t* t_lbl = MakeLabel(card, sender_str.c_str(), 10, 54, card_w - 20, 20, FONT_14, C_TEXT);
    lv_label_set_long_mode(t_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Scrollable Message Body Container
    int body_y = 80;
    int body_h = card_h - body_y - 44;
    lv_obj_t* body_box = lv_obj_create(card);
    lv_obj_set_pos(body_box, 8, body_y);
    lv_obj_set_size(body_box, card_w - 16, body_h);
    lv_obj_set_style_bg_color(body_box, C_PANEL_2, 0);
    lv_obj_set_style_border_color(body_box, C_BORDER, 0);
    lv_obj_set_style_border_width(body_box, 1, 0);
    lv_obj_set_style_radius(body_box, 8, 0);
    lv_obj_set_style_pad_all(body_box, 6, 0);
    lv_obj_set_scrollbar_mode(body_box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(body_box, LV_DIR_VER);

    lv_obj_t* msg_lbl = lv_label_create(body_box);
    lv_obj_set_width(msg_lbl, card_w - 32);
    lv_obj_set_style_text_font(msg_lbl, FONT_12, 0);
    lv_obj_set_style_text_color(msg_lbl, C_TEXT, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msg_lbl, msg.empty() ? "(Pesan kosong)" : msg.c_str());

    // Bottom action: Close button
    MakeButton(card, "TUTUP DETAIL", 10, card_h - 38, card_w - 20, 30,
               C_PANEL_2, C_CYAN, C_TEXT, FONT_12,
               [](lv_event_t* e) {
                   auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                   if (self) self->DismissNotificationDetail();
               }, this);

    lvgl_port_unlock();
}
