#pragma once

#include "../lcd_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "../gif/lvgl_gif.h"
#include "../lvgl_theme.h"
#include "settings.h"
#include "../theme_sync.h"
#include "youtube_audio_poller.h"

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_netif.h>
#include <esp_psram.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <material_symbols.h>
#include <noto_emoji.h>
#include <src/misc/cache/lv_cache.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "board.h"

#define TAG "LcdDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_4);
LV_FONT_DECLARE(font_noto_emoji_30_4);
LV_FONT_DECLARE(font_noto_sans_basic_14_1);
LV_FONT_DECLARE(font_noto_sans_basic_16_4);
LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_14);

#if CONFIG_IDF_TARGET_ESP32P4
inline constexpr uint32_t kMaxDisplayMessages = 40;
#else
inline constexpr uint32_t kMaxDisplayMessages = 20;
#endif

inline lv_color_t GetContrastTextColor(lv_color_t background) {
    uint32_t rgb = lv_color_to_int(background);
    uint8_t red = (rgb >> 16) & 0xFF;
    uint8_t green = (rgb >> 8) & 0xFF;
    uint8_t blue = rgb & 0xFF;
    int luminance = static_cast<int>(0.299f * red + 0.587f * green + 0.114f * blue);
    return (luminance > 140) ? lv_color_black() : lv_color_white();
}

