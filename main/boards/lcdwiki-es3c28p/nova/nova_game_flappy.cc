#include "nova_display_internal.h"

static void OnFlappyTouchStatic(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
    if (self)
        self->FlappyFlap();
}

static void OnFlappyTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self)
        self->FlappyTick();
}

void NovaDisplay::FlappyReset() {
    flappy_state_ = FLAPPY_RUNNING;
    flappy_score_ = 0;
    flappy_bird_y_ = (height_ > 240) ? 95.0f : 55.0f;
    flappy_bird_vy_ = -2.2f;
    flappy_pipe_x_ = (width_ > 240) ? 300.0f : 230.0f;
    flappy_pipe_gap_y_ = (height_ > 240) ? 75.0f : 45.0f;

    if (flappy_card_)
        lv_obj_add_flag(flappy_card_, LV_OBJ_FLAG_HIDDEN);
    if (flappy_score_lbl_)
        lv_label_set_text(flappy_score_lbl_, "0");
}

void NovaDisplay::FlappyFlap() {
    if (callbacks_.trigger_click)
        callbacks_.trigger_click();

    if (flappy_state_ == FLAPPY_IDLE || flappy_state_ == FLAPPY_OVER) {
        FlappyReset();
        return;
    }

    if (flappy_state_ == FLAPPY_RUNNING) {
        flappy_bird_vy_ = -3.4f;  // Lompatan pas dan terkontrol (tidak overshooting)
    }
}

void NovaDisplay::FlappyTick() {
    if (flappy_state_ != FLAPPY_RUNNING)
        return;

    // Bird gravity physics
    flappy_bird_y_ += flappy_bird_vy_;
    flappy_bird_vy_ += 0.28f;  // Gravitasi halus dan stabil
    if (flappy_bird_vy_ > 4.5f)
        flappy_bird_vy_ = 4.5f;

    // Pipe movement
    flappy_pipe_x_ -= 2.6f;
    int ground_y = (height_ > 240) ? 222 : 148;
    int gap_h = (height_ > 240) ? 82 : 68;
    if (flappy_pipe_x_ < -36.0f) {
        flappy_pipe_x_ = (width_ > 240) ? (width_ - 24.0f) : 224.0f;
        int max_gap_rand = ground_y - gap_h - 30;
        if (max_gap_rand < 10)
            max_gap_rand = 10;
        flappy_pipe_gap_y_ = 15.0f + (esp_random() % max_gap_rand);
        flappy_score_++;
        if (flappy_score_lbl_) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", flappy_score_);
            lv_label_set_text(flappy_score_lbl_, buf);
        }
        if (flappy_score_ > flappy_best_) {
            flappy_best_ = flappy_score_;
            Settings s("flappy", true);
            s.SetInt("best", flappy_best_);
            if (flappy_best_lbl_) {
                char bbuf[32];
                snprintf(bbuf, sizeof(bbuf), "REKOR: %d", flappy_best_);
                lv_label_set_text(flappy_best_lbl_, bbuf);
            }
        }
    }

    // Update Bird pos
    int bird_x = 42;
    int bird_y = (int)flappy_bird_y_;
    const int bird_size = 20;

    if (flappy_bird_) {
        lv_obj_set_pos(flappy_bird_, bird_x, bird_y);
    }

    // Update Pipes pos & size
    const int pipe_w = 32;

    if (flappy_pipe_top_) {
        int top_h = (int)flappy_pipe_gap_y_;
        lv_obj_set_pos(flappy_pipe_top_, (int)flappy_pipe_x_, 0);
        lv_obj_set_size(flappy_pipe_top_, pipe_w, std::max(2, top_h));
    }
    if (flappy_pipe_bot_) {
        int bot_y = (int)(flappy_pipe_gap_y_ + gap_h);
        int bot_h = ground_y - bot_y;
        lv_obj_set_pos(flappy_pipe_bot_, (int)flappy_pipe_x_, bot_y);
        lv_obj_set_size(flappy_pipe_bot_, pipe_w, std::max(2, bot_h));
    }

    // Collision Check: Floor and Ceiling
    bool dead = false;
    if (bird_y <= 1 || bird_y + bird_size >= ground_y) {
        dead = true;
    }

    // Collision Check: Pipes
    int px = (int)flappy_pipe_x_;
    if (bird_x + bird_size >= px && bird_x <= px + pipe_w) {
        if (bird_y < flappy_pipe_gap_y_ || bird_y + bird_size > flappy_pipe_gap_y_ + gap_h) {
            dead = true;
        }
    }

    if (dead) {
        flappy_state_ = FLAPPY_OVER;
        if (flappy_card_) {
            lv_obj_clear_flag(flappy_card_, LV_OBJ_FLAG_HIDDEN);
            if (flappy_card_title_)
                lv_label_set_text(flappy_card_title_, "GAME OVER!");
            if (flappy_card_sub_) {
                char buf[96];
                snprintf(buf, sizeof(buf), "Skor: %d  |  Rekor: %d", flappy_score_, flappy_best_);
                lv_label_set_text(flappy_card_sub_, buf);
            }
            if (flappy_card_btn_)
                lv_label_set_text(flappy_card_btn_, "MAIN LAGI");
        }
    }
}

