#include "chronchi_display.h"
#include "chronchi_ble.h"
#include "chronchi_alert.h"
#include "board.h"
#include "backlight.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <settings.h>
#include "theme_sync.h"
#include "audio_codec.h"
#include <esp_netif.h>
#include <material_symbols.h>
#include <cstring>
#include <cstdio>
#include <cmath>

LV_FONT_DECLARE(font_material_symbols_20_4);

extern "C" {
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

bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          enum qrcodegen_Mask mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

#undef lv_lock
#undef lv_unlock
#define lv_lock() lvgl_port_lock(0)
#define lv_unlock() lvgl_port_unlock()

#define TAG "ChronchiDisplay"

LV_FONT_DECLARE(lv_font_montserrat_24);

struct BackgroundTheme {
    uint32_t bg;
    uint32_t card_bg;
    uint32_t card_border;
    uint32_t text;
    uint32_t dim;
    uint32_t accent;
    const char* name;
};

// 16 Presets Tema Warna Stabilo Neon Super Cerah & Akurat
static const BackgroundTheme PRESET_THEMES[] = {
    // 0: Stabilo Kuning Neon (Classic Highlighter Yellow - Paling Ikonik & Cerah)
    { 0x000000, 0x141804, 0x5C6600, 0xFAFF00, 0xD4DB4B, 0xEAFF00, "Stabilo Kuning Neon" },

    // 1: Stabilo Hijau Neon (Classic Highlighter Green / Acid Lime)
    { 0x000000, 0x041908, 0x006622, 0x39FF14, 0x86EFAC, 0x00FF66, "Stabilo Hijau Neon" },

    // 2: Stabilo Cyan Laser (Electric Laser Aqua / Neon Cyan)
    { 0x000000, 0x04161E, 0x0369A1, 0x00FFFF, 0x7DD3FC, 0x00E5FF, "Stabilo Cyan Laser" },

    // 3: Stabilo Oranye Neon (Highlighter Orange / Electric Tangerine)
    { 0x000000, 0x1A0B02, 0x9A3412, 0xFF6600, 0xFDBA74, 0xFF7700, "Stabilo Oranye Neon" },

    // 4: Stabilo Pink Magenta (Hot Fluorescent Pink / Neon Magenta)
    { 0x000000, 0x1A0410, 0x9D174D, 0xFF007F, 0xF472B6, 0xFF1493, "Stabilo Pink Magenta" },

    // 5: Stabilo Merah Neon (Radical Laser Red / Fluorescent Crimson)
    { 0x000000, 0x1A0407, 0x991B1B, 0xFF1744, 0xFCA5A5, 0xFF0033, "Stabilo Merah Neon" },

    // 6: Stabilo Ungu Neon (Electric Violet / Ultraviolet Neon)
    { 0x000000, 0x13041A, 0x6B21A8, 0xB000FF, 0xD8B4FE, 0xC026D3, "Stabilo Ungu Neon" },

    // 7: Stabilo Biru Elektrik (Electric Cyberpunk Blue / Cobalt Neon)
    { 0x000000, 0x040E24, 0x1D4ED8, 0x00D2FF, 0x93C5FD, 0x2979FF, "Stabilo Biru Elektrik" },

    // 8: Stabilo Emas Cerah (Solar Gold / Vivid Amber Fluorescent)
    { 0x000000, 0x191202, 0x854D0E, 0xFFD700, 0xFDE047, 0xFFC107, "Stabilo Emas Cerah" },

    // 9: Stabilo Toska Neon (Electric Seafoam / Fluorescent Turquoise)
    { 0x000000, 0x021712, 0x0F766E, 0x00FFAA, 0x5EEAD4, 0x00F5A0, "Stabilo Toska Neon" },

    // 10: Stabilo Peach Coral (Fluorescent Peach / Apricot Glow)
    { 0x000000, 0x190A05, 0x9A3412, 0xFF6E40, 0xFDBA74, 0xFF8A65, "Stabilo Peach Coral" },

    // 11: Stabilo Matrix Green (Phosphor Terminal / Acid Green)
    { 0x000000, 0x021908, 0x15803D, 0x00FF41, 0x4ADE80, 0x76FF03, "Stabilo Matrix Green" },

    // 12: Stabilo Ice Blue (Arctic Glacier Neon / Diamond Ice)
    { 0x000000, 0x041724, 0x0284C7, 0x80E5FF, 0xBAE6FD, 0x38BDF8, "Stabilo Ice Blue" },

    // 13: Stabilo Sakura Rose (Fluorescent Sakura / Vivid Rose Pink)
    { 0x000000, 0x1A0512, 0xBE185D, 0xFF4081, 0xF9A8D4, 0xF50057, "Stabilo Sakura Rose" },

    // 14: Stabilo Kuning Lemon (Electric Lemon Highlighter)
    { 0x000000, 0x161802, 0x717704, 0xF4FF00, 0xFEF08A, 0xEEFF41, "Stabilo Kuning Lemon" },

    // 15: Stabilo Diamond White (High-Contrast Pure White with Cyan Glow)
    { 0x000000, 0x0F172A, 0x38BDF8, 0xFFFFFF, 0xCBD5E1, 0x00E5FF, "Stabilo Diamond White" },
};
static constexpr int NUM_THEMES = sizeof(PRESET_THEMES) / sizeof(PRESET_THEMES[0]);

// Convert 24-bit RGB hex to RGB565 for seamless canvas blending
static inline uint16_t hex_to_rgb565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static lv_obj_t* create_card(lv_obj_t* parent, int32_t w, int32_t h, int32_t radius) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, radius, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

ChronchiDisplay::ChronchiDisplay() = default;
ChronchiDisplay::~ChronchiDisplay() {
    if (chronchi_bottom_nav_) { lv_obj_del(chronchi_bottom_nav_); chronchi_bottom_nav_ = nullptr; }
    if (recent_apps_modal_) { lv_obj_del(recent_apps_modal_); recent_apps_modal_ = nullptr; }
    if (overlay_backdrop_) { lv_obj_del(overlay_backdrop_); overlay_backdrop_ = nullptr; }
    if (vol_overlay_card_) { lv_obj_del(vol_overlay_card_); vol_overlay_card_ = nullptr; }
    if (bright_overlay_card_) { lv_obj_del(bright_overlay_card_); bright_overlay_card_ = nullptr; }
    if (chronchi_navbar_) { lv_obj_del(chronchi_navbar_); chronchi_navbar_ = nullptr; }
    if (nav_icon_buf_) { free(nav_icon_buf_); nav_icon_buf_ = nullptr; }
    if (qr_img_buf_) { free(qr_img_buf_); qr_img_buf_ = nullptr; }
}

void ChronchiDisplay::init(int width, int height) {
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    } else {
        width_ = width;
        height_ = height;
    }
    ESP_LOGI(TAG, "init %dx%d (Borderless Chronchi Watchface)", width_, height_);

    // Load synchronized watchface theme and color from NVS
    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        current_layout_ = synced->chronchi_layout % NUM_THEMES;
        theme_color_ = synced->accent_color;
    } else {
        Settings settings("chronchi", true);
        current_layout_ = settings.GetInt("layout", 1) % NUM_THEMES;
        theme_color_ = settings.GetInt("color", PRESET_THEMES[current_layout_].text);
    }
    light_mode_ = (PRESET_THEMES[current_layout_].bg > 0x888888);

    Settings prefs_24h("nova", true);
    is_24h_format_ = (prefs_24h.GetInt("24hour", 1) != 0);

    ESP_LOGI(TAG, "Initial watchface: layout=%lu (%s), color=0x%06lX, 24h=%d",
             (unsigned long)current_layout_, PRESET_THEMES[current_layout_].name, (unsigned long)theme_color_, (int)is_24h_format_);

    lv_lock();

    // Allocate navigation icon buffer (48x48 RGB565)
    nav_icon_buf_ = (uint8_t*)heap_caps_malloc(48 * 48 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nav_icon_buf_) nav_icon_buf_ = (uint8_t*)malloc(48 * 48 * 2);
    if (nav_icon_buf_) {
        const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
        const auto* synced_bg = GetCurrentSyncedBg();
        uint32_t card_hex = (synced_bg) ? synced_bg->panel_color : th.card_bg;
        uint16_t initial_bg = hex_to_rgb565(card_hex);
        for (int i = 0; i < 48 * 48; i++) {
            nav_icon_buf_[i * 2] = initial_bg & 0xFF;
            nav_icon_buf_[i * 2 + 1] = (initial_bg >> 8) & 0xFF;
        }
        memset(&nav_icon_dsc_, 0, sizeof(nav_icon_dsc_));
        nav_icon_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        nav_icon_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        nav_icon_dsc_.header.w = 48;
        nav_icon_dsc_.header.h = 48;
        nav_icon_dsc_.header.stride = 48 * 2;
        nav_icon_dsc_.data_size = 48 * 48 * 2;
        nav_icon_dsc_.data = nav_icon_buf_;
    }

    // Allocate QR code image buffer (140x140 RGB565)
    qr_img_buf_ = (uint8_t*)heap_caps_malloc(140 * 140 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!qr_img_buf_) qr_img_buf_ = (uint8_t*)malloc(140 * 140 * 2);
    if (qr_img_buf_) {
        memset(qr_img_buf_, 0xFF, 140 * 140 * 2); // default all white
        memset(&qr_img_dsc_, 0, sizeof(qr_img_dsc_));
        qr_img_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        qr_img_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        qr_img_dsc_.header.w = 140;
        qr_img_dsc_.header.h = 140;
        qr_img_dsc_.header.stride = 140 * 2;
        qr_img_dsc_.data_size = 140 * 140 * 2;
        qr_img_dsc_.data = qr_img_buf_;
    }

    createHomeScreen();
    createQrScreen();
    createNavScreen();
    createNotifScreen();
    createWeatherScreen();
    createMusicScreen();
    createStatusScreen();
    createAlertScreen();
    createNavbar();
    createBottomNav();

    applyColorMode();
    updateResolution();
    showHome();

    lv_unlock();
}

// ---- NAVIGATION ICON DRAWING (Google Maps style) ----
static inline void put_pixel(uint8_t* buf, int x, int y, uint16_t color) {
    if (x < 0 || x >= 48 || y < 0 || y >= 48) return;
    int idx = (y * 48 + x) * 2;
    buf[idx] = color & 0xFF;
    buf[idx + 1] = (color >> 8) & 0xFF;
}

static void draw_filled_circle(uint8_t* buf, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                put_pixel(buf, cx+dx, cy+dy, color);
}

// Modern arrow up
static void draw_arrow_up(uint8_t* buf, uint16_t color) {
    for (int y = 24; y <= 42; y++)
        for (int x = 21; x <= 26; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 24, 42, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            put_pixel(buf, x, 6 + i, color);
    }
}

// Modern arrow left
static void draw_arrow_left(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 12; x <= 34; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 34, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 6 + i, y, color);
    }
}

// Modern arrow right
static void draw_arrow_right(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 13; x <= 35; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 13, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 41 - i, y, color);
    }
}

// Modern slight left
static void draw_arrow_slight_left(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 32 - i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            put_pixel(buf, x + d, y, color);
            put_pixel(buf, x, y + d, color);
        }
    }
    draw_filled_circle(buf, 32, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 12 - i/2, cy = 14 - i/2;
        for (int d = -half; d <= half; d++)
            put_pixel(buf, cx + d, cy, color);
    }
}

// Modern slight right
static void draw_arrow_slight_right(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 15 + i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            put_pixel(buf, x + d, y, color);
            put_pixel(buf, x, y + d, color);
        }
    }
    draw_filled_circle(buf, 15, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 35 + i/2, cy = 14 - i/2;
        for (int d = -half; d <= half; d++)
            put_pixel(buf, cx + d, cy, color);
    }
}

// Modern roundabout
static void draw_roundabout(uint8_t* buf, uint16_t color) {
    int r = 14;
    for (int a = 0; a < 360; a += 2) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -1; t <= 1; t++) {
            int x = 24 + (int)((r + t) * cosf(rad));
            int y = 24 + (int)((r + t) * sinf(rad));
            put_pixel(buf, x, y, color);
        }
    }
    for (int y = 21; y <= 26; y++)
        for (int x = 30; x <= 42; x++)
            put_pixel(buf, x, y, color);
    for (int i = 0; i < 8; i++) {
        int half = 1 + (i * 3) / 8;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 42 - i, y, color);
    }
}

// Modern arrive (Google Maps pin)
static void draw_arrive(uint8_t* buf, uint16_t color, uint16_t cutout) {
    draw_filled_circle(buf, 24, 14, 9, color);
    draw_filled_circle(buf, 24, 14, 4, cutout);
    for (int i = 0; i < 16; i++) {
        int half = 9 - (i * 6) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            put_pixel(buf, x, 23 + i, color);
    }
    put_pixel(buf, 24, 39, color);
    put_pixel(buf, 24, 40, color);
}

// Modern car icon
static void draw_car(uint8_t* buf, uint16_t color, uint16_t cutout) {
    for (int y = 24; y <= 36; y++)
        for (int x = 10; x <= 37; x++)
            put_pixel(buf, x, y, color);
    for (int y = 16; y <= 24; y++)
        for (int x = 15; x <= 32; x++)
            put_pixel(buf, x, y, color);
    for (int y = 17; y <= 23; y++)
        for (int x = 16; x <= 22; x++)
            put_pixel(buf, x, y, cutout);
    for (int y = 17; y <= 23; y++)
        for (int x = 25; x <= 31; x++)
            put_pixel(buf, x, y, cutout);
    draw_filled_circle(buf, 15, 37, 4, color);
    draw_filled_circle(buf, 32, 37, 4, color);
    put_pixel(buf, 10, 28, 0xFFE0);
    put_pixel(buf, 10, 29, 0xFFE0);
    put_pixel(buf, 37, 28, 0xFFE0);
    put_pixel(buf, 37, 29, 0xFFE0);
}

// Modern U-turn arrow
static void draw_arrow_uturn(uint8_t* buf, uint16_t color) {
    for (int y = 20; y <= 40; y++)
        for (int x = 27; x <= 32; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 29, 40, 2, color);
    for (int a = 0; a <= 180; a += 3) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -2; t <= 2; t++) {
            int x = 20 + (int)((9 + t) * cosf(rad));
            int y = 20 - (int)((9 + t) * sinf(rad));
            put_pixel(buf, x, y, color);
        }
    }
    for (int y = 20; y <= 32; y++)
        for (int x = 9; x <= 14; x++)
            put_pixel(buf, x, y, color);
    for (int i = 0; i < 12; i++) {
        int half = 1 + (i * 7) / 12;
        for (int x = 11 - half; x <= 11 + half; x++)
            put_pixel(buf, x, 32 + i, color);
    }
}

void ChronchiDisplay::setNavIcon(const char* maneuver) {
    if (!nav_icon_buf_) return;
    if (maneuver) last_maneuver_ = maneuver;
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    uint16_t clear_color = hex_to_rgb565(th.card_bg);
    for (int i = 0; i < 48 * 48; i++) {
        nav_icon_buf_[i * 2] = clear_color & 0xFF;
        nav_icon_buf_[i * 2 + 1] = (clear_color >> 8) & 0xFF;
    }
    uint16_t fg = hex_to_rgb565(th.accent);
    if (fg == clear_color) {
        fg = light_mode_ ? 0x0290 : 0x07FF;
    }
    uint16_t cutout = clear_color;

    const char* m = last_maneuver_.c_str();
    if (strcmp(m, "STRAIGHT") == 0) draw_arrow_up(nav_icon_buf_, fg);
    else if (strcmp(m, "LEFT") == 0) draw_arrow_left(nav_icon_buf_, fg);
    else if (strcmp(m, "RIGHT") == 0) draw_arrow_right(nav_icon_buf_, fg);
    else if (strcmp(m, "SLIGHT_LEFT") == 0) draw_arrow_slight_left(nav_icon_buf_, fg);
    else if (strcmp(m, "SLIGHT_RIGHT") == 0) draw_arrow_slight_right(nav_icon_buf_, fg);
    else if (strcmp(m, "UTURN") == 0 || strcmp(m, "U_TURN") == 0 || strcmp(m, "PUTAR_BALIK") == 0) draw_arrow_uturn(nav_icon_buf_, fg);
    else if (strcmp(m, "ROUNDABOUT") == 0) draw_roundabout(nav_icon_buf_, fg);
    else if (strcmp(m, "ARRIVE") == 0) draw_arrive(nav_icon_buf_, fg, cutout);
    else draw_car(nav_icon_buf_, fg, cutout);

    if (nav_icon_img_) lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
}

