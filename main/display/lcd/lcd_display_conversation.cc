#include "lcd_display_internal.h"

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
    lv_timer_t* timer = lv_timer_create(
        [](lv_timer_t* t) {
            lv_obj_t* obj = static_cast<lv_obj_t*>(lv_timer_get_user_data(t));
            if (obj && lv_obj_is_valid(obj)) {
                lv_obj_delete_async(obj);
            }
            lv_timer_del(t);
        },
        250, flash);
    lv_timer_set_repeat_count(timer, 1);
}

void LcdDisplay::SetZoomLevel(int level) {
    DisplayLockGuard lock(this);
    zoom_level_ = std::clamp(level, 1, 10);
    is_zoomed_ = (zoom_level_ > 1);

    // Update current preview image scale if visible
    if (preview_image_ && !lv_obj_has_flag(preview_image_, LV_OBJ_FLAG_HIDDEN) &&
        preview_image_cached_) {
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

void LcdDisplay::SetZoom(bool zoomed) { SetZoomLevel(zoomed ? 2 : 1); }

void LcdDisplay::SetChatMessage_Theme1(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(
            TAG,
            "SetChatMessage_Theme1('%s', '%s') called before SetupUI() - message will be lost!",
            role, content);
    }
    DisplayLockGuard lock(this);
    if (content != nullptr && content[0] != '\0' && idle_dashboard_visible_) {
        idle_dashboard_visible_ = false;
        if (idle_dashboard_)
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_)
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG,
                     "SetChatMessage_Theme1('%s', '%s') failed: chat_message_label_ is nullptr "
                     "(SetupUI() "
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
    DisplayLockGuard lock(this);
    lv_obj_t* top_layer = lv_layer_top();
    if (!top_layer)
        return;

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto icon_font = lvgl_theme ? lvgl_theme->icon_font()->font() : &BUILTIN_ICON_FONT;

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg = (synced_bg)
                           ? lv_color_hex(synced_bg->bg_color)
                           : (lvgl_theme ? lvgl_theme->background_color() : lv_color_hex(0x0B1220));
    lv_color_t xz_border = (synced_bg)
                               ? lv_color_hex(synced_bg->border_color)
                               : (lvgl_theme ? lvgl_theme->border_color() : lv_color_hex(0x2B3B52));
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);
    lv_color_t xz_muted = lv_color_hex(0x91A0B5);

    if (xiaozhi_bottom_nav_ != nullptr) {
        // Safe in-place style update: NEVER delete widgets during runtime to avoid watchdog crashes
        // and event corruption!
        lv_obj_set_size(xiaozhi_bottom_nav_, width_, 30);
        lv_obj_set_style_bg_color(xiaozhi_bottom_nav_, xz_bg, 0);
        lv_obj_set_style_border_color(xiaozhi_bottom_nav_, xz_border, 0);
        if (bottom_nav_back_btn_)
            lv_obj_set_style_bg_color(bottom_nav_back_btn_, xz_bg, 0);
        if (bottom_nav_back_lbl_) {
            lv_obj_set_style_text_font(bottom_nav_back_lbl_, icon_font, 0);
            lv_obj_set_style_text_color(bottom_nav_back_lbl_, xz_muted, 0);
        }
        if (bottom_nav_home_btn_)
            lv_obj_set_style_bg_color(bottom_nav_home_btn_, xz_bg, 0);
        if (bottom_nav_home_lbl_) {
            lv_obj_set_style_text_font(bottom_nav_home_lbl_, icon_font, 0);
            lv_obj_set_style_text_color(bottom_nav_home_lbl_, xz_accent, 0);
        }
        if (bottom_nav_recent_btn_)
            lv_obj_set_style_bg_color(bottom_nav_recent_btn_, xz_bg, 0);
        if (bottom_nav_recent_sq_)
            lv_obj_set_style_border_color(bottom_nav_recent_sq_, xz_muted, 0);
        return;
    }

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
    lv_obj_set_flex_align(xiaozhi_bottom_nav_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(xiaozhi_bottom_nav_, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Back Button (◀)
    bottom_nav_back_btn_ = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(bottom_nav_back_btn_, 26);
    lv_obj_set_flex_grow(bottom_nav_back_btn_, 1);
    lv_obj_set_style_radius(bottom_nav_back_btn_, 6, 0);
    lv_obj_set_style_bg_color(bottom_nav_back_btn_, xz_bg, 0);
    lv_obj_set_style_border_width(bottom_nav_back_btn_, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav_back_btn_, 0, 0);
    lv_obj_set_style_shadow_width(bottom_nav_back_btn_, 0, 0);
    bottom_nav_back_lbl_ = lv_label_create(bottom_nav_back_btn_);
    lv_label_set_text(bottom_nav_back_lbl_, MATERIAL_SYMBOLS_ARROW_BACK);
    lv_obj_set_style_text_font(bottom_nav_back_lbl_, icon_font, 0);
    lv_obj_set_style_text_color(bottom_nav_back_lbl_, xz_muted, 0);
    lv_obj_center(bottom_nav_back_lbl_);
    lv_obj_add_event_cb(
        bottom_nav_back_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (!display)
                return;
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
                app.Schedule([]() { Application::GetInstance().AbortSpeaking(kAbortReasonNone); });
            }
            Board::GetInstance().SwitchToMode(2);  // Kembali ke Nova Mini OS
        },
        LV_EVENT_CLICKED, this);

    // 2. Home Button (●)
    bottom_nav_home_btn_ = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(bottom_nav_home_btn_, 26);
    lv_obj_set_flex_grow(bottom_nav_home_btn_, 1);
    lv_obj_set_style_radius(bottom_nav_home_btn_, 6, 0);
    lv_obj_set_style_bg_color(bottom_nav_home_btn_, xz_bg, 0);
    lv_obj_set_style_border_width(bottom_nav_home_btn_, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav_home_btn_, 0, 0);
    lv_obj_set_style_shadow_width(bottom_nav_home_btn_, 0, 0);
    bottom_nav_home_lbl_ = lv_label_create(bottom_nav_home_btn_);
    lv_label_set_text(bottom_nav_home_lbl_, MATERIAL_SYMBOLS_HOME);
    lv_obj_set_style_text_font(bottom_nav_home_lbl_, icon_font, 0);
    lv_obj_set_style_text_color(bottom_nav_home_lbl_, xz_accent, 0);
    lv_obj_center(bottom_nav_home_lbl_);
    lv_obj_add_event_cb(
        bottom_nav_home_btn_,
        [](lv_event_t* e) {
            if (YouTubeAudioPoller::GetInstance().IsPlaying()) {
                YouTubeAudioPoller::GetInstance().Abort();
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.Schedule([]() { Application::GetInstance().AbortSpeaking(kAbortReasonNone); });
            }
            Board::GetInstance().SwitchToMode(2);  // Beranda Nova Mini OS
        },
        LV_EVENT_CLICKED, this);

    // 3. Recent Apps Button (▢)
    bottom_nav_recent_btn_ = lv_button_create(xiaozhi_bottom_nav_);
    lv_obj_set_height(bottom_nav_recent_btn_, 26);
    lv_obj_set_flex_grow(bottom_nav_recent_btn_, 1);
    lv_obj_set_style_radius(bottom_nav_recent_btn_, 6, 0);
    lv_obj_set_style_bg_color(bottom_nav_recent_btn_, xz_bg, 0);
    lv_obj_set_style_border_width(bottom_nav_recent_btn_, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav_recent_btn_, 0, 0);
    lv_obj_set_style_shadow_width(bottom_nav_recent_btn_, 0, 0);

    bottom_nav_recent_sq_ = lv_obj_create(bottom_nav_recent_btn_);
    lv_obj_set_size(bottom_nav_recent_sq_, 14, 14);
    lv_obj_center(bottom_nav_recent_sq_);
    lv_obj_clear_flag(bottom_nav_recent_sq_,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_radius(bottom_nav_recent_sq_, 3, 0);
    lv_obj_set_style_bg_opa(bottom_nav_recent_sq_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(bottom_nav_recent_sq_, xz_muted, 0);
    lv_obj_set_style_border_width(bottom_nav_recent_sq_, 2, 0);
    lv_obj_set_style_pad_all(bottom_nav_recent_sq_, 0, 0);
    lv_obj_set_style_shadow_width(bottom_nav_recent_sq_, 0, 0);

    lv_obj_add_event_cb(
        bottom_nav_recent_btn_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display) {
                if (display->IsRecentAppsModalVisible()) {
                    display->CloseRecentAppsModal();
                } else {
                    display->ShowRecentAppsModal();
                }
            }
        },
        LV_EVENT_CLICKED, this);
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
    if (top_bar_ != nullptr && ui_theme_id_ != 2) {
        lv_obj_set_size(top_bar_, width_, 30);
    }
    if (container_ != nullptr && ui_theme_id_ != 2) {
        lv_obj_set_size(container_, width_, height_ - 60);
        lv_obj_align(container_, LV_ALIGN_TOP_MID, 0, 30);
    }
    if (bg_image_ != nullptr && ui_theme_id_ != 2) {
        lv_obj_set_size(bg_image_, width_, height_ - 60);
        lv_obj_align(bg_image_, LV_ALIGN_CENTER, 0, 0);
        lv_image_set_inner_align(bg_image_, LV_IMAGE_ALIGN_COVER);
    }
}

void LcdDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    DisplayLockGuard lock(this);
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

void LcdDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (content == nullptr) {
        return;
    }

    if (ui_theme_id_ == 2 && content_ != nullptr && role != nullptr) {
        uint32_t cnt = lv_obj_get_child_cnt(content_);
        for (int32_t i = (int32_t)cnt - 1; i >= 0; --i) {
            auto container = lv_obj_get_child(content_, i);
            if (!container || !lv_obj_is_valid(container)) {
                continue;
            }
            auto bubble = container;
            auto bubble_role = static_cast<const char*>(lv_obj_get_user_data(bubble));
            if (bubble_role == nullptr && lv_obj_get_child_cnt(bubble) > 0) {
                bubble = lv_obj_get_child(bubble, 0);
                if (bubble && lv_obj_is_valid(bubble)) {
                    bubble_role = static_cast<const char*>(lv_obj_get_user_data(bubble));
                }
            }
            if (bubble_role == nullptr || strcmp(bubble_role, role) != 0 ||
                lv_obj_get_child_cnt(bubble) == 0) {
                continue;
            }
            auto label = lv_obj_get_child(bubble, 0);
            if (!label || !lv_obj_is_valid(label) || !lv_obj_check_type(label, &lv_label_class)) {
                continue;
            }
            const char* label_text = lv_label_get_text(label);
            if (label_text == nullptr || strcmp(label_text, content) != 0) {
                continue;
            }
            if (chat_message_label_ == label) {
                chat_message_label_ = nullptr;
            }
            lv_obj_del(container);
            return;
        }
    }

    if (chat_message_label_ != nullptr && lv_obj_is_valid(chat_message_label_)) {
        if (lv_obj_check_type(chat_message_label_, &lv_label_class)) {
            const char* cur = lv_label_get_text(chat_message_label_);
            if (cur != nullptr && strcmp(cur, content) == 0) {
                lv_anim_delete(chat_message_label_, nullptr);
                lv_label_set_text(chat_message_label_, "");
                if (bottom_bar_ != nullptr && lv_obj_is_valid(bottom_bar_)) {
                    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
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
        ShowNotification(theme_id == 2 ? "Thema 2 (Chat) Aktif" : "Thema 1 (Robot) Aktif");
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
    if (ui_theme_id_ == 1) {
        SetEmotion("neutral");
    }
    int cur_brightness = settings.GetInt("brightness", 70);
    if (cur_brightness < 5) {
        cur_brightness = (cur_brightness <= 0) ? 70 : 5;
    }
    SetBrightness(cur_brightness);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(lv_display_get_default());
    ESP_LOGI(TAG, "UI theme switched to Theme %d", ui_theme_id_);
    ShowNotification(theme_id == 2 ? "Thema 2: Chat Mode" : "Thema 1: Robot Mode");
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

    lv_anim_delete_all();

    auto screen = lv_screen_active();
    if (screen != nullptr) {
        lv_obj_clean(screen);
    }

    bottom_nav_back_btn_ = nullptr;
    bottom_nav_back_lbl_ = nullptr;
    bottom_nav_home_btn_ = nullptr;
    bottom_nav_home_lbl_ = nullptr;
    bottom_nav_recent_btn_ = nullptr;
    bottom_nav_recent_sq_ = nullptr;

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
    bg_image_ = nullptr;
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
    overlay_backdrop_ = nullptr;
    vol_overlay_card_ = nullptr;
    bright_overlay_card_ = nullptr;
    text_size_overlay_card_ = nullptr;
    header_color_overlay_card_ = nullptr;
    br_icon_label_ = nullptr;
    preview_rot_label_ = nullptr;
    preview_bright_label_ = nullptr;
    preview_vol_label_ = nullptr;
    preview_wifi_label_ = nullptr;
    preview_bat_label_ = nullptr;
    preview_bat_pct_label_ = nullptr;
    overlay_visible_ = false;
    setup_ui_called_ = false;
}

// ── Custom Idle Screen (Digital Clock, Date, Weather & Status) ──
void LcdDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);
    UpdateIdleDashboard();
}