void NovaDisplay::BuildFlappy() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "FLAPPY BIRD");

    Settings s("flappy", true);
    flappy_best_ = s.GetInt("best", 0);
    flappy_state_ = FLAPPY_IDLE;
    flappy_score_ = 0;

    // Game Box Container
    int box_w = (width_ > 240) ? (width_ - 16) : 224;
    int box_h = (height_ > 240) ? 240 : 166;
    int ground_h = 18;
    int ground_y = box_h - ground_h;

    flappy_box_ = lv_obj_create(root);
    lv_obj_set_pos(flappy_box_, (width_ - box_w) / 2, 38);
    lv_obj_set_size(flappy_box_, box_w, box_h);
    lv_obj_set_style_bg_color(flappy_box_, lv_color_hex(0x0A0F1D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flappy_box_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_box_, C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_box_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_box_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flappy_box_, 0, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_box_, LV_OBJ_FLAG_SCROLLABLE);

    // Ground Grass Line
    lv_obj_t* ground = lv_obj_create(flappy_box_);
    lv_obj_set_pos(ground, 0, ground_y);
    lv_obj_set_size(ground, box_w, ground_h);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0x15803D), LV_PART_MAIN);
    lv_obj_set_style_border_width(ground, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ground, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);

    // Top Pipe
    flappy_pipe_top_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_pipe_top_, 230, 0);
    lv_obj_set_size(flappy_pipe_top_, 32, 75);
    lv_obj_set_style_bg_color(flappy_pipe_top_, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_pipe_top_, lv_color_hex(0x166534), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_pipe_top_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_pipe_top_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_pipe_top_, LV_OBJ_FLAG_SCROLLABLE);

    // Bottom Pipe
    flappy_pipe_bot_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_pipe_bot_, 230, 150);
    lv_obj_set_size(flappy_pipe_bot_, 32, 72);
    lv_obj_set_style_bg_color(flappy_pipe_bot_, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_pipe_bot_, lv_color_hex(0x166534), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_pipe_bot_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_pipe_bot_, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_pipe_bot_, LV_OBJ_FLAG_SCROLLABLE);

    // Yellow Bird
    flappy_bird_ = lv_obj_create(flappy_box_);
    lv_obj_set_pos(flappy_bird_, 42, 100);
    lv_obj_set_size(flappy_bird_, 20, 20);
    lv_obj_set_style_bg_color(flappy_bird_, lv_color_hex(0xFACC15), LV_PART_MAIN);
    lv_obj_set_style_border_color(flappy_bird_, lv_color_hex(0xCA8A04), LV_PART_MAIN);
    lv_obj_set_style_border_width(flappy_bird_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(flappy_bird_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_bird_, LV_OBJ_FLAG_SCROLLABLE);

    // Bird Eye Dot
    lv_obj_t* eye = lv_obj_create(flappy_bird_);
    lv_obj_set_pos(eye, 11, 4);
    lv_obj_set_size(eye, 4, 4);
    lv_obj_set_style_bg_color(eye, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

    // Big Score Label at Top Center
    flappy_score_lbl_ = MakeCenteredLabel(flappy_box_, "0", 0, 8, box_w, 32, FONT_24, C_TEXT);

    // Best Score indicator inside the game box itself (top right corner, does NOT touch OS header!)
    char best_buf[32];
    snprintf(best_buf, sizeof(best_buf), "REKOR: %d", flappy_best_);
    flappy_best_lbl_ = MakeLabel(flappy_box_, best_buf, box_w - 92, 8, 84, 18, FONT_10, C_AMBER,
                                 LV_TEXT_ALIGN_RIGHT);

    // Modal Card overlay for Start / Game Over
    flappy_card_ = lv_obj_create(flappy_box_);
    lv_obj_set_size(flappy_card_, 190, 116);
    lv_obj_align(flappy_card_, LV_ALIGN_CENTER, 0, 0);
    StyleButton(flappy_card_, C_PANEL_2, C_BORDER, 12);
    lv_obj_set_style_pad_all(flappy_card_, 8, LV_PART_MAIN);
    lv_obj_clear_flag(flappy_card_, LV_OBJ_FLAG_SCROLLABLE);

    flappy_card_title_ =
        MakeCenteredLabel(flappy_card_, "FLAPPY BIRD", 0, 8, 174, 22, FONT_16, C_AMBER);
    flappy_card_sub_ = MakeCenteredLabel(flappy_card_, "Ketuk layar untuk lompat!", 0, 36, 174, 16,
                                         FONT_10, C_MUTED);

    lv_obj_t* btn_box = lv_btn_create(flappy_card_);
    lv_obj_set_pos(btn_box, 35, 66);
    lv_obj_set_size(btn_box, 120, 34);
    StyleButton(btn_box, C_CYAN, C_CYAN, 8);
    flappy_card_btn_ = lv_label_create(btn_box);
    lv_label_set_text(flappy_card_btn_, "MULAI MAIN");
    lv_obj_set_style_text_font(flappy_card_btn_, FONT_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(flappy_card_btn_, C_BG, LV_PART_MAIN);
    lv_obj_center(flappy_card_btn_);

    // Click handler on entire game box
    lv_obj_add_event_cb(flappy_box_, OnFlappyTouchStatic, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(btn_box, OnFlappyTouchStatic, LV_EVENT_CLICKED, this);

    // Start 35ms physics timer
    flappy_timer_ = lv_timer_create(OnFlappyTickStatic, 35, this);

    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// RADIO ONLINE STREAMING
// ============================================================================
