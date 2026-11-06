#include "lcd_display.h"
#include "application.h"
#include "audio_codec.h"
#include "youtube_audio_poller.h"
#include "assets/lang_config.h"
#include "gif/lvgl_gif.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "theme_sync.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <wifi_manager.h>
#include <ssid_manager.h>
#include <material_symbols.h>
#include <noto_emoji.h>
#include <src/misc/cache/lv_cache.h>
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

const lv_font_t* LcdDisplay::GetChatFont() const {
    switch (chat_font_level_) {
        case 0: return &font_noto_sans_basic_14_1;
        case 1: return &font_noto_sans_basic_16_4;
        case 2: return &font_noto_sans_basic_20_4;
        case 3: return &font_noto_sans_basic_20_4;
        default: return &font_noto_sans_basic_16_4;
    }
}

void LcdDisplay::UpdateTextSizeOverlayUI() {
    if (!text_size_val_label_) return;
    const char* names[] = {"14px (Kecil)", "16px (Sedang)", "20px (Besar)", "30px (Ekstra)"};
    if (chat_font_level_ >= 0 && chat_font_level_ <= 3) {
        lv_label_set_text(text_size_val_label_, names[chat_font_level_]);
    }
}

void LcdDisplay::SetChatFontLevel(int level) {
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    chat_font_level_ = level;

    Settings settings("display", true);
    settings.SetInt("chat_font_lvl", chat_font_level_);

    const lv_font_t* font = GetChatFont();

    DisplayLockGuard lock(this);
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_font(chat_message_label_, font, 0);
    }

    if (content_ != nullptr) {
        uint32_t count = lv_obj_get_child_cnt(content_);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* child = lv_obj_get_child(content_, i);
            if (!child) continue;
            lv_obj_t* bubble = child;
            if (lv_obj_get_child_cnt(child) > 0) {
                lv_obj_t* first = lv_obj_get_child(child, 0);
                if (first && lv_obj_get_user_data(first) != nullptr) {
                    bubble = first;
                }
            }
            if (bubble && lv_obj_get_child_cnt(bubble) > 0) {
                lv_obj_t* txt = lv_obj_get_child(bubble, 0);
                if (txt) {
                    lv_obj_set_style_text_font(txt, font, 0);
                    lv_obj_update_layout(txt);
                }
            }
        }
    }

    UpdateTextSizeOverlayUI();
}

void LcdDisplay::HideControlCenter() {
    DisplayLockGuard lock(this);
    if (!cp_drawer_ || !cp_backdrop_) return;
    if (cp_visible_) {
        cp_visible_ = false;
        lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        if (cp_color_box_) lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);
    }
}


void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_4);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_4);

    // Neo-Brutalism Cyber Yellow Theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFE600)); // Iconic Cyber Yellow #FFE600
    light_theme->set_text_color(lv_color_hex(0x000000));       // Deep solid black #000000
    light_theme->set_chat_background_color(lv_color_hex(0xFFE600));
    light_theme->set_user_bubble_color(lv_color_hex(0x00F0FF)); // Neo Cyan
    light_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // Stark White with black border
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light_theme->set_system_text_color(lv_color_hex(0x000000));
    light_theme->set_border_color(lv_color_hex(0x000000));      // Solid 2px black border
    light_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);
    light_theme->set_emoji_font(emoji_font);

    // Neo-Brutalism Dark Stabilo Green Theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));       // Pitch Black #000000
    dark_theme->set_text_color(lv_color_hex(0x39FF14));             // Stabilo Neon Green #39FF14
    dark_theme->set_chat_background_color(lv_color_hex(0x000000));  // Dark chat background #000000
    dark_theme->set_user_bubble_color(lv_color_hex(0x39FF14));      // Neon Green user bubble
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x121212)); // Deep charcoal with neon border
    dark_theme->set_system_bubble_color(lv_color_hex(0x121212));
    dark_theme->set_system_text_color(lv_color_hex(0x39FF14));      // Stabilo Neon Green
    dark_theme->set_border_color(lv_color_hex(0x39FF14));           // Bold Neon Green border #39FF14
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0055));      // Hot Red/Pink
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    // 3. Blue Theme (Biru Royal / Navy)
    auto blue_theme = new LvglTheme("blue");
    blue_theme->set_background_color(lv_color_hex(0x0A2F6E));
    blue_theme->set_text_color(lv_color_hex(0xBAE6FD));
    blue_theme->set_chat_background_color(lv_color_hex(0x0A2F6E));
    blue_theme->set_user_bubble_color(lv_color_hex(0x38BDF8));
    blue_theme->set_assistant_bubble_color(lv_color_hex(0x0C4A6E));
    blue_theme->set_system_bubble_color(lv_color_hex(0x0C4A6E));
    blue_theme->set_system_text_color(lv_color_hex(0xE0F2FE));
    blue_theme->set_border_color(lv_color_hex(0x38BDF8));
    blue_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    blue_theme->set_text_font(text_font);
    blue_theme->set_icon_font(icon_font);
    blue_theme->set_large_icon_font(large_icon_font);
    blue_theme->set_emoji_font(emoji_font);

    // 4. Red Theme (Merah Crimson)
    auto red_theme = new LvglTheme("red");
    red_theme->set_background_color(lv_color_hex(0x7F1D1D));
    red_theme->set_text_color(lv_color_hex(0xFECACA));
    red_theme->set_chat_background_color(lv_color_hex(0x7F1D1D));
    red_theme->set_user_bubble_color(lv_color_hex(0xEF4444));
    red_theme->set_assistant_bubble_color(lv_color_hex(0x991B1B));
    red_theme->set_system_bubble_color(lv_color_hex(0x991B1B));
    red_theme->set_system_text_color(lv_color_hex(0xFEE2E2));
    red_theme->set_border_color(lv_color_hex(0xF87171));
    red_theme->set_low_battery_color(lv_color_hex(0xFF0055));
    red_theme->set_text_font(text_font);
    red_theme->set_icon_font(icon_font);
    red_theme->set_large_icon_font(large_icon_font);
    red_theme->set_emoji_font(emoji_font);

    // 5. Green Theme (Hijau Emerald)
    auto green_theme = new LvglTheme("green");
    green_theme->set_background_color(lv_color_hex(0x064E3B));
    green_theme->set_text_color(lv_color_hex(0xA7F3D0));
    green_theme->set_chat_background_color(lv_color_hex(0x064E3B));
    green_theme->set_user_bubble_color(lv_color_hex(0x10B981));
    green_theme->set_assistant_bubble_color(lv_color_hex(0x065F46));
    green_theme->set_system_bubble_color(lv_color_hex(0x065F46));
    green_theme->set_system_text_color(lv_color_hex(0xD1FAE5));
    green_theme->set_border_color(lv_color_hex(0x34D399));
    green_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    green_theme->set_text_font(text_font);
    green_theme->set_icon_font(icon_font);
    green_theme->set_large_icon_font(large_icon_font);
    green_theme->set_emoji_font(emoji_font);

    // 6. Purple Theme (Ungu Royal)
    auto purple_theme = new LvglTheme("purple");
    purple_theme->set_background_color(lv_color_hex(0x4C1D95));
    purple_theme->set_text_color(lv_color_hex(0xDDD6FE));
    purple_theme->set_chat_background_color(lv_color_hex(0x4C1D95));
    purple_theme->set_user_bubble_color(lv_color_hex(0xA855F7));
    purple_theme->set_assistant_bubble_color(lv_color_hex(0x5B21B6));
    purple_theme->set_system_bubble_color(lv_color_hex(0x5B21B6));
    purple_theme->set_system_text_color(lv_color_hex(0xEDE9FE));
    purple_theme->set_border_color(lv_color_hex(0xC084FC));
    purple_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    purple_theme->set_text_font(text_font);
    purple_theme->set_icon_font(icon_font);
    purple_theme->set_large_icon_font(large_icon_font);
    purple_theme->set_emoji_font(emoji_font);

    // 7. White Theme (Putih Bersih / WeChat Style)
    auto white_theme = new LvglTheme("white");
    white_theme->set_background_color(lv_color_hex(0xEBEBEB)); // Light WeChat gray background #EBEBEB
    white_theme->set_text_color(lv_color_hex(0x000000));
    white_theme->set_chat_background_color(lv_color_hex(0xEBEBEB));
    white_theme->set_user_bubble_color(lv_color_hex(0x95EC69)); // WeChat Green #95EC69
    white_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // Stark White #FFFFFF
    white_theme->set_system_bubble_color(lv_color_hex(0xDCDCDC));
    white_theme->set_system_text_color(lv_color_hex(0x555555));
    white_theme->set_border_color(lv_color_hex(0xD8D8D8));
    white_theme->set_low_battery_color(lv_color_hex(0xDC2626));
    white_theme->set_text_font(text_font);
    white_theme->set_icon_font(icon_font);
    white_theme->set_large_icon_font(large_icon_font);
    white_theme->set_emoji_font(emoji_font);

    // 8. Orange Theme (Oranye Sunset)
    auto orange_theme = new LvglTheme("orange");
    orange_theme->set_background_color(lv_color_hex(0x7C2D12));
    orange_theme->set_text_color(lv_color_hex(0xFED7AA));
    orange_theme->set_chat_background_color(lv_color_hex(0x7C2D12));
    orange_theme->set_user_bubble_color(lv_color_hex(0xF97316));
    orange_theme->set_assistant_bubble_color(lv_color_hex(0x9A3412));
    orange_theme->set_system_bubble_color(lv_color_hex(0x9A3412));
    orange_theme->set_system_text_color(lv_color_hex(0xFFEDD5));
    orange_theme->set_border_color(lv_color_hex(0xFB923C));
    orange_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    orange_theme->set_text_font(text_font);
    orange_theme->set_icon_font(icon_font);
    orange_theme->set_large_icon_font(large_icon_font);
    orange_theme->set_emoji_font(emoji_font);

    // 9. Pink Theme (Merah Muda / Hot Pink)
    auto pink_theme = new LvglTheme("pink");
    pink_theme->set_background_color(lv_color_hex(0x831843));
    pink_theme->set_text_color(lv_color_hex(0xFCE7F3));
    pink_theme->set_chat_background_color(lv_color_hex(0x831843));
    pink_theme->set_user_bubble_color(lv_color_hex(0xEC4899));
    pink_theme->set_assistant_bubble_color(lv_color_hex(0x9D174D));
    pink_theme->set_system_bubble_color(lv_color_hex(0x9D174D));
    pink_theme->set_system_text_color(lv_color_hex(0xFDF2F8));
    pink_theme->set_border_color(lv_color_hex(0xF472B6));
    pink_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    pink_theme->set_text_font(text_font);
    pink_theme->set_icon_font(icon_font);
    pink_theme->set_large_icon_font(large_icon_font);
    pink_theme->set_emoji_font(emoji_font);

    // 10. Cyan Theme (Biru Laut / Toska)
    auto cyan_theme = new LvglTheme("cyan");
    cyan_theme->set_background_color(lv_color_hex(0x0E7490));
    cyan_theme->set_text_color(lv_color_hex(0xCFFAFE));
    cyan_theme->set_chat_background_color(lv_color_hex(0x0E7490));
    cyan_theme->set_user_bubble_color(lv_color_hex(0x06B6D4));
    cyan_theme->set_assistant_bubble_color(lv_color_hex(0x155E75));
    cyan_theme->set_system_bubble_color(lv_color_hex(0x155E75));
    cyan_theme->set_system_text_color(lv_color_hex(0xECFEFF));
    cyan_theme->set_border_color(lv_color_hex(0x22D3EE));
    cyan_theme->set_low_battery_color(lv_color_hex(0xFF1744));
    cyan_theme->set_text_font(text_font);
    cyan_theme->set_icon_font(icon_font);
    cyan_theme->set_large_icon_font(large_icon_font);
    cyan_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
    theme_manager.RegisterTheme("blue", blue_theme);
    theme_manager.RegisterTheme("red", red_theme);
    theme_manager.RegisterTheme("green", green_theme);
    theme_manager.RegisterTheme("purple", purple_theme);
    theme_manager.RegisterTheme("white", white_theme);
    theme_manager.RegisterTheme("wechat", white_theme);
    theme_manager.RegisterTheme("orange", orange_theme);
    theme_manager.RegisterTheme("pink", pink_theme);
    theme_manager.RegisterTheme("cyan", cyan_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load UI theme and color from settings (default UI is Theme 2 WhatsApp/WeChat chat style)
    Settings settings("display", true);
    chat_font_level_ = settings.GetInt("chat_font_lvl", 1);
    if (chat_font_level_ < 0 || chat_font_level_ > 3) {
        chat_font_level_ = 1;
    }
    ui_theme_id_ = settings.GetInt("ui_theme", 2);
    if (ui_theme_id_ < 1 || ui_theme_id_ > 2) {
        ui_theme_id_ = 2;
    }

    const auto* synced = GetCurrentSyncedTheme();
    std::string theme_name = settings.GetString("theme", (synced && synced->theme_name) ? synced->theme_name : "dark");
    if (theme_name.empty() || LvglThemeManager::GetInstance().GetTheme(theme_name) == nullptr) {
        theme_name = (synced && synced->theme_name) ? synced->theme_name : "dark";
        settings.SetString("theme", theme_name);
    }
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                LcdDisplay* display = static_cast<LcdDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // Initial black screen to prevent blinding flash on cold boot
    uint16_t bg_color = 0x0000;
    std::vector<uint16_t> buffer(width_, bg_color);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .swap_bytes = 1,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

// RGB LCD implementation
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x,
                             bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .swap_bytes = 0,
                .full_refresh = 1,
                .direct_mode = 1,
            },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {
                                                     .bb_mode = true,
                                                     .avoid_tearing = true,
                                                 }};

    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation =
            {
                .swap_xy = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = true,
                .buff_spiram = false,
                .sw_rotate = true,
            },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {.flags = {
                                                     .avoid_tearing = false,
                                                 }};
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

