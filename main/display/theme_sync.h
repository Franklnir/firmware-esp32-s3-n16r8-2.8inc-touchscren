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

