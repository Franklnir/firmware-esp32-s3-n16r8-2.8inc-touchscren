#pragma once

#include <string>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "settings.h"

struct ThemeSyncInfo {
    const char* theme_name;   // XiaoZhi theme name
    int chronchi_layout;      // Chronchi layout preset index
    uint32_t accent_color;    // Accent hex color
    uint32_t bg_color;        // Background hex color
    uint32_t panel_color;     // Card/Panel hex color
    uint32_t panel2_color;    // Active/Pressed Card hex color
    uint32_t border_color;    // Border hex color
    const char* display_name; // Friendly Indonesian name
};

inline const ThemeSyncInfo* GetThemeSyncTable(size_t* count = nullptr) {
    static const ThemeSyncInfo kTable[] = {
        { "dark",   1,  0x39FF14, 0x000000, 0x0E1A11, 0x14281A, 0x006622, "Stabilo Hijau Neon" },
        { "cyan",   2,  0x00FFFF, 0x04161E, 0x0C202F, 0x133044, 0x0369A1, "Stabilo Cyan Laser" },
        { "light",  0,  0xFAFF00, 0x121403, 0x222606, 0x33390A, 0x5C6600, "Stabilo Kuning Neon" },
        { "orange", 3,  0xFF6600, 0x140802, 0x261104, 0x381907, 0x9A3412, "Stabilo Oranye Neon" },
        { "pink",   4,  0xFF007F, 0x14030D, 0x260719, 0x380C26, 0x9D174D, "Stabilo Pink Magenta" },
        { "red",    5,  0xFF1744, 0x140306, 0x26070B, 0x380B12, 0x991B1B, "Stabilo Merah Neon" },
        { "purple", 6,  0xB000FF, 0x0F0314, 0x1E0728, 0x2E0B3D, 0x6B21A8, "Stabilo Ungu Neon" },
        { "blue",   7,  0x00D2FF, 0x040E24, 0x081A3F, 0x0D265A, 0x1D4ED8, "Stabilo Biru Elektrik" },
        { "green",  11, 0x00FF41, 0x021908, 0x042C0E, 0x063E14, 0x15803D, "Stabilo Matrix Green" },
        { "white",  15, 0xFFFFFF, 0x0B1220, 0x141F31, 0x1A2940, 0x38BDF8, "Stabilo Diamond White" }
    };
    if (count) *count = sizeof(kTable) / sizeof(kTable[0]);
    return kTable;
}

// Find theme info by XiaoZhi theme name
inline const ThemeSyncInfo* FindThemeByName(const std::string& name) {
    size_t count = 0;
    const auto* table = GetThemeSyncTable(&count);
    for (size_t i = 0; i < count; ++i) {
        if (name == table[i].theme_name) return &table[i];
    }
    return &table[0]; // default: dark / Neon Green
}

// Find theme info by Chronchi layout index
inline const ThemeSyncInfo* FindThemeByLayout(int layout) {
    size_t count = 0;
    const auto* table = GetThemeSyncTable(&count);
    for (size_t i = 0; i < count; ++i) {
        if (table[i].chronchi_layout == layout) return &table[i];
    }
    // Handle secondary Chronchi layouts mapping to primary themes
    switch (layout) {
        case 8:  return FindThemeByName("light");  // Emas Cerah
        case 9:  return FindThemeByName("cyan");   // Toska
        case 10: return FindThemeByName("orange"); // Peach
        case 12: return FindThemeByName("blue");   // Ice Blue
        case 13: return FindThemeByName("pink");   // Sakura
        case 14: return FindThemeByName("light");  // Lemon
        default: break;
    }
    return &table[0];
}

// Read the currently active synchronized theme from NVS
inline const ThemeSyncInfo* GetCurrentSyncedTheme() {
    size_t count = 0;
    const auto* table = GetThemeSyncTable(&count);

    // 1. Check XiaoZhi display settings
    Settings disp_s("display", true);
    std::string th = disp_s.GetString("theme", "");
    if (!th.empty()) {
        for (size_t i = 0; i < count; ++i) {
            if (th == table[i].theme_name) return &table[i];
        }
    }

    // 2. Check Chronchi layout settings
    Settings chr_s("chronchi", true);
    int layout = chr_s.GetInt("layout", -1);
    if (layout >= 0) {
        return FindThemeByLayout(layout);
    }

    return &table[0]; // Default: Stabilo Hijau Neon
}