// ---- UNIFIED CHRONCHI TOP NAVBAR (Displayed on every screen via lv_layer_top) ----
void ChronchiDisplay::createNavbar() {
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    chronchi_navbar_ = lv_obj_create(top_layer);
    lv_obj_set_size(chronchi_navbar_, width_, 34);
    lv_obj_align(chronchi_navbar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(chronchi_navbar_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(chronchi_navbar_, LV_OPA_90, 0);
    lv_obj_set_style_border_side(chronchi_navbar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(chronchi_navbar_, 1, 0);
    lv_obj_set_style_border_color(chronchi_navbar_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_radius(chronchi_navbar_, 0, 0);
    lv_obj_set_style_pad_all(chronchi_navbar_, 0, 0);
    lv_obj_set_style_pad_left(chronchi_navbar_, 4, 0);
    lv_obj_set_style_pad_right(chronchi_navbar_, 6, 0);
    lv_obj_set_flex_flow(chronchi_navbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chronchi_navbar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chronchi_navbar_, LV_OBJ_FLAG_SCROLLABLE);

    // Left group: XiaoZhi AI button + Brightness button
    lv_obj_t* left_box = lv_obj_create(chronchi_navbar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 3, 0);
    lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Rotation Button (Cyan circular rotation arrow - rotates 90 degrees)
    navbar_xz_btn_ = lv_button_create(left_box);
    lv_obj_set_size(navbar_xz_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_xz_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_xz_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_xz_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_xz_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_xz_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_xz_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_xz_btn_, 0, 0);
    lv_obj_t* rot_icon = lv_label_create(navbar_xz_btn_);
    lv_label_set_text(rot_icon, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(rot_icon, lv_color_hex(0x06B6D4), 0);
    lv_obj_center(rot_icon);
    lv_obj_add_event_cb(navbar_xz_btn_, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().RotateDisplay();
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Icon Button (Amber sun)
    navbar_br_btn_ = lv_button_create(left_box);
    lv_obj_set_size(navbar_br_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_br_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_br_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_br_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_br_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_br_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_br_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_br_btn_, 0, 0);
    lv_obj_t* br_lbl = lv_label_create(navbar_br_btn_);
    lv_label_set_text(br_lbl, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(br_lbl, lv_color_hex(0xF59E0B), 0);
    lv_obj_center(br_lbl);
    lv_obj_add_event_cb(navbar_br_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showBrightnessOverlay();
    }, LV_EVENT_CLICKED, this);

    // Center: Theme Color / Palette Button (Tap -> cycle watchface colors)
    navbar_th_btn_ = lv_button_create(chronchi_navbar_);
    lv_obj_set_size(navbar_th_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_th_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_th_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_th_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_th_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_th_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_th_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_th_btn_, 0, 0);
    navbar_th_lbl_ = lv_label_create(navbar_th_btn_);
    lv_label_set_text(navbar_th_lbl_, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(navbar_th_lbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_th_lbl_, lv_color_hex(0xFFE600), 0);
    lv_obj_center(navbar_th_lbl_);
    lv_obj_add_event_cb(navbar_th_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->cycleTheme();
    }, LV_EVENT_CLICKED, this);

    // Right group: Bluetooth -> Speaker/Volume -> Wi-Fi -> Battery Status
    lv_obj_t* right_box = lv_obj_create(chronchi_navbar_);
    lv_obj_set_size(right_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_box, 0, 0);
    lv_obj_set_style_pad_all(right_box, 0, 0);
    lv_obj_set_flex_flow(right_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_box, 3, 0);
    lv_obj_clear_flag(right_box, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Bluetooth Button in navbar (Tap -> QR pairing or status)
    navbar_bt_btn_ = lv_button_create(right_box);
    lv_obj_set_size(navbar_bt_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_bt_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_bt_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_bt_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_bt_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_bt_btn_, 0, 0);
    navbar_bt_label_ = lv_label_create(navbar_bt_btn_);
    lv_label_set_text(navbar_bt_label_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(navbar_bt_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_bt_label_, last_bt_connected_ ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    lv_obj_center(navbar_bt_label_);
    lv_obj_add_event_cb(navbar_bt_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            if (!disp->isBtConnected()) {
                disp->showQrPairing();
            } else {
                disp->setScreenState(STATUS);
            }
        }
    }, LV_EVENT_CLICKED, this);

    // 2. Speaker / Volume Button (Tap -> cycle volume)
    navbar_vol_btn_ = lv_button_create(right_box);
    lv_obj_set_size(navbar_vol_btn_, 24, 26);
    lv_obj_set_style_radius(navbar_vol_btn_, 8, 0);
    lv_obj_set_style_bg_opa(navbar_vol_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(navbar_vol_btn_, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(navbar_vol_btn_, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(navbar_vol_btn_, 0, 0);
    lv_obj_set_style_pad_all(navbar_vol_btn_, 0, 0);
    lv_obj_set_style_shadow_width(navbar_vol_btn_, 0, 0);
    navbar_vol_label_ = lv_label_create(navbar_vol_btn_);
    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 75;
    lv_label_set_text(navbar_vol_label_, cur_vol == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(navbar_vol_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_vol_label_, cur_vol == 0 ? lv_color_hex(0x71717A) : lv_color_hex(0x06B6D4), 0);
    lv_obj_center(navbar_vol_label_);
    lv_obj_add_event_cb(navbar_vol_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showVolumeOverlay();
    }, LV_EVENT_CLICKED, this);

    // 3. Wi-Fi Button hidden in Chronchi mode as requested
    navbar_wifi_btn_ = nullptr;
    navbar_wifi_label_ = nullptr;

    // 4. Battery Status (Formatted based on bat_style)
    navbar_batt_label_ = lv_label_create(right_box);
    Settings s("nova", true);
    int bat_style = s.GetInt("bat_style", 1);
    if (bat_style == 0) {
        lv_label_set_text(navbar_batt_label_, LV_SYMBOL_BATTERY_FULL);
    } else if (bat_style == 2) {
        lv_label_set_text(navbar_batt_label_, "100%");
    } else {
        lv_label_set_text(navbar_batt_label_, "100% " LV_SYMBOL_BATTERY_FULL);
    }
    lv_obj_set_style_text_font(navbar_batt_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(navbar_batt_label_, lv_color_hex(0x34D399), 0);
}

void ChronchiDisplay::updateResolution() {
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    }

    bool is_landscape = (width_ > height_);

    if (chronchi_navbar_) {
        lv_obj_set_width(chronchi_navbar_, width_);
        lv_obj_set_height(chronchi_navbar_, is_landscape ? 28 : 34);
    }
    if (chronchi_bottom_nav_) {
        lv_obj_set_width(chronchi_bottom_nav_, width_);
        lv_obj_set_height(chronchi_bottom_nav_, is_landscape ? 26 : 30);
        lv_obj_align(chronchi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (overlay_backdrop_) {
        lv_obj_set_size(overlay_backdrop_, width_, height_);
    }
    if (vol_overlay_card_) {
        lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, is_landscape ? 30 : 36);
    }
    if (bright_overlay_card_) {
        lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, is_landscape ? 30 : 36);
    }

    lv_obj_t* screens[] = {home_screen_, qr_screen_, nav_screen_, notif_screen_, weather_screen_, music_screen_, status_screen_, alert_screen_};
    for (lv_obj_t* scr : screens) {
        if (!scr) continue;
        lv_obj_set_size(scr, width_, height_);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_bottom(scr, 0, 0);
        lv_obj_scroll_to_y(scr, 0, LV_ANIM_OFF);
    }

    // 1. Home Screen
    if (home_time_card_) {
        lv_obj_set_size(home_time_card_, is_landscape ? 280 : 224, is_landscape ? 116 : 136);
        lv_obj_align(home_time_card_, LV_ALIGN_CENTER, 0, is_landscape ? -2 : -4);
    }
    if (home_date_card_) {
        lv_obj_set_width(home_date_card_, is_landscape ? 200 : 120);
        lv_obj_align(home_date_card_, LV_ALIGN_CENTER, 0, is_landscape ? 6 : 8);
    }

    // 2. Nav Screen (Google Maps Navigation)
    if (is_landscape) {
        if (nav_header_card_) {
            lv_obj_set_size(nav_header_card_, 304, 24);
            lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (nav_header_label_) lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 10, 0);
        if (nav_time_label_) lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -10, 0);

        if (nav_main_card_) {
            lv_obj_set_size(nav_main_card_, 156, 148);
            lv_obj_align(nav_main_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (nav_icon_img_) lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 6, 8);
        if (nav_dist_label_) lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 52, 6);
        if (nav_title_label_) {
            lv_obj_set_width(nav_title_label_, 98);
            lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 52, 32);
        }
        if (nav_street_label_) {
            lv_obj_set_width(nav_street_label_, 144);
            lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (nav_stats_card_) {
            lv_obj_set_size(nav_stats_card_, 142, 148);
            lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_LEFT, 170, 58);
        }
        if (nav_dur_label_) lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 10, 16);
        if (nav_eta_label_) lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_LEFT, 10, 58);
        if (nav_speed_label_) lv_obj_align(nav_speed_label_, LV_ALIGN_TOP_LEFT, 10, 100);
    } else {
        if (nav_header_card_) {
            lv_obj_set_size(nav_header_card_, 220, 26);
            lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 38);
        }
        if (nav_header_label_) lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 12, 0);
        if (nav_time_label_) lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -12, 0);

        if (nav_main_card_) {
            lv_obj_set_size(nav_main_card_, 220, 118);
            lv_obj_align(nav_main_card_, LV_ALIGN_TOP_MID, 0, 70);
        }
        if (nav_icon_img_) lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 10, 10);
        if (nav_dist_label_) lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 68, 8);
        if (nav_title_label_) {
            lv_obj_set_width(nav_title_label_, 142);
            lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 68, 36);
        }
        if (nav_street_label_) {
            lv_obj_set_width(nav_street_label_, 200);
            lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (nav_stats_card_) {
            lv_obj_set_size(nav_stats_card_, 220, 66);
            lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_MID, 0, 196);
        }
        if (nav_dur_label_) lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 12, 8);
        if (nav_eta_label_) lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_RIGHT, -12, 8);
        if (nav_speed_label_) lv_obj_align(nav_speed_label_, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    }

    // 3. Weather Screen
    if (is_landscape) {
        if (weather_header_card_) {
            lv_obj_set_size(weather_header_card_, 304, 24);
            lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (weather_main_card_) {
            lv_obj_set_size(weather_main_card_, 152, 148);
            lv_obj_align(weather_main_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (weather_temp_label_) lv_obj_align(weather_temp_label_, LV_ALIGN_TOP_LEFT, 12, 12);
        if (weather_cond_label_) {
            lv_obj_set_width(weather_cond_label_, 78);
            lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -8, 14);
        }
        if (weather_range_label_) {
            lv_obj_set_width(weather_range_label_, 136);
            lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_MID, 0, -12);
        }

        if (weather_stats_card_) {
            lv_obj_set_size(weather_stats_card_, 150, 88);
            lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_LEFT, 162, 58);
        }
        if (weather_humid_label_) lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 10, 14);
        if (weather_press_label_) lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 10, 48);

        if (weather_loc_card_) {
            lv_obj_set_size(weather_loc_card_, 150, 52);
            lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_LEFT, 162, 152);
        }
        if (weather_loc_label_) {
            lv_obj_set_width(weather_loc_label_, 140);
            lv_obj_center(weather_loc_label_);
        }
    } else {
        if (weather_header_card_) {
            lv_obj_set_size(weather_header_card_, 224, 26);
            lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (weather_main_card_) {
            lv_obj_set_size(weather_main_card_, 224, 80);
            lv_obj_align(weather_main_card_, LV_ALIGN_TOP_MID, 0, 68);
        }
        if (weather_temp_label_) lv_obj_align(weather_temp_label_, LV_ALIGN_LEFT_MID, 16, 0);
        if (weather_cond_label_) {
            lv_obj_set_width(weather_cond_label_, 110);
            lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -14, 14);
        }
        if (weather_range_label_) {
            lv_obj_set_width(weather_range_label_, 110);
            lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_RIGHT, -14, -14);
        }

        if (weather_stats_card_) {
            lv_obj_set_size(weather_stats_card_, 224, 72);
            lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_MID, 0, 154);
        }
        if (weather_humid_label_) lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 12, 12);
        if (weather_press_label_) lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 12, 42);

        if (weather_loc_card_) {
            lv_obj_set_size(weather_loc_card_, 224, 30);
            lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_MID, 0, 232);
        }
        if (weather_loc_label_) {
            lv_obj_set_width(weather_loc_label_, 200);
            lv_obj_center(weather_loc_label_);
        }
    }

    // 4. Music Screen
    if (is_landscape) {
        if (music_header_card_) {
            lv_obj_set_size(music_header_card_, 304, 24);
            lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (music_card_) {
            lv_obj_set_size(music_card_, 154, 148);
            lv_obj_align(music_card_, LV_ALIGN_TOP_LEFT, 8, 58);
        }
        if (music_title_label_) {
            lv_obj_set_width(music_title_label_, 138);
            lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 8, 8);
        }
        if (music_artist_label_) {
            lv_obj_set_width(music_artist_label_, 138);
            lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 8, 34);
        }
        if (music_recom_label_) {
            lv_obj_set_width(music_recom_label_, 138);
            lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 8, 60);
        }
        if (music_bar_) {
            lv_obj_set_width(music_bar_, 138);
            lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (music_controls_row_) {
            lv_obj_set_size(music_controls_row_, 142, 42);
            lv_obj_align(music_controls_row_, LV_ALIGN_TOP_LEFT, 170, 68);
        }
        if (music_prev_btn_) lv_obj_set_size(music_prev_btn_, 38, 38);
        if (music_play_btn_) lv_obj_set_size(music_play_btn_, 56, 38);
        if (music_next_btn_) lv_obj_set_size(music_next_btn_, 38, 38);

        if (music_vol_row_) {
            lv_obj_set_size(music_vol_row_, 142, 38);
            lv_obj_align(music_vol_row_, LV_ALIGN_TOP_LEFT, 170, 126);
        }
        if (music_vol_down_btn_) lv_obj_set_size(music_vol_down_btn_, 67, 34);
        if (music_vol_up_btn_) lv_obj_set_size(music_vol_up_btn_, 67, 34);
    } else {
        if (music_header_card_) {
            lv_obj_set_size(music_header_card_, 220, 26);
            lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 38);
        }
        if (music_card_) {
            lv_obj_set_size(music_card_, 220, 98);
            lv_obj_align(music_card_, LV_ALIGN_TOP_MID, 0, 70);
        }
        if (music_title_label_) {
            lv_obj_set_width(music_title_label_, 200);
            lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 10, 8);
        }
        if (music_artist_label_) {
            lv_obj_set_width(music_artist_label_, 200);
            lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 10, 30);
        }
        if (music_recom_label_) {
            lv_obj_set_width(music_recom_label_, 200);
            lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 10, 52);
        }
        if (music_bar_) {
            lv_obj_set_width(music_bar_, 200);
            lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -8);
        }

        if (music_controls_row_) {
            lv_obj_set_size(music_controls_row_, 220, 40);
            lv_obj_align(music_controls_row_, LV_ALIGN_TOP_MID, 0, 176);
        }
        if (music_prev_btn_) lv_obj_set_size(music_prev_btn_, 60, 38);
        if (music_play_btn_) lv_obj_set_size(music_play_btn_, 86, 38);
        if (music_next_btn_) lv_obj_set_size(music_next_btn_, 60, 38);

        if (music_vol_row_) {
            lv_obj_set_size(music_vol_row_, 220, 34);
            lv_obj_align(music_vol_row_, LV_ALIGN_TOP_MID, 0, 224);
        }
        if (music_vol_down_btn_) lv_obj_set_size(music_vol_down_btn_, 105, 32);
        if (music_vol_up_btn_) lv_obj_set_size(music_vol_up_btn_, 105, 32);
    }

    // 5. Status Screen
    if (is_landscape) {
        if (status_header_card_) {
            lv_obj_set_size(status_header_card_, 304, 24);
            lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (status_bat_card_) {
            lv_obj_set_size(status_bat_card_, 150, 66);
            lv_obj_align(status_bat_card_, LV_ALIGN_TOP_LEFT, 8, 56);
        }
        if (status_phone_bat_title_) lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 8, 8);
        if (status_phone_bat_label_) lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -8, 8);
        if (status_bat_bar_) {
            lv_obj_set_width(status_bat_bar_, 134);
            lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (status_net_card_) {
            lv_obj_set_size(status_net_card_, 150, 82);
            lv_obj_align(status_net_card_, LV_ALIGN_TOP_LEFT, 8, 126);
        }
        if (status_network_label_) lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 8, 6);
        if (status_signal_label_) lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 8, 26);
        if (status_net_hint_label_) lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 8, 46);

        if (status_find_phone_btn_) {
            lv_obj_set_size(status_find_phone_btn_, 150, 66);
            lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_LEFT, 162, 56);
        }
        if (status_bt_card_) {
            lv_obj_set_size(status_bt_card_, 150, 82);
            lv_obj_align(status_bt_card_, LV_ALIGN_TOP_LEFT, 162, 126);
        }
    } else {
        if (status_header_card_) {
            lv_obj_set_size(status_header_card_, 224, 26);
            lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (status_bat_card_) {
            lv_obj_set_size(status_bat_card_, 224, 62);
            lv_obj_align(status_bat_card_, LV_ALIGN_TOP_MID, 0, 66);
        }
        if (status_phone_bat_title_) lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 12, 10);
        if (status_phone_bat_label_) lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -12, 10);
        if (status_bat_bar_) {
            lv_obj_set_width(status_bat_bar_, 200);
            lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        if (status_net_card_) {
            lv_obj_set_size(status_net_card_, 224, 66);
            lv_obj_align(status_net_card_, LV_ALIGN_TOP_MID, 0, 132);
        }
        if (status_network_label_) lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 12, 8);
        if (status_signal_label_) lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 12, 28);
        if (status_net_hint_label_) lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 12, 48);

        if (status_find_phone_btn_) {
            lv_obj_set_size(status_find_phone_btn_, 224, 36);
            lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_MID, 0, 202);
        }
        if (status_bt_card_) {
            lv_obj_set_size(status_bt_card_, 224, 32);
            lv_obj_align(status_bt_card_, LV_ALIGN_TOP_MID, 0, 242);
        }
    }

    // 6. Notification Screen
    if (is_landscape) {
        if (notif_header_badge_) {
            lv_obj_set_size(notif_header_badge_, 304, 24);
            lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (notif_card_) {
            lv_obj_set_size(notif_card_, 304, 148);
            lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 58);
        }
        if (notif_title_label_) lv_obj_set_width(notif_title_label_, 284);
        if (notif_line_) lv_obj_set_width(notif_line_, 284);
        if (notif_msg_label_) lv_obj_set_width(notif_msg_label_, 284);
        if (notif_icon_label_) lv_obj_align(notif_icon_label_, LV_ALIGN_BOTTOM_MID, 0, -4);
    } else {
        if (notif_header_badge_) {
            lv_obj_set_size(notif_header_badge_, 216, 28);
            lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (notif_card_) {
            lv_obj_set_size(notif_card_, 216, 170);
            lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 68);
        }
        if (notif_title_label_) lv_obj_set_width(notif_title_label_, 196);
        if (notif_line_) lv_obj_set_width(notif_line_, 196);
        if (notif_msg_label_) lv_obj_set_width(notif_msg_label_, 196);
        if (notif_icon_label_) lv_obj_align(notif_icon_label_, LV_ALIGN_TOP_MID, 0, 248);
    }

    // 7. Alert Screen
    if (alert_card_) {
        lv_obj_set_size(alert_card_, is_landscape ? 280 : 216, is_landscape ? 150 : 180);
        lv_obj_align(alert_card_, LV_ALIGN_CENTER, 0, 0);
    }
    if (alert_msg_label_) lv_obj_set_width(alert_msg_label_, is_landscape ? 254 : 190);
    if (alert_btn_) lv_obj_set_width(alert_btn_, is_landscape ? 254 : 190);

    // 8. QR Screen
    if (is_landscape) {
        if (qr_card_) {
            lv_obj_set_size(qr_card_, 142, 142);
            lv_obj_align(qr_card_, LV_ALIGN_TOP_LEFT, 14, 32);
        }
        if (qr_header_card_) {
            lv_obj_set_size(qr_header_card_, 142, 26);
            lv_obj_align(qr_header_card_, LV_ALIGN_TOP_LEFT, 170, 32);
        }
        if (qr_info_card_) {
            lv_obj_set_size(qr_info_card_, 142, 110);
            lv_obj_align(qr_info_card_, LV_ALIGN_TOP_LEFT, 170, 64);
        }
        if (qr_name_label_) lv_obj_set_width(qr_name_label_, 138);
        if (qr_mac_label_) lv_obj_set_width(qr_mac_label_, 138);
        if (qr_clock_btn_) lv_obj_add_flag(qr_clock_btn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (qr_header_card_) {
            lv_obj_set_size(qr_header_card_, 220, 24);
            lv_obj_align(qr_header_card_, LV_ALIGN_TOP_MID, 0, 36);
        }
        if (qr_card_) {
            lv_obj_set_size(qr_card_, 142, 142);
            lv_obj_align(qr_card_, LV_ALIGN_TOP_MID, 0, 63);
        }
        if (qr_info_card_) {
            lv_obj_set_size(qr_info_card_, 224, 36);
            lv_obj_align(qr_info_card_, LV_ALIGN_TOP_MID, 0, 207);
        }
        if (qr_name_label_) lv_obj_set_width(qr_name_label_, 220);
        if (qr_mac_label_) lv_obj_set_width(qr_mac_label_, 220);
        if (qr_clock_btn_) lv_obj_clear_flag(qr_clock_btn_, LV_OBJ_FLAG_HIDDEN);
    }

    if (recent_apps_modal_) {
        ShowRecentAppsModal();
    }
    lv_obj_invalidate(lv_layer_top());
    lv_obj_invalidate(lv_screen_active());
}