LcdDisplay::~LcdDisplay() {
    SetPreviewImage(nullptr);

    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    if (music_anim_timer_ != nullptr) {
        lv_timer_del(music_anim_timer_);
        music_anim_timer_ = nullptr;
    }
    if (music_container_ != nullptr) {
        lv_obj_del(music_container_);
        music_container_ = nullptr;
    }

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_del(bottom_bar_);
    }
    if (xiaozhi_bottom_nav_ != nullptr) {
        lv_obj_del(xiaozhi_bottom_nav_);
        xiaozhi_bottom_nav_ = nullptr;
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (overlay_backdrop_ != nullptr) {
        lv_obj_del(overlay_backdrop_);
        overlay_backdrop_ = nullptr;
    }
    if (vol_overlay_card_ != nullptr) {
        lv_obj_del(vol_overlay_card_);
        vol_overlay_card_ = nullptr;
    }
    if (bright_overlay_card_ != nullptr) {
        lv_obj_del(bright_overlay_card_);
        bright_overlay_card_ = nullptr;
    }
    if (text_size_overlay_card_ != nullptr) {
        lv_obj_del(text_size_overlay_card_);
        text_size_overlay_card_ = nullptr;
    }
    if (cp_backdrop_ != nullptr) {
        lv_obj_del(cp_backdrop_);
        cp_backdrop_ = nullptr;
    }
    if (cp_drawer_ != nullptr) {
        lv_obj_del(cp_drawer_);
        cp_drawer_ = nullptr;
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void LcdDisplay::Unlock() { lvgl_port_unlock(); }

// Helper to determine whether text should be dark or light for optimal contrast on any bubble background
static inline lv_color_t GetContrastTextColor(lv_color_t bg) {
    uint32_t rgb = lv_color_to_int(bg);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    int lum = (int)(0.299f * r + 0.587f * g + 0.114f * b);
    return (lum > 140) ? lv_color_black() : lv_color_white();
}

void LcdDisplay::SetupUI_Theme2() {
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg = (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_panel = (synced_bg) ? lv_color_hex(synced_bg->panel_color) : lvgl_theme->background_color();
    lv_color_t xz_border = (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, xz_bg, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, xz_bg, 0);
    lv_obj_set_style_border_color(container_, xz_border, 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Preview image for camera stream - full screen display like Theme 1 */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_, height_);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - header with avatar on left, wifi & battery on right (matches Nova OS C_BG & C_BORDER) */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_bar_, xz_bg, 0);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_border_color(top_bar_, xz_border, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, 3, 0);
    lv_obj_set_style_pad_bottom(top_bar_, 3, 0);
    lv_obj_set_style_pad_left(top_bar_, 6, 0);
    lv_obj_set_style_pad_right(top_bar_, 6, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    // Left buttons container: Avatar button + Brightness button
    lv_obj_t* left_box = lv_obj_create(top_bar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 4, 0);

    // 1. Rotation Button (Tap rotates display by 90 degrees)
    lv_obj_t* rot_btn = lv_obj_create(left_box);
    lv_obj_set_size(rot_btn, 24, 24);
    lv_obj_set_style_bg_opa(rot_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rot_btn, 0, 0);
    lv_obj_set_style_pad_all(rot_btn, 0, 0);
    rot_icon_label_ = lv_label_create(rot_btn);
    lv_label_set_text(rot_icon_label_, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(rot_icon_label_, xz_accent, 0);
    lv_obj_center(rot_icon_label_);
    lv_obj_add_flag(rot_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rot_btn, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().RotateDisplay();
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Button (Tap -> Brightness Overlay Slider!)
    lv_obj_t* bright_btn = lv_obj_create(left_box);
    lv_obj_set_size(bright_btn, 24, 24);
    lv_obj_set_style_bg_opa(bright_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bright_btn, 0, 0);
    lv_obj_set_style_pad_all(bright_btn, 0, 0);
    lv_obj_t* br_icon_label = lv_label_create(bright_btn);
    lv_label_set_text(br_icon_label, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_icon_label, icon_font, 0);
    lv_obj_set_style_text_color(br_icon_label, lv_color_hex(0xF59E0B), 0);
    lv_obj_center(br_icon_label);
    lv_obj_add_flag(bright_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bright_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowBrightnessOverlay();
    }, LV_EVENT_CLICKED, this);

    // Right icons container: Bluetooth -> Speaker -> Wifi -> Battery (paling pinggir)
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 3, 0);

    // Bluetooth BLE Button hidden in XiaoZhi mode
    bluetooth_label_ = nullptr;

    // 2. Speaker Button (Tap -> Volume Overlay Slider!)
    lv_obj_t* vol_btn = lv_obj_create(right_icons);
    lv_obj_set_size(vol_btn, 22, 24);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_btn, 0, 0);
    lv_obj_set_style_pad_all(vol_btn, 0, 0);
    mute_label_ = lv_label_create(vol_btn);
    lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x06B6D4), 0);
    lv_obj_center(mute_label_);
    lv_obj_add_flag(vol_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vol_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowVolumeOverlay();
    }, LV_EVENT_CLICKED, this);

    // 3. Wifi Button
    lv_obj_t* wifi_btn = lv_obj_create(right_icons);
    lv_obj_set_size(wifi_btn, 22, 24);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    network_label_ = lv_label_create(wifi_btn);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0x22C55E), 0);
    lv_obj_center(network_label_);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowWifiConfigModal();
    }, LV_EVENT_CLICKED, this);

    // 4. Battery Button (paling pinggir kanan, with battery_label_ and battery_pct_label_)
    lv_obj_t* battery_btn = lv_obj_create(right_icons);
    lv_obj_set_size(battery_btn, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_bg_opa(battery_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_btn, 0, 0);
    lv_obj_set_style_pad_all(battery_btn, 0, 0);
    lv_obj_set_flex_flow(battery_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_btn, 2, 0);

    battery_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    battery_pct_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_pct_label_, "");
    lv_obj_set_style_text_font(battery_pct_label_, text_font, 0);
    lv_obj_set_style_text_color(battery_pct_label_, lvgl_theme->text_color(), 0);

    lv_obj_add_flag(battery_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(battery_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, 60, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, 4, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 56, 0);      // Confined between left buttons and right icons
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(status_bar_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, 80);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, 80);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, lv_pct(100));
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, xz_bg, 0);  // Background for chat area matches theme
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);

    // Enable touch scrolling for chat content
    lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(content_, 34, 0);

    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0);  // Space between messages

    lv_obj_add_flag(content_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display && (display->IsControlCenterVisible() || display->IsOverlayVisible())) {
            display->HideOverlays();
            if (display->IsControlCenterVisible()) {
                display->ToggleControlCenter();
            }
            return;
        }
        auto& app = Application::GetInstance();
        if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
            YouTubeAudioPoller::GetInstance().Abort();
        } else if (app.GetDeviceState() == kDeviceStateSpeaking) {
            app.Schedule([]() {
                Application::GetInstance().AbortSpeaking(kAbortReasonNone);
            });
        }
    }, LV_EVENT_CLICKED, this);

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;
    bottom_bar_ = nullptr;

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0,
                 text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    // Display AI logo (hidden by default in Theme 2)
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    CreateControlCenter();
    CreateDirectOverlays();
    CreateIdleDashboard();
}
#if CONFIG_IDF_TARGET_ESP32P4
#define MAX_MESSAGES 40
#else
#define MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage_Theme2(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage_Theme2('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (idle_dashboard_visible_) {
        idle_dashboard_visible_ = false;
        if (idle_dashboard_) lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        if (content_) lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }
    if (content_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage_Theme2('%s', '%s') failed: content_ is nullptr",
                     role, content);
        }
        return;
    }

    // Check if message count exceeds limit
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // Delete the oldest message (first child object)
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
            // Refresh child count after deletion
            child_count = lv_obj_get_child_cnt(content_);
        }
        // Scroll to the last message immediately (get last_child after deletion)
        if (child_count > 0) {
            lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
            if (last_child != nullptr && lv_obj_is_valid(last_child)) {
                lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
            }
        }
    }

    // Collapse system messages (if it's a system message, check if the last message is also a
    // system message)
    if (strcmp(role, "system") == 0) {
        // Refresh child count to get accurate count after potential deletion above
        child_count = lv_obj_get_child_cnt(content_);
        if (child_count > 0) {
            // Get the last message container
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_is_valid(last_container) &&
                lv_obj_get_child_cnt(last_container) > 0) {
                // Get the bubble inside the container
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr && lv_obj_is_valid(last_bubble)) {
                    // Check if bubble type is system message
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr &&
                        strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // If the last message is also a system message, delete it
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // Hide the centered AI logo
        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Avoid empty message boxes
    if (strlen(content) == 0) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(msg_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(msg_bubble, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_left(msg_bubble, 8, 0);
    lv_obj_set_style_pad_right(msg_bubble, 8, 0);
    lv_obj_set_style_pad_top(msg_bubble, 5, 0);
    lv_obj_set_style_pad_bottom(msg_bubble, 3, 0);
    lv_obj_set_flex_flow(msg_bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(msg_bubble, 1, 0);

    // Create the message text with dynamic chat font size
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_obj_set_style_text_font(msg_text, GetChatFont(), 0);
    lv_label_set_text(msg_text, content);

    // Calculate bubble width constraints (8px left pad + 8px right pad = 16px)
    lv_coord_t max_bubble_w = LV_HOR_RES * 85 / 100;  // 204px
    lv_coord_t min_bubble_w = 60;
    lv_coord_t max_text_w = max_bubble_w - 16;        // 188px text area inside bubble

    // Let LVGL calculate natural text width first
    lv_obj_set_width(msg_text, LV_SIZE_CONTENT);
    lv_obj_update_layout(msg_text);
    lv_coord_t natural_text_w = lv_obj_get_width(msg_text);

    lv_coord_t final_bubble_w;
    if (natural_text_w <= max_text_w) {
        // Fits in a single line
        final_bubble_w = natural_text_w + 16;
        if (final_bubble_w < min_bubble_w) {
            final_bubble_w = min_bubble_w;
        }
    } else {
        // Wraps cleanly into multiple lines
        final_bubble_w = max_bubble_w;
        lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);
    }

    lv_coord_t inner_w = final_bubble_w - 16;
    lv_obj_set_width(msg_text, inner_w);
    lv_obj_set_width(msg_bubble, final_bubble_w);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role and current theme
    if (strcmp(role, "user") == 0) {
        lv_color_t bubble_bg = lvgl_theme->user_bubble_color();
        lv_color_t text_col = GetContrastTextColor(bubble_bg);
        lv_obj_set_style_bg_color(msg_bubble, bubble_bg, 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lvgl_theme->border_color(), 0);
        lv_obj_set_style_text_color(msg_text, text_col, 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        lv_color_t bubble_bg = lvgl_theme->assistant_bubble_color();
        lv_color_t text_col = GetContrastTextColor(bubble_bg);
        lv_obj_set_style_bg_color(msg_bubble, bubble_bg, 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lvgl_theme->border_color(), 0);
        lv_obj_set_style_text_color(msg_text, text_col, 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);

        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"system");
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }

    // Add WhatsApp-style timestamp footer for user and assistant messages
    if (strcmp(role, "system") != 0) {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[16];
        if (timeinfo.tm_year > (2020 - 1900)) {
            snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            int sec = (int)(esp_timer_get_time() / 1000000);
            snprintf(time_str, sizeof(time_str), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
        }

        lv_color_t stamp_color = (strcmp(role, "user") == 0)
                                     ? GetContrastTextColor(lvgl_theme->user_bubble_color())
                                     : GetContrastTextColor(lvgl_theme->assistant_bubble_color());

        lv_obj_t* footer_row = lv_obj_create(msg_bubble);
        lv_obj_set_width(footer_row, inner_w);
        lv_obj_set_height(footer_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(footer_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer_row, 0, 0);
        lv_obj_set_style_pad_all(footer_row, 0, 0);
        lv_obj_set_flex_flow(footer_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(footer_row, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* time_label = lv_label_create(footer_row);
        lv_label_set_text(time_label, time_str);
        lv_obj_set_style_text_font(time_label, &font_noto_sans_basic_14_1, 0);
        lv_obj_set_style_text_color(time_label, stamp_color, 0);
        lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);
    }

    // Create a full-width container to manage alignment cleanly without flex cross-axis fighting
    lv_obj_t* container = lv_obj_create(content_);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_parent(msg_bubble, container);
    if (strcmp(role, "user") == 0) {
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);

    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}

void LcdDisplay::SetPreviewImage_Theme2(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fill screen height (maximize display area, same as Theme 1)
        int scale_w = 128 * width_ / img_dsc->header.w;
        int scale_h = 128 * height_ / img_dsc->header.h;
        int scale = (scale_w > scale_h) ? scale_w : scale_h;  // Fill screen
        
        // Apply zoom if active (1x to 10x)
        if (zoom_level_ > 1) {
            scale *= zoom_level_;
        }
        
        lv_image_set_scale(preview_image_, scale);
    }

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::ClearChatMessages_Theme2() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (music_visualizer_active_) {
        StopMusicVisualizer();
    }
    music_container_ = nullptr;
    for (int i = 0; i < 7; i++) {
        dj_bars_[i] = nullptr;
    }
    music_title_label_ = nullptr;
    music_sub_label_ = nullptr;

    // Use lv_obj_clean to delete all children of content_ (chat message bubbles)
    lv_obj_clean(content_);

    // Reset chat_message_label_ as it has been deleted
    chat_message_label_ = nullptr;

    // Show the centered AI logo (emoji_label_) again
    if (emoji_label_ != nullptr) {
        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Chat messages cleared (Theme 2)");
}

void LcdDisplay::SetupUI_Theme1() {
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg = (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_border = (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, xz_bg, 0);

    /* Clean Solid Dynamic Background Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(container_, xz_bg, 0);
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Bottom layer: emoji_box_ - centered display container */
    emoji_box_ = lv_obj_create(screen);
    lv_obj_set_size(emoji_box_, 200, 200);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);
    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emoji_label_);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    /* Middle layer: preview_image_ - full screen display */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_, height_);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - for status icons (matches Nova OS C_BG & C_BORDER) */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top_bar_, xz_bg, 0);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top_bar_, xz_border, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, 3, 0);
    lv_obj_set_style_pad_bottom(top_bar_, 3, 0);
    lv_obj_set_style_pad_left(top_bar_, 6, 0);
    lv_obj_set_style_pad_right(top_bar_, 6, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);

    // Left buttons container: Rotation button + Robot logo
    lv_obj_t* left_box = lv_obj_create(top_bar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 4, 0);

    lv_obj_t* rot_btn = lv_obj_create(left_box);
    lv_obj_set_size(rot_btn, 24, 24);
    lv_obj_set_style_bg_opa(rot_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rot_btn, 0, 0);
    lv_obj_set_style_pad_all(rot_btn, 0, 0);
    rot_icon_label_ = lv_label_create(rot_btn);
    lv_label_set_text(rot_icon_label_, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(rot_icon_label_, xz_accent, 0);
    lv_obj_center(rot_icon_label_);
    lv_obj_add_flag(rot_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rot_btn, [](lv_event_t* e) {
        (void)e;
        Board::GetInstance().RotateDisplay();
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Button (Tap -> Brightness Overlay Slider!)
    lv_obj_t* bright_btn = lv_obj_create(left_box);
    lv_obj_set_size(bright_btn, 24, 24);
    lv_obj_set_style_bg_opa(bright_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bright_btn, 0, 0);
    lv_obj_set_style_pad_all(bright_btn, 0, 0);
    lv_obj_t* br_icon_label = lv_label_create(bright_btn);
    lv_label_set_text(br_icon_label, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_icon_label, icon_font, 0);
    lv_obj_set_style_text_color(br_icon_label, lv_color_hex(0xF59E0B), 0);
    lv_obj_center(br_icon_label);
    lv_obj_add_flag(bright_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bright_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowBrightnessOverlay();
    }, LV_EVENT_CLICKED, this);

    // Right icons container: Speaker -> Wifi -> Battery (Bluetooth icon hidden in XiaoZhi mode)
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 3, 0);

    bluetooth_label_ = nullptr; // Sembunyikan icon Bluetooth di mode XiaoZhi

    // Speaker Button (Tap -> Volume Overlay Slider!)
    lv_obj_t* vol_btn = lv_obj_create(right_icons);
    lv_obj_set_size(vol_btn, 22, 24);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_btn, 0, 0);
    lv_obj_set_style_pad_all(vol_btn, 0, 0);
    mute_label_ = lv_label_create(vol_btn);
    lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lv_color_hex(0x06B6D4), 0);
    lv_obj_center(mute_label_);
    lv_obj_add_flag(vol_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vol_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowVolumeOverlay();
    }, LV_EVENT_CLICKED, this);

    // Wifi Button
    lv_obj_t* wifi_btn = lv_obj_create(right_icons);
    lv_obj_set_size(wifi_btn, 22, 24);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    network_label_ = lv_label_create(wifi_btn);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lv_color_hex(0x22C55E), 0);
    lv_obj_center(network_label_);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ShowWifiConfigModal();
    }, LV_EVENT_CLICKED, this);

    // Battery Button (paling pinggir kanan)
    lv_obj_t* battery_btn = lv_obj_create(right_icons);
    lv_obj_set_size(battery_btn, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_bg_opa(battery_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_btn, 0, 0);
    lv_obj_set_style_pad_all(battery_btn, 0, 0);
    lv_obj_set_flex_flow(battery_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_btn, 2, 0);

    battery_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    battery_pct_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_pct_label_, "");
    lv_obj_set_style_text_font(battery_pct_label_, text_font, 0);
    lv_obj_set_style_text_color(battery_pct_label_, lvgl_theme->text_color(), 0);

    lv_obj_add_flag(battery_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(battery_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(top_bar_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    lv_obj_add_flag(right_icons, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(right_icons, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, 80, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 50, 0);      // Confined between left logo and right icons (never covers speaker!)
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(status_bar_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, 80);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, 80);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    /* Bottom bar - auto height, grows upward with wrapped text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_width(bottom_bar_, lv_pct(100));
    lv_obj_set_height(bottom_bar_, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* chat_message_label_ placed in bottom_bar_, multiline wrapped display */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, lv_pct(96));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#else
    /* Top layer: Bottom bar - seamless borderless text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_size(bottom_bar_, lv_pct(100), text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_left(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* chat_message_label_ placed in bottom_bar_, single-line horizontal scroll */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, lv_pct(96));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    // Start scrolling after a delay (short text won't scroll)
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#endif

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);

    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // Create Quick Settings Control Center
    CreateControlCenter();
    CreateDirectOverlays();
    CreateIdleDashboard();
}

void LcdDisplay::SetBrightness(uint8_t brightness) {
    auto backlight = Board::GetInstance().GetBacklight();
    if (backlight) {
        backlight->SetBrightness(brightness);
    }
}

