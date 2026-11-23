#pragma once

#include <cstddef>
#include <cstdint>

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
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n) ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)
extern "C" {
bool qrcodegen_encodeText(const char* text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion,
                          enum qrcodegen_Mask mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

#include "../nova_display.h"

#include <draw/lv_draw_private.h>
#include <draw/lv_draw_label.h>

#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#if CONFIG_SPIRAM
#include <esp_psram.h>
#endif
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_lvgl_port.h>
#include <board.h>
#include <cJSON.h>
#include <material_symbols.h>
#include <settings.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#define TAG "NovaDisplay"

LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(font_material_symbols_20_4);
LV_FONT_DECLARE(font_material_symbols_30_4);

#define FONT_10 (&lv_font_montserrat_10)
#define FONT_12 (&lv_font_montserrat_12)
#define FONT_14 (&lv_font_montserrat_14)
#define FONT_16 (&lv_font_montserrat_14)
#define FONT_20 (&lv_font_montserrat_24)
#define FONT_24 (&lv_font_montserrat_24)
#define FONT_28 (&lv_font_montserrat_24)
#define FONT_36 (&lv_font_montserrat_24)
#define FONT_ICON (&font_material_symbols_20_4)

extern lv_color_t C_BG;
extern lv_color_t C_PANEL;
extern lv_color_t C_PANEL_2;
extern lv_color_t C_BORDER;
extern lv_color_t C_CYAN;
extern lv_color_t C_MINT;
extern lv_color_t C_AMBER;
extern lv_color_t C_VIOLET;
extern lv_color_t C_RED;
extern lv_color_t C_TEXT;
extern lv_color_t C_MUTED;
extern lv_color_t C_DARK;

// Board-private callbacks shared by multiple Nova feature modules.
void OnLaunchAiVoice(lv_event_t* event);
void OnLaunchChronchi(lv_event_t* event);
void OnMuteTileClicked(lv_event_t* event);
void OnNavClicked(lv_event_t* event);