// ---- UNIFIED CHRONCHI BOTTOM NAVIGATION BAR (Layaknya HP: Back, Home, Recent Apps) ----
void ChronchiDisplay::createBottomNav() {
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    chronchi_bottom_nav_ = lv_obj_create(top_layer);
    lv_obj_set_size(chronchi_bottom_nav_, width_, 30);
    lv_obj_align(chronchi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chronchi_bottom_nav_, lv_color_hex(0x0A0F1D), 0);
    lv_obj_set_style_bg_opa(chronchi_bottom_nav_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chronchi_bottom_nav_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_side(chronchi_bottom_nav_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(chronchi_bottom_nav_, 1, 0);
    lv_obj_set_style_radius(chronchi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_all(chronchi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_left(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_right(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_column(chronchi_bottom_nav_, 6, 0);
    lv_obj_set_flex_flow(chronchi_bottom_nav_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chronchi_bottom_nav_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chronchi_bottom_nav_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Back Button (◀)
    lv_obj_t* back_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(back_btn, 26);
    lv_obj_set_flex_grow(back_btn, 1);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(back_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(back_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            if (disp->IsRecentAppsVisible()) {
                disp->CloseRecentAppsModal();
                return;
            }
            if (disp->getScreenState() != HOME) {
                disp->setScreenState(HOME);
                return;
            }
        }
        Board::GetInstance().SwitchToMode(2); // Kembali ke Nova Mini OS
    }, LV_EVENT_CLICKED, this);

    // 2. Home Button (●)
    lv_obj_t* home_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(home_btn, 26);
    lv_obj_set_flex_grow(home_btn, 1);
    lv_obj_set_style_radius(home_btn, 6, 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(home_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(home_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(home_btn, 0, 0);
    lv_obj_set_style_pad_all(home_btn, 0, 0);
    lv_obj_set_style_shadow_width(home_btn, 0, 0);
    lv_obj_t* home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, MATERIAL_SYMBOLS_HOME);
    lv_obj_set_style_text_font(home_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(home_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(home_lbl);
    lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().SwitchToMode(2); // Langsung ke Beranda Nova Mini OS
    }, LV_EVENT_CLICKED, nullptr);

    // 3. Recent Apps Button (▢)
    lv_obj_t* recent_btn = lv_button_create(chronchi_bottom_nav_);
    lv_obj_set_height(recent_btn, 26);
    lv_obj_set_flex_grow(recent_btn, 1);
    lv_obj_set_style_radius(recent_btn, 6, 0);
    lv_obj_set_style_bg_opa(recent_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(recent_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(recent_btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(recent_btn, 0, 0);
    lv_obj_set_style_pad_all(recent_btn, 0, 0);
    lv_obj_set_style_shadow_width(recent_btn, 0, 0);

    lv_obj_t* sq_shape = lv_obj_create(recent_btn);
    lv_obj_set_size(sq_shape, 14, 14);
    lv_obj_center(sq_shape);
    lv_obj_clear_flag(sq_shape, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(sq_shape, 3, 0);
    lv_obj_set_style_bg_opa(sq_shape, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sq_shape, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_border_width(sq_shape, 2, 0);
    lv_obj_set_style_pad_all(sq_shape, 0, 0);
    lv_obj_set_style_shadow_width(sq_shape, 0, 0);

    lv_obj_add_event_cb(recent_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            disp->ShowRecentAppsModal();
        }
    }, LV_EVENT_CLICKED, this);
}

void ChronchiDisplay::hideOverlays() {
    if (overlay_backdrop_) lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_) lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_) lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
}

void ChronchiDisplay::showVolumeOverlay() {
    lv_lock();
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) {
        lv_unlock();
        return;
    }

    if (!overlay_backdrop_) {
        overlay_backdrop_ = lv_obj_create(top_layer);
        lv_obj_set_size(overlay_backdrop_, width_, height_);
        lv_obj_set_pos(overlay_backdrop_, 0, 0);
        lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
        lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
        lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
        lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay_backdrop_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(overlay_backdrop_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->hideOverlays();
        }, LV_EVENT_CLICKED, this);
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x141E33;
    uint32_t accent_hex = synced ? synced->accent_color : 0x00E5FF;

    if (!vol_overlay_card_) {
        vol_overlay_card_ = lv_obj_create(top_layer);
        lv_obj_set_size(vol_overlay_card_, 120, 195);
        lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, (width_ > height_) ? 30 : 36);
        lv_obj_set_style_radius(vol_overlay_card_, 14, 0);
        lv_obj_set_style_bg_opa(vol_overlay_card_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(vol_overlay_card_, 2, 0);
        lv_obj_set_style_pad_all(vol_overlay_card_, 8, 0);
        lv_obj_set_style_pad_row(vol_overlay_card_, 4, 0);
        lv_obj_set_flex_flow(vol_overlay_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(vol_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(vol_overlay_card_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(vol_overlay_card_, LV_OBJ_FLAG_SCROLLABLE);

        // Header: Icon + Value
        lv_obj_t* vol_hdr = lv_obj_create(vol_overlay_card_);
        lv_obj_set_size(vol_hdr, 104, 26);
        lv_obj_set_style_bg_opa(vol_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(vol_hdr, 0, 0);
        lv_obj_set_style_pad_all(vol_hdr, 0, 0);
        lv_obj_set_flex_flow(vol_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(vol_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(vol_hdr, 6, 0);

        vol_overlay_icon_ = lv_label_create(vol_hdr);
        lv_label_set_text(vol_overlay_icon_, MATERIAL_SYMBOLS_VOLUME_UP);
        lv_obj_set_style_text_font(vol_overlay_icon_, &font_material_symbols_20_4, 0);

        auto codec = Board::GetInstance().GetAudioCodec();
        int cur_vol = codec ? codec->output_volume() : 75;

        vol_overlay_label_ = lv_label_create(vol_hdr);
        lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
        lv_obj_set_style_text_font(vol_overlay_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vol_overlay_label_, lv_color_white(), 0);

        // Large Vertical Slider: 46px wide x 120px tall
        vol_overlay_slider_ = lv_slider_create(vol_overlay_card_);
        lv_obj_set_size(vol_overlay_slider_, 46, 120);
        lv_slider_set_range(vol_overlay_slider_, 0, 100);
        lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
        lv_obj_set_style_radius(vol_overlay_slider_, 10, 0);
        lv_obj_set_style_radius(vol_overlay_slider_, 10, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(vol_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(vol_overlay_slider_, 0, LV_PART_KNOB);

        lv_obj_add_event_cb(vol_overlay_slider_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto c = Board::GetInstance().GetAudioCodec();
            if (c) c->SetOutputVolume(val);
            Settings s("audio", true);
            s.SetInt("output_volume", val);
            s.SetInt("volume", val);
            Settings nova_s("nova", true);
            nova_s.SetInt("mute", val == 0 ? 1 : 0);
            if (disp && disp->vol_overlay_label_) {
                lv_label_set_text_fmt(disp->vol_overlay_label_, "%d%%", val);
            }
            if (disp && disp->vol_overlay_icon_) {
                lv_label_set_text(disp->vol_overlay_icon_,
                    val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
            }
            if (disp && disp->navbar_vol_label_) {
                lv_label_set_text(disp->navbar_vol_label_, val == 0 ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
                lv_obj_set_style_text_color(disp->navbar_vol_label_, val == 0 ? lv_color_hex(0x71717A) : lv_color_hex(0x06B6D4), 0);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* vol_hint = lv_label_create(vol_overlay_card_);
        lv_label_set_text(vol_hint, "▲ Geser Ke Atas/Bawah ▼");
        lv_obj_set_style_text_color(vol_hint, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(vol_hint, &lv_font_montserrat_14, 0);
    }

    // Refresh dynamic theme colors
    lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(border_hex), 0);
    if (vol_overlay_slider_) {
        lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(accent_hex), LV_PART_INDICATOR);
    }
    if (vol_overlay_icon_) {
        lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(accent_hex), 0);
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 75;
    if (vol_overlay_slider_) lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    if (vol_overlay_label_) lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (vol_overlay_icon_) {
        lv_label_set_text(vol_overlay_icon_, cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    }

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(vol_overlay_card_);
    lv_unlock();
}

void ChronchiDisplay::showBrightnessOverlay() {
    lv_lock();
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) {
        lv_unlock();
        return;
    }

    if (!overlay_backdrop_) {
        overlay_backdrop_ = lv_obj_create(top_layer);
        lv_obj_set_size(overlay_backdrop_, width_, height_);
        lv_obj_set_pos(overlay_backdrop_, 0, 0);
        lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
        lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
        lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
        lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
        lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
        lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay_backdrop_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(overlay_backdrop_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->hideOverlays();
        }, LV_EVENT_CLICKED, this);
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x141E33;

    if (!bright_overlay_card_) {
        bright_overlay_card_ = lv_obj_create(top_layer);
        lv_obj_set_size(bright_overlay_card_, 120, 195);
        lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, (width_ > height_) ? 30 : 36);
        lv_obj_set_style_radius(bright_overlay_card_, 14, 0);
        lv_obj_set_style_bg_opa(bright_overlay_card_, LV_OPA_90, 0);
        lv_obj_set_style_border_width(bright_overlay_card_, 2, 0);
        lv_obj_set_style_pad_all(bright_overlay_card_, 8, 0);
        lv_obj_set_style_pad_row(bright_overlay_card_, 4, 0);
        lv_obj_set_flex_flow(bright_overlay_card_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(bright_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(bright_overlay_card_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(bright_overlay_card_, LV_OBJ_FLAG_SCROLLABLE);

        // Header: Icon + Value
        lv_obj_t* br_hdr = lv_obj_create(bright_overlay_card_);
        lv_obj_set_size(br_hdr, 104, 26);
        lv_obj_set_style_bg_opa(br_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(br_hdr, 0, 0);
        lv_obj_set_style_pad_all(br_hdr, 0, 0);
        lv_obj_set_flex_flow(br_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(br_hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(br_hdr, 6, 0);

        lv_obj_t* br_icon = lv_label_create(br_hdr);
        lv_label_set_text(br_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
        lv_obj_set_style_text_font(br_icon, &font_material_symbols_20_4, 0);
        lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

        Settings disp_set("display", true);
        int cur_br = disp_set.GetInt("brightness", 70);

        bright_overlay_label_ = lv_label_create(br_hdr);
        lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);
        lv_obj_set_style_text_font(bright_overlay_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(bright_overlay_label_, lv_color_white(), 0);

        // Large Vertical Slider: 46px wide x 120px tall
        bright_overlay_slider_ = lv_slider_create(bright_overlay_card_);
        lv_obj_set_size(bright_overlay_slider_, 46, 120);
        lv_slider_set_range(bright_overlay_slider_, 10, 100);
        lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
        lv_obj_set_style_radius(bright_overlay_slider_, 10, 0);
        lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
        lv_obj_set_style_radius(bright_overlay_slider_, 10, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bright_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(bright_overlay_slider_, 0, LV_PART_KNOB);

        lv_obj_add_event_cb(bright_overlay_slider_, [](lv_event_t* e) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            int val = lv_slider_get_value(slider);
            auto bl = Board::GetInstance().GetBacklight();
            if (bl) bl->SetBrightness(val);
            Settings s("display", true);
            s.SetInt("brightness", val);
            Settings nova_s("nova", true);
            nova_s.SetInt("bright", (val * 255) / 100);
            if (disp && disp->bright_overlay_label_) {
                lv_label_set_text_fmt(disp->bright_overlay_label_, "%d%%", val);
            }
        }, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* br_hint = lv_label_create(bright_overlay_card_);
        lv_label_set_text(br_hint, "▲ Geser Ke Atas/Bawah ▼");
        lv_obj_set_style_text_color(br_hint, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_text_font(br_hint, &lv_font_montserrat_14, 0);
    }

    // Refresh dynamic theme colors
    lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(border_hex), 0);
    if (bright_overlay_slider_) {
        lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(panel2_hex), 0);
    }

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);
    if (bright_overlay_slider_) lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    if (bright_overlay_label_) lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(bright_overlay_card_);
    lv_unlock();
}

void ChronchiDisplay::CloseRecentAppsModal() {
    if (recent_apps_modal_ != nullptr) {
        lv_obj_del(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
}

void ChronchiDisplay::ShowRecentAppsModal() {
    CloseRecentAppsModal();
    hideOverlays();

    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x1E293B;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x0D1728;
    uint32_t accent_hex = synced ? synced->accent_color : 0x06B6D4;

    // 1. Full-screen dimmed backdrop
    recent_apps_modal_ = lv_obj_create(top_layer);
    lv_obj_set_pos(recent_apps_modal_, 0, 0);
    lv_obj_set_size(recent_apps_modal_, width_, height_);
    lv_obj_clear_flag(recent_apps_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(recent_apps_modal_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(recent_apps_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(recent_apps_modal_, 0, 0);
    lv_obj_set_style_radius(recent_apps_modal_, 0, 0);
    lv_obj_set_style_pad_all(recent_apps_modal_, 0, 0);

    // 2. Centered Floating Modal Card
    int card_w = (width_ > 240) ? 280 : 226;
    int card_h = (height_ < 260) ? (height_ - 16) : 252;
    lv_obj_t* card = lv_obj_create(recent_apps_modal_);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header Title
    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, "APLIKASI TERBUKA");
    lv_obj_set_pos(title_lbl, 8, 4);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(accent_hex), 0);

    lv_obj_t* sub_lbl = lv_label_create(card);
    lv_label_set_text(sub_lbl, "Ketuk untuk beralih mode instan");
    lv_obj_set_pos(sub_lbl, 8, 22);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x64748B), 0);

    // Close button (X)
    lv_obj_t* close_btn = lv_button_create(card);
    lv_obj_set_pos(close_btn, card_w - 34, 2);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, &font_material_symbols_20_4, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->CloseRecentAppsModal();
    }, LV_EVENT_CLICKED, this);

    // Scrollable container for App Cards
    int list_h = (card_h < 250) ? (card_h - 78) : 160;
    lv_obj_t* list = lv_obj_create(card);
    lv_obj_set_pos(list, 4, 38);
    lv_obj_set_size(list, card_w - 20, list_h);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    struct AppCardItem {
        const char* name;
        const char* desc;
        const char* icon;
        lv_color_t color;
        int action; // 0: xz, 1: watch (close), 2: nova os
    };

    const AppCardItem items[] = {
        {"Nova Mini OS", "Beranda & Dashboard HP", MATERIAL_SYMBOLS_HOME, lv_color_hex(0x06B6D4), 2},
        {"Xiaozhi AI", "Asisten Suara & Musik LLM", MATERIAL_SYMBOLS_ROBOT_2, lv_color_hex(0x22C55E), 0},
        {"Chronchi Watch", "Smartwatch & Jam [Aktif]", MATERIAL_SYMBOLS_WATCH, lv_color_hex(0xF59E0B), 1}
    };

    const int total_items = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < total_items; ++i) {
        lv_obj_t* app_card = lv_button_create(list);
        lv_obj_set_pos(app_card, 0, i * 44);
        lv_obj_set_size(app_card, card_w - 26, 38);
        lv_obj_set_style_bg_color(app_card, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_border_color(app_card, lv_color_hex(border_hex), 0);
        lv_obj_set_style_border_width(app_card, 1, 0);
        lv_obj_set_style_radius(app_card, 10, 0);
        lv_obj_set_style_pad_all(app_card, 0, 0);

        // App Icon
        lv_obj_t* icon_lbl = lv_label_create(app_card);
        lv_label_set_text(icon_lbl, items[i].icon);
        lv_obj_set_style_text_font(icon_lbl, &font_material_symbols_20_4, 0);
        lv_obj_set_style_text_color(icon_lbl, items[i].color, 0);
        lv_obj_set_pos(icon_lbl, 8, 9);

        // Titles
        lv_obj_t* c_title = lv_label_create(app_card);
        lv_label_set_text(c_title, items[i].name);
        lv_obj_set_pos(c_title, 36, 4);
        lv_obj_set_size(c_title, card_w - 92, 17);
        lv_label_set_long_mode(c_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_title, lv_color_white(), 0);

        lv_obj_t* c_desc = lv_label_create(app_card);
        lv_label_set_text(c_desc, items[i].desc);
        lv_obj_set_pos(c_desc, 36, 20);
        lv_obj_set_size(c_desc, card_w - 92, 14);
        lv_label_set_long_mode(c_desc, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_desc, lv_color_hex(0x64748B), 0);

        // Right arrow
        lv_obj_t* arr = lv_label_create(app_card);
        lv_label_set_text(arr, ">");
        lv_obj_set_pos(arr, card_w - 48, 10);
        lv_obj_set_style_text_color(arr, items[i].color, 0);

        lv_obj_set_user_data(app_card, (void*)(intptr_t)items[i].action);
        lv_obj_add_event_cb(app_card, [](lv_event_t* ev) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(ev));
            if (!disp) return;
            int act = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
            disp->CloseRecentAppsModal();
            if (act == 0) {
                Board::GetInstance().SwitchToMode(0);
            } else if (act == 1) {
                // Already in Watch
            } else if (act == 2) {
                Board::GetInstance().SwitchToMode(2);
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Bottom Button: KEMBALI KE BERANDA NOVA
    lv_obj_t* home_btn_bar = lv_button_create(card);
    lv_obj_set_pos(home_btn_bar, 4, card_h - 40);
    lv_obj_set_size(home_btn_bar, card_w - 22, 30);
    lv_obj_set_style_bg_color(home_btn_bar, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(home_btn_bar, lv_color_hex(accent_hex), 0);
    lv_obj_set_style_border_width(home_btn_bar, 1, 0);
    lv_obj_set_style_radius(home_btn_bar, 8, 0);
    lv_obj_set_style_pad_all(home_btn_bar, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(home_btn_bar);
    lv_label_set_text(btn_lbl, "KEMBALI KE BERANDA NOVA");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(home_btn_bar, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) {
            disp->CloseRecentAppsModal();
            Board::GetInstance().SwitchToMode(2);
        }
    }, LV_EVENT_CLICKED, this);
}

// ---- BORDERLESS AUTHENTIC CHRONCHI WATCHFACE ----
void ChronchiDisplay::createHomeScreen() {
    home_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(home_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(home_screen_, width_, height_);
    lv_obj_set_style_bg_opa(home_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_screen_, 0, 0);
    lv_obj_set_style_radius(home_screen_, 0, 0);
    lv_obj_set_style_pad_all(home_screen_, 0, 0);
    lv_obj_clear_flag(home_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Watchface Minimalis Chronos:
    // Tampilan simple, bersih, elegan: Jam:Menit:Detik + Hari, Tanggal Bulan Tahun
    home_time_card_ = create_card(home_screen_, 224, 136, 16);
    lv_obj_align(home_time_card_, LV_ALIGN_CENTER, 0, -4);

    home_time_label_ = lv_label_create(home_time_card_);
    lv_label_set_text(home_time_label_, "00:00:00");
    lv_obj_set_style_text_font(home_time_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(home_time_label_, LV_ALIGN_CENTER, 0, -20);

    // Garis aksen tipis elegan pembatas jam dan tanggal
    home_date_card_ = lv_obj_create(home_time_card_);
    lv_obj_set_size(home_date_card_, 120, 2);
    lv_obj_align(home_date_card_, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_opa(home_date_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_date_card_, 0, 0);
    lv_obj_clear_flag(home_date_card_, LV_OBJ_FLAG_SCROLLABLE);

    home_date_label_ = lv_label_create(home_time_card_);
    lv_label_set_text(home_date_label_, "--, -- --- ----");
    lv_obj_set_style_text_font(home_date_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(home_date_label_, LV_ALIGN_CENTER, 0, 26);

    // Layar / kartu jam bersih dari single click agar gestur double-tap layar utama responsif
    lv_obj_add_flag(home_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    lv_obj_add_flag(home_time_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_time_card_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Bersihkan pointer widget yang dihilangkan agar tidak ada sisa elemen
    home_chronchi_card_ = nullptr;
    home_chronchi_label_ = nullptr;
    home_bt_card_ = nullptr;
    home_bt_label_ = nullptr;
    home_batt_card_ = nullptr;
    home_batt_label_ = nullptr;
    home_batt_bar_ = nullptr;
    home_batt_bar_fill_ = nullptr;
    home_status_card_ = nullptr;
    home_status_label_ = nullptr;
}

// ---- QR PAIRING / BARCODE SCREEN ----
void ChronchiDisplay::createQrScreen() {
    qr_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(qr_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(qr_screen_, width_, height_);
    lv_obj_set_style_bg_opa(qr_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_screen_, 0, 0);
    lv_obj_set_style_radius(qr_screen_, 0, 0);
    lv_obj_set_style_pad_all(qr_screen_, 0, 0);
    lv_obj_clear_flag(qr_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Header Capsule (below the 34px top navbar)
    qr_header_card_ = create_card(qr_screen_, 220, 24, 12);
    lv_obj_align(qr_header_card_, LV_ALIGN_TOP_MID, 0, 36);
    qr_header_label_ = lv_label_create(qr_header_card_);
    lv_label_set_text(qr_header_label_, LV_SYMBOL_BLUETOOTH " Scan QR Chronos");
    lv_obj_set_style_text_font(qr_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(qr_header_label_);

    // 2. White Card Container for QR Code / Barcode (High contrast for easy camera scan)
    qr_card_ = lv_obj_create(qr_screen_);
    lv_obj_set_size(qr_card_, 142, 142);
    lv_obj_align(qr_card_, LV_ALIGN_TOP_MID, 0, 63);
    lv_obj_set_style_radius(qr_card_, 10, 0);
    lv_obj_set_style_bg_color(qr_card_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_card_, 2, 0);
    lv_obj_set_style_border_color(qr_card_, lv_color_hex(0x06B6D4), 0);
    lv_obj_set_style_pad_all(qr_card_, 4, 0);
    lv_obj_clear_flag(qr_card_, LV_OBJ_FLAG_SCROLLABLE);

    qr_code_obj_ = lv_image_create(qr_card_);
    if (qr_img_buf_) {
        lv_image_set_src(qr_code_obj_, &qr_img_dsc_);
    }
    lv_obj_center(qr_code_obj_);
    renderQrCode(qr_data_.c_str());

    // 3. Info Container (Device Name & MAC Address)
    qr_info_card_ = lv_obj_create(qr_screen_);
    lv_obj_set_size(qr_info_card_, 224, 36);
    lv_obj_align(qr_info_card_, LV_ALIGN_TOP_MID, 0, 207);
    lv_obj_set_style_bg_opa(qr_info_card_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qr_info_card_, 0, 0);
    lv_obj_set_style_pad_all(qr_info_card_, 0, 0);
    lv_obj_set_flex_flow(qr_info_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_info_card_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(qr_info_card_, 1, 0);
    lv_obj_clear_flag(qr_info_card_, LV_OBJ_FLAG_SCROLLABLE);

    qr_name_label_ = lv_label_create(qr_info_card_);
    char n_buf[64];
    snprintf(n_buf, sizeof(n_buf), "Nama BLE: %s", ble_device_name_.c_str());
    lv_label_set_text(qr_name_label_, n_buf);
    lv_obj_set_style_text_font(qr_name_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qr_name_label_, lv_color_hex(0x38BDF8), 0);

    qr_mac_label_ = lv_label_create(qr_info_card_);
    char m_buf[64];
    snprintf(m_buf, sizeof(m_buf), "MAC: %s", ble_mac_address_.c_str());
    lv_label_set_text(qr_mac_label_, m_buf);
    lv_obj_set_style_text_font(qr_mac_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qr_mac_label_, lv_color_hex(0x94A3B8), 0);

    // 4. Quick Clock Button (strictly above bottom navbar at y=284)
    qr_clock_btn_ = lv_button_create(qr_screen_);
    lv_obj_set_size(qr_clock_btn_, 140, 26);
    lv_obj_align(qr_clock_btn_, LV_ALIGN_TOP_MID, 0, 246);
    lv_obj_set_style_radius(qr_clock_btn_, 13, 0);
    lv_obj_set_style_bg_color(qr_clock_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(qr_clock_btn_, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(qr_clock_btn_);
    lv_label_set_text(btn_lbl, "🕒 Lihat Jam");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(qr_clock_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp) disp->showHome();
    }, LV_EVENT_CLICKED, this);
}

void ChronchiDisplay::renderQrCode(const char* text) {
    if (!qr_img_buf_ || !text || strlen(text) == 0) return;

    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    bool ok = qrcodegen_encodeText(text, tempBuffer, qrcode,
                                   qrcodegen_Ecc_LOW, 1, 10,
                                   qrcodegen_Mask_AUTO, true);
    if (!ok) {
        ESP_LOGE(TAG, "Failed to encode QR code for: %s", text);
        return;
    }

    int qr_size = qrcodegen_getSize(qrcode);
    int target_w = 140;
    int target_h = 140;

    // Fill background with pure white (0xFFFF in RGB565)
    uint16_t* pixels = (uint16_t*)qr_img_buf_;
    for (int i = 0; i < target_w * target_h; i++) {
        pixels[i] = 0xFFFF;
    }

    // Border / Quiet zone: 2 modules
    int border = 2;
    int total_modules = qr_size + border * 2;
    int scale = target_w / total_modules;
    if (scale < 1) scale = 1;
    int offset_x = (target_w - (total_modules * scale)) / 2 + border * scale;
    int offset_y = (target_h - (total_modules * scale)) / 2 + border * scale;

    // Draw black modules (0x0000 in RGB565)
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                int px_start = offset_x + x * scale;
                int py_start = offset_y + y * scale;
                for (int dy = 0; dy < scale; dy++) {
                    int py = py_start + dy;
                    if (py < 0 || py >= target_h) continue;
                    for (int dx = 0; dx < scale; dx++) {
                        int px = px_start + dx;
                        if (px < 0 || px >= target_w) continue;
                        pixels[py * target_w + px] = 0x0000;
                    }
                }
            }
        }
    }

    if (qr_code_obj_) {
        lv_image_set_src(qr_code_obj_, &qr_img_dsc_);
        lv_obj_invalidate(qr_code_obj_);
    }
}

void ChronchiDisplay::showQrPairing() {
    lv_lock();
    if (qr_screen_) {
        setScreenState(QR_PAIRING);
        ESP_LOGI(TAG, "Showing Chronchi QR Code / Barcode Pairing Screen");
    }
    lv_unlock();
}

void ChronchiDisplay::setBleInfo(const char* name, const char* mac, const char* qr_data) {
    if (name && strlen(name) > 0) ble_device_name_ = name;
    if (mac && strlen(mac) > 0) ble_mac_address_ = mac;
    if (qr_data && strlen(qr_data) > 0) qr_data_ = qr_data;

    lv_lock();
    if (qr_name_label_ && lv_obj_is_valid(qr_name_label_)) {
        char n_buf[64];
        snprintf(n_buf, sizeof(n_buf), "Nama BLE: %s", ble_device_name_.c_str());
        lv_label_set_text(qr_name_label_, n_buf);
    }
    if (qr_mac_label_ && lv_obj_is_valid(qr_mac_label_)) {
        char m_buf[64];
        snprintf(m_buf, sizeof(m_buf), "MAC: %s", ble_mac_address_.c_str());
        lv_label_set_text(qr_mac_label_, m_buf);
    }
    renderQrCode(qr_data_.c_str());
    lv_unlock();
}

// ---- NAVIGATION SCREEN ----
void ChronchiDisplay::createNavScreen() {
    nav_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(nav_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(nav_screen_, width_, height_);
    lv_obj_set_style_bg_opa(nav_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_screen_, 0, 0);
    lv_obj_set_style_radius(nav_screen_, 0, 0);
    lv_obj_set_style_pad_all(nav_screen_, 0, 0);
    lv_obj_clear_flag(nav_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(nav_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nav_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // 1. Header Capsule (below the 34px top navbar)
    nav_header_card_ = create_card(nav_screen_, 220, 26, 13);
    lv_obj_set_style_border_width(nav_header_card_, 0, 0);
    lv_obj_align(nav_header_card_, LV_ALIGN_TOP_MID, 0, 38);
    nav_header_label_ = lv_label_create(nav_header_card_);
    lv_label_set_text(nav_header_label_, "🧭 GOOGLE MAPS");
    lv_obj_set_style_text_font(nav_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_header_label_, LV_ALIGN_LEFT_MID, 12, 0);

    nav_time_label_ = lv_label_create(nav_header_card_);
    lv_label_set_text(nav_time_label_, "00:00");
    lv_obj_set_style_text_font(nav_time_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_time_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    // 2. Main Route & Maneuver Card (y=70, h=118, w=220)
    nav_main_card_ = create_card(nav_screen_, 220, 118, 14);
    lv_obj_set_style_border_width(nav_main_card_, 0, 0);
    lv_obj_align(nav_main_card_, LV_ALIGN_TOP_MID, 0, 70);

    if (nav_icon_buf_) {
        nav_icon_img_ = lv_image_create(nav_main_card_);
        lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
        lv_image_set_scale(nav_icon_img_, 240); // 45x45 px crisp turn arrow
        lv_obj_set_style_bg_opa(nav_icon_img_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(nav_icon_img_, 0, 0);
        lv_obj_align(nav_icon_img_, LV_ALIGN_TOP_LEFT, 10, 10);
    }

    nav_dist_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_dist_label_, "-- m");
    lv_obj_set_style_text_font(nav_dist_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(nav_dist_label_, LV_ALIGN_TOP_LEFT, 68, 8);

    nav_title_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_title_label_, "📍 Rute Navigasi");
    lv_obj_set_style_text_font(nav_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(nav_title_label_, 142);
    lv_obj_set_style_text_align(nav_title_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(nav_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(nav_title_label_, LV_ALIGN_TOP_LEFT, 68, 36);

    nav_street_label_ = lv_label_create(nav_main_card_);
    lv_label_set_text(nav_street_label_, "Siap bernavigasi...");
    lv_obj_set_style_text_font(nav_street_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(nav_street_label_, 200);
    lv_obj_set_style_text_align(nav_street_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(nav_street_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(nav_street_label_, LV_ALIGN_BOTTOM_MID, 0, -8);

    // 3. Dashboard Stats Card (y=196, h=66, w=220)
    nav_stats_card_ = create_card(nav_screen_, 220, 66, 12);
    lv_obj_set_style_border_width(nav_stats_card_, 0, 0);
    lv_obj_align(nav_stats_card_, LV_ALIGN_TOP_MID, 0, 196);

    nav_dur_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_dur_label_, "⏱ -- min");
    lv_obj_set_style_text_font(nav_dur_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_dur_label_, LV_ALIGN_TOP_LEFT, 12, 8);

    nav_eta_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_eta_label_, "🏁 ETA: --:--");
    lv_obj_set_style_text_font(nav_eta_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_eta_label_, LV_ALIGN_TOP_RIGHT, -12, 8);

    nav_speed_label_ = lv_label_create(nav_stats_card_);
    lv_label_set_text(nav_speed_label_, "⚡ Kecepatan: -- km/h");
    lv_obj_set_style_text_font(nav_speed_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(nav_speed_label_, LV_ALIGN_BOTTOM_LEFT, 12, -8);
}

// ---- NOTIFICATION SCREEN ----
void ChronchiDisplay::createNotifScreen() {
    notif_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(notif_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(notif_screen_, width_, height_);
    lv_obj_set_style_bg_opa(notif_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notif_screen_, 0, 0);
    lv_obj_set_style_radius(notif_screen_, 0, 0);
    lv_obj_set_style_pad_all(notif_screen_, 0, 0);
    lv_obj_clear_flag(notif_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // App Header Badge Capsule (below the 34px top navbar)
    notif_header_badge_ = create_card(notif_screen_, 216, 28, 12);
    lv_obj_align(notif_header_badge_, LV_ALIGN_TOP_MID, 0, 36);

    notif_app_label_ = lv_label_create(notif_header_badge_);
    lv_label_set_text(notif_app_label_, "💬 NOTIFIKASI");
    lv_obj_set_style_text_font(notif_app_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_app_label_, LV_ALIGN_LEFT_MID, 8, 0);

    notif_time_label_ = lv_label_create(notif_header_badge_);
    lv_label_set_text(notif_time_label_, "Baru saja");
    lv_obj_set_style_text_font(notif_time_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_time_label_, LV_ALIGN_RIGHT_MID, -8, 0);

    // Main Notification Card (ends at y=238)
    notif_card_ = create_card(notif_screen_, 216, 170, 14);
    lv_obj_align(notif_card_, LV_ALIGN_TOP_MID, 0, 68);

    notif_title_label_ = lv_label_create(notif_card_);
    lv_label_set_text(notif_title_label_, "Pengirim");
    lv_obj_set_style_text_font(notif_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(notif_title_label_, 196);
    lv_label_set_long_mode(notif_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(notif_title_label_, LV_ALIGN_TOP_LEFT, 8, 8);

    notif_line_ = lv_obj_create(notif_card_);
    lv_obj_set_size(notif_line_, 196, 2);
    lv_obj_align(notif_line_, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_obj_set_style_bg_opa(notif_line_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notif_line_, 0, 0);
    lv_obj_clear_flag(notif_line_, LV_OBJ_FLAG_SCROLLABLE);

    notif_msg_label_ = lv_label_create(notif_card_);
    lv_label_set_text(notif_msg_label_, "Pesan notifikasi...");
    lv_obj_set_style_text_font(notif_msg_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(notif_msg_label_, 196);
    lv_label_set_long_mode(notif_msg_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(notif_msg_label_, LV_ALIGN_TOP_LEFT, 8, 38);

    // Bottom Dismiss hint (placed safely at y=248, ends at 268)
    notif_icon_label_ = lv_label_create(notif_screen_);
    lv_label_set_text(notif_icon_label_, LV_SYMBOL_BELL " Tutup otomatis dalam 5s");
    lv_obj_set_style_text_font(notif_icon_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(notif_icon_label_, LV_ALIGN_TOP_MID, 0, 248);
}

// ---- WEATHER SCREEN ----
void ChronchiDisplay::createWeatherScreen() {
    weather_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(weather_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(weather_screen_, width_, height_);
    lv_obj_set_style_bg_opa(weather_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weather_screen_, 0, 0);
    lv_obj_set_style_radius(weather_screen_, 0, 0);
    lv_obj_set_style_pad_all(weather_screen_, 0, 0);
    lv_obj_clear_flag(weather_screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Weather App Header
    weather_header_card_ = create_card(weather_screen_, 220, 26, 13);
    lv_obj_align(weather_header_card_, LV_ALIGN_TOP_MID, 0, 38);
    weather_header_label_ = lv_label_create(weather_header_card_);
    lv_label_set_text(weather_header_label_, "⛅ CUACA HARI INI");
    lv_obj_set_style_text_font(weather_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(weather_header_label_);

    // Weather Main Hero Card (y=70, h=82, w=220)
    weather_main_card_ = create_card(weather_screen_, 220, 82, 14);
    lv_obj_align(weather_main_card_, LV_ALIGN_TOP_MID, 0, 70);

    weather_temp_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_temp_label_, "--°C");
    lv_obj_set_style_text_font(weather_temp_label_, &lv_font_montserrat_24, 0);
    lv_obj_align(weather_temp_label_, LV_ALIGN_LEFT_MID, 16, 0);

    weather_cond_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_cond_label_, "Memuat...");
    lv_obj_set_style_text_font(weather_cond_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_cond_label_, 110);
    lv_obj_set_style_text_align(weather_cond_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(weather_cond_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(weather_cond_label_, LV_ALIGN_TOP_RIGHT, -14, 14);

    weather_range_label_ = lv_label_create(weather_main_card_);
    lv_label_set_text(weather_range_label_, "H: --°  L: --°");
    lv_obj_set_style_text_font(weather_range_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_range_label_, 110);
    lv_obj_set_style_text_align(weather_range_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(weather_range_label_, LV_ALIGN_BOTTOM_RIGHT, -14, -14);

    // Weather Stats Card (y=154, h=72, w=224)
    weather_stats_card_ = create_card(weather_screen_, 224, 72, 12);
    lv_obj_align(weather_stats_card_, LV_ALIGN_TOP_MID, 0, 154);

    weather_humid_label_ = lv_label_create(weather_stats_card_);
    lv_label_set_text(weather_humid_label_, "💧 Kelembapan: --%");
    lv_obj_set_style_text_font(weather_humid_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_humid_label_, LV_ALIGN_TOP_LEFT, 12, 12);

    weather_press_label_ = lv_label_create(weather_stats_card_);
    lv_label_set_text(weather_press_label_, "⏱️ Tekanan: -- hPa");
    lv_obj_set_style_text_font(weather_press_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(weather_press_label_, LV_ALIGN_TOP_LEFT, 12, 42);

    // Weather Location (y=232, h=30, w=224)
    weather_loc_card_ = create_card(weather_screen_, 224, 30, 10);
    lv_obj_align(weather_loc_card_, LV_ALIGN_TOP_MID, 0, 232);
    weather_loc_label_ = lv_label_create(weather_loc_card_);
    lv_label_set_text(weather_loc_label_, "📍 Lokasi: --");
    lv_obj_set_style_text_font(weather_loc_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(weather_loc_label_, 200);
    lv_label_set_long_mode(weather_loc_label_, LV_LABEL_LONG_DOT);
    lv_obj_center(weather_loc_label_);

    lv_obj_add_flag(weather_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(weather_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);
}

// ---- MUSIC SCREEN (Chronos Spotify / YT Music) ----
void ChronchiDisplay::createMusicScreen() {
    music_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(music_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(music_screen_, width_, height_);
    lv_obj_set_style_bg_opa(music_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_screen_, 0, 0);
    lv_obj_set_style_radius(music_screen_, 0, 0);
    lv_obj_set_style_pad_all(music_screen_, 0, 0);
    lv_obj_clear_flag(music_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(music_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(music_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Music App Header (below the 34px top navbar)
    music_header_card_ = create_card(music_screen_, 220, 26, 13);
    lv_obj_set_style_border_width(music_header_card_, 0, 0);
    lv_obj_align(music_header_card_, LV_ALIGN_TOP_MID, 0, 38);

    music_header_label_ = lv_label_create(music_header_card_);
    lv_label_set_text(music_header_label_, "🎵 PEMUTAR MUSIK");
    lv_obj_set_style_text_font(music_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(music_header_label_, LV_ALIGN_LEFT_MID, 12, 0);

    music_state_label_ = lv_label_create(music_header_card_);
    lv_label_set_text(music_state_label_, "⏸ Jeda");
    lv_obj_set_style_text_font(music_state_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(music_state_label_, LV_ALIGN_RIGHT_MID, -12, 0);

    // Main Player Card (y=70, h=98, w=220)
    music_card_ = create_card(music_screen_, 220, 98, 14);
    lv_obj_set_style_border_width(music_card_, 0, 0);
    lv_obj_align(music_card_, LV_ALIGN_TOP_MID, 0, 70);

    // 1. Judul Lagu (Continuous Scrolling Marquee Text)
    music_title_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_title_label_, "🎵 Judul: Tidak Ada Media        ");
    lv_obj_set_style_text_font(music_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_title_label_, 200);
    lv_label_set_long_mode(music_title_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_title_label_, LV_ALIGN_TOP_LEFT, 10, 8);

    // 2. Nama Penyanyi / Artis (Continuous Scrolling Marquee Text)
    music_artist_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_artist_label_, "👤 Artis: Putar di Ponsel Anda        ");
    lv_obj_set_style_text_font(music_artist_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_artist_label_, 200);
    lv_label_set_long_mode(music_artist_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_artist_label_, LV_ALIGN_TOP_LEFT, 10, 30);

    // 3. Teks Rekomendasi (Continuous Scrolling Marquee Text)
    music_recom_label_ = lv_label_create(music_card_);
    lv_label_set_text(music_recom_label_, "✨ Bluetooth Audio Connected        ");
    lv_obj_set_style_text_font(music_recom_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(music_recom_label_, 200);
    lv_label_set_long_mode(music_recom_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(music_recom_label_, LV_ALIGN_TOP_LEFT, 10, 52);

    // Playback Progress Bar (Container + Fill)
    music_bar_ = create_card(music_card_, 200, 6, 3);
    lv_obj_set_style_border_width(music_bar_, 0, 0);
    lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -8);
    music_bar_fill_ = lv_obj_create(music_bar_);
    lv_obj_set_size(music_bar_fill_, 70, 6);
    lv_obj_set_style_radius(music_bar_fill_, 3, 0);
    lv_obj_set_style_bg_opa(music_bar_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_bar_fill_, 0, 0);
    lv_obj_set_style_pad_all(music_bar_fill_, 0, 0);
    lv_obj_align(music_bar_fill_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(music_bar_fill_, LV_OBJ_FLAG_SCROLLABLE);

    // Playback Controls Row: [⏮ Prev] [ ▶ Play / Pause ] [ Next ⏭ ] (y=176, h=40, w=220)
    music_controls_row_ = lv_obj_create(music_screen_);
    lv_obj_set_size(music_controls_row_, 220, 40);
    lv_obj_align(music_controls_row_, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_set_style_bg_opa(music_controls_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_controls_row_, 0, 0);
    lv_obj_set_style_pad_all(music_controls_row_, 0, 0);
    lv_obj_set_flex_flow(music_controls_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_controls_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(music_controls_row_, LV_OBJ_FLAG_SCROLLABLE);

    // Previous Button (⏮)
    music_prev_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_prev_btn_, 60, 38);
    lv_obj_set_style_radius(music_prev_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_prev_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(music_prev_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_prev_btn_, 0, 0);
    lv_obj_t* prev_lbl = lv_label_create(music_prev_btn_);
    lv_label_set_text(prev_lbl, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(prev_lbl, lv_color_white(), 0);
    lv_obj_center(prev_lbl);
    lv_obj_add_event_cb(music_prev_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9D02); // MUSIC_PREVIOUS
    }, LV_EVENT_CLICKED, this);

    // Play / Pause Button (⏯)
    music_play_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_play_btn_, 86, 38);
    lv_obj_set_style_radius(music_play_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_play_btn_, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(music_play_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_play_btn_, 0, 0);
    music_play_label_ = lv_label_create(music_play_btn_);
    lv_label_set_text(music_play_label_, LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(music_play_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(music_play_label_, lv_color_white(), 0);
    lv_obj_center(music_play_label_);
    lv_obj_add_event_cb(music_play_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9900); // MUSIC_TOGGLE
    }, LV_EVENT_CLICKED, this);

    // Next Button (⏭)
    music_next_btn_ = lv_button_create(music_controls_row_);
    lv_obj_set_size(music_next_btn_, 60, 38);
    lv_obj_set_style_radius(music_next_btn_, 10, 0);
    lv_obj_set_style_bg_color(music_next_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(music_next_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_next_btn_, 0, 0);
    lv_obj_t* next_lbl = lv_label_create(music_next_btn_);
    lv_label_set_text(next_lbl, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(next_lbl, lv_color_white(), 0);
    lv_obj_center(next_lbl);
    lv_obj_add_event_cb(music_next_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x9D03); // MUSIC_NEXT
    }, LV_EVENT_CLICKED, this);

    // Volume Row: [🔉 Vol -] [🔊 Vol +] (y=224, h=34, w=220)
    music_vol_row_ = lv_obj_create(music_screen_);
    lv_obj_set_size(music_vol_row_, 220, 34);
    lv_obj_align(music_vol_row_, LV_ALIGN_TOP_MID, 0, 224);
    lv_obj_set_style_bg_opa(music_vol_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_vol_row_, 0, 0);
    lv_obj_set_style_pad_all(music_vol_row_, 0, 0);
    lv_obj_set_flex_flow(music_vol_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(music_vol_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(music_vol_row_, LV_OBJ_FLAG_SCROLLABLE);

    music_vol_down_btn_ = lv_button_create(music_vol_row_);
    lv_obj_set_size(music_vol_down_btn_, 105, 32);
    lv_obj_set_style_radius(music_vol_down_btn_, 8, 0);
    lv_obj_set_style_bg_color(music_vol_down_btn_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(music_vol_down_btn_, 0, 0);
    lv_obj_set_style_pad_all(music_vol_down_btn_, 0, 0);
    lv_obj_t* vdown_lbl = lv_label_create(music_vol_down_btn_);
    lv_label_set_text(vdown_lbl, LV_SYMBOL_VOLUME_MID " Vol −");
    lv_obj_set_style_text_font(vdown_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vdown_lbl, lv_color_hex(0xE4E4E7), 0);
    lv_obj_center(vdown_lbl);
    lv_obj_add_event_cb(music_vol_down_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x99A2); // VOLUME_DOWN
    }, LV_EVENT_CLICKED, this);

    music_vol_up_btn_ = lv_button_create(music_vol_row_);
    lv_obj_set_size(music_vol_up_btn_, 105, 32);
    lv_obj_set_style_radius(music_vol_up_btn_, 8, 0);
    lv_obj_set_style_bg_color(music_vol_up_btn_, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(music_vol_up_btn_, 0, 0);
    lv_obj_set_style_border_color(music_vol_up_btn_, lv_color_hex(0x334155), 0);
    lv_obj_set_style_pad_all(music_vol_up_btn_, 0, 0);
    lv_obj_t* vup_lbl = lv_label_create(music_vol_up_btn_);
    lv_label_set_text(vup_lbl, LV_SYMBOL_VOLUME_MAX " Vol +");
    lv_obj_set_style_text_font(vup_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vup_lbl, lv_color_hex(0xE4E4E7), 0);
    lv_obj_center(vup_lbl);
    lv_obj_add_event_cb(music_vol_up_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && disp->music_ctrl_cb_) disp->music_ctrl_cb_(0x99A1); // VOLUME_UP
    }, LV_EVENT_CLICKED, this);

    music_back_btn_ = nullptr;
    music_hint_card_ = nullptr;
    music_hint_label_ = nullptr;
}

// ---- STATUS SCREEN (Phone & Network) ----
void ChronchiDisplay::createStatusScreen() {
    status_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(status_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(status_screen_, width_, height_);
    lv_obj_set_style_bg_opa(status_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_screen_, 0, 0);
    lv_obj_set_style_radius(status_screen_, 0, 0);
    lv_obj_set_style_pad_all(status_screen_, 0, 0);
    lv_obj_clear_flag(status_screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(status_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(status_screen_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
            auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
            if (disp) disp->cycleScreen();
        }
    }, LV_EVENT_DOUBLE_CLICKED, this);

    // Status Header (y=36, h=26, w=224)
    status_header_card_ = create_card(status_screen_, 224, 26, 13);
    lv_obj_set_style_border_width(status_header_card_, 0, 0);
    lv_obj_align(status_header_card_, LV_ALIGN_TOP_MID, 0, 36);
    status_header_label_ = lv_label_create(status_header_card_);
    lv_label_set_text(status_header_label_, "📱 STATUS PONSEL");
    lv_obj_set_style_text_font(status_header_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(status_header_label_);

    // Phone Battery Card (y=66, h=62, w=224)
    status_bat_card_ = create_card(status_screen_, 224, 62, 14);
    lv_obj_set_style_border_width(status_bat_card_, 0, 0);
    lv_obj_align(status_bat_card_, LV_ALIGN_TOP_MID, 0, 66);

    status_phone_bat_title_ = lv_label_create(status_bat_card_);
    lv_label_set_text(status_phone_bat_title_, "🔋 Baterai");
    lv_obj_set_style_text_font(status_phone_bat_title_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_phone_bat_title_, LV_ALIGN_TOP_LEFT, 12, 10);

    status_phone_bat_label_ = lv_label_create(status_bat_card_);
    lv_label_set_text(status_phone_bat_label_, "--%");
    lv_obj_set_style_text_font(status_phone_bat_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_phone_bat_label_, LV_ALIGN_TOP_RIGHT, -12, 10);

    status_bat_bar_ = create_card(status_bat_card_, 200, 8, 4);
    lv_obj_set_style_border_width(status_bat_bar_, 0, 0);
    lv_obj_align(status_bat_bar_, LV_ALIGN_BOTTOM_MID, 0, -10);
    status_bat_bar_fill_ = lv_obj_create(status_bat_bar_);
    lv_obj_set_size(status_bat_bar_fill_, 100, 8);
    lv_obj_set_style_radius(status_bat_bar_fill_, 4, 0);
    lv_obj_set_style_bg_opa(status_bat_bar_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_bat_bar_fill_, 0, 0);
    lv_obj_set_style_pad_all(status_bat_bar_fill_, 0, 0);
    lv_obj_align(status_bat_bar_fill_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(status_bat_bar_fill_, LV_OBJ_FLAG_SCROLLABLE);

    // Network & Signal Card (y=132, h=66, w=224) - Clean separate rows
    status_net_card_ = create_card(status_screen_, 224, 66, 14);
    lv_obj_set_style_border_width(status_net_card_, 0, 0);
    lv_obj_align(status_net_card_, LV_ALIGN_TOP_MID, 0, 132);

    status_network_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_network_label_, "📶 Jaringan: Seluler");
    lv_obj_set_style_text_font(status_network_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_network_label_, LV_ALIGN_TOP_LEFT, 12, 8);

    status_signal_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_signal_label_, "📶 Sinyal: Aktif");
    lv_obj_set_style_text_font(status_signal_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_signal_label_, LV_ALIGN_TOP_LEFT, 12, 28);

    status_net_hint_label_ = lv_label_create(status_net_card_);
    lv_label_set_text(status_net_hint_label_, "⚡ Koneksi: BLE Chronos");
    lv_obj_set_style_text_font(status_net_hint_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(status_net_hint_label_, LV_ALIGN_TOP_LEFT, 12, 48);

    // Find Phone Button (Ring Phone) (y=202, h=36, w=224)
    status_find_phone_btn_ = lv_button_create(status_screen_);
    lv_obj_set_size(status_find_phone_btn_, 224, 36);
    lv_obj_align(status_find_phone_btn_, LV_ALIGN_TOP_MID, 0, 202);
    lv_obj_set_style_radius(status_find_phone_btn_, 12, 0);
    lv_obj_set_style_bg_color(status_find_phone_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(status_find_phone_btn_, 0, 0);
    lv_obj_set_style_pad_all(status_find_phone_btn_, 0, 0);
    status_find_phone_lbl_ = lv_label_create(status_find_phone_btn_);
    lv_label_set_text(status_find_phone_lbl_, "🔔 CARI PONSEL SAYA");
    lv_obj_set_style_text_font(status_find_phone_lbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_find_phone_lbl_, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(status_find_phone_lbl_);
    lv_obj_add_event_cb(status_find_phone_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (!disp) return;
        disp->find_phone_active_ = !disp->find_phone_active_;
        if (disp->find_phone_cb_) {
            disp->find_phone_cb_(disp->find_phone_active_);
        }
        if (disp->find_phone_active_) {
            bool is_landscape = (disp->width_ > disp->height_);
            lv_label_set_text(disp->status_find_phone_lbl_, is_landscape ? "🔔 STOP" : "🔔 STOP BUNYI");
            lv_obj_set_style_bg_color(disp->status_find_phone_btn_, lv_color_hex(0xDC2626), 0);
            lv_obj_set_style_text_color(disp->status_find_phone_lbl_, lv_color_white(), 0);
        } else {
            bool is_landscape = (disp->width_ > disp->height_);
            lv_label_set_text(disp->status_find_phone_lbl_, is_landscape ? "🔔 CARI PONSEL" : "🔔 CARI PONSEL SAYA");
            lv_obj_set_style_bg_color(disp->status_find_phone_btn_, lv_color_hex(0x27272A), 0);
            lv_obj_set_style_text_color(disp->status_find_phone_lbl_, lv_color_hex(0x38BDF8), 0);
        }
    }, LV_EVENT_CLICKED, this);

    // Bluetooth Info Pill (y=242, h=32, w=224)
    status_bt_card_ = create_card(status_screen_, 224, 32, 10);
    lv_obj_set_style_border_width(status_bt_card_, 0, 0);
    lv_obj_align(status_bt_card_, LV_ALIGN_TOP_MID, 0, 242);
    status_bt_label_ = lv_label_create(status_bt_card_);
    lv_label_set_text(status_bt_label_, last_bt_connected_ ? "⚡ BLE Chronos Terhubung" : "🔗 Pindai QR BLE");
    lv_obj_set_style_text_font(status_bt_label_, &lv_font_montserrat_14, 0);
    lv_obj_center(status_bt_label_);
    lv_obj_add_event_cb(status_bt_card_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (disp && !disp->isBtConnected()) {
            disp->showQrPairing();
        }
    }, LV_EVENT_CLICKED, this);
}

// ---- ALERT SCREEN (Find Watch, Alarm, Camera trigger) ----
void ChronchiDisplay::createAlertScreen() {
    alert_screen_ = lv_obj_create(NULL);
    lv_obj_set_style_text_font(alert_screen_, &lv_font_montserrat_14, 0);
    lv_obj_set_size(alert_screen_, width_, height_);
    lv_obj_set_style_bg_opa(alert_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alert_screen_, 0, 0);
    lv_obj_set_style_radius(alert_screen_, 0, 0);
    lv_obj_set_style_pad_all(alert_screen_, 0, 0);
    lv_obj_clear_flag(alert_screen_, LV_OBJ_FLAG_SCROLLABLE);

    alert_card_ = create_card(alert_screen_, 216, 165, 16);
    lv_obj_align(alert_card_, LV_ALIGN_TOP_MID, 0, 38);

    alert_icon_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL "  " LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(alert_icon_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(alert_icon_label_, LV_ALIGN_TOP_MID, 0, 14);

    alert_title_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_title_label_, "PERINGATAN");
    lv_obj_set_style_text_font(alert_title_label_, &lv_font_montserrat_14, 0);
    lv_obj_align(alert_title_label_, LV_ALIGN_TOP_MID, 0, 44);

    alert_msg_label_ = lv_label_create(alert_card_);
    lv_label_set_text(alert_msg_label_, "");
    lv_obj_set_style_text_font(alert_msg_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_width(alert_msg_label_, 190);
    lv_label_set_long_mode(alert_msg_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(alert_msg_label_, LV_ALIGN_TOP_MID, 0, 75);

    alert_btn_ = lv_button_create(alert_screen_);
    lv_obj_set_size(alert_btn_, 190, 38);
    lv_obj_set_style_radius(alert_btn_, 10, 0);
    lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_pad_all(alert_btn_, 0, 0);
    lv_obj_align(alert_btn_, LV_ALIGN_TOP_MID, 0, 212);

    alert_btn_label_ = lv_label_create(alert_btn_);
    lv_label_set_text(alert_btn_label_, "✕ MATIKAN / STOP");
    lv_obj_set_style_text_font(alert_btn_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(alert_btn_label_, lv_color_white(), 0);
    lv_obj_center(alert_btn_label_);

    lv_obj_add_event_cb(alert_btn_, [](lv_event_t* e) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_event_get_user_data(e));
        if (!disp) return;
        if (disp->is_camera_mode_) {
            if (disp->camera_capture_cb_) {
                disp->camera_capture_cb_();
            }
            if (disp->alert_btn_label_) {
                lv_label_set_text(disp->alert_btn_label_, "✓ MEMOTRET...");
            }
        } else {
            ChronchiAlert::GetInstance().StopAlert();
            disp->showHome();
        }
    }, LV_EVENT_CLICKED, this);
}

// ---- DYNAMIC THEME & COLOR APPLICATION ----
void ChronchiDisplay::applyColorMode() {
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    lv_color_t bg_color       = lv_color_hex(th.bg);
    lv_color_t card_bg_col    = lv_color_hex(th.card_bg);
    lv_color_t card_bdr_col   = lv_color_hex(th.card_border);
    lv_color_t text_color     = lv_color_hex(th.text);
    lv_color_t dim_color      = lv_color_hex(th.dim);
    lv_color_t accent_color   = lv_color_hex(th.accent);
    lv_color_t success_color  = lv_color_hex(0x22C55E);
    lv_color_t notif_line_col = lv_color_hex(0x334155);
    lv_color_t notif_msg_col  = lv_color_hex(0xCBD5E1);

    const auto* synced_bg = GetCurrentSyncedBg();
    if (synced_bg) {
        bg_color     = lv_color_hex(synced_bg->bg_color);
        card_bg_col  = lv_color_hex(synced_bg->panel_color);
        card_bdr_col = lv_color_hex(synced_bg->border_color);
    }

    if (chronchi_navbar_ && lv_obj_is_valid(chronchi_navbar_)) {
        lv_obj_set_style_bg_color(chronchi_navbar_, card_bg_col, 0);
        lv_obj_set_style_border_color(chronchi_navbar_, card_bdr_col, 0);
    }
    if (chronchi_bottom_nav_ && lv_obj_is_valid(chronchi_bottom_nav_)) {
        lv_obj_set_style_bg_color(chronchi_bottom_nav_, card_bg_col, 0);
        lv_obj_set_style_border_color(chronchi_bottom_nav_, card_bdr_col, 0);
    }

    auto style_card = [](lv_obj_t* card, lv_color_t bg, lv_color_t border) {
        if (card && lv_obj_is_valid(card)) {
            lv_obj_set_style_bg_color(card, bg, 0);
            lv_obj_set_style_border_color(card, border, 0);
        }
    };

    // Style Navbar Theme Palette Button with active stabilo color
    if (navbar_th_lbl_ && lv_obj_is_valid(navbar_th_lbl_)) {
        lv_obj_set_style_text_color(navbar_th_lbl_, accent_color, 0);
    }
    if (navbar_th_btn_ && lv_obj_is_valid(navbar_th_btn_)) {
        lv_obj_set_style_border_color(navbar_th_btn_, accent_color, 0);
        lv_obj_set_style_border_width(navbar_th_btn_, 1, 0);
    }

    // 1. Home Screen (Simple Minimalist Chronos Watchface)
    if (home_screen_) lv_obj_set_style_bg_color(home_screen_, bg_color, 0);
    if (home_time_card_) style_card(home_time_card_, card_bg_col, card_bdr_col);
    if (home_time_label_) lv_obj_set_style_text_color(home_time_label_, text_color, 0);
    if (home_date_card_) lv_obj_set_style_bg_color(home_date_card_, accent_color, 0);
    if (home_date_label_) lv_obj_set_style_text_color(home_date_label_, dim_color, 0);

    // Navbar Bluetooth Button & Label styling
    if (navbar_bt_label_ && lv_obj_is_valid(navbar_bt_label_)) {
        lv_obj_set_style_text_color(navbar_bt_label_, last_bt_connected_ ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    }
    if (navbar_bt_btn_ && lv_obj_is_valid(navbar_bt_btn_)) {
        lv_obj_set_style_bg_color(navbar_bt_btn_, last_bt_connected_ ? lv_color_hex(0x0F2E4A) : lv_color_hex(0x27272A), 0);
    }

    auto style_card_borderless = [](lv_obj_t* card, lv_color_t bg) {
        if (card && lv_obj_is_valid(card)) {
            lv_obj_set_style_bg_color(card, bg, 0);
            lv_obj_set_style_border_width(card, 0, 0);
        }
    };

    // 2. Nav Screen (Borderless)
    if (nav_screen_) lv_obj_set_style_bg_color(nav_screen_, bg_color, 0);
    style_card_borderless(nav_header_card_, card_bg_col);
    if (nav_header_label_) lv_obj_set_style_text_color(nav_header_label_, lv_color_hex(0x3B82F6), 0);
    if (nav_time_label_) lv_obj_set_style_text_color(nav_time_label_, text_color, 0);

    style_card_borderless(nav_main_card_, card_bg_col);
    if (nav_dist_label_) lv_obj_set_style_text_color(nav_dist_label_, accent_color, 0);
    if (nav_title_label_) lv_obj_set_style_text_color(nav_title_label_, dim_color, 0);
    if (nav_street_label_) lv_obj_set_style_text_color(nav_street_label_, text_color, 0);

    style_card_borderless(nav_stats_card_, card_bg_col);
    if (nav_dur_label_) lv_obj_set_style_text_color(nav_dur_label_, text_color, 0);
    if (nav_eta_label_) lv_obj_set_style_text_color(nav_eta_label_, success_color, 0);
    if (nav_speed_label_) lv_obj_set_style_text_color(nav_speed_label_, accent_color, 0);
    setNavIcon(last_maneuver_.empty() ? "STRAIGHT" : last_maneuver_.c_str());

    // 3. Notif Screen
    if (notif_screen_) lv_obj_set_style_bg_color(notif_screen_, bg_color, 0);
    style_card(notif_header_badge_, card_bg_col, accent_color);
    if (notif_app_label_) lv_obj_set_style_text_color(notif_app_label_, accent_color, 0);
    if (notif_time_label_) lv_obj_set_style_text_color(notif_time_label_, dim_color, 0);
    style_card(notif_card_, card_bg_col, card_bdr_col);
    if (notif_line_) lv_obj_set_style_bg_color(notif_line_, notif_line_col, 0);
    if (notif_title_label_) lv_obj_set_style_text_color(notif_title_label_, text_color, 0);
    if (notif_msg_label_) lv_obj_set_style_text_color(notif_msg_label_, notif_msg_col, 0);
    if (notif_icon_label_) lv_obj_set_style_text_color(notif_icon_label_, dim_color, 0);

    // 4. Weather Screen
    if (weather_screen_) lv_obj_set_style_bg_color(weather_screen_, bg_color, 0);
    style_card(weather_header_card_, card_bg_col, lv_color_hex(0x0284C7));
    if (weather_header_label_) lv_obj_set_style_text_color(weather_header_label_, lv_color_hex(0x38BDF8), 0);
    style_card(weather_main_card_, card_bg_col, card_bdr_col);
    if (weather_temp_label_) lv_obj_set_style_text_color(weather_temp_label_, lv_color_hex(0xF59E0B), 0);
    if (weather_cond_label_) lv_obj_set_style_text_color(weather_cond_label_, text_color, 0);
    if (weather_range_label_) lv_obj_set_style_text_color(weather_range_label_, dim_color, 0);
    style_card(weather_stats_card_, card_bg_col, card_bdr_col);
    if (weather_humid_label_) lv_obj_set_style_text_color(weather_humid_label_, text_color, 0);
    if (weather_press_label_) lv_obj_set_style_text_color(weather_press_label_, dim_color, 0);
    style_card(weather_loc_card_, card_bg_col, card_bdr_col);
    if (weather_loc_label_) lv_obj_set_style_text_color(weather_loc_label_, lv_color_hex(0x00E5FF), 0);

    // 5. Music Screen (Borderless)
    if (music_screen_) lv_obj_set_style_bg_color(music_screen_, bg_color, 0);
    style_card_borderless(music_header_card_, card_bg_col);
    if (music_header_label_) lv_obj_set_style_text_color(music_header_label_, lv_color_hex(0x1DB954), 0);
    if (music_state_label_) lv_obj_set_style_text_color(music_state_label_, success_color, 0);
    style_card_borderless(music_card_, card_bg_col);
    if (music_title_label_) lv_obj_set_style_text_color(music_title_label_, text_color, 0);
    if (music_artist_label_) lv_obj_set_style_text_color(music_artist_label_, accent_color, 0);
    if (music_recom_label_) lv_obj_set_style_text_color(music_recom_label_, lv_color_hex(0xFBBF24), 0);
    if (music_bar_) {
        lv_obj_set_style_bg_color(music_bar_, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_width(music_bar_, 0, 0);
    }
    if (music_bar_fill_) {
        lv_obj_set_style_bg_color(music_bar_fill_, lv_color_hex(0x22C55E), 0);
    }

    // 6. Status Screen (Borderless)
    if (status_screen_) lv_obj_set_style_bg_color(status_screen_, bg_color, 0);
    style_card_borderless(status_header_card_, card_bg_col);
    if (status_header_label_) lv_obj_set_style_text_color(status_header_label_, lv_color_hex(0x00E5FF), 0);
    style_card_borderless(status_bat_card_, card_bg_col);
    if (status_phone_bat_title_) lv_obj_set_style_text_color(status_phone_bat_title_, dim_color, 0);
    if (status_phone_bat_label_) lv_obj_set_style_text_color(status_phone_bat_label_, success_color, 0);
    if (status_bat_bar_) {
        lv_obj_set_style_bg_color(status_bat_bar_, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_border_width(status_bat_bar_, 0, 0);
    }
    if (status_bat_bar_fill_) {
        lv_obj_set_style_bg_color(status_bat_bar_fill_, success_color, 0);
    }
    style_card_borderless(status_net_card_, card_bg_col);
    if (status_network_label_) lv_obj_set_style_text_color(status_network_label_, accent_color, 0);
    if (status_signal_label_) lv_obj_set_style_text_color(status_signal_label_, dim_color, 0);
    if (status_net_hint_label_) lv_obj_set_style_text_color(status_net_hint_label_, dim_color, 0);
    if (status_find_phone_btn_) {
        lv_obj_set_style_bg_color(status_find_phone_btn_, find_phone_active_ ? lv_color_hex(0xDC2626) : card_bg_col, 0);
        lv_obj_set_style_border_width(status_find_phone_btn_, 0, 0);
    }
    style_card_borderless(status_bt_card_, card_bg_col);
    if (status_bt_label_) lv_obj_set_style_text_color(status_bt_label_, last_bt_connected_ ? success_color : accent_color, 0);

    // 7. Alert Screen
    if (alert_screen_) lv_obj_set_style_bg_color(alert_screen_, bg_color, 0);
    style_card(alert_card_, light_mode_ ? lv_color_hex(0xFEE2E2) : lv_color_hex(0x3B0707), lv_color_hex(0xEF4444));
    if (alert_icon_label_) lv_obj_set_style_text_color(alert_icon_label_, lv_color_hex(0xFACC15), 0);
    if (alert_title_label_) lv_obj_set_style_text_color(alert_title_label_, lv_color_hex(0xEF4444), 0);
    if (alert_msg_label_) lv_obj_set_style_text_color(alert_msg_label_, light_mode_ ? lv_color_hex(0x7F1D1D) : lv_color_hex(0xFCA5A5), 0);
    if (is_camera_mode_) {
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0x0284C7), 0);
    } else {
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    }
    if (alert_btn_label_) lv_obj_set_style_text_color(alert_btn_label_, lv_color_hex(0xFFFFFF), 0);

    // 8. QR Pairing Screen
    if (qr_screen_) lv_obj_set_style_bg_color(qr_screen_, bg_color, 0);
    style_card(qr_header_card_, card_bg_col, accent_color);
    if (qr_header_label_) lv_obj_set_style_text_color(qr_header_label_, accent_color, 0);
    if (qr_clock_btn_) {
        lv_obj_set_style_bg_color(qr_clock_btn_, card_bg_col, 0);
        lv_obj_set_style_border_color(qr_clock_btn_, card_bdr_col, 0);
        lv_obj_set_style_border_width(qr_clock_btn_, 1, 0);
    }
}

void ChronchiDisplay::setWatchfaceTheme(uint32_t color, uint32_t layout) {
    ESP_LOGI(TAG, "Watchface customized via Chronos: color=0x%06lX, layout=%lu",
             (unsigned long)color, (unsigned long)layout);

    current_layout_ = layout % NUM_THEMES;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = (color != 0) ? color : th.text;
    light_mode_ = (th.bg > 0x888888);

    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);

    // Global theme synchronization across XiaoZhi, Chronchi, and Nova OS
    SyncThemeByLayout(current_layout_);

    lv_lock();
    applyColorMode();
    lv_unlock();
}

void ChronchiDisplay::toggleColorMode() {
    current_layout_ = (current_layout_ + 1) % NUM_THEMES;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = th.text;
    light_mode_ = (th.bg > 0x888888);

    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);

    // Global theme synchronization across XiaoZhi, Chronchi, and Nova OS
    SyncThemeByLayout(current_layout_);

    ESP_LOGI(TAG, "Watchface theme toggled to: %lu (%s) - Color: 0x%06lX",
             (unsigned long)current_layout_, th.name, (unsigned long)theme_color_);

    lv_lock();
    applyColorMode();

    if (home_status_label_ && lv_obj_is_valid(home_status_label_) && !google_maps_active_) {
        char toast[64];
        snprintf(toast, sizeof(toast), "🎨 %s", th.name);
        lv_label_set_text(home_status_label_, toast);
    }
    lv_unlock();
}

void ChronchiDisplay::setColorMode(bool light) {
    current_layout_ = light ? 1 : 0;
    const auto& th = PRESET_THEMES[current_layout_];
    theme_color_ = th.text;
    light_mode_ = (th.bg > 0x888888);
    Settings settings("chronchi", true);
    settings.SetInt("layout", current_layout_);
    settings.SetInt("color", theme_color_);
    settings.SetBool("light_mode", light_mode_);
    ESP_LOGI(TAG, "Color mode set to: %lu (%s)", (unsigned long)current_layout_, th.name);
    lv_lock();
    applyColorMode();
    lv_unlock();
}

void ChronchiDisplay::cycleTheme() {
    toggleColorMode();
}

void ChronchiDisplay::setTheme(int theme) {
    setWatchfaceTheme(theme_color_, theme);
}

void ChronchiDisplay::setThemeColor(uint32_t rgb) {
    setWatchfaceTheme(rgb, current_layout_);
}

void ChronchiDisplay::updateHomeWeather(const ChronchiWeather& weather) {
    lv_lock();
    if (home_status_label_ && lv_obj_is_valid(home_status_label_) && !google_maps_active_) {
        char sbuf[64];
        snprintf(sbuf, sizeof(sbuf), "%.0f°C  %s", weather.temperatureC, weather.condition.c_str());
        lv_label_set_text(home_status_label_, sbuf);
    }
    lv_unlock();
}

// ---- Screen management ----
void ChronchiDisplay::hideAllScreens() {
    if (home_screen_ && lv_screen_active() == home_screen_) lv_screen_load(lv_obj_get_screen(home_screen_));
}

void ChronchiDisplay::showScreen(lv_obj_t* screen) {
    if (screen) lv_screen_load(screen);
}

void ChronchiDisplay::setScreenState(ScreenState s) {
    prev_state_ = state_;
    state_ = s;
    switch (s) {
        case HOME: showScreen(home_screen_); break;
        case NAVIGATION: showScreen(nav_screen_); break;
        case NOTIFICATION: showScreen(notif_screen_); break;
        case WEATHER: showScreen(weather_screen_); break;
        case MUSIC: showScreen(music_screen_); break;
        case STATUS: showScreen(status_screen_); break;
        case ALERT: showScreen(alert_screen_); break;
        case QR_PAIRING: showScreen(qr_screen_); break;
    }
}

void ChronchiDisplay::showHome() {
    lv_lock();
    if (home_screen_) {
        lv_screen_load(home_screen_);
        ESP_LOGI(TAG, "Loaded Chronchi home watchface (Theme: %s)", PRESET_THEMES[current_layout_ % NUM_THEMES].name);
    }
    state_ = HOME;
    lv_unlock();
}

void ChronchiDisplay::showNavigation(const ChronchiNavigation& nav) {
    lv_lock();
    google_maps_active_ = nav.active;

    if (!nav.active) {
        if (state_ == NAVIGATION) {
            showHome();
        }
        if (home_status_label_ && lv_obj_is_valid(home_status_label_)) {
            lv_label_set_text(home_status_label_, "⚡ Chronos Smartwatch");
        }
        lv_unlock();
        return;
    }

    if (nav_title_label_) {
        if (!nav.title.empty()) {
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), "📍 %s", nav.title.c_str());
            lv_label_set_text(nav_title_label_, tbuf);
        } else {
            lv_label_set_text(nav_title_label_, "📍 Google Maps");
        }
    }
    if (nav_street_label_) {
        lv_label_set_text(nav_street_label_, nav.directions.empty() ? "Ikuti rute jalan..." : nav.directions.c_str());
    }
    if (nav_dist_label_) {
        lv_label_set_text(nav_dist_label_, nav.distance.empty() ? "-- m" : nav.distance.c_str());
    }
    if (nav_dur_label_) {
        char buf[32];
        if (!nav.duration.empty()) {
            snprintf(buf, sizeof(buf), "⏱ %s", nav.duration.c_str());
            lv_label_set_text(nav_dur_label_, buf);
        } else {
            lv_label_set_text(nav_dur_label_, "⏱ -- min");
        }
    }
    if (nav_eta_label_) {
        char buf[32];
        if (!nav.eta.empty()) {
            if (strstr(nav.eta.c_str(), "ETA") || strstr(nav.eta.c_str(), "eta")) {
                snprintf(buf, sizeof(buf), "🏁 %s", nav.eta.c_str());
            } else {
                snprintf(buf, sizeof(buf), "🏁 ETA %s", nav.eta.c_str());
            }
            lv_label_set_text(nav_eta_label_, buf);
        } else {
            lv_label_set_text(nav_eta_label_, "🏁 ETA --:--");
        }
    }
    if (nav_speed_label_) {
        char buf[32];
        if (!nav.speed.empty()) {
            if (strstr(nav.speed.c_str(), "km") || strstr(nav.speed.c_str(), "m/h")) {
                snprintf(buf, sizeof(buf), "⚡ %s", nav.speed.c_str());
            } else {
                snprintf(buf, sizeof(buf), "⚡ %s km/h", nav.speed.c_str());
            }
            lv_label_set_text(nav_speed_label_, buf);
        } else {
            lv_label_set_text(nav_speed_label_, "⚡ -- km/h");
        }
    }

    // Accurate Turn Icon: Use 1bpp BLE icon if provided, otherwise parse maneuver/directions
    if (nav.hasIcon) {
        setNavigationIcon(const_cast<uint8_t*>(nav.icon), 288);
    } else if (!nav.maneuver.empty()) {
        setNavIcon(nav.maneuver.c_str());
    } else if (!nav.directions.empty()) {
        std::string d = nav.directions;
        for (auto& c : d) c = (char)tolower((unsigned char)c);
        if (d.find("slight left") != std::string::npos || d.find("serong kiri") != std::string::npos || d.find("agak ke kiri") != std::string::npos) {
            setNavIcon("SLIGHT_LEFT");
        } else if (d.find("slight right") != std::string::npos || d.find("serong kanan") != std::string::npos || d.find("agak ke kanan") != std::string::npos) {
            setNavIcon("SLIGHT_RIGHT");
        } else if (d.find("putar balik") != std::string::npos || d.find("u-turn") != std::string::npos) {
            setNavIcon("UTURN");
        } else if (d.find("kiri") != std::string::npos || d.find("left") != std::string::npos) {
            setNavIcon("LEFT");
        } else if (d.find("kanan") != std::string::npos || d.find("right") != std::string::npos) {
            setNavIcon("RIGHT");
        } else if (d.find("bundaran") != std::string::npos || d.find("roundabout") != std::string::npos) {
            setNavIcon("ROUNDABOUT");
        } else if (d.find("sampai") != std::string::npos || d.find("arrive") != std::string::npos || d.find("tujuan") != std::string::npos || d.find("tiba") != std::string::npos) {
            setNavIcon("ARRIVE");
        } else {
            setNavIcon("STRAIGHT");
        }
    } else {
        setNavIcon("STRAIGHT");
    }

    // Keep home status glance updated in real time as well
    if (home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        char gbuf[64];
        if (!nav.distance.empty()) {
            snprintf(gbuf, sizeof(gbuf), "🧭 %s - %s", nav.distance.c_str(), nav.directions.empty() ? "Lurus" : nav.directions.c_str());
        } else {
            snprintf(gbuf, sizeof(gbuf), "🧭 %s", nav.directions.empty() ? "Google Maps Aktif" : nav.directions.c_str());
        }
        lv_label_set_text(home_status_label_, gbuf);
    }

    if (state_ != NAVIGATION) setScreenState(NAVIGATION);
    lv_unlock();
}

void ChronchiDisplay::showNotification(const ChronchiNotification& notif) {
    lv_lock();

    // Check if this notification is from Google Maps or Navigation
    std::string app_low = notif.app;
    for (auto& c : app_low) c = (char)tolower((unsigned char)c);
    std::string title_low = notif.title;
    for (auto& c : title_low) c = (char)tolower((unsigned char)c);
    std::string msg_low = notif.message;
    for (auto& c : msg_low) c = (char)tolower((unsigned char)c);

    bool is_maps = (app_low.find("map") != std::string::npos ||
                    app_low.find("navigasi") != std::string::npos ||
                    title_low.find("google maps") != std::string::npos ||
                    title_low.find("maps") != std::string::npos ||
                    title_low.find("navigasi") != std::string::npos);

    // If it's a Google Maps navigation notification, route to showNavigation for persistent real-time view!
    if (is_maps) {
        lv_unlock();
        ChronchiNavigation nav;
        nav.active = true;
        nav.title = "Google Maps";
        if (title_low.find(" m") != std::string::npos || title_low.find(" km") != std::string::npos) {
            nav.distance = notif.title;
            nav.directions = notif.message;
        } else if (msg_low.find(" m") != std::string::npos || msg_low.find(" km") != std::string::npos) {
            nav.distance = notif.message;
            nav.directions = notif.title;
        } else {
            nav.title = notif.title.empty() ? "Google Maps" : notif.title;
            nav.directions = notif.message;
        }
        showNavigation(nav);
        return;
    }

    if (notif_app_label_) {
        char abuf[64];
        snprintf(abuf, sizeof(abuf), "💬 %s", notif.app.empty() ? "NOTIFIKASI" : notif.app.c_str());
        lv_label_set_text(notif_app_label_, abuf);
    }
    if (notif_time_label_) lv_label_set_text(notif_time_label_, notif.time.empty() ? "Baru saja" : notif.time.c_str());
    if (notif_title_label_) lv_label_set_text(notif_title_label_, notif.title.c_str());
    if (notif_msg_label_) lv_label_set_text(notif_msg_label_, notif.message.c_str());

    // App-specific header color badge (WhatsApp, Telegram, Instagram, Spotify, etc.)
    if (notif_header_badge_) {
        lv_color_t badge_bg = lv_color_hex(0x1E293B);
        lv_color_t badge_border = lv_color_hex(0x3B82F6);
        lv_color_t badge_text = lv_color_hex(0x60A5FA);

        if (strstr(notif.app.c_str(), "WhatsApp") || strstr(notif.app.c_str(), "WA")) {
            badge_bg = lv_color_hex(0x064E3B);
            badge_border = lv_color_hex(0x10B981);
            badge_text = lv_color_hex(0x34D399);
        } else if (strstr(notif.app.c_str(), "Telegram")) {
            badge_bg = lv_color_hex(0x0C4A6E);
            badge_border = lv_color_hex(0x0284C7);
            badge_text = lv_color_hex(0x38BDF8);
        } else if (strstr(notif.app.c_str(), "Instagram")) {
            badge_bg = lv_color_hex(0x4A044E);
            badge_border = lv_color_hex(0xC026D3);
            badge_text = lv_color_hex(0xF472B6);
        } else if (strstr(notif.app.c_str(), "Spotify")) {
            badge_bg = lv_color_hex(0x052E16);
            badge_border = lv_color_hex(0x15803D);
            badge_text = lv_color_hex(0x22C55E);
        } else if (strstr(notif.app.c_str(), "YouTube")) {
            badge_bg = lv_color_hex(0x450A0A);
            badge_border = lv_color_hex(0xDC2626);
            badge_text = lv_color_hex(0xF87171);
        }

        lv_obj_set_style_bg_color(notif_header_badge_, badge_bg, 0);
        lv_obj_set_style_border_color(notif_header_badge_, badge_border, 0);
        if (notif_app_label_) lv_obj_set_style_text_color(notif_app_label_, badge_text, 0);
    }

    prev_state_ = state_;
    setScreenState(NOTIFICATION);

    // Auto-dismiss (4 detik jika navigasi Maps aktif agar cepat kembali ke peta, atau 6 detik jika di home)
    if (notif_timer_) {
        lv_timer_delete(notif_timer_);
        notif_timer_ = nullptr;
    }
    uint32_t dismiss_ms = google_maps_active_ ? 4000 : 6000;
    notif_timer_ = lv_timer_create([](lv_timer_t* t) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_timer_get_user_data(t));
        if (disp) {
            disp->notif_timer_ = nullptr;
            if (disp->state_ == NOTIFICATION) {
                if (disp->google_maps_active_) {
                    disp->setScreenState(NAVIGATION);
                } else {
                    disp->showHome();
                }
            }
        }
        lv_timer_delete(t);
    }, dismiss_ms, this);
    lv_timer_set_repeat_count(notif_timer_, 1);
    lv_unlock();
}

void ChronchiDisplay::set24HourFormat(bool is_24h) {
    is_24h_format_ = is_24h;
    Settings prefs("nova", true);
    prefs.SetInt("24hour", is_24h ? 1 : 0);
}

void ChronchiDisplay::updateTime(int hour, int minute, int second) {
    lv_lock();
    char buf[16];
    if (is_24h_format_) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, minute, second);
    } else {
        int h = hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, minute, second);
    }
    if (home_time_label_ && lv_obj_is_valid(home_time_label_)) lv_label_set_text(home_time_label_, buf);

    if (nav_time_label_ && lv_obj_is_valid(nav_time_label_)) {
        char nbuf[16];
        snprintf(nbuf, sizeof(nbuf), "%02d:%02d", hour, minute);
        lv_label_set_text(nav_time_label_, nbuf);
    }

    // Update date: Hari, Tanggal Bulan Tahun
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char date_buf[64];
    const char* days[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    const char* months[] = {
        "Januari", "Februari", "Maret", "April", "Mei", "Juni",
        "Juli", "Agustus", "September", "Oktober", "November", "Desember"
    };
    int year = tm_info->tm_year + 1900;
    if (is_24h_format_) {
        snprintf(date_buf, sizeof(date_buf), "%s, %d %s %d",
                 days[tm_info->tm_wday % 7], tm_info->tm_mday, months[tm_info->tm_mon % 12], year);
    } else {
        snprintf(date_buf, sizeof(date_buf), "%s • %s, %d %s %d",
                 hour >= 12 ? "PM" : "AM", days[tm_info->tm_wday % 7], tm_info->tm_mday, months[tm_info->tm_mon % 12], year);
    }
    if (home_date_label_ && lv_obj_is_valid(home_date_label_)) lv_label_set_text(home_date_label_, date_buf);
    lv_unlock();
}

void ChronchiDisplay::updateBattery(uint8_t level, bool charging) {
    lv_lock();
    last_batt_level_ = level;
    last_batt_charging_ = charging;

    if (navbar_batt_label_ && lv_obj_is_valid(navbar_batt_label_)) {
        const char* icon = charging ? LV_SYMBOL_CHARGE :
                           (level > 80) ? LV_SYMBOL_BATTERY_FULL :
                           (level > 40) ? LV_SYMBOL_BATTERY_3 :
                           (level > 20) ? LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_1;
        Settings s("nova", true);
        int bat_style = s.GetInt("bat_style", 1);
        if (bat_style == 0) {
            lv_label_set_text(navbar_batt_label_, icon);
        } else if (bat_style == 2) {
            lv_label_set_text_fmt(navbar_batt_label_, "%d%%", (int)level);
        } else {
            lv_label_set_text_fmt(navbar_batt_label_, "%d%% %s", (int)level, icon);
        }
        lv_obj_set_style_text_color(navbar_batt_label_, charging ? lv_color_hex(0xF59E0B) : (level <= 20 ? lv_color_hex(0xEF4444) : lv_color_hex(0x34D399)), 0);
    }

    if (home_batt_label_ && lv_obj_is_valid(home_batt_label_)) {
        char bbuf[16];
        snprintf(bbuf, sizeof(bbuf), "%d%%%s", (int)level, charging ? " " LV_SYMBOL_CHARGE : "");
        lv_label_set_text(home_batt_label_, bbuf);
        lv_color_t bat_c = charging ? lv_color_hex(0xFACC15) :
                           (level > 50) ? lv_color_hex(0x22C55E) :
                           (level > 20) ? lv_color_hex(0xF59E0B) : lv_color_hex(0xEF4444);
        lv_obj_set_style_text_color(home_batt_label_, bat_c, 0);
        lv_obj_clear_flag(home_batt_label_, LV_OBJ_FLAG_HIDDEN);
    }

    if (home_batt_bar_fill_ && lv_obj_is_valid(home_batt_bar_fill_)) {
        int32_t fw = (24 * level) / 100;
        if (fw < 2 && level > 0) fw = 2;
        if (fw > 24) fw = 24;
        lv_obj_set_width(home_batt_bar_fill_, fw);

        lv_color_t bar_col = charging ? lv_color_hex(0x00E5FF) :
                             (level > 50) ? lv_color_hex(0x22C55E) :
                             (level > 20) ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_bg_color(home_batt_bar_fill_, bar_col, 0);
    }

    if (home_batt_card_ && lv_obj_is_valid(home_batt_card_)) {
        const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
        lv_color_t border_col = (level <= 20) ? lv_color_hex(0xEA580C) : lv_color_hex(th.card_border);
        lv_obj_set_style_border_color(home_batt_card_, border_col, 0);
    }
    lv_unlock();
}

void ChronchiDisplay::updateWifi(bool connected) {
    lv_lock();
    if (navbar_wifi_label_ && lv_obj_is_valid(navbar_wifi_label_)) {
        static uint8_t wifi_tick = 0;
        wifi_tick++;
        if (connected) {
            lv_label_set_text(navbar_wifi_label_, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(navbar_wifi_label_, lv_color_hex(0x22C55E), 0);
        } else {
            lv_label_set_text(navbar_wifi_label_, (wifi_tick % 2 == 0) ? LV_SYMBOL_REFRESH : LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(navbar_wifi_label_, lv_color_hex(0xF59E0B), 0);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::updateBtStatus(bool connected) {
    lv_lock();
    last_bt_connected_ = connected;
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    lv_color_t success_color = light_mode_ ? lv_color_hex(0x16A34A) : lv_color_hex(0x22C55E);
    lv_color_t dim_color = lv_color_hex(th.dim);
    lv_color_t border_color = connected ? success_color : lv_color_hex(th.card_border);

    if (navbar_bt_label_ && lv_obj_is_valid(navbar_bt_label_)) {
        lv_obj_set_style_text_color(navbar_bt_label_, connected ? lv_color_hex(0x00E5FF) : lv_color_hex(0x71717A), 0);
    }
    if (navbar_bt_btn_ && lv_obj_is_valid(navbar_bt_btn_)) {
        lv_obj_set_style_bg_opa(navbar_bt_btn_, LV_OPA_TRANSP, 0);
    }

    if (home_bt_label_ && lv_obj_is_valid(home_bt_label_)) {
        lv_obj_set_style_text_color(home_bt_label_, connected ? success_color : dim_color, 0);
        lv_label_set_text(home_bt_label_, connected ? "Terhubung" : "Siap BLE");
    }
    if (home_bt_card_ && lv_obj_is_valid(home_bt_card_)) {
        lv_obj_set_style_border_color(home_bt_card_, border_color, 0);
    }

    if (connected) {
        if (state_ == QR_PAIRING) {
            setScreenState(HOME);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::setNavigationIcon(uint8_t* icon_data, int size) {
    if (!nav_icon_buf_ || !icon_data || size < 288) return;
    lv_lock();
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    uint16_t bg_color = hex_to_rgb565(th.card_bg);
    uint16_t fg_color = hex_to_rgb565(th.accent);
    if (fg_color == bg_color) {
        fg_color = light_mode_ ? 0x0290 : 0x07FF;
    }

    // Convert 1bpp 48x48 to RGB565 with transparent theme blending
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
    if (nav_icon_img_) lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
    lv_unlock();
}

void ChronchiDisplay::showWeather(const ChronchiWeather& weather) {
    lv_lock();
    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f°C", weather.temperatureC);
    if (weather_temp_label_) lv_label_set_text(weather_temp_label_, buf);
    if (weather_cond_label_) lv_label_set_text(weather_cond_label_, weather.condition.empty() ? "Cerah" : weather.condition.c_str());

    if (weather_range_label_) {
        if (weather.highC != 0 || weather.lowC != 0) {
            snprintf(buf, sizeof(buf), "H: %d°  L: %d°", weather.highC, weather.lowC);
        } else {
            snprintf(buf, sizeof(buf), "H: --°  L: --°");
        }
        lv_label_set_text(weather_range_label_, buf);
    }

    if (weather_humid_label_) {
        bool is_landscape = (width_ > height_);
        if (weather.uv > 0) {
            if (is_landscape) {
                snprintf(buf, sizeof(buf), "💧 %d%% • ☀️ UV %d", weather.humidity, weather.uv);
            } else {
                snprintf(buf, sizeof(buf), "💧 Lembap: %d%%  •  ☀️ UV: %d", weather.humidity, weather.uv);
            }
        } else {
            snprintf(buf, sizeof(buf), is_landscape ? "💧 Lembap: %d%%" : "💧 Kelembapan: %d%%", weather.humidity);
        }
        lv_label_set_text(weather_humid_label_, buf);
    }

    if (weather_press_label_) {
        bool is_landscape = (width_ > height_);
        if (weather.pressure > 0) {
            snprintf(buf, sizeof(buf), is_landscape ? "⏱️ %d hPa" : "⏱️ Tekanan: %d hPa", weather.pressure);
        } else {
            snprintf(buf, sizeof(buf), is_landscape ? "⏱️ 1012 hPa" : "⏱️ Tekanan: 1012 hPa");
        }
        lv_label_set_text(weather_press_label_, buf);
    }

    if (weather_loc_label_) {
        snprintf(buf, sizeof(buf), "📍 %s", weather.location.empty() ? "Prakiraan Cuaca" : weather.location.c_str());
        lv_label_set_text(weather_loc_label_, buf);
    }

    // Also update home status glance capsule if no navigation is active
    if (!google_maps_active_ && home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        char status_buf[64];
        if (!weather.condition.empty()) {
            snprintf(status_buf, sizeof(status_buf), "⛅ %.0f°C  %s", weather.temperatureC, weather.condition.c_str());
            lv_label_set_text(home_status_label_, status_buf);
        }
    }

    // Do NOT automatically switch away from HOME to WEATHER; let the user stay on the digital clock!
    lv_unlock();
}

void ChronchiDisplay::showPhoneStatus(const ChronchiPhoneStatus& status) {
    lv_lock();
    char buf[64];
    const char* chgIcon = status.charging ? " ⚡" : "";
    snprintf(buf, sizeof(buf), "%d%%%s", status.batteryLevel, chgIcon);
    if (status_phone_bat_label_) {
        lv_label_set_text(status_phone_bat_label_, buf);
        lv_color_t bat_col = status.batteryLevel > 50 ? lv_color_hex(0x10B981) :
                             status.batteryLevel > 20 ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_text_color(status_phone_bat_label_, bat_col, 0);
    }

    if (status_bat_bar_fill_ && status_bat_bar_) {
        int32_t bar_w = lv_obj_get_width(status_bat_bar_);
        if (bar_w <= 0) bar_w = (width_ > height_) ? 134 : 200;
        int32_t bw = (bar_w * status.batteryLevel) / 100;
        if (bw < 0) bw = 0;
        if (bw > bar_w) bw = bar_w;
        lv_obj_set_width(status_bat_bar_fill_, bw);
        lv_color_t bar_col = status.batteryLevel > 50 ? lv_color_hex(0x10B981) :
                             status.batteryLevel > 20 ? lv_color_hex(0xF59E0B) :
                             lv_color_hex(0xEF4444);
        lv_obj_set_style_bg_color(status_bat_bar_fill_, bar_col, 0);
    }

    bool is_landscape = (width_ > height_);
    if (!status.network.transport.empty()) {
        if (is_landscape) {
            snprintf(buf, sizeof(buf), "📶 %s %s",
                     status.network.transport.c_str(), status.network.generation.c_str());
        } else {
            snprintf(buf, sizeof(buf), "📶 Jaringan: %s %s",
                     status.network.transport.c_str(), status.network.generation.c_str());
        }
    } else {
        snprintf(buf, sizeof(buf), is_landscape ? "📶 Seluler" : "📶 Jaringan: Seluler");
    }
    if (status_network_label_) lv_label_set_text(status_network_label_, buf);

    if (status.network.signalLevel > 0) {
        const char* sigBars = (status.network.signalLevel >= 4) ? "●●●●" :
                              (status.network.signalLevel == 3) ? "●●●○" :
                              (status.network.signalLevel == 2) ? "●●○○" :
                              (status.network.signalLevel == 1) ? "●○○○" : "○○○○";
        snprintf(buf, sizeof(buf), is_landscape ? "Sinyal: %s" : "📶 Sinyal: %s", sigBars);
    } else {
        snprintf(buf, sizeof(buf), is_landscape ? "Sinyal: Aktif" : "📶 Sinyal: Aktif");
    }
    if (status_signal_label_) lv_label_set_text(status_signal_label_, buf);

    if (status_net_hint_label_) {
        lv_label_set_text(status_net_hint_label_, last_bt_connected_ ? (is_landscape ? "BLE: Aktif" : "⚡ Koneksi: BLE Chronos") :
                                                                       (is_landscape ? "BLE: Menunggu..." : "⚡ Menunggu Ponsel..."));
    }

    // Do NOT automatically switch away from HOME to STATUS; let the user stay on the digital clock!
    lv_unlock();
}

void ChronchiDisplay::showMusic(const ChronchiMusic& music) {
    lv_lock();
    if (music_state_label_) {
        bool playing = (music.state == 1);
        lv_label_set_text(music_state_label_, playing ? "▶ Putar" : "⏸ Jeda");
        lv_obj_set_style_text_color(music_state_label_,
            playing ? (light_mode_ ? lv_color_hex(0x16A34A) : lv_color_hex(0x22C55E)) :
                      lv_color_hex(0xF59E0B), 0);
    }
    if (music_title_label_) {
        std::string t = music.title.empty() ? "Tidak Ada Media" : music.title;
        char buf[160];
        snprintf(buf, sizeof(buf), "🎵 %s        ", t.c_str());
        lv_label_set_text(music_title_label_, buf);
    }
    if (music_artist_label_) {
        std::string a = music.artist.empty() ? "Putar musik di ponsel Anda" : music.artist;
        char buf[160];
        snprintf(buf, sizeof(buf), "👤 %s        ", a.c_str());
        lv_label_set_text(music_artist_label_, buf);
    }
    if (music_recom_label_) {
        std::string appName = music.app.empty() ? "Chronos Audio" : music.app;
        char buf[160];
        snprintf(buf, sizeof(buf), "✨ Direkomendasikan untuk mu • %s        ", appName.c_str());
        lv_label_set_text(music_recom_label_, buf);
    }
    if (music_bar_fill_) {
        int32_t mw = (music.state == 1) ? 140 : 40;
        lv_obj_set_width(music_bar_fill_, mw);
    }

    if (music_play_label_ && lv_obj_is_valid(music_play_label_)) {
        lv_label_set_text(music_play_label_, (music.state == 1) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    // Dynamic Home Screen status update: if music is active, show song on Home!
    if (!google_maps_active_ && home_status_label_ && lv_obj_is_valid(home_status_label_)) {
        if (music.state == 1 && !music.title.empty()) {
            char mbuf[64];
            snprintf(mbuf, sizeof(mbuf), "🎵 %s", music.title.c_str());
            lv_label_set_text(home_status_label_, mbuf);
        }
    }
    lv_unlock();
}

void ChronchiDisplay::showFindWatchAlert(bool active) {
    lv_lock();
    if (active) {
        is_camera_mode_ = false;
        if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL "  " LV_SYMBOL_WARNING);
        if (alert_title_label_) lv_label_set_text(alert_title_label_, "CARI JAM TANGAN!");
        if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Ponsel Anda sedang mencari jam ini.\nGetaran & suara aktif.");
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
        if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ MATIKAN ALARM");
        setScreenState(ALERT);
    } else {
        is_camera_mode_ = false;
        ChronchiAlert::GetInstance().StopAlert();
        if (state_ == ALERT) showHome();
    }
    lv_unlock();
}

void ChronchiDisplay::showCameraPrompt(bool ready) {
    lv_lock();
    if (ready) {
        is_camera_mode_ = true;
        if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_IMAGE " KAMERA");
        if (alert_title_label_) lv_label_set_text(alert_title_label_, "KAMERA AKTIF");
        if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Kamera ponsel Anda siap.\nTekan tombol di bawah untuk memotret foto!");
        if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0x0284C7), 0);
        if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "📸 JEPRET FOTO");
        setScreenState(ALERT);
    } else {
        is_camera_mode_ = false;
        if (state_ == ALERT) showHome();
    }
    lv_unlock();
}

void ChronchiDisplay::showAlarmAlert(int hour, int minute) {
    lv_lock();
    is_camera_mode_ = false;
    if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL " ALARM");
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "WAKTU: %02d:%02d", hour, minute);
    if (alert_title_label_) lv_label_set_text(alert_title_label_, tbuf);
    if (alert_msg_label_) lv_label_set_text(alert_msg_label_, "Alarm Berbunyi!\nWaktunya beraktivitas.");
    if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ MATIKAN ALARM");
    setScreenState(ALERT);
    lv_unlock();
}

void ChronchiDisplay::showReminderAlert(const char* title, const char* message) {
    lv_lock();
    is_camera_mode_ = false;
    if (alert_icon_label_) lv_label_set_text(alert_icon_label_, LV_SYMBOL_BELL " PENGINGAT");
    if (alert_title_label_) lv_label_set_text(alert_title_label_, title ? title : "PENGINGAT");
    if (alert_msg_label_) lv_label_set_text(alert_msg_label_, message ? message : "Waktunya aktivitas!");
    if (alert_btn_) lv_obj_set_style_bg_color(alert_btn_, lv_color_hex(0xDC2626), 0);
    if (alert_btn_label_) lv_label_set_text(alert_btn_label_, "✕ TUTUP");
    setScreenState(ALERT);
    lv_unlock();

    // Auto-dismiss after 8 seconds
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
        auto* disp = static_cast<ChronchiDisplay*>(lv_timer_get_user_data(t));
        if (disp && disp->state_ == ALERT) {
            disp->showHome();
        }
        lv_timer_del(t);
    }, 8000, this);
    lv_timer_set_repeat_count(timer, 1);
}

void ChronchiDisplay::cycleScreen() {
    static uint32_t last_cycle = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_cycle < 350) return;
    last_cycle = now;

    lv_lock();
    if (state_ == HOME) {
        setScreenState(WEATHER);
    } else if (state_ == WEATHER) {
        setScreenState(MUSIC);
    } else if (state_ == MUSIC) {
        setScreenState(NAVIGATION);
    } else if (state_ == NAVIGATION) {
        setScreenState(STATUS);
    } else if (state_ == STATUS) {
        if (!last_bt_connected_) {
            setScreenState(QR_PAIRING);
        } else {
            showHome();
        }
    } else if (state_ == QR_PAIRING) {
        showHome();
    } else {
        showHome();
    }
    lv_unlock();
}
