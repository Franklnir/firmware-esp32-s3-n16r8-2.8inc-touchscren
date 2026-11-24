#include "lcd_display_internal.h"

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
        if (child_count >= kMaxDisplayMessages) {
            for (uint32_t i = 0; i < child_count; i++) {
                lv_obj_t* ch = lv_obj_get_child(content_, i);
                if (ch && !lv_obj_has_flag(ch, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_add_flag(ch, LV_OBJ_FLAG_HIDDEN);
                    break;
                }
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

        // YouTube Music Header Badge (Single clean header label without colliding "Playing..."
        // text)
        music_sub_label_ = lv_label_create(music_container_);
        std::string sub_badge = "▶ YouTube Music • " + current_music_quality_;
        lv_label_set_text(music_sub_label_, sub_badge.c_str());
        lv_obj_set_style_text_color(music_sub_label_, lv_color_hex(0xE53935), 0);  // YouTube Red

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
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 5, 0);

        static const uint32_t kTheme2BarColors[7] = {
            0x07C160,  // WhatsApp Green
            0x25D366,  // Light WhatsApp Green
            0x00E676,  // Spring Neon Green
            0x00B0FF,  // Electric Cyan
            0x7C4DFF,  // Purple
            0xFFB300,  // Amber Yellow
            0xFF5252   // Coral Red
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
        lv_obj_set_style_text_color(music_duration_label_, lv_color_hex(0x07C160),
                                    0);  // WhatsApp Green
        lv_obj_set_style_text_font(music_duration_label_, lvgl_theme->text_font()->font(), 0);
        char init_dur2[64];
        snprintf(init_dur2, sizeof(init_dur2), "⏱ 00:00 / --:-- • %s", current_music_quality_.c_str());
        lv_label_set_text(music_duration_label_, init_dur2);

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
        lv_obj_set_flex_align(footer_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
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
        lv_obj_set_flex_align(music_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
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
        lv_obj_set_flex_align(bars_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(bars_box, 6, 0);

        static const uint32_t kBarColors[7] = {
            0x7C4DFF,  // Violet
            0x2979FF,  // Electric Blue
            0x00B0FF,  // Cyan
            0x00E676,  // Spring Neon Green
            0xFFD600,  // Electric Yellow
            0xFF6D00,  // Neon Orange
            0xFF1744   // Hot Pink / Red
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
        char init_dur1[64];
        snprintf(init_dur1, sizeof(init_dur1), "00:00 / --:-- • %s", current_music_quality_.c_str());
        lv_label_set_text(music_duration_label_, init_dur1);
        music_title_label_ = nullptr;
        music_sub_label_ = nullptr;
    }
}

void LcdDisplay::AnimateMusicVisualizer() {
    if (!music_visualizer_active_ || music_container_ == nullptr)
        return;

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
        if (dj_bars_[i] == nullptr)
            continue;

        float t = (float)music_anim_phase_ * 0.22f;
        float wave1 = sinf(t * (1.1f + i * 0.28f) + i * 0.8f);
        float wave2 = cosf(t * (0.75f + (6 - i) * 0.22f) - i * 0.5f);
        int beat_pulse = ((music_anim_phase_ + i * 2) % 10 < 3) ? (ui_theme_id_ == 2 ? 8 : 22) : 0;

        int h = (int)(min_h + (wave1 * 0.5f + 0.5f) * (max_h * 0.55f) +
                      (wave2 * 0.5f + 0.5f) * (max_h * 0.25f) + beat_pulse);
        if (h < min_h)
            h = min_h;
        if (h > max_h)
            h = max_h;

        lv_obj_set_height(dj_bars_[i], h);
    }
}

void LcdDisplay::StartMusicVisualizer(const char* title, int total_sec) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_)
        return;

    music_visualizer_active_ = true;
    current_music_sec_ = 0;
    total_music_sec_ = total_sec;

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
            std::string sub_badge = "⏹ YouTube Music • " + current_music_quality_;
            lv_label_set_text(music_sub_label_, sub_badge.c_str());
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
        char buf[64];
        const char* prefix = (ui_theme_id_ == 2) ? "⏱ " : "";
        if (total_sec > 0) {
            if (total_sec >= 3600) {
                snprintf(buf, sizeof(buf), "%s00:00:00 / %02d:%02d:%02d • %s", prefix, total_sec / 3600,
                         (total_sec % 3600) / 60, total_sec % 60, current_music_quality_.c_str());
            } else {
                snprintf(buf, sizeof(buf), "%s00:00 / %02d:%02d • %s", prefix, total_sec / 60,
                         total_sec % 60, current_music_quality_.c_str());
            }
        } else {
            snprintf(buf, sizeof(buf), "%s00:00 / --:-- • %s", prefix, current_music_quality_.c_str());
        }
        lv_label_set_text(music_duration_label_, buf);
    }

    // Start animation timer if not already created
    if (music_anim_timer_ == nullptr) {
        music_anim_timer_ = lv_timer_create(
            [](lv_timer_t* timer) {
                auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(timer));
                if (self) {
                    self->AnimateMusicVisualizer();
                }
            },
            60, this);
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
        std::string st = "🎧 DJ PLAYING • " + current_music_quality_;
        SetStatus(st.c_str());
        if (title && title[0] != '\0') {
            std::string song_display = "🎵 " + std::string(title);
            SetChatMessage("assistant", song_display.c_str());
        }
    }
}

