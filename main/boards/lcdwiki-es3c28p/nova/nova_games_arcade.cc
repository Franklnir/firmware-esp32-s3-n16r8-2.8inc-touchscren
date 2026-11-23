#include "nova_display_internal.h"

static void OnBrickTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self)
        self->BrickTick();
}

void NovaDisplay::ResetBrick() {
    brick_state_ = BRICK_IDLE;
    brick_score_ = 0;
    brick_lives_ = 3;
    bricks_left_ = 30;

    Settings prefs("game_brick", true);
    brick_best_ = prefs.GetInt("best", 0);

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            bricks_[r][c] = true;
            if (brick_objs_[r][c]) {
                lv_obj_clear_flag(brick_objs_[r][c], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    int arena_w = (width_ > 240) ? 200 : 220;
    int arena_h = (width_ > 240) ? 168 : 216;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    paddle_x_ = (arena_w - paddle_w) / 2.0f;
    ball_x_ = paddle_x_ + (paddle_w / 2.0f) - 4.0f;
    ball_y_ = arena_h - 26.0f;
    ball_vx_ = 2.4f;
    ball_vy_ = -3.0f;

    if (brick_card_)
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
    UpdateBrickUI();
}

void NovaDisplay::StartBrick() {
    brick_state_ = BRICK_RUNNING;
    if (brick_card_)
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
    if (!brick_timer_) {
        brick_timer_ = lv_timer_create(OnBrickTickStatic, 25, this);
    }
}

void NovaDisplay::SetPaddleX(float x) {
    int arena_w = (width_ > 240) ? 200 : 220;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    paddle_x_ = std::clamp(x, 0.0f, (float)(arena_w - paddle_w));
    if (brick_paddle_obj_) {
        lv_obj_set_x(brick_paddle_obj_, (int)paddle_x_);
    }
}

void NovaDisplay::BrickTick() {
    if (brick_state_ != BRICK_RUNNING)
        return;

    int arena_w = (width_ > 240) ? 200 : 220;
    int arena_h = (width_ > 240) ? 168 : 216;
    float paddle_w = (width_ > 240) ? 40.0f : 44.0f;
    float paddle_h = 8.0f;
    float paddle_y = (width_ > 240) ? (168.0f - 14.0f) : (216.0f - 16.0f);

    ball_x_ += ball_vx_;
    ball_y_ += ball_vy_;

    // Wall bounce
    if (ball_x_ <= 0.0f) {
        ball_x_ = 0.0f;
        ball_vx_ = fabsf(ball_vx_);
        PlaySound(SOUND_WALL_HIT);
    } else if (ball_x_ >= arena_w - 8.0f) {
        ball_x_ = arena_w - 8.0f;
        ball_vx_ = -fabsf(ball_vx_);
        PlaySound(SOUND_WALL_HIT);
    }
    if (ball_y_ <= 0.0f) {
        ball_y_ = 0.0f;
        ball_vy_ = fabsf(ball_vy_);
        PlaySound(SOUND_WALL_HIT);
    }

    // Paddle collision
    if (ball_y_ + 8.0f >= paddle_y && ball_y_ <= paddle_y + paddle_h) {
        if (ball_x_ + 8.0f >= paddle_x_ && ball_x_ <= paddle_x_ + paddle_w) {
            if (ball_vy_ > 0.0f) {
                ball_vy_ = -fabsf(ball_vy_);
                float hit_offset = (ball_x_ + 4.0f) - (paddle_x_ + paddle_w / 2.0f);
                ball_vx_ = hit_offset * 0.18f;
                if (fabsf(ball_vx_) < 0.6f)
                    ball_vx_ = (ball_vx_ >= 0) ? 1.5f : -1.5f;
                PlaySound(SOUND_PADDLE_HIT);
            }
        }
    }

    // Bricks collision
    int bw = (width_ > 240) ? 28 : 32;
    int bh = (width_ > 240) ? 9 : 10;
    int gx = 4, gy = 4;
    int start_y = (width_ > 240) ? 6 : 8;
    int total_grid_w = 6 * bw + 5 * gx;
    int start_x = (arena_w - total_grid_w) / 2;

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (!bricks_[r][c])
                continue;
            int bx = start_x + c * (bw + gx);
            int by = start_y + r * (bh + gy);

            if (ball_x_ + 8.0f >= bx && ball_x_ <= bx + bw && ball_y_ + 8.0f >= by &&
                ball_y_ <= by + bh) {
                bricks_[r][c] = false;
                bricks_left_--;
                brick_score_ += 10;
                ball_vy_ = -ball_vy_;
                PlaySound(SOUND_BRICK_HIT);

                if (brick_objs_[r][c]) {
                    lv_obj_add_flag(brick_objs_[r][c], LV_OBJ_FLAG_HIDDEN);
                }

                if (brick_score_ > brick_best_) {
                    brick_best_ = brick_score_;
                    Settings prefs("game_brick", true);
                    prefs.SetInt("best", brick_best_);
                }

                if (bricks_left_ <= 0) {
                    brick_state_ = BRICK_WON;
                    if (brick_timer_) {
                        lv_timer_del(brick_timer_);
                        brick_timer_ = nullptr;
                    }
                    PlaySound(SOUND_WIN);
                    UpdateBrickUI();
                    return;
                }
                goto after_bricks;
            }
        }
    }
after_bricks:

    // Ball miss bottom
    if (ball_y_ > arena_h) {
        brick_lives_--;
        PlaySound(SOUND_LOSE_BALL);
        if (brick_lives_ > 0) {
            ball_x_ = paddle_x_ + paddle_w / 2.0f - 4.0f;
            ball_y_ = paddle_y - 12.0f;
            ball_vx_ = (esp_random() % 2 == 0) ? 2.4f : -2.4f;
            ball_vy_ = -3.0f;
        } else {
            brick_state_ = BRICK_OVER;
            if (brick_timer_) {
                lv_timer_del(brick_timer_);
                brick_timer_ = nullptr;
            }
            UpdateBrickUI();
            return;
        }
    }

    UpdateBrickUI();
}

