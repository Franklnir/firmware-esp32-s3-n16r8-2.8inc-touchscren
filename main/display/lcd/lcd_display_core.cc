#include "lcd_display_internal.h"

const lv_font_t* LcdDisplay::GetChatFont() const {
    switch (chat_font_level_) {
        case 0:
            return &font_noto_sans_basic_14_1;
        case 1:
            return &font_noto_sans_basic_16_4;
        case 2:
            return &font_noto_sans_basic_20_4;
        case 3:
            return &font_noto_sans_basic_20_4;
        default:
            return &font_noto_sans_basic_16_4;
    }
}

void LcdDisplay::UpdateTextSizeOverlayUI() {
    if (!text_size_val_label_)
        return;
    const char* names[] = {"14px (Kecil)", "16px (Sedang)", "20px (Besar)", "30px (Ekstra)"};
    if (chat_font_level_ >= 0 && chat_font_level_ <= 3) {
        lv_label_set_text(text_size_val_label_, names[chat_font_level_]);
    }
}

void LcdDisplay::SetChatFontLevel(int level) {
    if (level < 0)
        level = 0;
    if (level > 3)
        level = 3;
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
            if (!child)
                continue;
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
    if (!cp_drawer_ || !cp_backdrop_)
        return;
    if (cp_visible_) {
        cp_visible_ = false;
        lv_obj_add_flag(cp_backdrop_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cp_drawer_, LV_OBJ_FLAG_HIDDEN);
        if (cp_color_box_)
            lv_obj_add_flag(cp_color_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_4);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_4);

    // Neo-Brutalism Cyber Yellow Theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFE600));  // Iconic Cyber Yellow #FFE600
    light_theme->set_text_color(lv_color_hex(0x000000));        // Deep solid black #000000
    light_theme->set_chat_background_color(lv_color_hex(0xFFE600));
    light_theme->set_user_bubble_color(lv_color_hex(0x00F0FF));  // Neo Cyan
    light_theme->set_assistant_bubble_color(
        lv_color_hex(0xFFFFFF));  // Stark White with black border
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light_theme->set_system_text_color(lv_color_hex(0x000000));
    light_theme->set_border_color(lv_color_hex(0x000000));  // Solid 2px black border
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
    dark_theme->set_assistant_bubble_color(
        lv_color_hex(0x121212));  // Deep charcoal with neon border
    dark_theme->set_system_bubble_color(lv_color_hex(0x121212));
    dark_theme->set_system_text_color(lv_color_hex(0x39FF14));  // Stabilo Neon Green
    dark_theme->set_border_color(lv_color_hex(0x39FF14));       // Bold Neon Green border #39FF14
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0055));  // Hot Red/Pink
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
    white_theme->set_background_color(
        lv_color_hex(0xEBEBEB));  // Light WeChat gray background #EBEBEB
    white_theme->set_text_color(lv_color_hex(0x000000));
    white_theme->set_chat_background_color(lv_color_hex(0xEBEBEB));
    white_theme->set_user_bubble_color(lv_color_hex(0x95EC69));       // WeChat Green #95EC69
    white_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));  // Stark White #FFFFFF
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
    std::string theme_name =
        settings.GetString("theme", (synced && synced->theme_name) ? synced->theme_name : "dark");
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
        bottom_nav_back_btn_ = nullptr;
        bottom_nav_back_lbl_ = nullptr;
        bottom_nav_home_btn_ = nullptr;
        bottom_nav_home_lbl_ = nullptr;
        bottom_nav_recent_btn_ = nullptr;
        bottom_nav_recent_sq_ = nullptr;
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

// Helper to determine whether text should be dark or light for optimal contrast on any bubble
// background