// Save synchronized theme across all modes (XiaoZhi, Chronchi, and Nova OS)
inline void SyncThemeGlobal(const std::string& theme_name) {
    const auto* match = FindThemeByName(theme_name);

    // 1. Save to XiaoZhi Display settings
    Settings disp_s("display", true);
    disp_s.SetString("theme", match->theme_name);

    // 2. Save to Chronchi Smartwatch settings
    Settings chr_s("chronchi", true);
    chr_s.SetInt("layout", match->chronchi_layout);
    chr_s.SetInt("color", match->accent_color);

    // 3. Save to Nova Mini OS settings
    Settings nova_s("nova", true);
    nova_s.SetString("theme", match->theme_name);
}

inline void SyncThemeByLayout(int layout) {
    const auto* match = FindThemeByLayout(layout);
    SyncThemeGlobal(match->theme_name);
}

// ----------------------------------------------------------------------------
// Custom Background Presets (15 Premium Choices)
// ----------------------------------------------------------------------------
struct BgSyncInfo {
    const char* id;
    const char* display_name;
    uint32_t swatch_color;   // Warna terang pada dot palet agar terlihat jelas!
    uint32_t bg_color;       // Warna latar belakang yang diterapkan ke layar
    uint32_t panel_color;    // Warna kartu / panel harmonis
    uint32_t panel2_color;   // Warna kartu aktif / ditekan
    uint32_t border_color;   // Warna garis batas kartu
};