void LcdDisplay::CreateControlCenter() {
    auto screen = lv_screen_active();
    if (!screen) return;

    // 1. Semi-transparent backdrop (click to dismiss) - Deep Obsidian Glass
    cp_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(cp_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(cp_backdrop_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(cp_backdrop_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(cp_backdrop_, 0, 0);
    lv_obj_set_style_radius(cp_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(cp_backdrop_, 0, 0);
    lv_obj_align(cp_backdrop_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cp_backdrop_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->ToggleControlCenter();
    }, LV_EVENT_CLICKED, this);

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;

    // 2. Control Drawer (Bottom half screen - Nova Cyber Glass Panel)
    cp_drawer_ = lv_obj_create(screen);
    lv_obj_set_size(cp_drawer_, LV_HOR_RES, 205);
    lv_obj_set_style_radius(cp_drawer_, 16, 0);
    lv_obj_set_style_bg_color(cp_drawer_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(cp_drawer_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(cp_drawer_, 2, 0);
    lv_obj_set_style_border_color(cp_drawer_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(cp_drawer_, 6, 0);
    lv_obj_set_style_pad_row(cp_drawer_, 5, 0);
    lv_obj_set_flex_flow(cp_drawer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cp_drawer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(cp_drawer_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(cp_drawer_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);

    // Top Grab Handle pill
    lv_obj_t* handle = lv_obj_create(cp_drawer_);
    lv_obj_set_size(handle, 32, 4);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(0x71717A), 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);

    // Top Quick Info Row: Wi-Fi, CPU, Mode
    lv_obj_t* top_row = lv_obj_create(cp_drawer_);
    lv_obj_set_size(top_row, 228, 28);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);

    // Wi-Fi Chip
    lv_obj_t* wifi_chip = lv_button_create(top_row);
    lv_obj_set_size(wifi_chip, 72, 26);
    lv_obj_set_style_bg_color(wifi_chip, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(wifi_chip, 6, 0);
    lv_obj_set_style_border_width(wifi_chip, 0, 0);
    lv_obj_set_style_pad_all(wifi_chip, 2, 0);
    cp_wifi_label_ = lv_label_create(wifi_chip);
    lv_label_set_text(cp_wifi_label_, MATERIAL_SYMBOLS_WIFI " Wi-Fi");
    lv_obj_set_style_text_color(cp_wifi_label_, lv_color_hex(0x22C55E), 0);
    lv_obj_center(cp_wifi_label_);
    lv_obj_add_event_cb(wifi_chip, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->HideControlCenter();
        display->ShowWifiConfigModal();
    }, LV_EVENT_CLICKED, this);

    // Text Size Button Chip (Replaced 240M as requested)
    cp_text_size_btn_ = lv_button_create(top_row);
    lv_obj_set_size(cp_text_size_btn_, 72, 26);
    lv_obj_set_style_bg_color(cp_text_size_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(cp_text_size_btn_, 6, 0);
    lv_obj_set_style_border_width(cp_text_size_btn_, 0, 0);
    lv_obj_set_style_pad_all(cp_text_size_btn_, 2, 0);
    cp_text_size_label_ = lv_label_create(cp_text_size_btn_);
    lv_label_set_text(cp_text_size_label_, MATERIAL_SYMBOLS_EDIT_SQUARE " Teks");
    lv_obj_set_style_text_color(cp_text_size_label_, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(cp_text_size_label_);
    lv_obj_add_event_cb(cp_text_size_btn_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->HideControlCenter();
        display->ShowTextSizeOverlay();
    }, LV_EVENT_CLICKED, this);

    // Mode Switch Button Chip
    cp_mode_btn_ = lv_button_create(top_row);
    lv_obj_set_size(cp_mode_btn_, 72, 26);
    lv_obj_set_style_bg_color(cp_mode_btn_, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(cp_mode_btn_, 6, 0);
    lv_obj_set_style_border_width(cp_mode_btn_, 0, 0);
    lv_obj_set_style_pad_all(cp_mode_btn_, 2, 0);
    cp_mode_label_ = lv_label_create(cp_mode_btn_);
    Settings chronchi_set("chronchi", true);
    int current_mode = chronchi_set.GetInt("mode", 0);
    lv_label_set_text(cp_mode_label_, current_mode == 1 ? (MATERIAL_SYMBOLS_WATCH " Watch") : (MATERIAL_SYMBOLS_ROBOT_2 " AI"));
    lv_obj_set_style_text_color(cp_mode_label_, lv_color_white(), 0);
    lv_obj_center(cp_mode_label_);
    lv_obj_add_event_cb(cp_mode_btn_, [](lv_event_t* e) {
        (void)e;
        Settings set("chronchi", true);
        int cur = set.GetInt("mode", 0);
        int target = (cur == 1) ? 0 : 1;
        Board::GetInstance().SwitchToMode(target);
    }, LV_EVENT_CLICKED, this);

    // Middle Row: Volume Slider, Brightness Slider, Theme & Color Card
    lv_obj_t* mid_row = lv_obj_create(cp_drawer_);
    lv_obj_set_size(mid_row, 228, 108);
    lv_obj_set_flex_flow(mid_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(mid_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mid_row, 0, 0);
    lv_obj_set_style_pad_all(mid_row, 0, 0);

    // 1. Speaker Volume Card (Vertical Slider)
    lv_obj_t* vol_card = lv_obj_create(mid_row);
    lv_obj_set_size(vol_card, 52, 104);
    lv_obj_set_style_bg_color(vol_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(vol_card, 12, 0);
    lv_obj_set_style_border_width(vol_card, 0, 0);
    lv_obj_set_style_pad_all(vol_card, 3, 0);
    lv_obj_set_flex_flow(vol_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;

    cp_vol_label_ = lv_label_create(vol_card);
    lv_label_set_text_fmt(cp_vol_label_, "%d%%", cur_vol);
    lv_obj_set_style_text_color(cp_vol_label_, lv_color_white(), 0);

    cp_vol_slider_ = lv_slider_create(vol_card);
    lv_obj_set_size(cp_vol_slider_, 22, 54);
    lv_slider_set_range(cp_vol_slider_, 0, 100);
    lv_slider_set_value(cp_vol_slider_, cur_vol, LV_ANIM_OFF);
    lv_obj_set_style_radius(cp_vol_slider_, 8, 0);
    lv_obj_set_style_bg_color(cp_vol_slider_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_bg_color(cp_vol_slider_, lv_color_hex(0x06B6D4), LV_PART_INDICATOR);
    lv_obj_set_style_radius(cp_vol_slider_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cp_vol_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(cp_vol_slider_, 0, LV_PART_KNOB);

    cp_vol_icon_ = lv_label_create(vol_card);
    lv_label_set_text(cp_vol_icon_, cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    lv_obj_set_style_text_color(cp_vol_icon_, lv_color_hex(0x06B6D4), 0);

    lv_obj_add_event_cb(cp_vol_slider_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int val = lv_slider_get_value(slider);
        auto c = Board::GetInstance().GetAudioCodec();
        if (c) c->SetOutputVolume(val);
        if (display->cp_vol_label_) lv_label_set_text_fmt(display->cp_vol_label_, "%d%%", val);
        if (display->cp_vol_icon_) {
            lv_label_set_text(display->cp_vol_icon_, val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // 2. Brightness Card (Vertical Slider)
    lv_obj_t* br_card = lv_obj_create(mid_row);
    lv_obj_set_size(br_card, 52, 104);
    lv_obj_set_style_bg_color(br_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(br_card, 12, 0);
    lv_obj_set_style_border_width(br_card, 0, 0);
    lv_obj_set_style_pad_all(br_card, 3, 0);
    lv_obj_set_flex_flow(br_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(br_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);

    cp_bright_label_ = lv_label_create(br_card);
    lv_label_set_text_fmt(cp_bright_label_, "%d%%", cur_br);
    lv_obj_set_style_text_color(cp_bright_label_, lv_color_white(), 0);

    cp_bright_slider_ = lv_slider_create(br_card);
    lv_obj_set_size(cp_bright_slider_, 22, 54);
    lv_slider_set_range(cp_bright_slider_, 10, 100);
    lv_slider_set_value(cp_bright_slider_, cur_br, LV_ANIM_OFF);
    lv_obj_set_style_radius(cp_bright_slider_, 8, 0);
    lv_obj_set_style_bg_color(cp_bright_slider_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_bg_color(cp_bright_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(cp_bright_slider_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cp_bright_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(cp_bright_slider_, 0, LV_PART_KNOB);

    lv_obj_t* br_icon = lv_label_create(br_card);
    lv_label_set_text(br_icon, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

    lv_obj_add_event_cb(cp_bright_slider_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int val = lv_slider_get_value(slider);
        display->SetBrightness(val);
        Settings s("display", true);
        s.SetInt("brightness", val);
        if (display->cp_bright_label_) lv_label_set_text_fmt(display->cp_bright_label_, "%d%%", val);
    }, LV_EVENT_VALUE_CHANGED, this);

    // 3. Theme & Color Card
    lv_obj_t* act_card = lv_obj_create(mid_row);
    lv_obj_set_size(act_card, 110, 104);
    lv_obj_set_style_bg_color(act_card, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(act_card, 12, 0);
    lv_obj_set_style_border_width(act_card, 0, 0);
    lv_obj_set_style_pad_all(act_card, 4, 0);
    lv_obj_set_flex_flow(act_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(act_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Button: Icon Thema (Click toggles between Thema 1 and Thema 2)
    cp_theme_icon_btn_ = lv_button_create(act_card);
    lv_obj_set_size(cp_theme_icon_btn_, 100, 42);
    lv_obj_set_style_radius(cp_theme_icon_btn_, 8, 0);
    lv_obj_set_style_bg_color(cp_theme_icon_btn_, lv_color_hex(0x3F3F46), 0);
    cp_theme_icon_label_ = lv_label_create(cp_theme_icon_btn_);
    lv_label_set_text_fmt(cp_theme_icon_label_, MATERIAL_SYMBOLS_STAR " Thema %d", ui_theme_id_);
    lv_obj_set_style_text_color(cp_theme_icon_label_, lv_color_hex(0xFFE600), 0);
    lv_obj_center(cp_theme_icon_label_);

    lv_obj_add_event_cb(cp_theme_icon_btn_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        lv_async_call([](void* user_data) {
            auto disp = static_cast<LcdDisplay*>(user_data);
            int target = (disp->GetUiThemeId() == 1) ? 2 : 1;
            disp->SetUiThemeId(target);
            if (disp->cp_theme_icon_label_) {
                lv_label_set_text_fmt(disp->cp_theme_icon_label_, MATERIAL_SYMBOLS_STAR " Thema %d", target);
            }
            disp->ShowNotification(target == 1 ? "Switched to Thema 1" : "Switched to Thema 2");
        }, display);
    }, LV_EVENT_CLICKED, this);

    // Button: Icon Warna (Click reveals real-time color choices!)
    lv_obj_t* color_btn = lv_button_create(act_card);
    lv_obj_set_size(color_btn, 100, 42);
    lv_obj_set_style_radius(color_btn, 8, 0);
    lv_obj_set_style_bg_color(color_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* color_lbl = lv_label_create(color_btn);
    lv_label_set_text(color_lbl, MATERIAL_SYMBOLS_EXPLORE " Warna");
    lv_obj_set_style_text_color(color_lbl, lv_color_hex(0xEC4899), 0);
    lv_obj_center(color_lbl);

    lv_obj_add_event_cb(color_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display->cp_color_box_) {
            if (lv_obj_has_flag(display->cp_color_box_, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_remove_flag(display->cp_color_box_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(display->cp_color_box_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }, LV_EVENT_CLICKED, this);

    // Bottom Color Palette Row (Hidden until "Warna" is clicked)
    cp_color_box_ = lv_obj_create(cp_drawer_);
    lv_obj_set_size(cp_color_box_, 228, 36);
    lv_obj_set_flex_flow(cp_color_box_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cp_color_box_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cp_color_box_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_radius(cp_color_box_, 8, 0);
    lv_obj_set_style_border_width(cp_color_box_, 0, 0);
    lv_obj_set_style_pad_all(cp_color_box_, 4, 0);
    lv_obj_set_style_pad_column(cp_color_box_, 6, 0);
    lv_obj_set_scroll_dir(cp_color_box_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(cp_color_box_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);

    struct ColorItem {
        const char* theme_name;
        uint32_t color_hex;
    };
    static const ColorItem color_items[] = {
        {"light", 0xFFE600},
        {"dark",  0x39FF14},
        {"blue",  0x38BDF8},
        {"red",   0xEF4444},
        {"green", 0x10B981},
        {"purple",0xA855F7},
        {"white", 0xEBEBEB},
        {"orange",0xF97316},
        {"pink",  0xEC4899},
        {"cyan",  0x06B6D4}
    };

    for (const auto& item : color_items) {
        lv_obj_t* p = lv_button_create(cp_color_box_);
        lv_obj_set_size(p, 24, 24);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(p, lv_color_hex(item.color_hex), 0);
        lv_obj_set_style_border_width(p, 2, 0);
        lv_obj_set_style_border_color(p, lv_color_white(), 0);
        lv_obj_set_style_pad_all(p, 0, 0);

        struct EventData {
            LcdDisplay* display;
            const char* name;
        };
        auto* ed = new EventData{this, item.theme_name};
        lv_obj_add_event_cb(p, [](lv_event_t* e) {
            auto* d = static_cast<EventData*>(lv_event_get_user_data(e));
            auto th = LvglThemeManager::GetInstance().GetTheme(d->name);
            if (th) {
                SyncThemeGlobal(d->name);
                d->display->SetTheme(th);
                d->display->ShowNotification(std::string("Theme: ") + d->name);
            }
        }, LV_EVENT_CLICKED, ed);
    }
}

void LcdDisplay::ToggleControlCenter() {
    DisplayLockGuard lock(this);
    if (!cp_drawer_ || !cp_backdrop_) return;

    HideOverlays();

    cp_visible_ = !cp_visible_;
    if (cp_visible_) {
        UpdateControlCenterValues();
        lv_obj_remove_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cp_backdrop_);
        lv_obj_move_foreground(cp_drawer_);
    } else {
        lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        if (cp_color_box_) lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::UpdateControlCenterValues() {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec && cp_vol_slider_) {
        int v = codec->output_volume();
        lv_slider_set_value(cp_vol_slider_, v, LV_ANIM_OFF);
        if (cp_vol_label_) lv_label_set_text_fmt(cp_vol_label_, "%d%%", v);
        if (cp_vol_icon_) {
            lv_label_set_text(cp_vol_icon_, v > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
        }
    }
    if (cp_bright_slider_) {
        Settings s("display", true);
        int b = s.GetInt("brightness", 70);
        lv_slider_set_value(cp_bright_slider_, b, LV_ANIM_OFF);
        if (cp_bright_label_) lv_label_set_text_fmt(cp_bright_label_, "%d%%", b);
    }
    if (cp_mode_label_) {
        Settings cs("chronchi", true);
        int m = cs.GetInt("mode", 0);
        lv_label_set_text(cp_mode_label_, m == 1 ? (MATERIAL_SYMBOLS_WATCH " Watch") : (MATERIAL_SYMBOLS_ROBOT_2 " AI"));
    }
}

void LcdDisplay::CreateDirectOverlays() {
    auto screen = lv_screen_active();
    if (!screen) return;

    // Common overlay backdrop (tap outside to dismiss) - Obsidian Deep Glass
    overlay_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(overlay_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(overlay_backdrop_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(overlay_backdrop_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(overlay_backdrop_, 0, 0);
    lv_obj_set_style_radius(overlay_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(overlay_backdrop_, 0, 0);
    lv_obj_align(overlay_backdrop_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay_backdrop_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->HideOverlays();
    }, LV_EVENT_CLICKED, this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : nullptr;
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : nullptr;

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel_hex = synced ? synced->panel_color : 0x0B1220;
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x1E293B;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;
    uint32_t accent_hex = synced ? synced->accent_color : 0x00E5FF;

    // --- 1. Volume Overlay Card (Nova Cyber Neon Style) ---
    vol_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(vol_overlay_card_, 120, 195);
    lv_obj_align(vol_overlay_card_, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_radius(vol_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(vol_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(vol_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(vol_overlay_card_, 8, 0);
    lv_obj_set_style_pad_row(vol_overlay_card_, 4, 0);
    lv_obj_set_flex_flow(vol_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vol_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(vol_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);

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
    if (icon_font) lv_obj_set_style_text_font(vol_overlay_icon_, icon_font, 0);
    lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(accent_hex), 0);

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;

    vol_overlay_label_ = lv_label_create(vol_hdr);
    lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (text_font) lv_obj_set_style_text_font(vol_overlay_label_, text_font, 0);
    lv_obj_set_style_text_color(vol_overlay_label_, lv_color_white(), 0);

    // Large Vertical Slider: 46px wide x 120px tall
    vol_overlay_slider_ = lv_slider_create(vol_overlay_card_);
    lv_obj_set_size(vol_overlay_slider_, 46, 120);
    lv_slider_set_range(vol_overlay_slider_, 0, 100);
    lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    lv_obj_set_style_radius(vol_overlay_slider_, 10, 0);
    lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(accent_hex), LV_PART_INDICATOR);
    lv_obj_set_style_radius(vol_overlay_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(vol_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(vol_overlay_slider_, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(vol_overlay_slider_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int val = lv_slider_get_value(slider);
        auto c = Board::GetInstance().GetAudioCodec();
        if (c) c->SetOutputVolume(val);
        if (display->vol_overlay_label_) lv_label_set_text_fmt(display->vol_overlay_label_, "%d%%", val);
        if (display->vol_overlay_icon_) {
            lv_label_set_text(display->vol_overlay_icon_,
                val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
        }
        if (display->mute_label_) {
            lv_label_set_text(display->mute_label_,
                val > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
        }
        if (display->cp_vol_slider_) {
            lv_slider_set_value(display->cp_vol_slider_, val, LV_ANIM_OFF);
        }
        if (display->cp_vol_label_) {
            lv_label_set_text_fmt(display->cp_vol_label_, "%d%%", val);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // Hint label
    lv_obj_t* vol_hint = lv_label_create(vol_overlay_card_);
    lv_label_set_text(vol_hint, "▲ Geser Ke Atas/Bawah ▼");
    lv_obj_set_style_text_color(vol_hint, lv_color_hex(0x94A3B8), 0);

    // --- 2. Brightness Overlay Card (Nova Cyber Neon Style) ---
    bright_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(bright_overlay_card_, 120, 195);
    lv_obj_align(bright_overlay_card_, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_radius(bright_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(bright_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bright_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(bright_overlay_card_, 8, 0);
    lv_obj_set_style_pad_row(bright_overlay_card_, 4, 0);
    lv_obj_set_flex_flow(bright_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bright_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(bright_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);

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
    if (icon_font) lv_obj_set_style_text_font(br_icon, icon_font, 0);
    lv_obj_set_style_text_color(br_icon, lv_color_hex(0xF59E0B), 0);

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);

    bright_overlay_label_ = lv_label_create(br_hdr);
    lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);
    if (text_font) lv_obj_set_style_text_font(bright_overlay_label_, text_font, 0);
    lv_obj_set_style_text_color(bright_overlay_label_, lv_color_white(), 0);

    // Large Vertical Slider: 46px wide x 120px tall
    bright_overlay_slider_ = lv_slider_create(bright_overlay_card_);
    lv_obj_set_size(bright_overlay_slider_, 46, 120);
    lv_slider_set_range(bright_overlay_slider_, 10, 100);
    lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    lv_obj_set_style_radius(bright_overlay_slider_, 10, 0);
    lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(0xF59E0B), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bright_overlay_slider_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bright_overlay_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bright_overlay_slider_, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(bright_overlay_slider_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        auto slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int val = lv_slider_get_value(slider);
        display->SetBrightness(val);
        Settings s("display", true);
        s.SetInt("brightness", val);
        if (display->bright_overlay_label_) lv_label_set_text_fmt(display->bright_overlay_label_, "%d%%", val);
        if (display->cp_bright_slider_) {
            lv_slider_set_value(display->cp_bright_slider_, val, LV_ANIM_OFF);
        }
        if (display->cp_bright_label_) {
            lv_label_set_text_fmt(display->cp_bright_label_, "%d%%", val);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // Hint label
    lv_obj_t* br_hint = lv_label_create(bright_overlay_card_);
    lv_label_set_text(br_hint, "▲ Geser Ke Atas/Bawah ▼");
    lv_obj_set_style_text_color(br_hint, lv_color_hex(0x94A3B8), 0);

    // --- 3. Text Size Overlay Card (Nova Cyber Neon Style) ---
    text_size_overlay_card_ = lv_obj_create(screen);
    lv_obj_set_size(text_size_overlay_card_, 200, 115);
    lv_obj_align(text_size_overlay_card_, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(text_size_overlay_card_, 14, 0);
    lv_obj_set_style_bg_color(text_size_overlay_card_, lv_color_hex(panel_hex), 0);
    lv_obj_set_style_bg_opa(text_size_overlay_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(text_size_overlay_card_, 2, 0);
    lv_obj_set_style_border_color(text_size_overlay_card_, lv_color_hex(border_hex), 0);
    lv_obj_set_style_pad_all(text_size_overlay_card_, 10, 0);
    lv_obj_set_flex_flow(text_size_overlay_card_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_size_overlay_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(text_size_overlay_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);

    // Title: "Ukuran Teks Chat"
    lv_obj_t* ts_hdr = lv_label_create(text_size_overlay_card_);
    lv_label_set_text(ts_hdr, MATERIAL_SYMBOLS_EDIT_SQUARE " Ukuran Teks Chat");
    lv_obj_set_style_text_color(ts_hdr, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_text_font(ts_hdr, &font_noto_sans_basic_14_1, 0);

    // Control Row: [-] [ Val ] [+]
    lv_obj_t* ts_row = lv_obj_create(text_size_overlay_card_);
    lv_obj_set_size(ts_row, 180, 48);
    lv_obj_set_style_bg_opa(ts_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ts_row, 0, 0);
    lv_obj_set_style_pad_all(ts_row, 0, 0);
    lv_obj_set_flex_flow(ts_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ts_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Button Minus [-]
    lv_obj_t* btn_minus = lv_button_create(ts_row);
    lv_obj_set_size(btn_minus, 44, 40);
    lv_obj_set_style_radius(btn_minus, 10, 0);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(btn_minus, 1, 0);
    lv_obj_set_style_border_color(btn_minus, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* lbl_minus = lv_label_create(btn_minus);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_set_style_text_font(lbl_minus, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(lbl_minus, lv_color_hex(0xEF4444), 0);
    lv_obj_center(lbl_minus);
    lv_obj_add_event_cb(btn_minus, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display->GetChatFontLevel() > 0) {
            display->SetChatFontLevel(display->GetChatFontLevel() - 1);
        }
    }, LV_EVENT_CLICKED, this);

    // Center Value Label
    text_size_val_label_ = lv_label_create(ts_row);
    lv_obj_set_width(text_size_val_label_, 84);
    lv_obj_set_style_text_align(text_size_val_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(text_size_val_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_size_val_label_, &font_noto_sans_basic_14_1, 0);
    UpdateTextSizeOverlayUI();

    // Button Plus [+]
    lv_obj_t* btn_plus = lv_button_create(ts_row);
    lv_obj_set_size(btn_plus, 44, 40);
    lv_obj_set_style_radius(btn_plus, 10, 0);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_border_width(btn_plus, 1, 0);
    lv_obj_set_style_border_color(btn_plus, lv_color_hex(0x3F3F46), 0);
    lv_obj_t* lbl_plus = lv_label_create(btn_plus);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_set_style_text_font(lbl_plus, &font_noto_sans_basic_20_4, 0);
    lv_obj_set_style_text_color(lbl_plus, lv_color_hex(0x22C55E), 0);
    lv_obj_center(lbl_plus);
    lv_obj_add_event_cb(btn_plus, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display->GetChatFontLevel() < 3) {
            display->SetChatFontLevel(display->GetChatFontLevel() + 1);
        }
    }, LV_EVENT_CLICKED, this);

    // Subtitle Hint
    lv_obj_t* ts_hint = lv_label_create(text_size_overlay_card_);
    lv_label_set_text(ts_hint, "Batas: 14px - 30px");
    lv_obj_set_style_text_color(ts_hint, lv_color_hex(0x71717A), 0);
    lv_obj_set_style_text_font(ts_hint, &font_noto_sans_basic_14_1, 0);
}

void LcdDisplay::ShowVolumeOverlay() {
    DisplayLockGuard lock(this);
    if (!vol_overlay_card_ || !overlay_backdrop_) return;

    HideOverlays();
    if (cp_visible_) ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(vol_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(vol_overlay_card_, lv_color_hex(synced->border_color), 0);
        if (vol_overlay_slider_) {
            lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(synced->panel2_color), 0);
            lv_obj_set_style_bg_color(vol_overlay_slider_, lv_color_hex(synced->accent_color), LV_PART_INDICATOR);
        }
        if (vol_overlay_icon_) {
            lv_obj_set_style_text_color(vol_overlay_icon_, lv_color_hex(synced->accent_color), 0);
        }
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    int cur_vol = codec ? codec->output_volume() : 80;
    if (vol_overlay_slider_) lv_slider_set_value(vol_overlay_slider_, cur_vol, LV_ANIM_OFF);
    if (vol_overlay_label_) lv_label_set_text_fmt(vol_overlay_label_, "%d%%", cur_vol);
    if (vol_overlay_icon_) {
        lv_label_set_text(vol_overlay_icon_, cur_vol > 0 ? MATERIAL_SYMBOLS_VOLUME_UP : MATERIAL_SYMBOLS_VOLUME_OFF);
    }

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(vol_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::ShowBrightnessOverlay() {
    DisplayLockGuard lock(this);
    if (!bright_overlay_card_ || !overlay_backdrop_) return;

    HideOverlays();
    if (cp_visible_) ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(bright_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(bright_overlay_card_, lv_color_hex(synced->border_color), 0);
        if (bright_overlay_slider_) {
            lv_obj_set_style_bg_color(bright_overlay_slider_, lv_color_hex(synced->panel2_color), 0);
        }
    }

    Settings disp_set("display", true);
    int cur_br = disp_set.GetInt("brightness", 70);
    if (bright_overlay_slider_) lv_slider_set_value(bright_overlay_slider_, cur_br, LV_ANIM_OFF);
    if (bright_overlay_label_) lv_label_set_text_fmt(bright_overlay_label_, "%d%%", cur_br);

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(bright_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::ShowTextSizeOverlay() {
    DisplayLockGuard lock(this);
    if (!text_size_overlay_card_ || !overlay_backdrop_) return;

    HideOverlays();
    if (cp_visible_) ToggleControlCenter();

    const auto* synced = GetCurrentSyncedTheme();
    if (synced) {
        lv_obj_set_style_bg_color(text_size_overlay_card_, lv_color_hex(synced->panel_color), 0);
        lv_obj_set_style_border_color(text_size_overlay_card_, lv_color_hex(synced->border_color), 0);
    }

    UpdateTextSizeOverlayUI();

    lv_obj_remove_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_backdrop_);
    lv_obj_move_foreground(text_size_overlay_card_);
    overlay_visible_ = true;
}

void LcdDisplay::CloseRecentAppsModal() {
    if (recent_apps_modal_ != nullptr) {
        lv_obj_del(recent_apps_modal_);
        recent_apps_modal_ = nullptr;
    }
}

void LcdDisplay::ShowRecentAppsModal() {
    DisplayLockGuard lock(this);
    CloseRecentAppsModal();
    if (overlay_backdrop_) lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_) lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_) lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (text_size_overlay_card_) lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    overlay_visible_ = false;
    if (IsControlCenterVisible()) {
        ToggleControlCenter();
    }

    const auto* synced = GetCurrentSyncedTheme();
    uint32_t panel2_hex = synced ? synced->panel2_color : 0x1E293B;
    uint32_t border_hex = synced ? synced->border_color : 0x334155;

    auto screen = lv_screen_active();
    recent_apps_modal_ = lv_obj_create(screen);
    lv_obj_set_pos(recent_apps_modal_, 0, 0);
    lv_obj_set_size(recent_apps_modal_, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(recent_apps_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(recent_apps_modal_, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(recent_apps_modal_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(recent_apps_modal_, 0, 0);
    lv_obj_set_style_radius(recent_apps_modal_, 0, 0);
    lv_obj_set_style_pad_all(recent_apps_modal_, 0, 0);

    // Header Title
    lv_obj_t* title_lbl = lv_label_create(recent_apps_modal_);
    lv_label_set_text(title_lbl, "APLIKASI TERBUKA");
    lv_obj_set_pos(title_lbl, 14, 10);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    lv_obj_t* sub_lbl = lv_label_create(recent_apps_modal_);
    lv_label_set_text(sub_lbl, "Ketuk aplikasi untuk beralih instan");
    lv_obj_set_pos(sub_lbl, 14, 28);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x94A3B8), 0);

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : &BUILTIN_ICON_FONT;

    int modal_w = LV_HOR_RES;
    int modal_h = LV_VER_RES;
    int list_w = (modal_w > 240) ? 280 : (modal_w - 20);
    int list_x = (modal_w - list_w) / 2;
    int list_h = (modal_h < 260) ? (modal_h - 88) : 230;

    // Close button (X)
    lv_obj_t* close_btn = lv_button_create(recent_apps_modal_);
    lv_obj_set_pos(close_btn, modal_w - 36, 8);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(panel2_hex), 0);
    lv_obj_set_style_border_color(close_btn, lv_color_hex(border_hex), 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_font(close_lbl, icon_font, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display) display->CloseRecentAppsModal();
    }, LV_EVENT_CLICKED, this);

    // Scrollable container for App Cards
    lv_obj_t* list = lv_obj_create(recent_apps_modal_);
    lv_obj_set_pos(list, list_x, 46);
    lv_obj_set_size(list, list_w, list_h);
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
        int action; // 0: close (active xz), 1: watch, 2: nova os, 3: control center
    };

    const AppCardItem items[] = {
        {"Nova Mini OS", "Beranda & Dashboard HP", MATERIAL_SYMBOLS_HOME, lv_color_hex(0x06B6D4), 2},
        {"Xiaozhi AI", "Asisten Suara [Sedang Aktif]", MATERIAL_SYMBOLS_ROBOT_2, lv_color_hex(0x22C55E), 0},
        {"Chronchi Watch", "Smartwatch & Kalender Pintar", MATERIAL_SYMBOLS_WATCH, lv_color_hex(0xF59E0B), 1},
        {"Control Center", "Pengaturan Cepat, Kecerahan & Vol", MATERIAL_SYMBOLS_SETTINGS, lv_color_hex(0xA855F7), 3}
    };

    const int total_items = sizeof(items) / sizeof(items[0]);
    for (int i = 0; i < total_items; ++i) {
        lv_obj_t* card = lv_button_create(list);
        lv_obj_set_pos(card, 0, i * 44);
        lv_obj_set_size(card, list_w - 14, 38);
        lv_obj_set_style_bg_color(card, lv_color_hex(panel2_hex), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(border_hex), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        // App Icon
        lv_obj_t* icon_lbl = lv_label_create(card);
        lv_label_set_text(icon_lbl, items[i].icon);
        lv_obj_set_style_text_font(icon_lbl, icon_font, 0);
        lv_obj_set_style_text_color(icon_lbl, items[i].color, 0);
        lv_obj_set_pos(icon_lbl, 8, 9);

        // Titles
        lv_obj_t* c_title = lv_label_create(card);
        lv_label_set_text(c_title, items[i].name);
        lv_obj_set_pos(c_title, 36, 4);
        lv_obj_set_size(c_title, list_w - 70, 17);
        lv_label_set_long_mode(c_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_title, lv_color_white(), 0);

        lv_obj_t* c_desc = lv_label_create(card);
        lv_label_set_text(c_desc, items[i].desc);
        lv_obj_set_pos(c_desc, 36, 20);
        lv_obj_set_size(c_desc, list_w - 70, 14);
        lv_label_set_long_mode(c_desc, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(c_desc, lv_color_hex(0x64748B), 0);

        // Right arrow
        lv_obj_t* arr = lv_label_create(card);
        lv_label_set_text(arr, ">");
        lv_obj_set_pos(arr, list_w - 30, 10);
        lv_obj_set_style_text_color(arr, items[i].color, 0);

        lv_obj_set_user_data(card, (void*)(intptr_t)items[i].action);
        lv_obj_add_event_cb(card, [](lv_event_t* ev) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(ev));
            if (!display) return;
            int act = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(ev));
            display->CloseRecentAppsModal();
            if (act == 0) {
                // Already in Xiaozhi AI
                return;
            } else if (act == 1) {
                Board::GetInstance().SwitchToMode(1);
            } else if (act == 2) {
                Board::GetInstance().SwitchToMode(2);
            } else if (act == 3) {
                display->ToggleControlCenter();
            }
        }, LV_EVENT_CLICKED, this);
    }

    // Bottom Button: KEMBALI KE BERANDA NOVA
    lv_obj_t* home_btn_bar = lv_button_create(recent_apps_modal_);
    lv_obj_set_pos(home_btn_bar, list_x, modal_h - 36);
    lv_obj_set_size(home_btn_bar, list_w, 30);
    lv_obj_set_style_bg_color(home_btn_bar, lv_color_hex(0x132238), 0);
    lv_obj_set_style_border_color(home_btn_bar, lv_color_hex(0x06B6D4), 0);
    lv_obj_set_style_border_width(home_btn_bar, 1, 0);
    lv_obj_set_style_radius(home_btn_bar, 8, 0);
    lv_obj_set_style_pad_all(home_btn_bar, 0, 0);
    lv_obj_t* btn_lbl = lv_label_create(home_btn_bar);
    lv_label_set_text(btn_lbl, "KEMBALI KE BERANDA NOVA");
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(home_btn_bar, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display) {
            display->CloseRecentAppsModal();
            Board::GetInstance().SwitchToMode(2);
        }
    }, LV_EVENT_CLICKED, this);
}

void LcdDisplay::HideOverlays() {
    DisplayLockGuard lock(this);
    if (overlay_backdrop_) lv_obj_add_flag(overlay_backdrop_, LV_OBJ_FLAG_HIDDEN);
    if (vol_overlay_card_) lv_obj_add_flag(vol_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (bright_overlay_card_) lv_obj_add_flag(bright_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    if (text_size_overlay_card_) lv_obj_add_flag(text_size_overlay_card_, LV_OBJ_FLAG_HIDDEN);
    CloseRecentAppsModal();
    overlay_visible_ = false;
}

void LcdDisplay::SetPreviewImage_Theme1(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fill screen height (maximize display area)
        int scale_w = 128 * width_ / img_dsc->header.w;
        int scale_h = 128 * height_ / img_dsc->header.h;
        int scale = (scale_w > scale_h) ? scale_w : scale_h;  // Fill screen
        
        // Apply zoom if active (1x to 10x)
        if (zoom_level_ > 1) {
            scale *= zoom_level_;
        }
        
        lv_image_set_scale(preview_image_, scale);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::ShowCaptureFlash() {
    DisplayLockGuard lock(this);
    
    // Create white flash overlay
    lv_obj_t* flash = lv_obj_create(lv_screen_active());
    lv_obj_set_size(flash, width_, height_);
    lv_obj_set_style_bg_color(flash, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flash, LV_OPA_80, 0);
    lv_obj_set_style_border_width(flash, 0, 0);
    lv_obj_set_style_radius(flash, 0, 0);
    lv_obj_center(flash);
    
    // Camera icon in center
    lv_obj_t* icon = lv_label_create(flash);
    lv_label_set_text(icon, MATERIAL_SYMBOLS_PHOTO_CAMERA);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(icon, &font_material_symbols_30_4, 0);
    lv_obj_center(icon);
    
    // Use LVGL timer to remove flash after 250ms
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
        lv_obj_t* obj = static_cast<lv_obj_t*>(lv_timer_get_user_data(t));
        if (obj && lv_obj_is_valid(obj)) {
            lv_obj_del(obj);
        }
        lv_timer_del(t);
    }, 250, flash);
    lv_timer_set_repeat_count(timer, 1);
}

void LcdDisplay::SetZoomLevel(int level) {
    DisplayLockGuard lock(this);
    zoom_level_ = std::clamp(level, 1, 10);
    is_zoomed_ = (zoom_level_ > 1);
    
    // Update current preview image scale if visible
    if (preview_image_ && !lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN) && preview_image_cached_) {
        auto img_dsc = preview_image_cached_->image_dsc();
        if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
            int scale_w = 128 * width_ / img_dsc->header.w;
            int scale_h = 128 * height_ / img_dsc->header.h;
            int base_scale = (scale_w > scale_h) ? scale_w : scale_h;
            lv_image_set_scale(preview_image_, base_scale * zoom_level_);
        }
    }
    
    ESP_LOGI(TAG, "Camera zoom level: %dx", zoom_level_);
}

int LcdDisplay::ZoomIn() {
    int new_level = std::min(10, zoom_level_ + 1);
    SetZoomLevel(new_level);
    return zoom_level_;
}

int LcdDisplay::ZoomOut() {
    int new_level = std::max(1, zoom_level_ - 1);
    SetZoomLevel(new_level);
    return zoom_level_;
}

void LcdDisplay::SetZoom(bool zoomed) {
    SetZoomLevel(zoomed ? 2 : 1);
}

void LcdDisplay::SetChatMessage_Theme1(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage_Theme1('%s', '%s') called before SetupUI() - message will be lost!",
                 role, content);
    }
    DisplayLockGuard lock(this);
    if (content != nullptr && content[0] != '\0' && idle_dashboard_visible_) {
        idle_dashboard_visible_ = false;
        if (idle_dashboard_) lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_) lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage_Theme1('%s', '%s') failed: chat_message_label_ is nullptr (SetupUI() "
                     "was called but label not created)",
                     role, content);
        }
        return;
    }
    lv_obj_set_style_text_font(chat_message_label_, GetChatFont(), 0);
    lv_label_set_text(chat_message_label_, content);
    // Show bottom_bar_ only when there is content (and subtitle is not globally hidden)
    if (bottom_bar_ != nullptr) {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else if (!hide_subtitle_) {
            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    // Re-align bottom_bar_ after text change so it stays anchored above the bottom nav
    if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -30);
    }
#endif
}

void LcdDisplay::ClearChatMessages_Theme1() {
    DisplayLockGuard lock(this);
    // In non-wechat mode, just clear the chat message label and hide the bar
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::CreateBottomNav() {
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer) return;

    if (xiaozhi_bottom_nav_ != nullptr) {
        lv_obj_del(xiaozhi_bottom_nav_);
        xiaozhi_bottom_nav_ = nullptr;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : &BUILTIN_ICON_FONT;

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg = (synced_bg) ? lv_color_hex(synced_bg->bg_color) : (lvgl_theme ? lvgl_theme->background_color() : lv_color_hex(0x0B1220));
    lv_color_t xz_border = (synced_bg) ? lv_color_hex(synced_bg->border_color) : (lvgl_theme ? lvgl_theme->border_color() : lv_color_hex(0x2B3B52));
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);
    lv_color_t xz_muted = lv_color_hex(0x91A0B5);

    xiaozhi_bottom_nav_ = lv_obj_create(top_layer);
    lv_obj_set_size(xiaozhi_bottom_nav_, width_, 30);
    lv_obj_align(xiaozhi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(xiaozhi_bottom_nav_, xz_bg, 0);
    lv_obj_set_style_bg_opa(xiaozhi_bottom_nav_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(xiaozhi_bottom_nav_, xz_border, 0);
    lv_obj_set_style_border_side(xiaozhi_bottom_nav_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(xiaozhi_bottom_nav_, 1, 0);
    lv_obj_set_style_radius(xiaozhi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_all(xiaozhi_bottom_nav_, 0, 0);
    lv_obj_set_style_pad_left(xiaozhi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_right(xiaozhi_bottom_nav_, 6, 0);
    lv_obj_set_style_pad_column(xiaozhi_bottom_nav_, 6, 0);
    lv_obj_set_flex_flow(xiaozhi_bottom_nav_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(xiaozhi_bottom_nav_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(xiaozhi_bottom_nav_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Back Button (◀)
    lv_obj_t* back_btn = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(back_btn, 26);
    lv_obj_set_flex_grow(back_btn, 1);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_bg_color(back_btn, xz_bg, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(back_lbl, icon_font, 0);
    lv_obj_set_style_text_color(back_lbl, xz_muted, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (!display) return;
        if (display->IsRecentAppsModalVisible()) {
            display->CloseRecentAppsModal();
            return;
        }
        if (display->IsControlCenterVisible() || display->IsOverlayVisible()) {
            display->HideOverlays();
            if (display->IsControlCenterVisible()) {
                display->ToggleControlCenter();
            }
            return;
        }
        if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
            YouTubeAudioPoller::GetInstance().Abort();
        }
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateSpeaking) {
            app.Schedule([]() {
                Application::GetInstance().AbortSpeaking(kAbortReasonNone);
            });
        }
        Board::GetInstance().SwitchToMode(2); // Kembali ke Nova Mini OS
    }, LV_EVENT_CLICKED, this);

    // 2. Home Button (●)
    lv_obj_t* home_btn = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(home_btn, 26);
    lv_obj_set_flex_grow(home_btn, 1);
    lv_obj_set_style_radius(home_btn, 6, 0);
    lv_obj_set_style_bg_color(home_btn, xz_bg, 0);
    lv_obj_set_style_border_width(home_btn, 0, 0);
    lv_obj_set_style_pad_all(home_btn, 0, 0);
    lv_obj_set_style_shadow_width(home_btn, 0, 0);
    lv_obj_t* home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, MATERIAL_SYMBOLS_HOME);
    lv_obj_set_style_text_font(home_lbl, icon_font, 0);
    lv_obj_set_style_text_color(home_lbl, xz_accent, 0);
    lv_obj_center(home_lbl);
    lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
            YouTubeAudioPoller::GetInstance().Abort();
        }
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateSpeaking) {
            app.Schedule([]() {
                Application::GetInstance().AbortSpeaking(kAbortReasonNone);
            });
        }
        Board::GetInstance().SwitchToMode(2); // Beranda Nova Mini OS
    }, LV_EVENT_CLICKED, this);

    // 3. Recent Apps Button (▢)
    lv_obj_t* recent_btn = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(recent_btn, 26);
    lv_obj_set_flex_grow(recent_btn, 1);
    lv_obj_set_style_radius(recent_btn, 6, 0);
    lv_obj_set_style_bg_color(recent_btn, xz_bg, 0);
    lv_obj_set_style_border_width(recent_btn, 0, 0);
    lv_obj_set_style_pad_all(recent_btn, 0, 0);
    lv_obj_set_style_shadow_width(recent_btn, 0, 0);

    lv_obj_t* sq_shape = lv_obj_create(recent_btn);
    lv_obj_set_size(sq_shape, 14, 14);
    lv_obj_center(sq_shape);
    lv_obj_clear_flag(sq_shape, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(sq_shape, 3, 0);
    lv_obj_set_style_bg_opa(sq_shape, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sq_shape, xz_muted, 0);
    lv_obj_set_style_border_width(sq_shape, 2, 0);
    lv_obj_set_style_pad_all(sq_shape, 0, 0);
    lv_obj_set_style_shadow_width(sq_shape, 0, 0);

    lv_obj_add_event_cb(recent_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display) {
            if (display->IsRecentAppsModalVisible()) {
                display->CloseRecentAppsModal();
            } else {
                display->ShowRecentAppsModal();
            }
        }
    }, LV_EVENT_CLICKED, this);
}

void LcdDisplay::UpdateResolution() {
    DisplayLockGuard lock(this);
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        width_ = lv_display_get_horizontal_resolution(disp);
        height_ = lv_display_get_vertical_resolution(disp);
    }
    if (xiaozhi_bottom_nav_ != nullptr) {
        lv_obj_set_width(xiaozhi_bottom_nav_, width_);
        lv_obj_align(xiaozhi_bottom_nav_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

void LcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    if (ui_theme_id_ == 2) {
        SetupUI_Theme2();
    } else {
        SetupUI_Theme1();
    }
    CreateBottomNav();
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (ui_theme_id_ == 2) {
        SetChatMessage_Theme2(role, content);
    } else {
        SetChatMessage_Theme1(role, content);
    }
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    if (ui_theme_id_ == 2) {
        SetPreviewImage_Theme2(std::move(image));
    } else {
        SetPreviewImage_Theme1(std::move(image));
    }
}

void LcdDisplay::ClearChatMessages() {
    if (ui_theme_id_ == 2) {
        ClearChatMessages_Theme2();
    } else {
        ClearChatMessages_Theme1();
    }
}

bool LcdDisplay::SetUiThemeId(int theme_id) {
    if (theme_id < 1 || theme_id > 2) {
        return false;
    }
    if (theme_id == ui_theme_id_ && setup_ui_called_) {
        return true;
    }
    DisplayLockGuard lock(this);
    DestroyUI();
    ui_theme_id_ = theme_id;
    Settings settings("display", true);
    settings.SetInt("ui_theme", ui_theme_id_);

    SetupUI();
    UpdateStatusBar(true);
    SetTheme(current_theme_);
    int cur_brightness = settings.GetInt("brightness", 70);
    if (cur_brightness < 5) {
        cur_brightness = (cur_brightness <= 0) ? 70 : 5;
    }
    SetBrightness(cur_brightness);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    ESP_LOGI(TAG, "UI theme switched to Theme %d", ui_theme_id_);
    return true;
}

void LcdDisplay::DestroyUI() {
    if (music_visualizer_active_) {
        StopMusicVisualizer();
    }
    if (music_anim_timer_ != nullptr) {
        lv_timer_delete(music_anim_timer_);
        music_anim_timer_ = nullptr;
    }
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }

    auto screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_clean(screen);
    }

    music_container_ = nullptr;
    dj_icon_label_ = nullptr;
    music_title_label_ = nullptr;
    music_sub_label_ = nullptr;
    for (int i = 0; i < 7; i++) {
        dj_bars_[i] = nullptr;
    }
    preview_image_ = nullptr;
    emoji_box_ = nullptr;
    emoji_label_ = nullptr;
    emoji_image_ = nullptr;
    low_battery_popup_ = nullptr;
    low_battery_label_ = nullptr;
    status_bar_ = nullptr;
    status_label_ = nullptr;
    notification_label_ = nullptr;
    bottom_bar_ = nullptr;
    xiaozhi_bottom_nav_ = nullptr;
    recent_apps_modal_ = nullptr;
    side_bar_ = nullptr;
    container_ = nullptr;
    top_bar_ = nullptr;
    rot_icon_label_ = nullptr;
    content_ = nullptr;
    network_label_ = nullptr;
    mute_label_ = nullptr;
    battery_label_ = nullptr;
    battery_pct_label_ = nullptr;
    bluetooth_label_ = nullptr;
    chat_message_label_ = nullptr;
    avatar_circle_ = nullptr;
    cp_backdrop_ = nullptr;
    cp_drawer_ = nullptr;
    cp_wifi_label_ = nullptr;
    cp_text_size_btn_ = nullptr;
    cp_text_size_label_ = nullptr;
    cp_mode_btn_ = nullptr;
    cp_mode_label_ = nullptr;
    cp_vol_slider_ = nullptr;
    cp_vol_label_ = nullptr;
    cp_vol_icon_ = nullptr;
    cp_bright_slider_ = nullptr;
    cp_bright_label_ = nullptr;
    cp_theme_icon_btn_ = nullptr;
    cp_theme_icon_label_ = nullptr;
    cp_color_box_ = nullptr;
    cp_visible_ = false;
    idle_dashboard_ = nullptr;
    idle_time_label_ = nullptr;
    idle_sec_label_ = nullptr;
    idle_date_card_ = nullptr;
    idle_date_label_ = nullptr;
    idle_weather_card_ = nullptr;
    idle_weather_icon_ = nullptr;
    idle_temp_label_ = nullptr;
    idle_cond_label_ = nullptr;
    idle_loc_label_ = nullptr;
    idle_hint_card_ = nullptr;
    idle_footer_label_ = nullptr;
    idle_dashboard_visible_ = false;
    wifi_modal_backdrop_ = nullptr;
    wifi_modal_ = nullptr;
    wifi_list_view_ = nullptr;
    wifi_list_container_ = nullptr;
    wifi_scan_status_label_ = nullptr;
    wifi_pwd_view_ = nullptr;
    wifi_pwd_ssid_label_ = nullptr;
    wifi_pwd_ta_ = nullptr;
    wifi_pwd_eye_btn_ = nullptr;
    wifi_pwd_eye_icon_ = nullptr;
    wifi_keyboard_ = nullptr;
    wifi_connecting_view_ = nullptr;
    wifi_connecting_label_ = nullptr;
    wifi_modal_visible_ = false;
    battery_icon_ = nullptr;
    network_icon_ = nullptr;
    setup_ui_called_ = false;
}

// ── Custom Idle Screen (Digital Clock, Date, Weather & Status) ──
void LcdDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);
    UpdateIdleDashboard();
}

void LcdDisplay::CreateIdleDashboard() {
    auto screen = lv_screen_active();
    (void)screen;
    // Disabled as requested: XiaoZhi maintains clean standard idle view with centered emote
    idle_dashboard_ = nullptr;
    idle_dashboard_visible_ = false;
    return;

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    /* Main Idle Dashboard Container */
    idle_dashboard_ = lv_obj_create(screen);
    lv_obj_set_size(idle_dashboard_, LV_HOR_RES, LV_VER_RES - 34 - 36);
    lv_obj_set_pos(idle_dashboard_, 0, 34);
    lv_obj_set_style_bg_opa(idle_dashboard_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_dashboard_, 0, 0);
    lv_obj_set_style_radius(idle_dashboard_, 0, 0);
    lv_obj_set_style_pad_all(idle_dashboard_, 0, 0);
    lv_obj_set_style_pad_top(idle_dashboard_, 4, 0);
    lv_obj_set_flex_flow(idle_dashboard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(idle_dashboard_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(idle_dashboard_, 6, 0);
    lv_obj_clear_flag(idle_dashboard_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Hero Digital Clock Card (Futuristic Obsidian / Cyan Glass)
    lv_obj_t* time_box = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(time_box, 216, 68);
    lv_obj_set_style_radius(time_box, 14, 0);
    lv_obj_set_style_bg_color(time_box, lv_color_hex(0x090D16), 0);
    lv_obj_set_style_bg_opa(time_box, LV_OPA_90, 0);
    lv_obj_set_style_border_width(time_box, 1, 0);
    lv_obj_set_style_border_color(time_box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_pad_all(time_box, 0, 0);
    lv_obj_set_flex_flow(time_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(time_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(time_box, 0, 0);
    lv_obj_clear_flag(time_box, LV_OBJ_FLAG_SCROLLABLE);

    idle_time_label_ = lv_label_create(time_box);
    lv_obj_set_style_text_font(idle_time_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idle_time_label_, lv_color_hex(0x38BDF8), 0); // Vibrant Electric Sky
    lv_label_set_text(idle_time_label_, "00:00");

    idle_sec_label_ = lv_label_create(time_box);
    lv_obj_set_style_text_font(idle_sec_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(idle_sec_label_, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(idle_sec_label_, "· 00 detik ·");

    // 2. Day & Date Glassmorphic Card (Hari & Tanggal)
    idle_date_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_date_card_, 216, 34);
    lv_obj_set_style_radius(idle_date_card_, 10, 0);
    lv_obj_set_style_bg_color(idle_date_card_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(idle_date_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_date_card_, 1, 0);
    lv_obj_set_style_border_color(idle_date_card_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(idle_date_card_, 0, 0);
    lv_obj_set_flex_flow(idle_date_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_date_card_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(idle_date_card_, 6, 0);
    lv_obj_clear_flag(idle_date_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cal_icon = lv_label_create(idle_date_card_);
    lv_obj_set_style_text_font(cal_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(cal_icon, MATERIAL_SYMBOLS_CALENDAR_MONTH);
    lv_obj_set_style_text_color(cal_icon, lv_color_hex(0x38BDF8), 0);

    idle_date_label_ = lv_label_create(idle_date_card_);
    lv_obj_set_style_text_font(idle_date_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_date_label_, lv_color_hex(0xF1F5F9), 0);
    lv_label_set_text(idle_date_label_, "SABTU, 10 OKT 2026");

    // 3. Modern Weather & Climate Card (Suhu & Cuaca)
    idle_weather_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_weather_card_, 216, 66);
    lv_obj_set_style_radius(idle_weather_card_, 12, 0);
    lv_obj_set_style_bg_color(idle_weather_card_, lv_color_hex(0x0C192C), 0);
    lv_obj_set_style_bg_opa(idle_weather_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_weather_card_, 1, 0);
    lv_obj_set_style_border_color(idle_weather_card_, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_pad_hor(idle_weather_card_, 10, 0);
    lv_obj_set_style_pad_ver(idle_weather_card_, 4, 0);
    lv_obj_set_flex_flow(idle_weather_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_weather_card_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(idle_weather_card_, LV_OBJ_FLAG_SCROLLABLE);

    // Left: Thermostat Icon & Temperature
    lv_obj_t* w_left = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_left, 86, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_left, 0, 0);
    lv_obj_set_style_pad_all(w_left, 0, 0);
    lv_obj_set_flex_flow(w_left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w_left, 4, 0);
    lv_obj_clear_flag(w_left, LV_OBJ_FLAG_SCROLLABLE);

    idle_weather_icon_ = lv_label_create(w_left);
    lv_obj_set_style_text_font(idle_weather_icon_, &BUILTIN_ICON_FONT, 0);
    lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0x38BDF8), 0);
    lv_label_set_text(idle_weather_icon_, MATERIAL_SYMBOLS_DEVICE_THERMOSTAT);

    idle_temp_label_ = lv_label_create(w_left);
    lv_obj_set_style_text_font(idle_temp_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idle_temp_label_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(idle_temp_label_, "28°C");

    // Divider Line
    lv_obj_t* w_div = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_div, 1, 38);
    lv_obj_set_style_bg_color(w_div, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(w_div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w_div, 0, 0);
    lv_obj_set_style_pad_all(w_div, 0, 0);
    lv_obj_clear_flag(w_div, LV_OBJ_FLAG_SCROLLABLE);

    // Right: Condition & Location (with Marquee scrolling)
    lv_obj_t* w_right = lv_obj_create(idle_weather_card_);
    lv_obj_set_size(w_right, 102, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_right, 0, 0);
    lv_obj_set_style_pad_all(w_right, 0, 0);
    lv_obj_set_flex_flow(w_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w_right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(w_right, 2, 0);
    lv_obj_clear_flag(w_right, LV_OBJ_FLAG_SCROLLABLE);

    idle_cond_label_ = lv_label_create(w_right);
    lv_obj_set_style_text_font(idle_cond_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_cond_label_, lv_color_hex(0xFDE047), 0); // Warm Sun Yellow
    lv_obj_set_width(idle_cond_label_, 100);
    lv_label_set_long_mode(idle_cond_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(idle_cond_label_, "Cerah Berawan");

    idle_loc_label_ = lv_label_create(w_right);
    lv_obj_set_style_text_font(idle_loc_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_loc_label_, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_width(idle_loc_label_, 100);
    lv_label_set_long_mode(idle_loc_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(idle_loc_label_, "Jakarta • 72% RH");

    // 4. Quick Action Mic Pill (Tap to talk! with Marquee running text)
    idle_hint_card_ = lv_obj_create(idle_dashboard_);
    lv_obj_set_size(idle_hint_card_, 216, 36);
    lv_obj_set_style_radius(idle_hint_card_, 18, 0);
    lv_obj_set_style_bg_color(idle_hint_card_, lv_color_hex(0x1E1B4B), 0);
    lv_obj_set_style_bg_opa(idle_hint_card_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(idle_hint_card_, 1, 0);
    lv_obj_set_style_border_color(idle_hint_card_, lv_color_hex(0x4F46E5), 0);
    lv_obj_set_style_pad_hor(idle_hint_card_, 10, 0);
    lv_obj_set_style_pad_ver(idle_hint_card_, 0, 0);
    lv_obj_set_flex_flow(idle_hint_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(idle_hint_card_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(idle_hint_card_, 6, 0);
    lv_obj_clear_flag(idle_hint_card_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* mic_icon = lv_label_create(idle_hint_card_);
    lv_obj_set_style_text_font(mic_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(mic_icon, MATERIAL_SYMBOLS_MIC);
    lv_obj_set_style_text_color(mic_icon, lv_color_hex(0xA5B4FC), 0);

    lv_obj_t* hint_text = lv_label_create(idle_hint_card_);
    lv_obj_set_style_text_font(hint_text, text_font, 0);
    lv_obj_set_style_text_color(hint_text, lv_color_hex(0xE0E7FF), 0);
    lv_obj_set_width(hint_text, 172);
    lv_label_set_long_mode(hint_text, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(hint_text, "Ketuk 2x Layar Untuk Bicara   •   Sentuh Disini Untuk Bicara   •   ");

    lv_obj_add_flag(idle_hint_card_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(idle_hint_card_, [](lv_event_t* e) {
        (void)e;
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().ToggleChatState();
        });
    }, LV_EVENT_CLICKED, nullptr);

    // 5. Subtle Footer
    idle_footer_label_ = lv_label_create(idle_dashboard_);
    lv_obj_set_style_text_font(idle_footer_label_, text_font, 0);
    lv_obj_set_style_text_color(idle_footer_label_, lv_color_hex(0x64748B), 0);
    lv_label_set_text(idle_footer_label_, "• XiaoZhi AI  ·  Standby •");

    if (ui_theme_id_ != 2) {
        lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        idle_dashboard_visible_ = false;
    } else {
        idle_dashboard_visible_ = true;
    }
    UpdateIdleDashboard();
}

void LcdDisplay::UpdateIdleDashboard() {
    DisplayLockGuard lock(this);
    if (!idle_dashboard_) return;

    if (ui_theme_id_ != 2) {
        // Theme 1: Emote robot tetap di tengah layar seperti biasa
        if (idle_dashboard_visible_) {
            idle_dashboard_visible_ = false;
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ && lv_obj_has_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN) && !music_visualizer_active_ && (preview_image_ == nullptr || lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN))) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    bool should_show_idle = (state == kDeviceStateIdle) && !music_visualizer_active_ && (preview_image_ == nullptr || lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN));

    if (should_show_idle) {
        if (!idle_dashboard_visible_) {
            idle_dashboard_visible_ = true;
            lv_obj_remove_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (content_) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
            if (ui_theme_id_ != 2 && bottom_bar_) lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }

        // Update clock and date every second
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);

        // Auto-poke SNTP if Wi-Fi connected and time not synced yet!
        esp_netif_ip_info_t ip_info;
        esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        bool wifi_conn = (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0);
        if (tm->tm_year < 125 && wifi_conn) {
            static uint32_t last_idle_sntp = 0;
            uint32_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_idle_sntp >= 4000) {
                last_idle_sntp = now_ms;
                if (!esp_sntp_enabled()) {
                    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    esp_sntp_setservername(0, "id.pool.ntp.org");
                    esp_sntp_setservername(1, "pool.ntp.org");
                    esp_sntp_setservername(2, "time.google.com");
                    esp_sntp_init();
                } else {
                    esp_sntp_restart();
                }
            }
        }

        Settings nova_prefs("nova", true);
        bool is_24h = nova_prefs.GetBool("24hour", true);

        char time_str[32];
        if (tm->tm_year >= 125) {
            if (is_24h) {
                snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
            } else {
                int h = tm->tm_hour;
                h = (h == 0) ? 12 : (h > 12) ? h - 12 : h;
                snprintf(time_str, sizeof(time_str), "%d:%02d", h, tm->tm_min);
            }
        } else {
            snprintf(time_str, sizeof(time_str), "--:--");
        }
        if (idle_time_label_) {
            lv_label_set_text(idle_time_label_, time_str);
        }

        if (idle_sec_label_) {
            char sec_str[32];
            if (tm->tm_year >= 125) {
                if (!is_24h) {
                    const char* ampm = (tm->tm_hour >= 12) ? "PM" : "AM";
                    snprintf(sec_str, sizeof(sec_str), "· %02d detik (%s) ·", tm->tm_sec, ampm);
                } else {
                    snprintf(sec_str, sizeof(sec_str), "· %02d detik ·", tm->tm_sec);
                }
            } else {
                snprintf(sec_str, sizeof(sec_str), wifi_conn ? "· Sinkronisasi Waktu... ·" : "· Hubungkan Wi-Fi ·");
            }
            lv_label_set_text(idle_sec_label_, sec_str);
        }

        if (idle_date_label_) {
            static const char* kDays[] = {"MINGGU", "SENIN", "SELASA", "RABU", "KAMIS", "JUMAT", "SABTU"};
            static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun", "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};
            char date_str[64];
            if (tm->tm_year >= 125) {
                int wday = std::clamp(tm->tm_wday, 0, 6);
                int mon = std::clamp(tm->tm_mon, 0, 11);
                snprintf(date_str, sizeof(date_str), "%s, %d %s %d", kDays[wday], tm->tm_mday, kMonths[mon], tm->tm_year + 1900);
            } else {
                snprintf(date_str, sizeof(date_str), wifi_conn ? "Menyinkronkan Tanggal..." : "Waktu Belum Sinkron");
            }
            lv_label_set_text(idle_date_label_, date_str);
        }

        // Update Weather & Temperature
        Settings weather_settings("weather", true);
        int temp = weather_settings.GetInt("temp", 28);
        std::string cond = weather_settings.GetString("condition", "Cerah Berawan");
        std::string city = weather_settings.GetString("city", "Jakarta");
        int humid = weather_settings.GetInt("humidity", 72);

        if (idle_temp_label_) {
            char temp_str[16];
            snprintf(temp_str, sizeof(temp_str), "%d°C", temp);
            lv_label_set_text(idle_temp_label_, temp_str);
        }

        if (idle_weather_icon_) {
            lv_label_set_text(idle_weather_icon_, MATERIAL_SYMBOLS_DEVICE_THERMOSTAT);
            if (temp >= 33) {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0xEF4444), 0); // Hot Red
            } else if (temp <= 22) {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0x38BDF8), 0); // Cool Cyan
            } else {
                lv_obj_set_style_text_color(idle_weather_icon_, lv_color_hex(0xF59E0B), 0); // Warm Amber
            }
        }

        if (idle_cond_label_) {
            lv_label_set_text(idle_cond_label_, cond.c_str());
        }

        if (idle_loc_label_) {
            char loc_str[48];
            snprintf(loc_str, sizeof(loc_str), "%s • %d%% RH", city.c_str(), humid);
            lv_label_set_text(idle_loc_label_, loc_str);
        }
    } else {
        if (idle_dashboard_visible_) {
            idle_dashboard_visible_ = false;
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_) lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            if (content_) lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
            if (ui_theme_id_ != 2 && bottom_bar_) lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ── Touchscreen Wi-Fi Scanner & Virtual Keyboard ──
struct WifiItemData {
    LcdDisplay* display;
    std::string ssid;
    bool encrypted;
};

void LcdDisplay::CreateWifiModal() {
    if (wifi_modal_ != nullptr) return;

    auto screen = lv_screen_active();
    if (!screen) return;

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    // 1. Semi-transparent Backdrop
    wifi_modal_backdrop_ = lv_obj_create(screen);
    lv_obj_set_size(wifi_modal_backdrop_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(wifi_modal_backdrop_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wifi_modal_backdrop_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(wifi_modal_backdrop_, 0, 0);
    lv_obj_set_style_radius(wifi_modal_backdrop_, 0, 0);
    lv_obj_set_style_pad_all(wifi_modal_backdrop_, 0, 0);
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wifi_modal_backdrop_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->HideWifiConfigModal();
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);

    // 2. Main Modal Card (Width: 236/312, Height: 304/232)
    bool is_land = (LV_HOR_RES > 260);
    int modal_w = is_land ? (LV_HOR_RES - 8) : 236;
    int modal_h = is_land ? (LV_VER_RES - 8) : 304;
    int view_w = modal_w - 10;

    wifi_modal_ = lv_obj_create(screen);
    lv_obj_set_size(wifi_modal_, modal_w, modal_h);
    lv_obj_align(wifi_modal_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(wifi_modal_, lv_color_hex(0x18181B), 0); // Zinc 900
    lv_obj_set_style_bg_opa(wifi_modal_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_modal_, 2, 0);
    lv_obj_set_style_border_color(wifi_modal_, lv_color_hex(0x2563EB), 0); // Vibrant Blue Border
    lv_obj_set_style_radius(wifi_modal_, 12, 0);
    lv_obj_set_style_pad_all(wifi_modal_, 4, 0);
    lv_obj_clear_flag(wifi_modal_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);

    // Top Modal Header: Title, Refresh button, Close button
    lv_obj_t* header = lv_obj_create(wifi_modal_);
    lv_obj_set_size(header, view_w, 32);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 2, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title with Wifi Icon
    lv_obj_t* title_box = lv_obj_create(header);
    lv_obj_set_size(title_box, 140, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_box, 0, 0);
    lv_obj_set_style_pad_all(title_box, 0, 0);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_box, 4, 0);
    lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* h_icon = lv_label_create(title_box);
    lv_obj_set_style_text_font(h_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(h_icon, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_color(h_icon, lv_color_hex(0x38BDF8), 0);

    lv_obj_t* h_title = lv_label_create(title_box);
    lv_obj_set_style_text_font(h_title, text_font, 0);
    lv_obj_set_style_text_color(h_title, lv_color_white(), 0);
    lv_label_set_text(h_title, "Pilih Wi-Fi");

    // Action button container: Refresh and Close
    lv_obj_t* h_btns = lv_obj_create(header);
    lv_obj_set_size(h_btns, 76, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(h_btns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h_btns, 0, 0);
    lv_obj_set_style_pad_all(h_btns, 0, 0);
    lv_obj_set_flex_flow(h_btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(h_btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(h_btns, 6, 0);
    lv_obj_clear_flag(h_btns, LV_OBJ_FLAG_SCROLLABLE);

    wifi_ref_btn_ = lv_button_create(h_btns);
    lv_obj_set_size(wifi_ref_btn_, 32, 28);
    lv_obj_set_style_radius(wifi_ref_btn_, 6, 0);
    lv_obj_set_style_bg_color(wifi_ref_btn_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(wifi_ref_btn_, 0, 0);
    lv_obj_t* ref_lbl = lv_label_create(wifi_ref_btn_);
    lv_obj_set_style_text_font(ref_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(ref_lbl, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_color(ref_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(ref_lbl);
    lv_obj_add_event_cb(wifi_ref_btn_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->StartWifiScan();
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* close_btn = lv_button_create(h_btns);
    lv_obj_set_size(close_btn, 32, 28);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(close_lbl, MATERIAL_SYMBOLS_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(0xEF4444), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->HideWifiConfigModal();
    }, LV_EVENT_CLICKED, this);

    // ─── VIEW 1: Scanned Wi-Fi List ───
    wifi_list_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_list_view_, view_w, modal_h - 44);
    lv_obj_align(wifi_list_view_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_opa(wifi_list_view_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_view_, 0, 0);
    lv_obj_clear_flag(wifi_list_view_, LV_OBJ_FLAG_SCROLLABLE);

    wifi_scan_status_label_ = lv_label_create(wifi_list_view_);
    lv_obj_set_style_text_font(wifi_scan_status_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_scan_status_label_, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(wifi_scan_status_label_, "Memindai Wi-Fi di sekitar...");
    lv_obj_align(wifi_scan_status_label_, LV_ALIGN_TOP_LEFT, 4, 2);

    wifi_list_container_ = lv_obj_create(wifi_list_view_);
    lv_obj_set_size(wifi_list_container_, view_w, modal_h - 70);
    lv_obj_align(wifi_list_container_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(wifi_list_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list_container_, 0, 0);
    lv_obj_set_style_pad_all(wifi_list_container_, 0, 0);
    lv_obj_set_flex_flow(wifi_list_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_list_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_list_container_, 6, 0);
    lv_obj_add_flag(wifi_list_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wifi_list_container_, LV_DIR_VER);

    // ─── VIEW 2: Password Input & Virtual Keyboard ───
    wifi_pwd_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_pwd_view_, view_w, modal_h - 40);
    lv_obj_align(wifi_pwd_view_, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_opa(wifi_pwd_view_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_pwd_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_pwd_view_, 0, 0);
    lv_obj_clear_flag(wifi_pwd_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);

    // Subheader: Back button + Selected SSID
    lv_obj_t* pwd_sub = lv_obj_create(wifi_pwd_view_);
    lv_obj_set_size(pwd_sub, view_w, 26);
    lv_obj_set_style_bg_opa(pwd_sub, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pwd_sub, 0, 0);
    lv_obj_set_style_pad_all(pwd_sub, 0, 0);
    lv_obj_set_flex_flow(pwd_sub, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pwd_sub, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pwd_sub, 6, 0);
    lv_obj_clear_flag(pwd_sub, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_button_create(pwd_sub);
    lv_obj_set_size(back_btn, 28, 24);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_pad_all(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_obj_set_style_text_font(back_lbl, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(back_lbl, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x38BDF8), 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);

    wifi_pwd_ssid_label_ = lv_label_create(pwd_sub);
    lv_obj_set_style_text_font(wifi_pwd_ssid_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_pwd_ssid_label_, lv_color_hex(0xFDE047), 0);
    lv_obj_set_width(wifi_pwd_ssid_label_, view_w - 40);
    lv_label_set_long_mode(wifi_pwd_ssid_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(wifi_pwd_ssid_label_, "SSID: -");

    // Input row: Text Area + Eye Toggle Button (+ action buttons in landscape)
    lv_obj_t* input_row = lv_obj_create(wifi_pwd_view_);
    lv_obj_set_size(input_row, view_w, is_land ? 28 : 36);
    lv_obj_align(input_row, LV_ALIGN_TOP_MID, 0, is_land ? 24 : 28);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(input_row, 4, 0);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    wifi_pwd_ta_ = lv_textarea_create(input_row);
    lv_obj_set_size(wifi_pwd_ta_, is_land ? 134 : (view_w - 44), is_land ? 26 : 34);
    lv_textarea_set_one_line(wifi_pwd_ta_, true);
    lv_textarea_set_placeholder_text(wifi_pwd_ta_, "Kata Sandi");
    // User requested password to be visible!
    wifi_pwd_visible_ = true;
    lv_textarea_set_password_mode(wifi_pwd_ta_, !wifi_pwd_visible_);
    lv_obj_set_style_bg_color(wifi_pwd_ta_, lv_color_hex(0x27272A), 0);
    lv_obj_set_style_text_color(wifi_pwd_ta_, lv_color_white(), 0);
    lv_obj_set_style_border_width(wifi_pwd_ta_, 1, 0);
    lv_obj_set_style_border_color(wifi_pwd_ta_, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_radius(wifi_pwd_ta_, 6, 0);
    lv_obj_set_style_pad_all(wifi_pwd_ta_, is_land ? 3 : 6, 0);

    // Eye toggle button (intip sandi)
    wifi_pwd_eye_btn_ = lv_button_create(input_row);
    lv_obj_set_size(wifi_pwd_eye_btn_, is_land ? 28 : 38, is_land ? 26 : 34);
    lv_obj_set_style_radius(wifi_pwd_eye_btn_, 6, 0);
    lv_obj_set_style_bg_color(wifi_pwd_eye_btn_, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(wifi_pwd_eye_btn_, 0, 0);
    wifi_pwd_eye_icon_ = lv_label_create(wifi_pwd_eye_btn_);
    lv_obj_set_style_text_font(wifi_pwd_eye_icon_, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
    lv_obj_set_style_text_color(wifi_pwd_eye_icon_, lv_color_hex(0x10B981), 0);
    lv_obj_center(wifi_pwd_eye_icon_);

    lv_obj_add_event_cb(wifi_pwd_eye_btn_, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        display->wifi_pwd_visible_ = !display->wifi_pwd_visible_;
        if (display->wifi_pwd_ta_) {
            lv_textarea_set_password_mode(display->wifi_pwd_ta_, !display->wifi_pwd_visible_);
        }
        if (display->wifi_pwd_eye_icon_) {
            if (display->wifi_pwd_visible_) {
                lv_label_set_text(display->wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
                lv_obj_set_style_text_color(display->wifi_pwd_eye_icon_, lv_color_hex(0x10B981), 0); // Green = Visible
            } else {
                lv_label_set_text(display->wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK);
                lv_obj_set_style_text_color(display->wifi_pwd_eye_icon_, lv_color_hex(0xF59E0B), 0); // Amber = Hidden
            }
        }
    }, LV_EVENT_CLICKED, this);

    // In landscape, put action buttons directly into input_row for 0px wasted vertical space!
    lv_obj_t* btn_parent = is_land ? input_row : lv_obj_create(wifi_pwd_view_);
    if (!is_land) {
        lv_obj_set_size(btn_parent, view_w, 30);
        lv_obj_align(btn_parent, LV_ALIGN_TOP_MID, 0, 66);
        lv_obj_set_style_bg_opa(btn_parent, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_parent, 0, 0);
        lv_obj_set_style_pad_all(btn_parent, 0, 0);
        lv_obj_set_flex_flow(btn_parent, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_parent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btn_parent, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t* cancel_btn = lv_button_create(btn_parent);
    lv_obj_set_size(cancel_btn, is_land ? 52 : 80, is_land ? 26 : 28);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_pad_all(cancel_btn, 0, 0);
    lv_obj_t* c_lbl = lv_label_create(cancel_btn);
    lv_obj_set_style_text_font(c_lbl, text_font, 0);
    lv_label_set_text(c_lbl, "Batal");
    lv_obj_center(c_lbl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* conn_btn = lv_button_create(btn_parent);
    lv_obj_set_size(conn_btn, is_land ? 72 : 138, is_land ? 26 : 28);
    lv_obj_set_style_radius(conn_btn, 6, 0);
    lv_obj_set_style_bg_color(conn_btn, lv_color_hex(0x16A34A), 0); // Green Connect
    lv_obj_set_style_pad_all(conn_btn, 0, 0);
    lv_obj_t* conn_lbl = lv_label_create(conn_btn);
    lv_obj_set_style_text_font(conn_lbl, text_font, 0);
    lv_label_set_text(conn_lbl, is_land ? "Konek" : "Hubungkan");
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(conn_btn, [](lv_event_t* e) {
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        const char* pwd = lv_textarea_get_text(display->wifi_pwd_ta_);
        display->ConnectToWifi(display->wifi_selected_ssid_, pwd ? pwd : "");
    }, LV_EVENT_CLICKED, this);

    // Virtual Keyboard (Width: view_w, Height: modal_h - 56 in landscape)
#if LV_USE_KEYBOARD
    wifi_keyboard_ = lv_keyboard_create(wifi_pwd_view_);
    lv_obj_set_size(wifi_keyboard_, view_w, is_land ? (modal_h - 58) : 160);
    lv_obj_align(wifi_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_keyboard_, wifi_pwd_ta_);
    lv_obj_add_event_cb(wifi_keyboard_, [](lv_event_t* e) {
        auto code = lv_event_get_code(e);
        auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (code == LV_EVENT_READY) {
            const char* pwd = lv_textarea_get_text(display->wifi_pwd_ta_);
            display->ConnectToWifi(display->wifi_selected_ssid_, pwd ? pwd : "");
        } else if (code == LV_EVENT_CANCEL) {
            lv_obj_add_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(display->wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
        }
    }, LV_EVENT_ALL, this);
#endif

    // ─── VIEW 3: Connecting Status Overlay ───
    wifi_connecting_view_ = lv_obj_create(wifi_modal_);
    lv_obj_set_size(wifi_connecting_view_, view_w, modal_h - 44);
    lv_obj_align(wifi_connecting_view_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(wifi_connecting_view_, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_border_width(wifi_connecting_view_, 0, 0);
    lv_obj_set_style_pad_all(wifi_connecting_view_, 12, 0);
    lv_obj_set_flex_flow(wifi_connecting_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifi_connecting_view_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(wifi_connecting_view_, 12, 0);
    lv_obj_clear_flag(wifi_connecting_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* conn_icon = lv_label_create(wifi_connecting_view_);
    lv_obj_set_style_text_font(conn_icon, &BUILTIN_ICON_FONT, 0);
    lv_label_set_text(conn_icon, MATERIAL_SYMBOLS_WIFI);
    lv_obj_set_style_text_color(conn_icon, lv_color_hex(0x38BDF8), 0);

    wifi_connecting_label_ = lv_label_create(wifi_connecting_view_);
    lv_obj_set_style_text_font(wifi_connecting_label_, text_font, 0);
    lv_obj_set_style_text_color(wifi_connecting_label_, lv_color_white(), 0);
    lv_obj_set_style_text_align(wifi_connecting_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(wifi_connecting_label_, "Menghubungkan...");
}

void LcdDisplay::ShowWifiConfigModal() {
    DisplayLockGuard lock(this);
    if (wifi_modal_) {
        bool is_land = (LV_HOR_RES > 260);
        int expected_w = is_land ? (LV_HOR_RES - 8) : 236;
        if (lv_obj_get_width(wifi_modal_) != expected_w) {
            lv_obj_del(wifi_modal_);
            wifi_modal_ = nullptr;
            if (wifi_modal_backdrop_) {
                lv_obj_del(wifi_modal_backdrop_);
                wifi_modal_backdrop_ = nullptr;
            }
        }
    }
    CreateWifiModal();
    if (!wifi_modal_ || !wifi_modal_backdrop_) return;

    wifi_modal_visible_ = true;
    lv_obj_remove_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    StartWifiScan();
}

void LcdDisplay::HideWifiConfigModal() {
    DisplayLockGuard lock(this);
    if (!wifi_modal_ || !wifi_modal_backdrop_) return;

    wifi_modal_visible_ = false;
    lv_obj_add_flag(wifi_modal_backdrop_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_modal_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::StartWifiScan() {
    if (!wifi_modal_ || !wifi_list_container_) return;

    // Prevent concurrent/overlapping scans that cause ESP_ERR_WIFI_STATE conflicts
    if (wifi_scanning_.exchange(true)) {
        ESP_LOGI("LcdDisplay", "Wi-Fi scan already in progress, ignoring duplicate trigger");
        return;
    }

    lv_obj_clean(wifi_list_container_);
    if (wifi_scan_status_label_) {
        lv_label_set_text(wifi_scan_status_label_, "Memindai Wi-Fi di sekitar...");
    }
    if (wifi_ref_btn_) {
        lv_obj_add_state(wifi_ref_btn_, LV_STATE_DISABLED);
    }

    xTaskCreate([](void* arg) {
        auto display = static_cast<LcdDisplay*>(arg);

        // 1. Ensure WiFi driver is active and in Station mode
        wifi_mode_t cur_mode;
        if (esp_wifi_get_mode(&cur_mode) != ESP_OK) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
        } else if (cur_mode != WIFI_MODE_STA && cur_mode != WIFI_MODE_APSTA) {
            esp_wifi_set_mode(WIFI_MODE_STA);
        }

        // 2. Temporarily disable modem sleep / power save so the radio catches all beacons & probes
        wifi_ps_type_t orig_ps = WIFI_PS_NONE;
        esp_wifi_get_ps(&orig_ps);
        esp_wifi_set_ps(WIFI_PS_NONE);

        std::vector<std::tuple<std::string, int, bool>> results;
        std::vector<std::string> seen;

        // 3. Robust scan loop with up to 3 attempts
        for (int attempt = 1; attempt <= 3; attempt++) {
            // Cancel any lingering or background scan
            esp_wifi_scan_stop();
            vTaskDelay(pdMS_TO_TICKS(100));

            wifi_scan_config_t scan_config = {};
            scan_config.show_hidden = false;
            scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            scan_config.scan_time.active.min = 120;
            scan_config.scan_time.active.max = 280;

            esp_err_t err = esp_wifi_scan_start(&scan_config, true);
            if (err == ESP_OK) {
                uint16_t ap_count = 0;
                esp_wifi_scan_get_ap_num(&ap_count);
                if (ap_count > 0) {
                    std::vector<wifi_ap_record_t> records(ap_count);
                    esp_wifi_scan_get_ap_records(&ap_count, records.data());

                    std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
                        return a.rssi > b.rssi;
                    });

                    for (const auto& ap : records) {
                        std::string ssid = reinterpret_cast<const char*>(ap.ssid);
                        if (ssid.empty()) continue;
                        if (std::find(seen.begin(), seen.end(), ssid) != seen.end()) continue;
                        seen.push_back(ssid);
                        bool enc = (ap.authmode != WIFI_AUTH_OPEN);
                        results.push_back({ssid, ap.rssi, enc});
                        if (results.size() >= 20) break;
                    }

                    if (!results.empty()) {
                        ESP_LOGI("LcdDisplay", "Wi-Fi scan succeeded on attempt %d: found %d APs", attempt, (int)results.size());
                        break;
                    }
                }
            } else if (err == ESP_ERR_WIFI_STATE) {
                ESP_LOGW("LcdDisplay", "Wi-Fi driver busy on attempt %d (ESP_ERR_WIFI_STATE), checking cached scan...", attempt);
                uint16_t cached_ap_count = 0;
                if (esp_wifi_scan_get_ap_num(&cached_ap_count) == ESP_OK && cached_ap_count > 0) {
                    std::vector<wifi_ap_record_t> records(cached_ap_count);
                    esp_wifi_scan_get_ap_records(&cached_ap_count, records.data());
                    for (const auto& ap : records) {
                        std::string ssid = reinterpret_cast<const char*>(ap.ssid);
                        if (ssid.empty()) continue;
                        if (std::find(seen.begin(), seen.end(), ssid) != seen.end()) continue;
                        seen.push_back(ssid);
                        bool enc = (ap.authmode != WIFI_AUTH_OPEN);
                        results.push_back({ssid, ap.rssi, enc});
                        if (results.size() >= 20) break;
                    }
                    if (!results.empty()) {
                        ESP_LOGI("LcdDisplay", "Recovered %d APs from completed background scan", (int)results.size());
                        break;
                    }
                }
                // Disconnect any ongoing connect attempt to free up the radio
                esp_wifi_scan_stop();
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(250));
            } else {
                ESP_LOGW("LcdDisplay", "esp_wifi_scan_start attempt %d returned %s", attempt, esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(150));
            }
        }

        // 4. Restore original power saving mode
        esp_wifi_set_ps(orig_ps);

        display->wifi_scanning_ = false;

        Application::GetInstance().Schedule([display, results]() {
            display->PopulateWifiScanResults(results);
        });

        vTaskDelete(NULL);
    }, "wifi_scanner", 4096, this, 3, NULL);
}

void LcdDisplay::PopulateWifiScanResults(const std::vector<std::tuple<std::string, int, bool>>& aps) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_ || !wifi_list_container_) return;

    if (wifi_ref_btn_) {
        lv_obj_remove_state(wifi_ref_btn_, LV_STATE_DISABLED);
    }

    lv_obj_clean(wifi_list_container_);

    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme ? lvgl_theme->text_font()->font() : &lv_font_montserrat_14;

    if (aps.empty()) {
        if (wifi_scan_status_label_) {
            lv_label_set_text(wifi_scan_status_label_, "Tidak ada Wi-Fi ditemukan.");
        }

        lv_obj_t* retry_btn = lv_button_create(wifi_list_container_);
        lv_obj_set_size(retry_btn, 170, 36);
        lv_obj_set_style_radius(retry_btn, 8, 0);
        lv_obj_set_style_bg_color(retry_btn, lv_color_hex(0x27272A), 0);
        lv_obj_set_style_border_width(retry_btn, 1, 0);
        lv_obj_set_style_border_color(retry_btn, lv_color_hex(0x0284C7), 0);
        lv_obj_align(retry_btn, LV_ALIGN_CENTER, 0, 16);

        lv_obj_t* retry_lbl = lv_label_create(retry_btn);
        lv_obj_set_style_text_font(retry_lbl, text_font, 0);
        lv_label_set_text(retry_lbl, "🔄 Pindai Ulang");
        lv_obj_set_style_text_color(retry_lbl, lv_color_hex(0x38BDF8), 0);
        lv_obj_center(retry_lbl);

        lv_obj_add_event_cb(retry_btn, [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->StartWifiScan();
        }, LV_EVENT_CLICKED, this);
        return;
    }

    if (wifi_scan_status_label_) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Ditemukan %d Wi-Fi (Sentuh untuk pilih):", (int)aps.size());
        lv_label_set_text(wifi_scan_status_label_, buf);
    }

    for (const auto& [ssid, rssi, enc] : aps) {
        lv_obj_t* btn = lv_button_create(wifi_list_container_);
        lv_obj_set_size(btn, 206, 38);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x27272A), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x3F3F46), 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Left box: Wifi icon + SSID name
        lv_obj_t* left_box = lv_obj_create(btn);
        lv_obj_set_size(left_box, 155, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left_box, 0, 0);
        lv_obj_set_style_pad_all(left_box, 0, 0);
        lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(left_box, 6, 0);
        lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* icon = lv_label_create(left_box);
        lv_obj_set_style_text_font(icon, &BUILTIN_ICON_FONT, 0);
        const char* wifi_glyph = (rssi >= -65) ? MATERIAL_SYMBOLS_WIFI :
                                 ((rssi >= -78) ? MATERIAL_SYMBOLS_WIFI_2_BAR : MATERIAL_SYMBOLS_WIFI_1_BAR);
        lv_label_set_text(icon, wifi_glyph);
        lv_obj_set_style_text_color(icon, lv_color_hex(rssi >= -65 ? 0x22C55E : (rssi >= -78 ? 0xEAB308 : 0xEF4444)), 0);

        lv_obj_t* name = lv_label_create(left_box);
        lv_obj_set_style_text_font(name, text_font, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_width(name, 125);
        lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(name, ssid.c_str());

        // Right box: Lock icon
        lv_obj_t* lock = lv_label_create(btn);
        lv_obj_set_style_text_font(lock, &BUILTIN_ICON_FONT, 0);
        if (enc) {
            lv_label_set_text(lock, MATERIAL_SYMBOLS_LOCK);
            lv_obj_set_style_text_color(lock, lv_color_hex(0xF59E0B), 0);
        } else {
            lv_label_set_text(lock, MATERIAL_SYMBOLS_LOCK_OPEN);
            lv_obj_set_style_text_color(lock, lv_color_hex(0x10B981), 0);
        }

        auto* item_data = new WifiItemData{this, ssid, enc};
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* data = static_cast<WifiItemData*>(lv_event_get_user_data(e));
            if (!data) return;
            if (data->encrypted) {
                data->display->ShowWifiPasswordInput(data->ssid);
            } else {
                data->display->ConnectToWifi(data->ssid, "");
            }
        }, LV_EVENT_CLICKED, item_data);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_DELETE) {
                auto* data = static_cast<WifiItemData*>(lv_event_get_user_data(e));
                delete data;
            }
        }, LV_EVENT_DELETE, item_data);
    }
}

void LcdDisplay::ShowWifiPasswordInput(const std::string& ssid) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_) return;

    wifi_selected_ssid_ = ssid;

    lv_obj_add_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    if (wifi_pwd_ssid_label_) {
        std::string txt = "SSID: " + ssid;
        lv_label_set_text(wifi_pwd_ssid_label_, txt.c_str());
    }

    if (wifi_pwd_ta_) {
        lv_textarea_set_text(wifi_pwd_ta_, "");
        // User requested: "saya mau kata sandi nya bisa di lihat ya"
        wifi_pwd_visible_ = true;
        lv_textarea_set_password_mode(wifi_pwd_ta_, !wifi_pwd_visible_);
    }

    if (wifi_pwd_eye_icon_) {
        lv_label_set_text(wifi_pwd_eye_icon_, MATERIAL_SYMBOLS_LOCK_OPEN);
        lv_obj_set_style_text_color(wifi_pwd_eye_icon_, lv_color_hex(0x10B981), 0);
    }

#if LV_USE_KEYBOARD
    if (wifi_keyboard_ && wifi_pwd_ta_) {
        lv_keyboard_set_textarea(wifi_keyboard_, wifi_pwd_ta_);
    }
#endif
}

void LcdDisplay::ConnectToWifi(const std::string& ssid, const std::string& password) {
    DisplayLockGuard lock(this);
    if (!wifi_modal_) return;

    lv_obj_add_flag(wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);

    if (wifi_connecting_label_) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Menghubungkan ke\n%s...", ssid.c_str());
        lv_label_set_text(wifi_connecting_label_, buf);
    }

    // Save SSID & Password to NVS
    SsidManager::GetInstance().AddSsid(ssid, password);

    // Launch connection attempt in background
    struct ConnectParams {
        LcdDisplay* display;
        std::string ssid;
        std::string password;
    };
    auto* params = new ConnectParams{this, ssid, password};

    xTaskCreate([](void* arg) {
        auto* p = static_cast<ConnectParams*>(arg);
        auto display = p->display;
        std::string target_ssid = p->ssid;
        std::string target_password = p->password;
        delete p;

        // Restart station so WifiManager and WifiStation reload the new SSID from SsidManager immediately
        auto& mgr = WifiManager::GetInstance();
        if (mgr.IsInitialized()) {
            mgr.StopStation();
            vTaskDelay(pdMS_TO_TICKS(100));
            mgr.StartStation();
        } else {
            wifi_config_t wifi_cfg = {};
            strncpy((char*)wifi_cfg.sta.ssid, target_ssid.c_str(), sizeof(wifi_cfg.sta.ssid) - 1);
            strncpy((char*)wifi_cfg.sta.password, target_password.c_str(), sizeof(wifi_cfg.sta.password) - 1);
            wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
            esp_wifi_connect();
        }

        // Wait up to 15 seconds for connection
        bool ok = false;
        for (int i = 0; i < 30; i++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (WifiManager::GetInstance().IsConnected()) {
                ok = true;
                break;
            }
        }

        if (ok) {
            Application::GetInstance().Schedule([display]() {
                DisplayLockGuard lock(display);
                display->ShowNotification("Wi-Fi Berhasil Terhubung!");
                display->HideWifiConfigModal();
                display->UpdateStatusBar(true);
            });
        } else {
            Application::GetInstance().Schedule([display, target_ssid]() {
                DisplayLockGuard lock(display);
                if (display->wifi_connecting_label_) {
                    char err_msg[128];
                    snprintf(err_msg, sizeof(err_msg), "Gagal terhubung ke\n%s!\nPeriksa kata sandi.", target_ssid.c_str());
                    lv_label_set_text(display->wifi_connecting_label_, err_msg);
                }
            });
            vTaskDelay(pdMS_TO_TICKS(2000));
            Application::GetInstance().Schedule([display]() {
                DisplayLockGuard lock(display);
                lv_obj_add_flag(display->wifi_connecting_view_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(display->wifi_pwd_view_, LV_OBJ_FLAG_HIDDEN);
            });
        }

        vTaskDelete(NULL);
    }, "wifi_connector", 4096, params, 3, NULL);
}

void LcdDisplay::SetStatus(const char* status) {
    if (music_visualizer_active_ && status != nullptr) {
        // Allow music status updates while visualizer is active, prevent standard state strings (like SPEAKING) from replacing it
        if (strcmp(status, "🎧 DJ PLAYING") != 0 && strncmp(status, "🎵", 3) != 0) {
            return;
        }
    }
    LvglDisplay::SetStatus(status);
    UpdateIdleDashboard();
}

void LcdDisplay::SetEmotion(const char* emotion) {
    if (music_visualizer_active_) {
        return;
    }
    if (ui_theme_id_ == 2) {
        // Theme 2 uses WeChat chat bubble UI without big center emoji
        return;
    }
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!",
                 emotion);
    }
    if (emoji_image_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetEmotion('%s') failed: emoji_image_ is nullptr (SetupUI() was called but "
                     "emoji image not created)",
                     emotion);
        }
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        const char* utf8 = noto_emoji_get_utf8(emotion);
        const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
        if (utf8 == nullptr) {
            utf8 = material_symbols_get_utf8(emotion);
            emotion_font = lvgl_theme->large_icon_font()->font();
        }
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            if (gif_controller_) {
                gif_controller_->Stop();
                gif_controller_.reset();
            }
            lv_obj_set_style_text_font(emoji_label_, emotion_font, 0);
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_center(emoji_label_);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_) lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
        }
        return;
    }

    DisplayLockGuard lock(this);
    // Stop any running GIF animation in the same lock scope as setting new image
    // to prevent LVGL from accessing freed image data between operations
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());

        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback(
                [this]() { lv_image_set_src(emoji_image_, gif_controller_->image_dsc()); });

            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();

            // Show GIF, hide others
            lv_obj_center(emoji_image_);
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_box_) lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_center(emoji_image_);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_) lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
    }
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);

    // Get the screen belonging to container_
    lv_obj_t* screen = (container_ != nullptr) ? lv_obj_get_screen(container_) : lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg = (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_panel = (synced_bg) ? lv_color_hex(synced_bg->panel_color) : lvgl_theme->background_color();
    lv_color_t xz_border = (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);

    // Set parent text color and background
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, xz_bg, 0);

    // Set background image or solid color
    if (container_ != nullptr) {
        if (lvgl_theme->background_image() != nullptr) {
            lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
        } else {
            lv_obj_set_style_bg_image_src(container_, nullptr, 0);
            lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(container_, xz_bg, 0);
        }
    }

    // Update top bar styling based on UI theme (matches Nova OS C_BG & C_BORDER)
    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(top_bar_, xz_bg, 0);
        lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(top_bar_, xz_border, 0);
        lv_obj_set_style_border_width(top_bar_, 1, 0);
    }

    if (rot_icon_label_ != nullptr) {
        lv_obj_set_style_text_color(rot_icon_label_, xz_accent, 0);
    }

    // Update status bar elements
    uint32_t bg_rgb = lv_color_to_int(lvgl_theme->background_color());
    int bg_lum = (int)(0.299f * ((bg_rgb >> 16) & 0xFF) + 0.587f * ((bg_rgb >> 8) & 0xFF) + 0.114f * (bg_rgb & 0xFF));
    bool is_light_theme = (bg_lum > 140);

    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, is_light_theme ? lv_color_hex(0x475569) : lvgl_theme->text_color(), 0);
    if (battery_pct_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_pct_label_, is_light_theme ? lv_color_hex(0x475569) : lv_color_white(), 0);
    }
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    if (ui_theme_id_ == 2) {
        // Theme 2: WeChat Chat Bubble UI - dynamically adapt content background & existing bubbles
        if (content_ != nullptr) {
            lv_obj_set_style_bg_color(content_, xz_bg, 0);
            lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);

            // Iterate through all children of content (message containers or bubbles)
            uint32_t child_count = lv_obj_get_child_cnt(content_);
            for (uint32_t i = 0; i < child_count; i++) {
                lv_obj_t* obj = lv_obj_get_child(content_, i);
                if (obj == nullptr)
                    continue;

                lv_obj_t* bubble = nullptr;
                if (lv_obj_get_child_cnt(obj) > 0) {
                    lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
                    if (bg_opa == LV_OPA_TRANSP) {
                        bubble = lv_obj_get_child(obj, 0);
                    } else {
                        bubble = obj;
                    }
                } else {
                    continue;
                }

                if (bubble == nullptr)
                    continue;

                void* bubble_type_ptr = lv_obj_get_user_data(bubble);
                if (bubble_type_ptr != nullptr) {
                    const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
                    lv_color_t bubble_bg;

                    if (strcmp(bubble_type, "user") == 0) {
                        bubble_bg = lvgl_theme->user_bubble_color();
                    } else if (strcmp(bubble_type, "assistant") == 0) {
                        bubble_bg = lvgl_theme->assistant_bubble_color();
                    } else if (strcmp(bubble_type, "system") == 0) {
                        bubble_bg = lvgl_theme->system_bubble_color();
                    } else {
                        bubble_bg = lvgl_theme->system_bubble_color();
                    }

                    lv_obj_set_style_bg_color(bubble, bubble_bg, 0);
                    lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);

                    // Update text color for the message ensuring high contrast
                    if (lv_obj_get_child_cnt(bubble) > 0) {
                        lv_obj_t* text = lv_obj_get_child(bubble, 0);
                        if (text != nullptr) {
                            if (strcmp(bubble_type, "system") == 0) {
                                lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);
                            } else {
                                lv_obj_set_style_text_color(text, GetContrastTextColor(bubble_bg), 0);
                            }
                        }
                    }
                    if (lv_obj_get_child_cnt(bubble) > 1) {
                        lv_obj_t* footer = lv_obj_get_child(bubble, 1);
                        if (footer != nullptr && lv_obj_get_child_cnt(footer) > 0) {
                            lv_obj_t* time_lbl = lv_obj_get_child(footer, 0);
                            if (time_lbl != nullptr) {
                                lv_obj_set_style_text_color(time_lbl, GetContrastTextColor(bubble_bg), 0);
                            }
                        }
                    }
                }
            }
        }
        if (music_container_ != nullptr) {
            lv_color_t card_bg = lvgl_theme->assistant_bubble_color();
            lv_color_t text_col = GetContrastTextColor(card_bg);
            lv_obj_set_style_bg_color(music_container_, card_bg, 0);
            lv_obj_set_style_border_color(music_container_, lvgl_theme->border_color(), 0);
            if (music_title_label_ != nullptr) {
                lv_obj_set_style_text_color(music_title_label_, text_col, 0);
            }
        }
    } else {
        // Theme 1: Simple UI mode - just update the main chat message
        if (chat_message_label_ != nullptr) {
            lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
        }

        if (emoji_label_ != nullptr) {
            lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
        }

        // Update bottom bar background color with 50% opacity and text color
        if (bottom_bar_ != nullptr && ui_theme_id_ != 2) {
            lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
            lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
            lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
        }
    }

    // Recreate/update bottom navigation bar so it always matches active theme and background
    CreateBottomNav();

    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}

void LcdDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    hide_subtitle_ = hide;

    // Immediately update UI visibility based on the setting
    if (bottom_bar_ != nullptr && ui_theme_id_ != 2) {
        if (hide) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Only show if there is actual content to display
            const char* text =
                (chat_message_label_ != nullptr) ? lv_label_get_text(chat_message_label_) : nullptr;
            if (text != nullptr && text[0] != '\0') {
                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void LcdDisplay::CreateMusicVisualizerUI() {
    if (music_container_ != nullptr) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    if (ui_theme_id_ == 2) {
        if (content_ == nullptr) {
            return;
        }

        // WhatsApp Chat Column Music Player Bubble inside content_
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        if (child_count >= MAX_MESSAGES) {
            lv_obj_t* first_child = lv_obj_get_child(content_, 0);
            if (first_child != nullptr) {
                lv_obj_del(first_child);
            }
        }

        music_container_ = lv_obj_create(content_);
        lv_coord_t bubble_w = LV_HOR_RES * 85 / 100 - 16;
        lv_obj_set_width(music_container_, bubble_w);
        lv_obj_set_height(music_container_, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(music_container_, 10, 0);

        lv_color_t card_bg = lvgl_theme->assistant_bubble_color();
        lv_color_t text_col = GetContrastTextColor(card_bg);
        lv_obj_set_style_bg_color(music_container_, card_bg, 0);
        lv_obj_set_style_bg_opa(music_container_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(music_container_, 1, 0);
        lv_obj_set_style_border_color(music_container_, lvgl_theme->border_color(), 0);
        lv_obj_set_style_pad_left(music_container_, 10, 0);
        lv_obj_set_style_pad_right(music_container_, 10, 0);
        lv_obj_set_style_pad_top(music_container_, 8, 0);
        lv_obj_set_style_pad_bottom(music_container_, 6, 0);
        lv_obj_set_style_pad_row(music_container_, 4, 0);
        lv_obj_set_scrollbar_mode(music_container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(music_container_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_user_data(music_container_, (void*)"assistant");

        // YouTube Music Header Badge (Single clean header label without colliding "Playing..." text)
        music_sub_label_ = lv_label_create(music_container_);
        lv_label_set_text(music_sub_label_, "▶ YouTube Music");
        lv_obj_set_style_text_color(music_sub_label_, lv_color_hex(0xE53935), 0); // YouTube Red

        // Title Label with circular marquee
        music_title_label_ = lv_label_create(music_container_);
        lv_obj_set_width(music_title_label_, LV_PCT(100));
        lv_label_set_long_mode(music_title_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(music_title_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(music_title_label_, text_col, 0);
        lv_label_set_text(music_title_label_, "YouTube Audio");

        // Spectrum bars container
        lv_obj_t* bars_box = lv_obj_create(music_container_);
        lv_obj_set_size(bars_box, LV_PCT(100), 34);
        lv_obj_set_style_bg_opa(bars_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bars_box, 0, 0);
        lv_obj_set_style_pad_all(bars_box, 0, 0);
        lv_obj_set_scrollbar_mode(bars_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(bars_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 5, 0);

        static const uint32_t kTheme2BarColors[7] = {
            0x07C160, // WhatsApp Green
            0x25D366, // Light WhatsApp Green
            0x00E676, // Spring Neon Green
            0x00B0FF, // Electric Cyan
            0x7C4DFF, // Purple
            0xFFB300, // Amber Yellow
            0xFF5252  // Coral Red
        };

        for (int i = 0; i < 7; i++) {
            dj_bars_[i] = lv_obj_create(bars_box);
            lv_obj_set_size(dj_bars_[i], 9, 8);
            lv_obj_set_style_bg_color(dj_bars_[i], lv_color_hex(kTheme2BarColors[i]), 0);
            lv_obj_set_style_bg_opa(dj_bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dj_bars_[i], 4, 0);
            lv_obj_set_style_border_width(dj_bars_[i], 0, 0);
            lv_obj_set_scrollbar_mode(dj_bars_[i], LV_SCROLLBAR_MODE_OFF);
        }
        dj_icon_label_ = nullptr;

        // Status durasi tepat di bawah animasi musik di dalam bubble chat
        music_duration_label_ = lv_label_create(music_container_);
        lv_obj_set_width(music_duration_label_, LV_PCT(100));
        lv_obj_set_style_text_align(music_duration_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(music_duration_label_, lv_color_hex(0x07C160), 0); // WhatsApp Green
        lv_obj_set_style_text_font(music_duration_label_, lvgl_theme->text_font()->font(), 0);
        lv_label_set_text(music_duration_label_, "⏱ 00:00 / --:--");

        // WhatsApp-style timestamp footer
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[16];
        if (timeinfo.tm_year > (2020 - 1900)) {
            snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            int sec = (int)(esp_timer_get_time() / 1000000);
            snprintf(time_str, sizeof(time_str), "%02d:%02d", (sec / 3600) % 24, (sec / 60) % 60);
        }

        lv_obj_t* footer_row = lv_obj_create(music_container_);
        lv_obj_set_width(footer_row, LV_PCT(100));
        lv_obj_set_height(footer_row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(footer_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer_row, 0, 0);
        lv_obj_set_style_pad_all(footer_row, 0, 0);
        lv_obj_set_flex_flow(footer_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(footer_row, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* time_label = lv_label_create(footer_row);
        lv_label_set_text(time_label, time_str);
        lv_obj_set_style_text_font(time_label, lvgl_theme->text_font()->font(), 0);
        lv_obj_set_style_text_color(time_label, text_col, 0);
        lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);

        // Align bubble to left (assistant message)
        lv_obj_align(music_container_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_scroll_to_view_recursive(music_container_, LV_ANIM_ON);
    } else {
        // Theme 1: Default Retro DJ Visualizer
        auto screen = lv_screen_active();
        auto large_icon_font = lvgl_theme->large_icon_font()->font();

        music_container_ = lv_obj_create(screen);
        lv_obj_set_size(music_container_, LV_HOR_RES - 20, 150);
        lv_obj_align(music_container_, LV_ALIGN_CENTER, 0, -10);
        lv_obj_set_style_bg_opa(music_container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(music_container_, 0, 0);
        lv_obj_set_style_pad_all(music_container_, 0, 0);
        lv_obj_set_scrollbar_mode(music_container_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(music_container_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(music_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(music_container_, 8, 0);

        // DJ Headphone Icon
        dj_icon_label_ = lv_label_create(music_container_);
        lv_obj_set_style_text_font(dj_icon_label_, large_icon_font, 0);
        lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x7C4DFF), 0);
        lv_label_set_text(dj_icon_label_, MATERIAL_SYMBOLS_HEADPHONES);

        // Spectrum Bars container
        lv_obj_t* bars_box = lv_obj_create(music_container_);
        lv_obj_set_size(bars_box, LV_SIZE_CONTENT, 85);
        lv_obj_set_style_bg_opa(bars_box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bars_box, 0, 0);
        lv_obj_set_style_pad_all(bars_box, 0, 0);
        lv_obj_set_scrollbar_mode(bars_box, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(bars_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 6, 0);

        static const uint32_t kBarColors[7] = {
            0x7C4DFF, // Violet
            0x2979FF, // Electric Blue
            0x00B0FF, // Cyan
            0x00E676, // Spring Neon Green
            0xFFD600, // Electric Yellow
            0xFF6D00, // Neon Orange
            0xFF1744  // Hot Pink / Red
        };

        for (int i = 0; i < 7; i++) {
            dj_bars_[i] = lv_obj_create(bars_box);
            lv_obj_set_size(dj_bars_[i], 12, 16);
            lv_obj_set_style_bg_color(dj_bars_[i], lv_color_hex(kBarColors[i]), 0);
            lv_obj_set_style_bg_opa(dj_bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dj_bars_[i], 6, 0);
            lv_obj_set_style_border_width(dj_bars_[i], 0, 0);
            lv_obj_set_scrollbar_mode(dj_bars_[i], LV_SCROLLBAR_MODE_OFF);
        }
        music_duration_label_ = lv_label_create(music_container_);
        lv_obj_set_style_text_align(music_duration_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(music_duration_label_, lv_color_hex(0x00E676), 0);
        lv_obj_set_style_text_font(music_duration_label_, lvgl_theme->text_font()->font(), 0);
        lv_label_set_text(music_duration_label_, "00:00 / --:--");
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
    }
}

void LcdDisplay::AnimateMusicVisualizer() {
    if (!music_visualizer_active_ || music_container_ == nullptr) return;

    music_anim_phase_++;

    // Pulse DJ headphone icon color between Electric Violet and Glowing Cyan in Theme 1
    if (dj_icon_label_ != nullptr) {
        if ((music_anim_phase_ / 8) % 2 == 0) {
            lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x7C4DFF), 0);
        } else {
            lv_obj_set_style_text_color(dj_icon_label_, lv_color_hex(0x00B0FF), 0);
        }
    }

    int max_h = (ui_theme_id_ == 2) ? 30 : 78;
    int min_h = (ui_theme_id_ == 2) ? 6 : 12;

    // Dynamic spectrum heights calculation for 7 bars
    for (int i = 0; i < 7; i++) {
        if (dj_bars_[i] == nullptr) continue;

        float t = (float)music_anim_phase_ * 0.22f;
        float wave1 = sinf(t * (1.1f + i * 0.28f) + i * 0.8f);
        float wave2 = cosf(t * (0.75f + (6 - i) * 0.22f) - i * 0.5f);
        int beat_pulse = ((music_anim_phase_ + i * 2) % 10 < 3) ? (ui_theme_id_ == 2 ? 8 : 22) : 0;

        int h = (int)(min_h + (wave1 * 0.5f + 0.5f) * (max_h * 0.55f) + (wave2 * 0.5f + 0.5f) * (max_h * 0.25f) + beat_pulse);
        if (h < min_h) h = min_h;
        if (h > max_h) h = max_h;

        lv_obj_set_height(dj_bars_[i], h);
    }
}

void LcdDisplay::StartMusicVisualizer(const char* title) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;

    music_visualizer_active_ = true;

    // Hide preview image or camera if any
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide emoji_box_ in Theme 1
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // In Theme 2: if previous music_container_ was active, finalize it cleanly
    if (ui_theme_id_ == 2 && music_container_ != nullptr) {
        if (music_sub_label_ != nullptr) {
            lv_label_set_text(music_sub_label_, "⏹ YouTube Music");
            lv_obj_set_style_text_opa(music_sub_label_, LV_OPA_60, 0);
        }
        for (int i = 0; i < 7; i++) {
            if (dj_bars_[i] != nullptr) {
                lv_obj_set_height(dj_bars_[i], 4);
                dj_bars_[i] = nullptr;
            }
        }
        music_container_ = nullptr;
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
        music_duration_label_ = nullptr;
    }

    // Create music visualizer UI if not created yet
    CreateMusicVisualizerUI();

    if (music_container_ != nullptr) {
        lv_obj_remove_flag(music_container_, LV_OBJ_FLAG_HIDDEN);
    }

    // Update title label if available
    if (music_title_label_ != nullptr && title && title[0] != '\0') {
        lv_label_set_text(music_title_label_, title);
    }
    if (music_duration_label_ != nullptr) {
        lv_label_set_text(music_duration_label_, "00:00 / --:--");
    }

    // Start animation timer if not already created
    if (music_anim_timer_ == nullptr) {
        music_anim_timer_ = lv_timer_create([](lv_timer_t* timer) {
            auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(timer));
            if (self) {
                self->AnimateMusicVisualizer();
            }
        }, 60, this);
    } else {
        lv_timer_resume(music_anim_timer_);
    }

    if (ui_theme_id_ == 2) {
        if (title && title[0] != '\0') {
            std::string st = "🎵 " + std::string(title);
            SetStatus(st.c_str());
        } else {
            SetStatus("🎵 YouTube Playing");
        }
        if (music_container_ != nullptr) {
            lv_obj_scroll_to_view_recursive(music_container_, LV_ANIM_ON);
        }
    } else {
        SetStatus("🎧 DJ PLAYING");
        if (title && title[0] != '\0') {
            std::string song_display = "🎵 " + std::string(title);
            SetChatMessage("assistant", song_display.c_str());
        }
    }
}

void LcdDisplay::StopMusicVisualizer() {
    DisplayLockGuard lock(this);
    if (!music_visualizer_active_) return;

    music_visualizer_active_ = false;

    if (music_anim_timer_ != nullptr) {
        lv_timer_pause(music_anim_timer_);
    }

    if (ui_theme_id_ == 2) {
        // In Theme 2 (WhatsApp Chat): keep the music bubble in the chat log, set status to finished
        if (music_sub_label_ != nullptr) {
            lv_label_set_text(music_sub_label_, "⏹ YouTube Music");
            lv_obj_set_style_text_opa(music_sub_label_, LV_OPA_60, 0);
        }
        for (int i = 0; i < 7; i++) {
            if (dj_bars_[i] != nullptr) {
                lv_obj_set_height(dj_bars_[i], 4);
                dj_bars_[i] = nullptr;
            }
        }
        music_container_ = nullptr;
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
        music_duration_label_ = nullptr;
    } else {
        // Theme 1: Hide the overlay container and restore emoji
        if (music_container_ != nullptr) {
            lv_obj_add_flag(music_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_box_ != nullptr) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_label_ != nullptr) {
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    SetStatus(Lang::Strings::STANDBY);
    SetEmotion("neutral");
}

void LcdDisplay::UpdateMusicProgress(int current_sec, int total_sec) {
    DisplayLockGuard lock(this);
    if (!music_visualizer_active_ || music_duration_label_ == nullptr) return;

    char buf[40];
    if (total_sec >= 3600 || current_sec >= 3600) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d / %02d:%02d:%02d",
                 current_sec / 3600, (current_sec % 3600) / 60, current_sec % 60,
                 total_sec / 3600, (total_sec % 3600) / 60, total_sec % 60);
    } else if (total_sec > 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d / %02d:%02d",
                 current_sec / 60, current_sec % 60,
                 total_sec / 60, total_sec % 60);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d / --:--",
                 current_sec / 60, current_sec % 60);
    }
    lv_label_set_text(music_duration_label_, buf);
}
