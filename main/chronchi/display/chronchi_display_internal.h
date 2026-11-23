#pragma once

#include "../chronchi_display.h"
#include "../chronchi_ble.h"
#include "../chronchi_alert.h"
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
inline constexpr BackgroundTheme PRESET_THEMES[] = {
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
inline constexpr int NUM_THEMES = sizeof(PRESET_THEMES) / sizeof(PRESET_THEMES[0]);

// Convert 24-bit RGB hex to RGB565 for seamless canvas blending
inline uint16_t hex_to_rgb565(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline lv_obj_t* create_card(lv_obj_t* parent, int32_t w, int32_t h, int32_t radius) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, radius, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

