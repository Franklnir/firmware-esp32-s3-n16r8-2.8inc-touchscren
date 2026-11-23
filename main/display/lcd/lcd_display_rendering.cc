#include "lcd_display_internal.h"

void LcdDisplay::SetStatus(const char* status) {
    if (music_visualizer_active_ && status != nullptr) {
        // Allow music status updates while visualizer is active, prevent standard state strings
        // (like SPEAKING) from replacing it
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
            if (emoji_box_)
                lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
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
            if (emoji_box_)
                lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_center(emoji_image_);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_)
            lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
    }
}

void LcdDisplay::SetEmojiCollection(std::shared_ptr<EmojiCollection> collection) {
    DisplayLockGuard lock(this);
    auto& tm = LvglThemeManager::GetInstance();
    for (auto& pair : tm.GetAllThemes()) {
        if (pair.second != nullptr) {
            pair.second->set_emoji_collection(collection);
        }
    }
    if (current_theme_ != nullptr) {
        static_cast<LvglTheme*>(current_theme_)->set_emoji_collection(collection);
    }
    if (ui_theme_id_ == 1 && setup_ui_called_) {
        SetEmotion("neutral");
    }
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    current_theme_ = lvgl_theme;

    if (!setup_ui_called_) {
        Display::SetTheme(lvgl_theme);
        return;
    }

    // Get the screen belonging to container_
    lv_obj_t* screen = (container_ != nullptr) ? lv_obj_get_screen(container_) : lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        if (mute_label_ != nullptr)
            lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        if (battery_label_ != nullptr)
            lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        if (network_label_ != nullptr)
            lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        if (mute_label_ != nullptr)
            lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        if (battery_label_ != nullptr)
            lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        if (network_label_ != nullptr)
            lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    const auto* synced = GetCurrentSyncedTheme();
    const auto* synced_bg = GetCurrentSyncedBg();
    lv_color_t xz_bg =
        (synced_bg) ? lv_color_hex(synced_bg->bg_color) : lvgl_theme->background_color();
    lv_color_t xz_panel =
        (synced_bg) ? lv_color_hex(synced_bg->panel_color) : lvgl_theme->background_color();
    lv_color_t xz_border =
        (synced_bg) ? lv_color_hex(synced_bg->border_color) : lvgl_theme->border_color();
    lv_color_t xz_accent = (synced) ? lv_color_hex(synced->accent_color) : lv_color_hex(0x63DDF2);

    uint32_t eff_text = 0xF1F5FA;
    uint32_t eff_muted = 0x91A0B5;
    ResolveEffectiveTextColor(lv_color_to_int(xz_bg), eff_text, eff_muted);
    lv_color_t xz_text = lv_color_hex(eff_text);
    lv_color_t xz_muted = lv_color_hex(eff_muted);

    // Set parent text color and background
    if (screen != nullptr) {
        lv_obj_set_style_text_font(screen, text_font, 0);
        lv_obj_set_style_text_color(screen, xz_text, 0);
        lv_obj_set_style_bg_color(screen, xz_bg, 0);
    }

    // Set background image or solid color
    if (container_ != nullptr) {
        if (ui_theme_id_ != 2) {
            lv_obj_set_size(container_, width_, height_ - 60);
            lv_obj_align(container_, LV_ALIGN_TOP_MID, 0, 30);
        }
        if (bg_image_ != nullptr) {
            if (ui_theme_id_ != 2) {
                lv_obj_set_size(bg_image_, width_, height_ - 60);
                lv_obj_align(bg_image_, LV_ALIGN_CENTER, 0, 0);
            }
            if (lvgl_theme->background_image() != nullptr) {
                lv_image_set_src(bg_image_, lvgl_theme->background_image()->image_dsc());
                lv_image_set_inner_align(bg_image_, LV_IMAGE_ALIGN_COVER);
                lv_obj_remove_flag(bg_image_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(bg_image_, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (lvgl_theme->background_image() != nullptr) {
            lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(),
                                          0);
            lv_obj_set_style_bg_image_opa(container_, LV_OPA_COVER, 0);
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

    lv_color_t icon_clr = lv_color_hex(header_icon_color_);
    if (rot_icon_label_ != nullptr) {
        lv_obj_set_style_text_color(rot_icon_label_, icon_clr, 0);
    }
    if (br_icon_label_ != nullptr) {
        lv_obj_set_style_text_color(br_icon_label_, icon_clr, 0);
    }

    // Update status bar elements
    if (network_label_ != nullptr)
        lv_obj_set_style_text_color(network_label_, icon_clr, 0);
    if (status_label_ != nullptr)
        lv_obj_set_style_text_color(status_label_, xz_text, 0);
    if (notification_label_ != nullptr)
        lv_obj_set_style_text_color(notification_label_, xz_text, 0);
    if (mute_label_ != nullptr)
        lv_obj_set_style_text_color(mute_label_, icon_clr, 0);
    if (battery_label_ != nullptr)
        lv_obj_set_style_text_color(battery_label_, icon_clr, 0);
    if (battery_pct_label_ != nullptr) {
        lv_obj_set_style_text_color(battery_pct_label_, icon_clr, 0);
    }
    if (emoji_label_ != nullptr)
        lv_obj_set_style_text_color(emoji_label_, xz_text, 0);

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
                                lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(),
                                                            0);
                            } else {
                                lv_obj_set_style_text_color(text, GetContrastTextColor(bubble_bg),
                                                            0);
                            }
                        }
                    }
                    if (lv_obj_get_child_cnt(bubble) > 1) {
                        lv_obj_t* footer = lv_obj_get_child(bubble, 1);
                        if (footer != nullptr && lv_obj_get_child_cnt(footer) > 0) {
                            lv_obj_t* time_lbl = lv_obj_get_child(footer, 0);
                            if (time_lbl != nullptr) {
                                lv_obj_set_style_text_color(time_lbl,
                                                            GetContrastTextColor(bubble_bg), 0);
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
            lv_obj_set_style_text_color(chat_message_label_, xz_text, 0);
        }

        if (emoji_label_ != nullptr) {
            lv_obj_set_style_text_color(emoji_label_, xz_text, 0);
        }

        // Update bottom bar background color with 50% opacity and text color
        if (bottom_bar_ != nullptr && ui_theme_id_ != 2) {
            lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
            lv_obj_set_style_bg_color(bottom_bar_, xz_bg, 0);
            lv_obj_set_style_text_color(bottom_bar_, xz_text, 0);
        }
    }

    // Recreate/update bottom navigation bar so it always matches active theme and background
    CreateBottomNav();

    // Update low battery popup
    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    }

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