void NovaDisplay::UpdateBrickUI() {
    if (brick_score_lbl_) {
        char s[32];
        snprintf(s, sizeof(s), "SKOR: %d", brick_score_);
        lv_label_set_text(brick_score_lbl_, s);
    }
    if (brick_best_lbl_) {
        char s[32];
        snprintf(s, sizeof(s), "REKOR: %d", brick_best_);
        lv_label_set_text(brick_best_lbl_, s);
    }
    if (brick_lives_lbl_) {
        std::string hearts = "";
        for (int i = 0; i < brick_lives_; ++i)
            hearts += "♥";
        if (hearts.empty())
            hearts = "---";
        lv_label_set_text(brick_lives_lbl_, hearts.c_str());
    }
    if (brick_ball_obj_) {
        lv_obj_set_pos(brick_ball_obj_, (int)ball_x_, (int)ball_y_);
    }
    if (brick_paddle_obj_) {
        lv_obj_set_x(brick_paddle_obj_, (int)paddle_x_);
    }

    if (brick_card_) {
        if (brick_state_ == BRICK_OVER) {
            lv_obj_clear_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(brick_card_title_, "GAME OVER!");
            lv_obj_set_style_text_color(brick_card_title_, C_RED, LV_PART_MAIN);
            char sub[48];
            snprintf(sub, sizeof(sub), "Skor Akhir: %d", brick_score_);
            lv_label_set_text(brick_card_sub_, sub);
            if (brick_card_btn_)
                lv_label_set_text(brick_card_btn_, "MAIN LAGI");
        } else if (brick_state_ == BRICK_WON) {
            lv_obj_clear_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(brick_card_title_, "MENANG!");
            lv_obj_set_style_text_color(brick_card_title_, C_MINT, LV_PART_MAIN);
            char sub[48];
            snprintf(sub, sizeof(sub), "Hebat! Semua Bata Hancur!");
            lv_label_set_text(brick_card_sub_, sub);
            if (brick_card_btn_)
                lv_label_set_text(brick_card_btn_, "MAIN LAGI");
        }
    }
}