inline uint32_t ElevateColor(uint32_t hex, int boost_r, int boost_g, int boost_b) {
    int r = std::min(255, (int)((hex >> 16) & 0xFF) + boost_r);
    int g = std::min(255, (int)((hex >> 8) & 0xFF) + boost_g);
    int b = std::min(255, (int)(hex & 0xFF) + boost_b);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

inline BgSyncInfo MakeCustomBg(uint32_t bg_hex) {
    BgSyncInfo info;
    info.id = "custom";
    info.display_name = "Kustom RGB Mixer";
    int r = (bg_hex >> 16) & 0xFF;
    int g = (bg_hex >> 8) & 0xFF;
    int b = bg_hex & 0xFF;
    int max_c = r;
    if (g > max_c) max_c = g;
    if (b > max_c) max_c = b;
    if (max_c < 1) max_c = 1;
    int sr = std::min(255, (r * 255) / max_c);
    int sg = std::min(255, (g * 255) / max_c);
    int sb = std::min(255, (b * 255) / max_c);
    if (r == 0 && g == 0 && b == 0) {
        sr = sg = sb = 0x71; // nice neutral grey if completely black
    }
    info.swatch_color = ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | (uint32_t)sb;
    info.bg_color     = bg_hex;
    info.panel_color  = ElevateColor(bg_hex, 16, 18, 22);
    info.panel2_color = ElevateColor(bg_hex, 26, 30, 36);
    info.border_color = ElevateColor(bg_hex, 45, 52, 62);
    return info;
}

inline const BgSyncInfo* GetBackgroundTable(size_t* count = nullptr) {
    static const BgSyncInfo kBgTable[] = {
        // --- KATEGORI 1: MONOKROM, SLATE & TITANIUM ---
        // 1. OLED Pure Black (Hitam pekat tanpa kompromi)
        { "oled",        "OLED Pure Black",     0x52525B, 0x000000, 0x0E0E0E, 0x181818, 0x27272A },
        // 2. Deep Slate Grey (Abu-abu slate)
        { "slate",       "Deep Slate Grey",     0x64748B, 0x0F172A, 0x1E293B, 0x28384E, 0x334155 },
        // 3. Obsidian Charcoal (Arang gelap)
        { "obsidian",    "Obsidian Charcoal",   0x71717A, 0x141416, 0x1E1E22, 0x28282E, 0x383842 },
        // 4. Graphite Titanium (Grafit abu-abu)
        { "graphite",    "Graphite Titanium",   0x94A3B8, 0x191C22, 0x232730, 0x2F3542, 0x424A5C },

        // --- KATEGORI 2: SAMUDRA, ES & LANGIT (BLUES & TEALS) ---
        // 5. Midnight Space (Biru malam standar Nova)
        { "midnight",    "Midnight Space",      0x3B82F6, 0x0B1220, 0x141F31, 0x1A2940, 0x2B3B52 },
        // 6. Royal Navy Blue (Biru navy elegan)
        { "navy",        "Royal Navy Blue",     0x1D4ED8, 0x081A3E, 0x0E2656, 0x143475, 0x1E4694 },
        // 7. Deep Sapphire Ocean (Biru safir samudra)
        { "sapphire",    "Sapphire Ocean",      0x2563EB, 0x04152D, 0x092348, 0x0E3163, 0x184687 },
        // 8. Arctic Glacier Cyan (Cyan es kutub)
        { "glacier",     "Glacier Arctic Cyan", 0x06B6D4, 0x08242E, 0x0E3544, 0x14495C, 0x1D637C },
        // 9. Lagoon Ocean Teal (Toska samudra)
        { "teal",        "Lagoon Ocean Teal",   0x0D9488, 0x06282E, 0x0B3B44, 0x10505C, 0x156B7A },
        // 10. Deep Aquamarine (Biru hijau aquamarine laut)
        { "aquamarine",  "Deep Aquamarine",     0x14B8A6, 0x042323, 0x083535, 0x0D4848, 0x156363 },

        // --- KATEGORI 3: RIMBUN ALAM & HUTAN (GREENS & OLIVES) ---
        // 11. Dark Forest Pine (Hijau pinus rimba)
        { "forest",      "Dark Forest Pine",    0x16A34A, 0x052B14, 0x0A3F1F, 0x0F562B, 0x16743A },
        // 12. Emerald Jewel (Hijau zamrud elegan)
        { "emerald",     "Emerald Jewel",       0x10B981, 0x032617, 0x063923, 0x0B4E31, 0x126A43 },
        // 13. Cyber Moss Green (Hijau lumut cyber)
        { "moss",        "Cyber Moss Green",    0x84CC16, 0x0F240A, 0x173610, 0x214A17, 0x2E6421 },
        // 14. Midnight Olive (Zaitun malam)
        { "olive",       "Midnight Olive",      0xA3E635, 0x162208, 0x23350D, 0x324A14, 0x45661D },
        // 15. Imperial Matcha (Matcha Jepang)
        { "matcha",      "Imperial Matcha",     0x65A30D, 0x1A280B, 0x273B11, 0x365118, 0x4B6F23 },

        // --- KATEGORI 4: HANGAT, KOPI & BARA (WARM & EARTHY) ---
        // 16. Dark Espresso Coffee (Cokelat kopi hangat)
        { "espresso",    "Dark Espresso",       0xD97706, 0x26140A, 0x3B2011, 0x522D19, 0x734024 },
        // 17. Warm Velvet Mocha (Moka beludru)
        { "mocha",       "Warm Velvet Mocha",   0xB45309, 0x28160E, 0x3D2317, 0x543121, 0x75452E },
        // 18. Toasted Caramel (Karamel manis gelap)
        { "caramel",     "Toasted Caramel",     0xF59E0B, 0x2C1B06, 0x42290B, 0x5A3810, 0x7A4E17 },
        // 19. Rustic Terracotta (Bata tembikar hangat)
        { "terracotta",  "Rustic Terracotta",   0xC2410C, 0x2D130A, 0x421D10, 0x5C2918, 0x7E3A23 },
        // 20. Crimson Lava Ember (Bara api gelap)
        { "ember",       "Crimson Lava Ember",  0xEA580C, 0x2B0D05, 0x421509, 0x5C1E0D, 0x7E2A13 },

        // --- KATEGORI 5: ROMANTIS, MEWAH & CYBERPUNK (REDS & PURPLES) ---
        // 21. Royal Burgundy Wine (Merah anggur mewah)
        { "wine",        "Royal Burgundy Wine", 0xDC2626, 0x2A0812, 0x400D1C, 0x571327, 0x781C37 },
        // 22. Imperial Ruby Red (Merah permata delima)
        { "ruby",        "Imperial Ruby Red",   0xE11D48, 0x2B0715, 0x420D22, 0x5C1330, 0x7E1B43 },
        // 23. Imperial Velvet Purple (Ungu kaisar)
        { "purple",      "Imperial Velvet",     0x9333EA, 0x1F0A33, 0x30114E, 0x42196A, 0x5E2496 },
        // 24. Cyberpunk Amethyst (Kecubung amethyst neon)
        { "amethyst",    "Cyberpunk Amethyst",  0xA855F7, 0x1C062E, 0x2E0C49, 0x401265, 0x5A1C8C },
        // 25. Synthwave Magenta (Midnight synthwave)
        { "magenta",     "Synthwave Midnight",  0xD946EF, 0x26072B, 0x3C0D43, 0x54145E, 0x741E80 }
    };
    if (count) *count = sizeof(kBgTable) / sizeof(kBgTable[0]);
    return kBgTable;
}

inline const BgSyncInfo* FindBgById(const std::string& id) {
    static BgSyncInfo s_custom_cache;
    if (id == "custom") {
        Settings nova_s("nova", true);
        uint32_t custom_hex = (uint32_t)nova_s.GetInt("custom_bg", 0x1A2B3C);
        s_custom_cache = MakeCustomBg(custom_hex);
        return &s_custom_cache;
    }
    size_t count = 0;
    const auto* table = GetBackgroundTable(&count);
    for (size_t i = 0; i < count; ++i) {
        if (id == table[i].id) return &table[i];
    }
    return &table[4]; // Default: midnight
}

inline const BgSyncInfo* GetCurrentSyncedBg() {
    // 1. Check Nova OS settings
    Settings nova_s("nova", true);
    std::string bg_id = nova_s.GetString("bg_id", "");
    if (!bg_id.empty()) {
        return FindBgById(bg_id);
    }
    // 2. Check XiaoZhi Display settings
    Settings disp_s("display", true);
    bg_id = disp_s.GetString("bg_id", "");
    if (!bg_id.empty()) {
        return FindBgById(bg_id);
    }
    // 3. Check Chronchi Smartwatch settings
    Settings chr_s("chronchi", true);
    bg_id = chr_s.GetString("bg_id", "");
    if (!bg_id.empty()) {
        return FindBgById(bg_id);
    }
    return FindBgById("midnight");
}

inline void SyncBgGlobal(const std::string& bg_id) {
    const auto* match = FindBgById(bg_id);

    // 1. Save to Nova Mini OS settings
    Settings nova_s("nova", true);
    nova_s.SetString("bg_id", match->id);

    // 2. Save to XiaoZhi Display settings
    Settings disp_s("display", true);
    disp_s.SetString("bg_id", match->id);
    disp_s.SetInt("bg_color", (int)match->bg_color);
    disp_s.SetInt("panel_color", (int)match->panel_color);

    // 3. Save to Chronchi Smartwatch settings
    Settings chr_s("chronchi", true);
    chr_s.SetString("bg_id", match->id);
    chr_s.SetInt("bg_color", (int)match->bg_color);
    chr_s.SetInt("card_bg", (int)match->panel_color);
    chr_s.SetInt("card_border", (int)match->border_color);
}

inline void SyncBgCustom(uint32_t bg_hex) {
    BgSyncInfo custom = MakeCustomBg(bg_hex);

    // 1. Save to Nova Mini OS settings
    Settings nova_s("nova", true);
    nova_s.SetString("bg_id", "custom");
    nova_s.SetInt("custom_bg", (int)bg_hex);

    // 2. Save to XiaoZhi Display settings
    Settings disp_s("display", true);
    disp_s.SetString("bg_id", "custom");
    disp_s.SetInt("bg_color", (int)custom.bg_color);
    disp_s.SetInt("panel_color", (int)custom.panel_color);

    // 3. Save to Chronchi Smartwatch settings
    Settings chr_s("chronchi", true);
    chr_s.SetString("bg_id", "custom");
    chr_s.SetInt("bg_color", (int)custom.bg_color);
    chr_s.SetInt("card_bg", (int)custom.panel_color);
    chr_s.SetInt("card_border", (int)custom.border_color);
}

// ----------------------------------------------------------------------------
// Contrast, Sharpness & Text Weight System
// ----------------------------------------------------------------------------
inline int GetCurrentContrastLevel() {
    Settings nova_s("nova", true);
    return nova_s.GetInt("contrast", 1); // 0: Lembut, 1: Normal, 2: Tinggi, 3: Ekstrem
}

inline void SetCurrentContrastLevel(int level) {
    Settings nova_s("nova", true);
    nova_s.SetInt("contrast", level);
}

inline int GetCurrentSharpnessLevel() {
    Settings nova_s("nova", true);
    return nova_s.GetInt("sharpness", 1); // 0: Halus, 1: Standar, 2: Tajam, 3: Ekstrem
}

inline void SetCurrentSharpnessLevel(int level) {
    Settings nova_s("nova", true);
    nova_s.SetInt("sharpness", level);
}

inline int GetCurrentTextWeightLevel() {
    Settings nova_s("nova", true);
    return nova_s.GetInt("text_weight", 0); // 0: Normal, 1: Sedang, 2: Tebal, 3: Ekstra
}

inline void SetCurrentTextWeightLevel(int level) {
    Settings nova_s("nova", true);
    nova_s.SetInt("text_weight", level);
}

inline void ApplyContrastToThemeColors(uint32_t bg_hex, uint32_t base_panel, uint32_t base_panel2, uint32_t base_border,
                                       int contrast_level,
                                       uint32_t& out_bg, uint32_t& out_panel, uint32_t& out_panel2, uint32_t& out_border) {
    int r = (bg_hex >> 16) & 0xFF;
    int g = (bg_hex >> 8) & 0xFF;
    int b = bg_hex & 0xFF;

    if (contrast_level == 0) {
        // Lembut / Soft (Subdued contrast)
        out_bg = bg_hex;
        out_panel = base_panel;
        out_panel2 = base_panel2;
        out_border = base_border;
    } else if (contrast_level == 1) {
        // Normal / Standar
        out_bg = bg_hex;
        out_panel = base_panel;
        out_panel2 = base_panel2;
        out_border = base_border;
    } else if (contrast_level == 2) {
        // Tinggi / High Contrast: 30% deeper background, brighter cards and borders
        int nr = (r * 65) / 100;
        int ng = (g * 65) / 100;
        int nb = (b * 65) / 100;
        out_bg = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
        out_panel = ElevateColor(base_panel, 14, 16, 20);
        out_panel2 = ElevateColor(base_panel2, 22, 26, 32);
        out_border = ElevateColor(base_border, 32, 38, 48);
    } else {
        // 3: Ekstrem / Ultra OLED Contrast: Pitch black 0x000000 background, crisp sharp cards and borders
        out_bg = 0x000000;
        out_panel = ElevateColor(base_panel, 22, 26, 32);
        out_panel2 = ElevateColor(base_panel2, 34, 40, 48);
        out_border = ElevateColor(base_border, 50, 60, 72);
    }
}

inline uint32_t ApplySharpnessToAccent(uint32_t accent_hex, int sharpness_level) {
    int r = (accent_hex >> 16) & 0xFF;
    int g = (accent_hex >> 8) & 0xFF;
    int b = accent_hex & 0xFF;

    if (sharpness_level == 0) {
        // Halus / Pastel: slightly softer pastel tone
        int avg = (r + g + b) / 3;
        int nr = (r * 75 + avg * 25) / 100;
        int ng = (g * 75 + avg * 25) / 100;
        int nb = (b * 75 + avg * 25) / 100;
        return ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    } else if (sharpness_level == 1) {
        // Standar
        return accent_hex;
    } else if (sharpness_level == 2) {
        // Tajam (Sharp / Neon Boost): boost primary channels, increase vibrant punch
        int max_c = std::max({r, g, b});
        int min_c = std::min({r, g, b});
        int nr = (r > min_c) ? std::min(255, (int)(r * 1.15f)) : (int)(r * 0.85f);
        int ng = (g > min_c) ? std::min(255, (int)(g * 1.15f)) : (int)(g * 0.85f);
        int nb = (b > min_c) ? std::min(255, (int)(b * 1.15f)) : (int)(b * 0.85f);
        if (max_c > 0 && nr == 0 && ng == 0 && nb == 0) return accent_hex;
        return ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    } else {
        // Ekstrem (Ultra Sharp / Max Saturation)
        int max_c = std::max({r, g, b});
        int min_c = std::min({r, g, b});
        int nr = (r == max_c) ? 255 : ((r > min_c) ? std::min(255, (int)(r * 1.3f)) : (int)(r * 0.7f));
        int ng = (g == max_c) ? 255 : ((g > min_c) ? std::min(255, (int)(g * 1.3f)) : (int)(g * 0.7f));
        int nb = (b == max_c) ? 255 : ((b > min_c) ? std::min(255, (int)(b * 1.3f)) : (int)(b * 0.7f));
        return ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    }
}

// ----------------------------------------------------------------------------
// Text Color Synchronization System (Expanded Full Spectrum Palette)
// ----------------------------------------------------------------------------
struct TextColorSyncInfo {
    const char* id;
    const char* display_name;
    uint32_t swatch_color;   // Warna terang pada dot palet agar terlihat jelas
    uint32_t text_color;     // Warna teks utama (0 untuk auto)
    uint32_t muted_color;    // Warna teks sekunder / muted
};

inline TextColorSyncInfo MakeCustomTextColor(uint32_t text_hex) {
    TextColorSyncInfo info;
    info.id = "custom";
    info.display_name = "Kustom RGB Teks";
    info.swatch_color = text_hex;
    info.text_color   = text_hex;
    int r = (text_hex >> 16) & 0xFF;
    int g = (text_hex >> 8) & 0xFF;
    int b = text_hex & 0xFF;
    int mr = (r * 70) / 100;
    int mg = (g * 70) / 100;
    int mb = (b * 70) / 100;
    info.muted_color = ((uint32_t)mr << 16) | ((uint32_t)mg << 8) | (uint32_t)mb;
    return info;
}

inline const TextColorSyncInfo* GetTextColorTable(size_t* count = nullptr) {
    static const TextColorSyncInfo kTextColorTable[] = {
        // --- 1. ADAPTIF ---
        { "auto",        "Otomatis (Adaptif Kontras)",  0x38BDF8, 0x000000, 0x000000 },

        // --- 2. PUTIH & MONOKROM ---
        { "white",       "Putih Kristal (Ultra White)", 0xFFFFFF, 0xFFFFFF, 0xCBD5E1 },
        { "chalk",       "Putih Kapur (Soft White)",    0xF1F5F9, 0xF1F5F9, 0x94A3B8 },
        { "silver",      "Perak Platinum (Silver)",     0xCBD5E1, 0xCBD5E1, 0x64748B },
        { "slate",       "Abu-abu Slate (Steel)",       0x94A3B8, 0x94A3B8, 0x475569 },
        { "black",       "Hitam Pekat (Deep Black)",    0x0F172A, 0x0F172A, 0x475569 },

        // --- 3. KUNING & EMAS ---
        { "yellow",      "Kuning Stabilo (Neon)",       0xFAFF00, 0xFAFF00, 0xD4D700 },
        { "gold",        "Emas Murni (Pure Gold)",      0xFACC15, 0xFACC15, 0xCA8A04 },
        { "amber",       "Emas Amber (Hangat)",         0xF59E0B, 0xF59E0B, 0xD97706 },
        { "warm_yellow", "Kuning Jagung (Warm)",        0xFDE047, 0xFDE047, 0xEAB308 },

        // --- 4. ORANYE & PERSIS ---
        { "orange",      "Oranye Sunset (Vibrant)",     0xFF6600, 0xFF6600, 0xEA580C },
        { "coral",       "Oranye Karang (Coral)",       0xFB923C, 0xFB923C, 0xC2410C },
        { "peach",       "Peach Manis (Pastel)",        0xFDBA74, 0xFDBA74, 0xF97316 },

        // --- 5. MERAH & PINK ---
        { "red",         "Merah Neon (Bright Red)",     0xFF1744, 0xFF1744, 0xDC2626 },
        { "crimson",     "Merah Crimson (Delima)",      0xE11D48, 0xE11D48, 0xBE123C },
        { "rose",        "Mawar Rose (Soft Pink)",      0xFB7185, 0xFB7185, 0xF43F5E },
        { "pink",        "Pink Stabilo (Hot Magenta)",  0xFF007F, 0xFF007F, 0xBE185D },
        { "fuchsia",     "Fuchsia Cyberpunk",           0xD946EF, 0xD946EF, 0xA21CAF },

        // --- 6. UNGU & VIOLET ---
        { "purple",      "Ungu Neon (Electric)",        0xB000FF, 0xB000FF, 0x7E22CE },
        { "violet",      "Lembayung Violet",            0x8B5CF6, 0x8B5CF6, 0x6D28D9 },
        { "indigo",      "Biru Indigo (Deep)",          0x6366F1, 0x6366F1, 0x4338CA },
        { "lavender",    "Lavender (Pastel)",           0xC4B5FD, 0xC4B5FD, 0x8B5CF6 },

        // --- 7. BIRU & ES ---
        { "blue",        "Biru Elektrik (Royal Blue)",  0x00D2FF, 0x00D2FF, 0x0284C7 },
        { "sky",         "Biru Langit (Sky Blue)",      0x38BDF8, 0x38BDF8, 0x0EA5E9 },
        { "ice",         "Cyan Es Kutub (Ice)",         0x7DD3FC, 0x7DD3FC, 0x38BDF8 },
        { "ocean",       "Biru Samudra (Deep Ocean)",   0x2563EB, 0x2563EB, 0x1D4ED8 },

        // --- 8. CYAN & TOSKA ---
        { "cyan",        "Cyan Laser (High Tech)",      0x00FFFF, 0x00FFFF, 0x06B6D4 },
        { "teal",        "Toska Samudra (Teal)",        0x14B8A6, 0x14B8A6, 0x0F766E },
        { "aquamarine",  "Aquamarine Tropis",           0x2DD4BF, 0x2DD4BF, 0x0D9488 },

        // --- 9. HIJAU & LIME ---
        { "green",       "Hijau Matrix (Neon Green)",   0x39FF14, 0x39FF14, 0x16A34A },
        { "lime",        "Hijau Lemon (Cyber Lime)",    0x84CC16, 0x84CC16, 0x65A30D },
        { "emerald",     "Hijau Zamrud (Emerald)",      0x10B981, 0x10B981, 0x059669 },
        { "mint",        "Hijau Mint (Pastel)",         0x7DE0B5, 0x7DE0B5, 0x14B8A6 }
    };
    if (count) *count = sizeof(kTextColorTable) / sizeof(kTextColorTable[0]);
    return kTextColorTable;
}

inline const TextColorSyncInfo* FindTextColorById(const std::string& id) {
    static TextColorSyncInfo s_custom_tc_cache;
    if (id == "custom") {
        Settings nova_s("nova", true);
        uint32_t custom_hex = (uint32_t)nova_s.GetInt("custom_tc", 0x38BDF8);
        s_custom_tc_cache = MakeCustomTextColor(custom_hex);
        return &s_custom_tc_cache;
    }
    size_t count = 0;
    const auto* table = GetTextColorTable(&count);
    for (size_t i = 0; i < count; ++i) {
        if (id == table[i].id) return &table[i];
    }
    return &table[0]; // default: auto
}

inline const TextColorSyncInfo* GetCurrentSyncedTextColor() {
    Settings nova_s("nova", true);
    std::string tc_id = nova_s.GetString("text_color_id", "");
    if (!tc_id.empty()) {
        return FindTextColorById(tc_id);
    }
    Settings disp_s("display", true);
    tc_id = disp_s.GetString("text_color_id", "");
    if (!tc_id.empty()) {
        return FindTextColorById(tc_id);
    }
    return FindTextColorById("auto");
}

inline void SyncTextColorGlobal(const std::string& tc_id) {
    const auto* match = FindTextColorById(tc_id);
    Settings nova_s("nova", true);
    nova_s.SetString("text_color_id", match->id);
    Settings disp_s("display", true);
    disp_s.SetString("text_color_id", match->id);
}

inline void SyncTextColorCustom(uint32_t text_hex) {
    Settings nova_s("nova", true);
    nova_s.SetString("text_color_id", "custom");
    nova_s.SetInt("custom_tc", (int)text_hex);
    Settings disp_s("display", true);
    disp_s.SetString("text_color_id", "custom");
}

inline void ResolveEffectiveTextColor(uint32_t bg_hex, uint32_t& out_text, uint32_t& out_muted) {
    const auto* tc = GetCurrentSyncedTextColor();
    if (tc && strcmp(tc->id, "auto") != 0) {
        out_text = tc->text_color;
        out_muted = tc->muted_color;
        return;
    }
    // Auto contrast based on luminance
    int r = (bg_hex >> 16) & 0xFF;
    int g = (bg_hex >> 8) & 0xFF;
    int b = bg_hex & 0xFF;
    int lum = (int)(0.299f * r + 0.587f * g + 0.114f * b);
    if (lum > 140) {
        out_text = 0x0F172A; // Deep dark slate (black)
        out_muted = 0x475569; // Dark gray
    } else {
        out_text = 0xF1F5FA; // Crisp white
        out_muted = 0x91A0B5; // Cool gray
    }
}


