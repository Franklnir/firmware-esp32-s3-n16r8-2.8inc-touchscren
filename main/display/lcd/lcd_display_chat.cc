#include "lcd_display_internal.h"

void LcdDisplay::SetupUI_Theme2() {
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg =
        (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_panel =
        (synced_bg) ? lv_color_hex(synced_bg->panel_color) : lvgl_theme->background_color();
    lv_color_t xz_border =
        (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
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
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, xz_bg, 0);
    }
    lv_obj_set_style_border_color(container_, xz_border, 0);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Preview image for camera stream - full screen display like Theme 1 */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_, height_);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    /* Layer 1: Top bar - header with avatar on left, wifi & battery on right (matches Nova OS C_BG
     * & C_BORDER) */
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

    // Left buttons container: Rotation button + Brightness button
    lv_obj_t* left_box = lv_obj_create(top_bar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 5, 0);

    lv_color_t hdr_clr = lv_color_hex(header_icon_color_);

    // 1. Rotation Button (Tap rotates display by 90 degrees)
    lv_obj_t* rot_btn = lv_obj_create(left_box);
    lv_obj_set_size(rot_btn, 24, 24);
    lv_obj_set_style_bg_opa(rot_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rot_btn, 0, 0);
    lv_obj_set_style_pad_all(rot_btn, 0, 0);
    rot_icon_label_ = lv_label_create(rot_btn);
    lv_label_set_text(rot_icon_label_, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(rot_icon_label_, hdr_clr, 0);
    lv_obj_center(rot_icon_label_);
    lv_obj_add_flag(rot_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        rot_btn,
        [](lv_event_t* e) {
            (void)e;
            Board::GetInstance().RotateDisplay();
        },
        LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Button (Tap -> Brightness Overlay Slider!)
    lv_obj_t* bright_btn = lv_obj_create(left_box);
    lv_obj_set_size(bright_btn, 24, 24);
    lv_obj_set_style_bg_opa(bright_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bright_btn, 0, 0);
    lv_obj_set_style_pad_all(bright_btn, 0, 0);
    br_icon_label_ = lv_label_create(bright_btn);
    lv_label_set_text(br_icon_label_, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(br_icon_label_, hdr_clr, 0);
    lv_obj_center(br_icon_label_);
    lv_obj_add_flag(bright_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        bright_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowBrightnessOverlay();
        },
        LV_EVENT_CLICKED, this);

    // Right icons container: Speaker -> Wifi -> Battery (paling pinggir)
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 5, 0);

    // Bluetooth BLE Button hidden in XiaoZhi mode
    bluetooth_label_ = nullptr;

    // 2. Speaker Button (Tap -> Volume Overlay Slider!)
    lv_obj_t* vol_btn = lv_obj_create(right_icons);
    lv_obj_set_size(vol_btn, 24, 24);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_btn, 0, 0);
    lv_obj_set_style_pad_all(vol_btn, 0, 0);
    mute_label_ = lv_label_create(vol_btn);
    lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, hdr_clr, 0);
    lv_obj_center(mute_label_);
    lv_obj_add_flag(vol_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        vol_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowVolumeOverlay();
        },
        LV_EVENT_CLICKED, this);

    // 3. Wifi Button
    lv_obj_t* wifi_btn = lv_obj_create(right_icons);
    lv_obj_set_size(wifi_btn, 24, 24);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    network_label_ = lv_label_create(wifi_btn);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, hdr_clr, 0);
    lv_obj_center(network_label_);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        wifi_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowWifiConfigModal();
        },
        LV_EVENT_CLICKED, this);

    // 4. Battery Button (paling pinggir kanan, with battery_label_ and battery_pct_label_)
    lv_obj_t* battery_btn = lv_obj_create(right_icons);
    lv_obj_set_size(battery_btn, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_bg_opa(battery_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_btn, 0, 0);
    lv_obj_set_style_pad_all(battery_btn, 0, 0);
    lv_obj_set_flex_flow(battery_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_btn, 3, 0);

    battery_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, hdr_clr, 0);

    battery_pct_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_pct_label_, "");
    lv_obj_set_style_text_font(battery_pct_label_, text_font, 0);
    lv_obj_set_style_text_color(battery_pct_label_, hdr_clr, 0);

    lv_obj_add_flag(battery_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        battery_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

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
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 56,
                 0);  // Confined between left buttons and right icons
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        status_bar_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

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
    lv_obj_add_event_cb(
        content_,
        [](lv_event_t* e) {
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
                app.Schedule([]() { Application::GetInstance().AbortSpeaking(kAbortReasonNone); });
            }
        },
        LV_EVENT_CLICKED, this);

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
void LcdDisplay::SetChatMessage_Theme2(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(
            TAG,
            "SetChatMessage_Theme2('%s', '%s') called before SetupUI() - message will be lost!",
            role, content);
    }
    DisplayLockGuard lock(this);
    if (idle_dashboard_visible_) {
        idle_dashboard_visible_ = false;
        if (idle_dashboard_)
            lv_obj_add_flag(idle_dashboard_, LV_OBJ_FLAG_HIDDEN);
        if (content_)
            lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }
    if (content_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG, "SetChatMessage_Theme2('%s', '%s') failed: content_ is nullptr", role,
                     content);
        }
        return;
    }

    // Check if message count exceeds limit - safely hide oldest unhidden message
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= kMaxDisplayMessages) {
        for (uint32_t i = 0; i < child_count; i++) {
            lv_obj_t* ch = lv_obj_get_child(content_, i);
            if (ch && !lv_obj_has_flag(ch, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(ch, LV_OBJ_FLAG_HIDDEN);
                break;
            }
        }
    }

    // Collapse or update system messages (if it's a system message, update existing or hide it)
    if (strcmp(role, "system") == 0) {
        child_count = lv_obj_get_child_cnt(content_);
        if (child_count > 0) {
            lv_obj_t* last_obj = lv_obj_get_child(content_, child_count - 1);
            if (last_obj != nullptr && lv_obj_is_valid(last_obj)) {
                void* bubble_type_ptr = lv_obj_get_user_data(last_obj);
                lv_obj_t* text_label = nullptr;
                if (lv_obj_get_child_cnt(last_obj) > 0) {
                    lv_obj_t* child0 = lv_obj_get_child(last_obj, 0);
                    if (child0 != nullptr && lv_obj_is_valid(child0)) {
                        if (bubble_type_ptr == nullptr) {
                            bubble_type_ptr = lv_obj_get_user_data(child0);
                        }
                        if (lv_obj_get_child_cnt(child0) > 0) {
                            text_label = lv_obj_get_child(child0, 0);
                        } else {
                            text_label = child0;
                        }
                    }
                }

                if (bubble_type_ptr != nullptr &&
                    strcmp((const char*)bubble_type_ptr, "system") == 0) {
                    if (content == nullptr || content[0] == '\0') {
                        // Empty message: cleanly hide previous system message without destroying
                        // objects
                        lv_obj_add_flag(last_obj, LV_OBJ_FLAG_HIDDEN);
                        return;
                    } else if (text_label != nullptr && lv_obj_is_valid(text_label)) {
                        // In-place text update without deleting/reallocating widgets
                        lv_obj_remove_flag(last_obj, LV_OBJ_FLAG_HIDDEN);
                        lv_label_set_text(text_label, content);
                        lv_obj_scroll_to_view_recursive(last_obj, LV_ANIM_OFF);
                        return;
                    } else {
                        // Hide previous system message to avoid memory/event corruption
                        lv_obj_add_flag(last_obj, LV_OBJ_FLAG_HIDDEN);
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
    if (content == nullptr || content[0] == '\0') {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    // 1. Create a full-width container first under content_
    lv_obj_t* container = lv_obj_create(content_);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(container, (void*)role);

    // 2. Create the message bubble directly inside container
    lv_obj_t* msg_bubble = lv_obj_create(container);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(msg_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_left(msg_bubble, 8, 0);
    lv_obj_set_style_pad_right(msg_bubble, 8, 0);
    lv_obj_set_style_pad_top(msg_bubble, 5, 0);
    lv_obj_set_style_pad_bottom(msg_bubble, 3, 0);
    lv_obj_set_flex_flow(msg_bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(msg_bubble, 1, 0);
    lv_obj_set_user_data(msg_bubble, (void*)role);

    // Create the message text with dynamic chat font size
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_obj_set_style_text_font(msg_text, GetChatFont(), 0);
    lv_label_set_text(msg_text, content);

    // Calculate bubble width constraints (8px left pad + 8px right pad = 16px)
    lv_coord_t max_bubble_w = LV_HOR_RES * 85 / 100;  // 204px
    lv_coord_t min_bubble_w = 60;
    lv_coord_t max_text_w = max_bubble_w - 16;  // 188px text area inside bubble

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
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        lv_color_t bubble_bg = lvgl_theme->assistant_bubble_color();
        lv_color_t text_col = GetContrastTextColor(bubble_bg);
        lv_obj_set_style_bg_color(msg_bubble, bubble_bg, 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(msg_bubble, 1, 0);
        lv_obj_set_style_border_color(msg_bubble, lvgl_theme->border_color(), 0);
        lv_obj_set_style_text_color(msg_text, text_col, 0);
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);
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
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(footer_row, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* time_label = lv_label_create(footer_row);
        lv_label_set_text(time_label, time_str);
        lv_obj_set_style_text_font(time_label, &font_noto_sans_basic_14_1, 0);
        lv_obj_set_style_text_color(time_label, stamp_color, 0);
        lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);
    }

    if (strcmp(role, "user") == 0) {
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
    lv_obj_scroll_to_view_recursive(container, LV_ANIM_OFF);

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
    lv_color_t xz_bg =
        (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_border =
        (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, xz_bg, 0);

    /* Clean Solid Dynamic Background Container - Fits exactly between header (top_bar_) and bottom
     * nav */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, width_, height_ - 60);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(container_, xz_bg, 0);
    lv_obj_align(container_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    /* Background image widget scaled with LV_IMAGE_ALIGN_COVER to completely eliminate top/bottom gaps */
    bg_image_ = lv_image_create(container_);
    lv_obj_set_size(bg_image_, width_, height_ - 60);
    lv_obj_align(bg_image_, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_inner_align(bg_image_, LV_IMAGE_ALIGN_COVER);
    lv_obj_remove_flag(bg_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(bg_image_, LV_OBJ_FLAG_SCROLLABLE);
    if (lvgl_theme->background_image() != nullptr) {
        lv_image_set_src(bg_image_, lvgl_theme->background_image()->image_dsc());
        lv_obj_remove_flag(bg_image_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bg_image_, LV_OBJ_FLAG_HIDDEN);
    }

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
    lv_obj_set_size(top_bar_, width_, 30);
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

    // Left buttons container: Rotation button + Brightness button
    lv_obj_t* left_box = lv_obj_create(top_bar_);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_box, 5, 0);

    lv_color_t hdr_clr = lv_color_hex(header_icon_color_);

    lv_obj_t* rot_btn = lv_obj_create(left_box);
    lv_obj_set_size(rot_btn, 24, 24);
    lv_obj_set_style_bg_opa(rot_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rot_btn, 0, 0);
    lv_obj_set_style_pad_all(rot_btn, 0, 0);
    rot_icon_label_ = lv_label_create(rot_btn);
    lv_label_set_text(rot_icon_label_, MATERIAL_SYMBOLS_REFRESH);
    lv_obj_set_style_text_font(rot_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(rot_icon_label_, hdr_clr, 0);
    lv_obj_center(rot_icon_label_);
    lv_obj_add_flag(rot_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        rot_btn,
        [](lv_event_t* e) {
            (void)e;
            Board::GetInstance().RotateDisplay();
        },
        LV_EVENT_CLICKED, nullptr);

    // 2. Brightness Button (Tap -> Brightness Overlay Slider!)
    lv_obj_t* bright_btn = lv_obj_create(left_box);
    lv_obj_set_size(bright_btn, 24, 24);
    lv_obj_set_style_bg_opa(bright_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bright_btn, 0, 0);
    lv_obj_set_style_pad_all(bright_btn, 0, 0);
    br_icon_label_ = lv_label_create(bright_btn);
    lv_label_set_text(br_icon_label_, MATERIAL_SYMBOLS_BRIGHTNESS_6);
    lv_obj_set_style_text_font(br_icon_label_, icon_font, 0);
    lv_obj_set_style_text_color(br_icon_label_, hdr_clr, 0);
    lv_obj_center(br_icon_label_);
    lv_obj_add_flag(bright_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        bright_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowBrightnessOverlay();
        },
        LV_EVENT_CLICKED, this);

    // Right icons container: Speaker -> Wifi -> Battery (Bluetooth icon hidden in XiaoZhi mode)
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_icons, 5, 0);

    bluetooth_label_ = nullptr;  // Sembunyikan icon Bluetooth di mode XiaoZhi

    // Speaker Button (Tap -> Volume Overlay Slider!)
    lv_obj_t* vol_btn = lv_obj_create(right_icons);
    lv_obj_set_size(vol_btn, 24, 24);
    lv_obj_set_style_bg_opa(vol_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_btn, 0, 0);
    lv_obj_set_style_pad_all(vol_btn, 0, 0);
    mute_label_ = lv_label_create(vol_btn);
    lv_label_set_text(mute_label_, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, hdr_clr, 0);
    lv_obj_center(mute_label_);
    lv_obj_add_flag(vol_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        vol_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowVolumeOverlay();
        },
        LV_EVENT_CLICKED, this);

    // Wifi Button
    lv_obj_t* wifi_btn = lv_obj_create(right_icons);
    lv_obj_set_size(wifi_btn, 24, 24);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    network_label_ = lv_label_create(wifi_btn);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, hdr_clr, 0);
    lv_obj_center(network_label_);
    lv_obj_add_flag(wifi_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        wifi_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ShowWifiConfigModal();
        },
        LV_EVENT_CLICKED, this);

    // Battery Button (paling pinggir kanan)
    lv_obj_t* battery_btn = lv_obj_create(right_icons);
    lv_obj_set_size(battery_btn, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_bg_opa(battery_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_btn, 0, 0);
    lv_obj_set_style_pad_all(battery_btn, 0, 0);
    lv_obj_set_flex_flow(battery_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_btn, 3, 0);

    battery_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, hdr_clr, 0);

    battery_pct_label_ = lv_label_create(battery_btn);
    lv_label_set_text(battery_pct_label_, "");
    lv_obj_set_style_text_font(battery_pct_label_, text_font, 0);
    lv_obj_set_style_text_color(battery_pct_label_, hdr_clr, 0);

    lv_obj_add_flag(battery_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        battery_btn,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

    lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        top_bar_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

    lv_obj_add_flag(right_icons, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        right_icons,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

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
    lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 50,
                 0);  // Confined between left logo and right icons (never covers speaker!)
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        status_bar_,
        [](lv_event_t* e) {
            auto display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            display->ToggleControlCenter();
        },
        LV_EVENT_CLICKED, this);

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