void NovaDisplay::BuildBrick() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "BRICK BREAKER");

    bool is_land = (width_ > 240);
    int arena_w = is_land ? 200 : 220;
    int arena_h = is_land ? 168 : 216;
    float paddle_w = is_land ? 40.0f : 44.0f;
    float paddle_h = 8.0f;
    float paddle_y = is_land ? (168.0f - 14.0f) : (216.0f - 16.0f);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Arena on left
        brick_arena_ = lv_obj_create(root);
        lv_obj_set_pos(brick_arena_, 8, 36);
        lv_obj_set_size(brick_arena_, arena_w, arena_h);
        lv_obj_clear_flag(brick_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(brick_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(brick_arena_, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(brick_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(brick_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(brick_arena_, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(brick_arena_, 0, LV_PART_MAIN);

        // Sidebar on right (w=98)
        lv_obj_t* p1 = MakePanel(root, 214, 36, 98, 26, C_PANEL, C_BORDER, 6);
        brick_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 98, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 214, 66, 98, 26, C_PANEL, C_BORDER, 6);
        brick_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 98, 16, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 214, 96, 98, 26, C_PANEL, C_BORDER, 6);
        brick_lives_lbl_ = MakeCenteredLabel(p3, "♥♥♥", 0, 5, 98, 16, FONT_10, C_RED);

        MakeButton(
            root, "RESET", 214, 126, 98, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetBrick();
                    self->StartBrick();
                }
            },
            this);

        // Overlay Card
        brick_card_ = MakePanel(brick_arena_, (arena_w - 160) / 2, (arena_h - 96) / 2, 160, 96,
                                C_PANEL, C_BORDER, 10);
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
        brick_card_title_ =
            MakeCenteredLabel(brick_card_, "BRICK BREAKER", 0, 10, 160, 18, FONT_12, C_CYAN);
        brick_card_sub_ =
            MakeCenteredLabel(brick_card_, "Sentuh untuk mulai", 0, 32, 160, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            brick_card_, "MULAI", 30, 56, 100, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetBrick();
                    self->StartBrick();
                }
            },
            this);
        brick_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* p1 = MakePanel(root, 10, 36, 68, 26, C_PANEL, C_BORDER, 8);
        brick_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 68, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 82, 36, 68, 26, C_PANEL, C_BORDER, 8);
        brick_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 68, 15, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 154, 36, 42, 26, C_PANEL, C_BORDER, 8);
        brick_lives_lbl_ = MakeCenteredLabel(p3, "♥♥♥", 0, 5, 42, 15, FONT_10, C_RED);

        MakeButton(
            root, "RST", 200, 36, 30, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetBrick();
                    self->StartBrick();
                }
            },
            this);

        brick_arena_ = lv_obj_create(root);
        lv_obj_set_pos(brick_arena_, 10, 66);
        lv_obj_set_size(brick_arena_, arena_w, arena_h);
        lv_obj_clear_flag(brick_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(brick_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(brick_arena_, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(brick_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(brick_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(brick_arena_, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(brick_arena_, 0, LV_PART_MAIN);

        // Overlay Card
        brick_card_ = MakePanel(brick_arena_, (arena_w - 180) / 2, (arena_h - 100) / 2, 180, 100,
                                C_PANEL, C_BORDER, 12);
        lv_obj_add_flag(brick_card_, LV_OBJ_FLAG_HIDDEN);
        brick_card_title_ =
            MakeCenteredLabel(brick_card_, "BRICK BREAKER", 0, 10, 180, 20, FONT_12, C_CYAN);
        brick_card_sub_ = MakeCenteredLabel(brick_card_, "Geser layar untuk paddle", 0, 34, 180, 16,
                                            FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            brick_card_, "MULAI", 40, 60, 100, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetBrick();
                    self->StartBrick();
                }
            },
            this);
        brick_card_btn_ = lv_obj_get_child(btn, 0);
    }

    // Paddle
    brick_paddle_obj_ = lv_obj_create(brick_arena_);
    lv_obj_set_pos(brick_paddle_obj_, (int)paddle_x_, (int)paddle_y);
    lv_obj_set_size(brick_paddle_obj_, (int)paddle_w, (int)paddle_h);
    lv_obj_clear_flag(brick_paddle_obj_,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(brick_paddle_obj_, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_radius(brick_paddle_obj_, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(brick_paddle_obj_, 0, LV_PART_MAIN);

    // Ball
    brick_ball_obj_ = lv_obj_create(brick_arena_);
    lv_obj_set_pos(brick_ball_obj_, (int)ball_x_, (int)ball_y_);
    lv_obj_set_size(brick_ball_obj_, 8, 8);
    lv_obj_clear_flag(brick_ball_obj_,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(brick_ball_obj_, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_radius(brick_ball_obj_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(brick_ball_obj_, 0, LV_PART_MAIN);

    // Bricks Grid
    int bw = is_land ? 28 : 32;
    int bh = is_land ? 9 : 10;
    int gx = 4, gy = 4;
    int start_y = is_land ? 6 : 8;
    int total_grid_w = 6 * bw + 5 * gx;
    int start_x = (arena_w - total_grid_w) / 2;

    static const lv_color_t kRowColors[5] = {
        lv_color_hex(0xEF4444),  // Red
        lv_color_hex(0xF97316),  // Orange
        lv_color_hex(0xF59E0B),  // Amber
        lv_color_hex(0x10B981),  // Green
        lv_color_hex(0x06B6D4)   // Cyan
    };

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            lv_obj_t* brk = lv_obj_create(brick_arena_);
            lv_obj_set_pos(brk, start_x + c * (bw + gx), start_y + r * (bh + gy));
            lv_obj_set_size(brk, bw, bh);
            lv_obj_clear_flag(brk, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_bg_color(brk, kRowColors[r], LV_PART_MAIN);
            lv_obj_set_style_radius(brk, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(brk, 0, LV_PART_MAIN);
            brick_objs_[r][c] = brk;
        }
    }

    // Touch event handler for paddle dragging
    lv_obj_add_event_cb(
        brick_arena_,
        [](lv_event_t* e) {
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
                if (self->brick_state_ == BRICK_IDLE) {
                    self->StartBrick();
                }
                lv_point_t pt;
                lv_indev_get_point(lv_indev_active(), &pt);
                lv_area_t a;
                lv_obj_get_coords(self->brick_arena_, &a);
                int local_x = pt.x - a.x1;
                float paddle_w = (self->width_ > 240) ? 40.0f : 44.0f;
                self->SetPaddleX((float)local_x - paddle_w / 2.0f);
            }
        },
        LV_EVENT_ALL, this);

    ResetBrick();
    StartBrick();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME MINESWEEPER IMPLEMENTATION ---
// ============================================================================

static void OnMineTimerTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self)
        self->MineTimerTick();
}

void NovaDisplay::MineTimerTick() {
    if (mine_state_ == MINE_PLAYING && !mine_first_click_) {
        mine_time_sec_++;
        UpdateMinesweeperUI();
    }
}

void NovaDisplay::ResetMinesweeper() {
    mine_state_ = MINE_PLAYING;
    mine_first_click_ = true;
    mine_flag_mode_ = false;
    mine_flags_left_ = MINE_TOTAL_MINES;
    mine_time_sec_ = 0;

    if (mine_timer_) {
        lv_timer_del(mine_timer_);
        mine_timer_ = nullptr;
    }

    memset(mine_grid_, 0, sizeof(mine_grid_));
    memset(mine_adjacent_, 0, sizeof(mine_adjacent_));
    memset(mine_revealed_, 0, sizeof(mine_revealed_));
    memset(mine_flagged_, 0, sizeof(mine_flagged_));

    UpdateMinesweeperUI();
}

void NovaDisplay::ToggleMineMode() {
    mine_flag_mode_ = !mine_flag_mode_;
    PlaySound(SOUND_MINE_CLICK);
    UpdateMinesweeperUI();
}

void NovaDisplay::OnMineTileClick(int r, int c) {
    if (r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS)
        return;
    if (mine_state_ != MINE_PLAYING)
        return;

    if (mine_flag_mode_) {
        ToggleMineFlag(r, c);
        return;
    }

    if (mine_flagged_[r][c]) {
        return;
    }

    if (mine_first_click_) {
        // Place mines avoiding clicked cell and its neighbors
        int placed = 0;
        while (placed < MINE_TOTAL_MINES) {
            int mr = esp_random() % MINE_ROWS;
            int mc = esp_random() % MINE_COLS;
            if (abs(mr - r) <= 1 && abs(mc - c) <= 1)
                continue;
            if (!mine_grid_[mr][mc]) {
                mine_grid_[mr][mc] = true;
                placed++;
            }
        }

        // Calculate adjacent numbers
        for (int i = 0; i < MINE_ROWS; ++i) {
            for (int j = 0; j < MINE_COLS; ++j) {
                if (mine_grid_[i][j])
                    continue;
                int count = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        int nr = i + dr, nc = j + dc;
                        if (nr >= 0 && nr < MINE_ROWS && nc >= 0 && nc < MINE_COLS) {
                            if (mine_grid_[nr][nc])
                                count++;
                        }
                    }
                }
                mine_adjacent_[i][j] = (uint8_t)count;
            }
        }

        mine_first_click_ = false;
        if (!mine_timer_) {
            mine_timer_ = lv_timer_create(OnMineTimerTickStatic, 1000, this);
        }
    }

    if (mine_grid_[r][c]) {
        // Mine Detonated!
        mine_state_ = MINE_LOST;
        mine_revealed_[r][c] = true;
        if (mine_timer_) {
            lv_timer_del(mine_timer_);
            mine_timer_ = nullptr;
        }
        PlaySound(SOUND_MINE_EXPLODE);
        UpdateMinesweeperUI();
        return;
    }

    RevealMine(r, c);
    PlaySound(SOUND_MINE_CLICK);
    CheckMineWin();
    UpdateMinesweeperUI();
}

void NovaDisplay::ToggleMineFlag(int r, int c) {
    if (mine_revealed_[r][c])
        return;
    mine_flagged_[r][c] = !mine_flagged_[r][c];
    if (mine_flagged_[r][c]) {
        mine_flags_left_--;
    } else {
        mine_flags_left_++;
    }
    PlaySound(SOUND_MINE_FLAG);
    CheckMineWin();
    UpdateMinesweeperUI();
}

void NovaDisplay::RevealMine(int r, int c) {
    if (r < 0 || r >= MINE_ROWS || c < 0 || c >= MINE_COLS)
        return;
    if (mine_revealed_[r][c] || mine_flagged_[r][c])
        return;

    mine_revealed_[r][c] = true;

    // Flood fill empty neighbors
    if (mine_adjacent_[r][c] == 0) {
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr != 0 || dc != 0) {
                    RevealMine(r + dr, c + dc);
                }
            }
        }
    }
}

void NovaDisplay::CheckMineWin() {
    int unrevealed_safe = 0;
    for (int r = 0; r < MINE_ROWS; ++r) {
        for (int c = 0; c < MINE_COLS; ++c) {
            if (!mine_grid_[r][c] && !mine_revealed_[r][c]) {
                unrevealed_safe++;
            }
        }
    }

    if (unrevealed_safe == 0 && mine_state_ == MINE_PLAYING) {
        mine_state_ = MINE_WON;
        if (mine_timer_) {
            lv_timer_del(mine_timer_);
            mine_timer_ = nullptr;
        }
        PlaySound(SOUND_WIN);
    }
}

void NovaDisplay::UpdateMinesweeperUI() {
    if (mine_count_lbl_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "💣 %02d", mine_flags_left_);
        lv_label_set_text(mine_count_lbl_, buf);
    }
    if (mine_timer_lbl_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "⏱ %03d", std::min(mine_time_sec_, 999));
        lv_label_set_text(mine_timer_lbl_, buf);
    }
    if (mine_face_lbl_) {
        if (mine_state_ == MINE_WON)
            lv_label_set_text(mine_face_lbl_, "😎");
        else if (mine_state_ == MINE_LOST)
            lv_label_set_text(mine_face_lbl_, "😵");
        else
            lv_label_set_text(mine_face_lbl_, "🙂");
    }
    if (mine_mode_btn_) {
        lv_obj_t* lbl = lv_obj_get_child(mine_mode_btn_, 0);
        if (lbl) {
            lv_label_set_text(lbl, mine_flag_mode_ ? "🚩 TANDAI" : "⛏ GALI");
            lv_obj_set_style_text_color(lbl, mine_flag_mode_ ? C_RED : C_CYAN, LV_PART_MAIN);
        }
        lv_obj_set_style_border_color(mine_mode_btn_, mine_flag_mode_ ? C_RED : C_CYAN,
                                      LV_PART_MAIN);
    }

    static const lv_color_t kNumColors[] = {
        lv_color_hex(0x06B6D4),  // 1: Cyan
        lv_color_hex(0x10B981),  // 2: Green
        lv_color_hex(0xEF4444),  // 3: Red
        lv_color_hex(0x8B5CF6),  // 4: Purple
        lv_color_hex(0xF59E0B),  // 5: Amber
        lv_color_hex(0x14B8A6),  // 6: Teal
        lv_color_hex(0xEC4899),  // 7: Pink
        lv_color_hex(0x94A3B8)   // 8: Grey
    };

    for (int r = 0; r < MINE_ROWS; ++r) {
        for (int c = 0; c < MINE_COLS; ++c) {
            lv_obj_t* btn = mine_btns_[r][c];
            lv_obj_t* lbl = mine_lbls_[r][c];
            if (!btn || !lbl)
                continue;

            if (mine_state_ == MINE_LOST && mine_grid_[r][c]) {
                // Show exploded / unflagged mine
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x5A121A), LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_RED, LV_PART_MAIN);
                lv_label_set_text(lbl, "💣");
                lv_obj_set_style_text_color(lbl, C_RED, LV_PART_MAIN);
            } else if (mine_revealed_[r][c]) {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D1525), LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x1E293B), LV_PART_MAIN);
                int count = mine_adjacent_[r][c];
                if (count > 0 && count <= 8) {
                    char num_str[4];
                    snprintf(num_str, sizeof(num_str), "%d", count);
                    lv_label_set_text(lbl, num_str);
                    lv_obj_set_style_text_color(lbl, kNumColors[count - 1], LV_PART_MAIN);
                } else {
                    lv_label_set_text(lbl, "");
                }
            } else if (mine_flagged_[r][c]) {
                lv_obj_set_style_bg_color(btn, C_PANEL_2, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_RED, LV_PART_MAIN);
                lv_label_set_text(lbl, "🚩");
                lv_obj_set_style_text_color(lbl, C_RED, LV_PART_MAIN);
            } else {
                // Covered cell
                lv_obj_set_style_bg_color(btn, C_PANEL, LV_PART_MAIN);
                lv_obj_set_style_border_color(btn, C_BORDER, LV_PART_MAIN);
                lv_label_set_text(lbl, "");
            }
        }
    }
}