void LcdDisplay::StopMusicVisualizer() {
    DisplayLockGuard lock(this);
    if (!music_visualizer_active_)
        return;

    music_visualizer_active_ = false;

    if (music_anim_timer_ != nullptr) {
        lv_timer_pause(music_anim_timer_);
    }

    if (ui_theme_id_ == 2) {
        // In Theme 2 (WhatsApp Chat): keep the music bubble in the chat log, set status to finished
        if (music_sub_label_ != nullptr) {
            std::string sub_badge = "⏹ YouTube Music • " + current_music_quality_;
            lv_label_set_text(music_sub_label_, sub_badge.c_str());
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
    current_music_sec_ = current_sec;
    total_music_sec_ = total_sec;
    if (!music_visualizer_active_ || music_duration_label_ == nullptr)
        return;

    char buf[64];
    const char* prefix = (ui_theme_id_ == 2) ? "⏱ " : "";
    if (total_sec >= 3600 || current_sec >= 3600) {
        snprintf(buf, sizeof(buf), "%s%02d:%02d:%02d / %02d:%02d:%02d • %s", prefix,
                 current_sec / 3600, (current_sec % 3600) / 60, current_sec % 60,
                 total_sec / 3600, (total_sec % 3600) / 60, total_sec % 60,
                 current_music_quality_.c_str());
    } else if (total_sec > 0) {
        snprintf(buf, sizeof(buf), "%s%02d:%02d / %02d:%02d • %s", prefix,
                 current_sec / 60, current_sec % 60, total_sec / 60, total_sec % 60,
                 current_music_quality_.c_str());
    } else {
        snprintf(buf, sizeof(buf), "%s%02d:%02d / --:-- • %s", prefix,
                 current_sec / 60, current_sec % 60, current_music_quality_.c_str());
    }
    lv_label_set_text(music_duration_label_, buf);
}

void LcdDisplay::SetMusicBitrate(const char* bitrate) {
    DisplayLockGuard lock(this);
    if (!bitrate || bitrate[0] == '\0') {
        return;
    }
    current_music_quality_ = bitrate;
    if (!music_visualizer_active_) {
        return;
    }

    if (ui_theme_id_ == 2) {
        if (music_sub_label_ != nullptr) {
            std::string sub = "▶ YouTube Music • " + current_music_quality_;
            lv_label_set_text(music_sub_label_, sub.c_str());
        }
    } else {
        std::string st = "🎧 DJ PLAYING • " + current_music_quality_;
        SetStatus(st.c_str());
    }

    if (music_duration_label_ != nullptr) {
        UpdateMusicProgress(current_music_sec_, total_music_sec_);
    }
}