void NovaDisplay::BuildMinesweeper() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "MINESWEEPER");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Board: 8 cols x 21px = 168px wide at X=10, Y=36
        const int tile_sz = 20;
        const int gap = 1;
        lv_obj_t* grid_box = lv_obj_create(root);
        lv_obj_set_pos(grid_box, 10, 36);
        lv_obj_set_size(grid_box, 172, 172);
        lv_obj_clear_flag(grid_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(grid_box, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(grid_box, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(grid_box, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(grid_box, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(grid_box, 2, LV_PART_MAIN);

        for (int r = 0; r < MINE_ROWS; ++r) {
            for (int c = 0; c < MINE_COLS; ++c) {
                lv_obj_t* btn = lv_btn_create(grid_box);
                lv_obj_set_pos(btn, c * (tile_sz + gap), r * (tile_sz + gap));
                lv_obj_set_size(btn, tile_sz, tile_sz);
                StyleButton(btn, C_PANEL, C_BORDER, 3);
                lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

                lv_obj_t* lbl = lv_label_create(btn);
                lv_label_set_text(lbl, "");
                lv_obj_set_style_text_font(lbl, FONT_10, LV_PART_MAIN);
                lv_obj_center(lbl);

                mine_btns_[r][c] = btn;
                mine_lbls_[r][c] = lbl;

                lv_obj_set_user_data(btn, (void*)(intptr_t)(r * 8 + c));
                lv_obj_add_event_cb(
                    btn,
                    [](lv_event_t* e) {
                        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                            return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                        if (!self)
                            return;
                        int idx =
                            (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                        self->OnMineTileClick(idx / 8, idx % 8);
                    },
                    LV_EVENT_CLICKED, this);
            }
        }

        // Right sidebar (w=122, X=188, Y=36)
        lv_obj_t* p1 = MakePanel(root, 188, 36, 122, 28, C_PANEL, C_BORDER, 8);
        mine_count_lbl_ = MakeCenteredLabel(p1, "💣 10", 0, 6, 122, 16, FONT_10, C_RED);

        lv_obj_t* p2 = MakePanel(root, 188, 68, 122, 28, C_PANEL, C_BORDER, 8);
        mine_timer_lbl_ = MakeCenteredLabel(p2, "⏱ 000", 0, 6, 122, 16, FONT_10, C_AMBER);

        mine_mode_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_mode_btn_, 188, 100);
        lv_obj_set_size(mine_mode_btn_, 122, 30);
        StyleButton(mine_mode_btn_, C_PANEL, C_CYAN, 8);
        lv_obj_t* m_lbl = lv_label_create(mine_mode_btn_);
        lv_label_set_text(m_lbl, "⛏ GALI");
        lv_obj_set_style_text_font(m_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_lbl, C_CYAN, LV_PART_MAIN);
        lv_obj_center(m_lbl);
        lv_obj_add_event_cb(
            mine_mode_btn_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ToggleMineMode();
            },
            LV_EVENT_CLICKED, this);

        mine_face_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_face_btn_, 188, 134);
        lv_obj_set_size(mine_face_btn_, 122, 30);
        StyleButton(mine_face_btn_, C_PANEL_2, C_BORDER, 8);
        mine_face_lbl_ =
            MakeCenteredLabel(mine_face_btn_, "🙂 RESET", 0, 6, 122, 18, FONT_12, C_AMBER);
        lv_obj_add_event_cb(
            mine_face_btn_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ResetMinesweeper();
            },
            LV_EVENT_CLICKED, this);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status bar (Y=36, H=30)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 54, 30, C_PANEL, C_BORDER, 8);
        mine_count_lbl_ = MakeCenteredLabel(p1, "💣 10", 0, 7, 54, 16, FONT_10, C_RED);

        mine_face_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_face_btn_, 68, 36);
        lv_obj_set_size(mine_face_btn_, 34, 30);
        StyleButton(mine_face_btn_, C_PANEL_2, C_BORDER, 8);
        mine_face_lbl_ = MakeCenteredLabel(mine_face_btn_, "🙂", 0, 5, 34, 18, FONT_14, C_AMBER);
        lv_obj_add_event_cb(
            mine_face_btn_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ResetMinesweeper();
            },
            LV_EVENT_CLICKED, this);

        lv_obj_t* p2 = MakePanel(root, 106, 36, 54, 30, C_PANEL, C_BORDER, 8);
        mine_timer_lbl_ = MakeCenteredLabel(p2, "⏱ 000", 0, 7, 54, 16, FONT_10, C_AMBER);

        mine_mode_btn_ = lv_btn_create(root);
        lv_obj_set_pos(mine_mode_btn_, 164, 36);
        lv_obj_set_size(mine_mode_btn_, 66, 30);
        StyleButton(mine_mode_btn_, C_PANEL, C_CYAN, 8);
        lv_obj_t* m_lbl = lv_label_create(mine_mode_btn_);
        lv_label_set_text(m_lbl, "⛏ GALI");
        lv_obj_set_style_text_font(m_lbl, FONT_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_lbl, C_CYAN, LV_PART_MAIN);
        lv_obj_center(m_lbl);
        lv_obj_add_event_cb(
            mine_mode_btn_,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->ToggleMineMode();
            },
            LV_EVENT_CLICKED, this);

        // Grid Container: 8x8 at X=16, Y=72 (208x208)
        const int tile_sz = 24;
        const int gap = 2;
        lv_obj_t* grid_box = lv_obj_create(root);
        lv_obj_set_pos(grid_box, 16, 72);
        lv_obj_set_size(grid_box, 208, 208);
        lv_obj_clear_flag(grid_box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(grid_box, lv_color_hex(0x080D1A), LV_PART_MAIN);
        lv_obj_set_style_border_color(grid_box, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(grid_box, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(grid_box, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(grid_box, 1, LV_PART_MAIN);

        for (int r = 0; r < MINE_ROWS; ++r) {
            for (int c = 0; c < MINE_COLS; ++c) {
                lv_obj_t* btn = lv_btn_create(grid_box);
                lv_obj_set_pos(btn, c * (tile_sz + gap), r * (tile_sz + gap));
                lv_obj_set_size(btn, tile_sz, tile_sz);
                StyleButton(btn, C_PANEL, C_BORDER, 4);
                lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

                lv_obj_t* lbl = lv_label_create(btn);
                lv_label_set_text(lbl, "");
                lv_obj_set_style_text_font(lbl, FONT_10, LV_PART_MAIN);
                lv_obj_center(lbl);

                mine_btns_[r][c] = btn;
                mine_lbls_[r][c] = lbl;

                lv_obj_set_user_data(btn, (void*)(intptr_t)(r * 8 + c));
                lv_obj_add_event_cb(
                    btn,
                    [](lv_event_t* e) {
                        if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                            return;
                        auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                        if (!self)
                            return;
                        int idx =
                            (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
                        self->OnMineTileClick(idx / 8, idx % 8);
                    },
                    LV_EVENT_CLICKED, this);
            }
        }
    }

    ResetMinesweeper();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME RETRO HIGHWAY RACER IMPLEMENTATION ---
// ============================================================================

static void OnRacerTickStatic(lv_timer_t* timer) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(timer));
    if (self)
        self->RacerTick();
}

void NovaDisplay::SpawnTrafficCar(int i, float min_y) {
    // Check which lanes have cars near the spawn area
    bool lane_occupied[3] = {false, false, false};
    for (int j = 0; j < MAX_TRAFFIC; ++j) {
        if (j != i && traffic_[j].active) {
            if (fabsf(traffic_[j].y - min_y) < 75.0f) {
                if (traffic_[j].lane >= 0 && traffic_[j].lane < 3) {
                    lane_occupied[traffic_[j].lane] = true;
                }
            }
        }
    }

    // Pick from strictly free lanes to guarantee a clear passage
    int free_lanes[3];
    int free_count = 0;
    for (int l = 0; l < 3; ++l) {
        if (!lane_occupied[l]) {
            free_lanes[free_count++] = l;
        }
    }

    int chosen_lane = 0;
    if (free_count > 0) {
        chosen_lane = free_lanes[esp_random() % free_count];
    } else {
        chosen_lane = esp_random() % 3;
    }

    static const lv_color_t t_colors[] = {
        lv_color_hex(0xEF4444),  // Sporty Red
        lv_color_hex(0xF59E0B),  // Taxi Amber
        lv_color_hex(0x10B981),  // Emerald Green
        lv_color_hex(0xA855F7)   // Violet Coupe
    };

    traffic_[i].lane = chosen_lane;
    traffic_[i].y = min_y;
    // Base speed: smooth 2.0f to 2.6f
    traffic_[i].speed = 2.0f + (esp_random() % 7) * 0.1f;
    traffic_[i].color = t_colors[esp_random() % 4];
    traffic_[i].active = (i < 2 || racer_score_ >= 6);
}

void NovaDisplay::ResetRacer() {
    racer_state_ = RACER_IDLE;
    racer_player_lane_ = 1;
    racer_score_ = 0;
    racer_speed_ = 80;
    road_stripe_y_ = 0.0f;

    Settings prefs("game_racer", true);
    racer_best_ = prefs.GetInt("best", 0);

    // Initial stagger: spaced 105px apart well above screen so player has time to react
    SpawnTrafficCar(0, -60.0f);
    SpawnTrafficCar(1, -165.0f);
    SpawnTrafficCar(2, -270.0f);

    if (racer_card_) {
        lv_obj_clear_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(racer_card_title_, "HIGHWAY RACER");
        lv_label_set_text(racer_card_sub_, "Ketuk jalan / tombol utk gas!");
        if (racer_card_btn_)
            lv_label_set_text(racer_card_btn_, "MULAI BALAPAN");
    }
    UpdateRacerUI();
}

void NovaDisplay::StartRacer() {
    if (racer_state_ == RACER_RUNNING)
        return;
    racer_state_ = RACER_RUNNING;
    if (racer_card_)
        lv_obj_add_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
    if (!racer_timer_) {
        racer_timer_ = lv_timer_create(OnRacerTickStatic, 30, this);
    }
}

void NovaDisplay::ChangeRacerLane(int delta) {
    if (racer_state_ == RACER_IDLE || racer_state_ == RACER_CRASHED) {
        ResetRacer();
        StartRacer();
        return;
    }
    if (racer_state_ != RACER_RUNNING)
        return;

    int new_lane = racer_player_lane_ + delta;
    if (new_lane >= 0 && new_lane <= 2) {
        racer_player_lane_ = new_lane;
        PlaySound(SOUND_CAR_STEER);
        UpdateRacerUI();
    }
}

void NovaDisplay::RacerTick() {
    if (racer_state_ != RACER_RUNNING)
        return;

    int road_h = (width_ > 240) ? 168 : 174;
    int car_h = (width_ > 240) ? 38 : 44;
    float player_y = (width_ > 240) ? (168.0f - car_h - 10.0f) : (174.0f - car_h - 10.0f);

    // Scroll road stripes smoothly
    road_stripe_y_ += 4.5f;
    if (road_stripe_y_ >= 48.0f) {
        road_stripe_y_ -= 48.0f;
    }

    // Traffic cars movement
    float extra_speed = std::min(1.4f, (racer_score_ / 8) * 0.15f);
    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        if (!traffic_[i].active) {
            if (i == 2 && racer_score_ >= 6) {
                float top_y = 0.0f;
                for (int j = 0; j < 2; ++j) {
                    if (traffic_[j].y < top_y)
                        top_y = traffic_[j].y;
                }
                SpawnTrafficCar(2, std::min(-60.0f, top_y - 90.0f));
            }
            continue;
        }

        traffic_[i].y += traffic_[i].speed + extra_speed;

        // Passed screen bottom -> Score!
        if (traffic_[i].y > road_h + 10.0f) {
            racer_score_++;
            PlaySound(SOUND_CAR_SCORE);

            if (racer_score_ > racer_best_) {
                racer_best_ = racer_score_;
                Settings prefs("game_racer", true);
                prefs.SetInt("best", racer_best_);
            }

            float top_y = 0.0f;
            for (int j = 0; j < MAX_TRAFFIC; ++j) {
                if (j != i && traffic_[j].active && traffic_[j].y < top_y) {
                    top_y = traffic_[j].y;
                }
            }
            float spawn_y = std::min(-60.0f, top_y - 85.0f - (esp_random() % 35));
            SpawnTrafficCar(i, spawn_y);
        }

        // Fair Collision check (with 8px bumper tolerance)
        if (traffic_[i].active && traffic_[i].lane == racer_player_lane_) {
            if (traffic_[i].y + car_h - 8.0f > player_y + 8.0f &&
                traffic_[i].y + 8.0f < player_y + car_h - 8.0f) {
                racer_state_ = RACER_CRASHED;
                if (racer_timer_) {
                    lv_timer_del(racer_timer_);
                    racer_timer_ = nullptr;
                }
                PlaySound(SOUND_CAR_CRASH);
                UpdateRacerUI();
                return;
            }
        }
    }

    UpdateRacerUI();
}

void NovaDisplay::UpdateRacerUI() {
    if (racer_score_lbl_) {
        char s[32];
        snprintf(s, sizeof(s), "SKOR: %d", racer_score_);
        lv_label_set_text(racer_score_lbl_, s);
    }
    if (racer_best_lbl_) {
        char s[32];
        snprintf(s, sizeof(s), "REKOR: %d", racer_best_);
        lv_label_set_text(racer_best_lbl_, s);
    }
    if (racer_speed_lbl_) {
        int current_speed = racer_speed_ + std::min(120, racer_score_ * 3);
        char s[32];
        snprintf(s, sizeof(s), (width_ > 240) ? "SPD: %d km/h" : "%d km/h", current_speed);
        lv_label_set_text(racer_speed_lbl_, s);
    }

    int road_w = 180;
    int lane_w = road_w / 3;  // 60
    int car_w = (width_ > 240) ? 28 : 32;
    int car_h = (width_ > 240) ? 38 : 44;
    float player_y = (width_ > 240) ? (168.0f - car_h - 10.0f) : (174.0f - car_h - 10.0f);

    if (racer_player_car_) {
        int px = racer_player_lane_ * lane_w + (lane_w - car_w) / 2;
        lv_obj_set_pos(racer_player_car_, px, (int)player_y);
    }

    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        if (racer_traffic_cars_[i]) {
            if (!traffic_[i].active) {
                lv_obj_add_flag(racer_traffic_cars_[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(racer_traffic_cars_[i], LV_OBJ_FLAG_HIDDEN);
            int tx = traffic_[i].lane * lane_w + (lane_w - car_w) / 2;
            lv_obj_set_pos(racer_traffic_cars_[i], tx, (int)traffic_[i].y);
            lv_obj_set_style_bg_color(racer_traffic_cars_[i], traffic_[i].color, LV_PART_MAIN);
        }
    }

    // Road stripes (both lane dividers scroll simultaneously)
    for (int i = 0; i < 4; ++i) {
        int sy = (int)(road_stripe_y_ + i * 48.0f - 24.0f);
        if (racer_stripes_[i])
            lv_obj_set_y(racer_stripes_[i], sy);
        if (racer_stripes_[i + 4])
            lv_obj_set_y(racer_stripes_[i + 4], sy);
    }

    if (racer_card_) {
        if (racer_state_ == RACER_CRASHED) {
            lv_obj_clear_flag(racer_card_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(racer_card_title_, "TABRAKAN!!");
            char sub[48];
            snprintf(sub, sizeof(sub), "Skor Balapan: %d", racer_score_);
            lv_label_set_text(racer_card_sub_, sub);
            if (racer_card_btn_)
                lv_label_set_text(racer_card_btn_, "BALAP LAGI");
        }
    }
}

void NovaDisplay::BuildRacer() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "HIGHWAY RACER");

    bool is_land = (width_ > 240);
    int road_w = 180;
    int road_h = is_land ? 168 : 174;
    int lane_w = road_w / 3;
    int car_w = is_land ? 28 : 32;
    int car_h = is_land ? 38 : 44;

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        racer_road_ = lv_obj_create(root);
        lv_obj_set_pos(racer_road_, 12, 36);
        lv_obj_set_size(racer_road_, road_w, road_h);
        lv_obj_clear_flag(racer_road_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(racer_road_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(racer_road_, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_border_color(racer_road_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_road_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(racer_road_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(racer_road_, 0, LV_PART_MAIN);

        // Sidebar on right (w=110, X=202)
        lv_obj_t* p1 = MakePanel(root, 202, 36, 110, 26, C_PANEL, C_BORDER, 6);
        racer_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 110, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 202, 66, 110, 26, C_PANEL, C_BORDER, 6);
        racer_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 110, 16, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 202, 96, 110, 26, C_PANEL, C_BORDER, 6);
        racer_speed_lbl_ = MakeCenteredLabel(p3, "SPD: 80 km/h", 0, 5, 110, 16, FONT_10, C_MINT);

        MakeButton(
            root, "<", 202, 128, 52, 34, C_PANEL, C_BORDER, C_CYAN, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(-1);
            },
            this);
        MakeButton(
            root, ">", 260, 128, 52, 34, C_PANEL, C_BORDER, C_CYAN, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(1);
            },
            this);

        // Start / Crash Card
        racer_card_ = MakePanel(racer_road_, (road_w - 150) / 2, (road_h - 96) / 2, 150, 96,
                                C_PANEL, C_CYAN, 10);
        racer_card_title_ =
            MakeCenteredLabel(racer_card_, "HIGHWAY RACER", 0, 10, 150, 18, FONT_12, C_CYAN);
        racer_card_sub_ =
            MakeCenteredLabel(racer_card_, "Ketuk jalan utk gas!", 0, 32, 150, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            racer_card_, "MULAI BALAPAN", 20, 56, 110, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    if (self->racer_state_ == RACER_CRASHED)
                        self->ResetRacer();
                    self->StartRacer();
                }
            },
            this);
        racer_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        lv_obj_t* p1 = MakePanel(root, 10, 36, 68, 26, C_PANEL, C_BORDER, 8);
        racer_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 68, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 82, 36, 68, 26, C_PANEL, C_BORDER, 8);
        racer_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 68, 15, FONT_10, C_AMBER);

        lv_obj_t* p3 = MakePanel(root, 154, 36, 76, 26, C_PANEL, C_BORDER, 8);
        racer_speed_lbl_ = MakeCenteredLabel(p3, "80 km/h", 0, 5, 76, 15, FONT_10, C_MINT);

        racer_road_ = lv_obj_create(root);
        lv_obj_set_pos(racer_road_, 30, 66);
        lv_obj_set_size(racer_road_, road_w, road_h);
        lv_obj_clear_flag(racer_road_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(racer_road_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(racer_road_, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_border_color(racer_road_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_road_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(racer_road_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(racer_road_, 0, LV_PART_MAIN);

        // Steering buttons at Y=246
        MakeButton(
            root, "< KIRI", 30, 246, 85, 36, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(-1);
            },
            this);
        MakeButton(
            root, "KANAN >", 125, 246, 85, 36, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeRacerLane(1);
            },
            this);

        // Start / Crash Card
        racer_card_ = MakePanel(racer_road_, (road_w - 160) / 2, (road_h - 100) / 2, 160, 100,
                                C_PANEL, C_CYAN, 12);
        racer_card_title_ =
            MakeCenteredLabel(racer_card_, "HIGHWAY RACER", 0, 10, 160, 20, FONT_12, C_CYAN);
        racer_card_sub_ = MakeCenteredLabel(racer_card_, "Ketuk jalan / tombol utk gas!", 0, 34,
                                            160, 16, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            racer_card_, "MULAI BALAPAN", 25, 60, 110, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    if (self->racer_state_ == RACER_CRASHED)
                        self->ResetRacer();
                    self->StartRacer();
                }
            },
            this);
        racer_card_btn_ = lv_obj_get_child(btn, 0);
    }

    // Road Stripes (4 dashed dividers on each lane boundary: total 8 stripes)
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* s1 = lv_obj_create(racer_road_);
        lv_obj_set_pos(s1, lane_w - 1, i * 48);
        lv_obj_set_size(s1, 2, 24);
        lv_obj_set_style_bg_color(s1, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_border_width(s1, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s1, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        racer_stripes_[i] = s1;

        lv_obj_t* s2 = lv_obj_create(racer_road_);
        lv_obj_set_pos(s2, 2 * lane_w - 1, i * 48);
        lv_obj_set_size(s2, 2, 24);
        lv_obj_set_style_bg_color(s2, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
        lv_obj_set_style_border_width(s2, 0, LV_PART_MAIN);
        lv_obj_clear_flag(s2, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        racer_stripes_[i + 4] = s2;
    }

    // Traffic Cars
    for (int i = 0; i < MAX_TRAFFIC; ++i) {
        racer_traffic_cars_[i] = lv_obj_create(racer_road_);
        lv_obj_set_size(racer_traffic_cars_[i], car_w, car_h);
        lv_obj_clear_flag(racer_traffic_cars_[i],
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_radius(racer_traffic_cars_[i], 6, LV_PART_MAIN);
        lv_obj_set_style_border_width(racer_traffic_cars_[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(racer_traffic_cars_[i], traffic_[i].color, LV_PART_MAIN);

        // Windshield on traffic car
        lv_obj_t* w = lv_obj_create(racer_traffic_cars_[i]);
        lv_obj_set_pos(w, (car_w - 20) / 2, 18);
        lv_obj_set_size(w, 20, 10);
        lv_obj_set_style_bg_color(w, lv_color_hex(0x0F172A), LV_PART_MAIN);
        lv_obj_set_style_radius(w, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(w, 0, LV_PART_MAIN);
        lv_obj_clear_flag(w, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    }

    // Player Car (Cyan Sports Car)
    racer_player_car_ = lv_obj_create(racer_road_);
    lv_obj_set_size(racer_player_car_, car_w, car_h);
    lv_obj_clear_flag(racer_player_car_,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(racer_player_car_, C_CYAN, LV_PART_MAIN);
    lv_obj_set_style_radius(racer_player_car_, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(racer_player_car_, 0, LV_PART_MAIN);

    // Player Windshield
    lv_obj_t* pw = lv_obj_create(racer_player_car_);
    lv_obj_set_pos(pw, (car_w - 20) / 2, 10);
    lv_obj_set_size(pw, 20, 12);
    lv_obj_set_style_bg_color(pw, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_radius(pw, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(pw, 0, LV_PART_MAIN);
    lv_obj_clear_flag(pw, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Player Spoiler
    lv_obj_t* ps = lv_obj_create(racer_player_car_);
    lv_obj_set_pos(ps, 2, car_h - 6);
    lv_obj_set_size(ps, car_w - 4, 4);
    lv_obj_set_style_bg_color(ps, lv_color_hex(0x0891B2), LV_PART_MAIN);
    lv_obj_set_style_radius(ps, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(ps, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ps, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Intuitive Direct Lane Touch: Tap left lane -> lane 0, mid -> lane 1, right -> lane 2
    lv_obj_add_event_cb(
        racer_road_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                return;
            auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
            if (!self)
                return;

            if (self->racer_state_ == RACER_IDLE || self->racer_state_ == RACER_CRASHED) {
                if (self->racer_state_ == RACER_CRASHED)
                    self->ResetRacer();
                self->StartRacer();
                return;
            }

            lv_point_t pt;
            lv_indev_get_point(lv_indev_active(), &pt);
            lv_area_t a;
            lv_obj_get_coords(self->racer_road_, &a);
            int road_width = a.x2 - a.x1 + 1;
            int lane_width = road_width / 3;
            int rel_x = pt.x - a.x1;

            int target_lane = rel_x / lane_width;
            if (target_lane < 0)
                target_lane = 0;
            if (target_lane > 2)
                target_lane = 2;

            if (target_lane != self->racer_player_lane_) {
                self->racer_player_lane_ = target_lane;
                self->PlaySound(SOUND_CAR_STEER);
                self->UpdateRacerUI();
            }
        },
        LV_EVENT_CLICKED, this);

    ResetRacer();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// MINI PIANO / TOUCH SYNTHESIZER
// ============================================================================
