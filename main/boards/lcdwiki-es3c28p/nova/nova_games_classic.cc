#include "nova_display_internal.h"

static lv_color_t Get2048Color(int val) {
    switch (val) {
        case 0:
            return lv_color_hex(0x131B2A);  // Empty
        case 2:
            return lv_color_hex(0x1E2E42);
        case 4:
            return lv_color_hex(0x273F5C);
        case 8:
            return lv_color_hex(0xF59E0B);  // Amber
        case 16:
            return lv_color_hex(0xEA580C);  // Orange
        case 32:
            return lv_color_hex(0xDC2626);  // Red
        case 64:
            return lv_color_hex(0xE11D48);  // Rose
        case 128:
            return lv_color_hex(0x7C3AED);  // Violet
        case 256:
            return lv_color_hex(0x4F46E5);  // Indigo
        case 512:
            return lv_color_hex(0x0284C7);  // Sky
        case 1024:
            return lv_color_hex(0x059669);  // Emerald
        case 2048:
            return lv_color_hex(0xD97706);  // Gold
        default:
            return lv_color_hex(0xB45309);
    }
}

void NovaDisplay::Reset2048() {
    memset(board_2048_, 0, sizeof(board_2048_));
    score_2048_ = 0;
    game_over_2048_ = false;
    won_2048_ = false;

    Settings prefs("game_2048", true);
    best_2048_ = prefs.GetInt("best", 0);

    SpawnTile2048();
    SpawnTile2048();
    UpdateUI2048();
}

void NovaDisplay::SpawnTile2048() {
    struct EmptyCell {
        int r, c;
    };
    EmptyCell empties[16];
    int count = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (board_2048_[r][c] == 0) {
                empties[count++] = {r, c};
            }
        }
    }
    if (count == 0)
        return;
    int idx = rand() % count;
    board_2048_[empties[idx].r][empties[idx].c] = (rand() % 10 == 0) ? 4 : 2;
}

bool NovaDisplay::CanMove2048() {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (board_2048_[r][c] == 0)
                return true;
            if (c < 3 && board_2048_[r][c] == board_2048_[r][c + 1])
                return true;
            if (r < 3 && board_2048_[r][c] == board_2048_[r + 1][c])
                return true;
        }
    }
    return false;
}

bool NovaDisplay::Move2048(int dir) {
    if (game_over_2048_)
        return false;

    bool changed = false;

    auto slide_line = [this](int line[4]) -> bool {
        int temp[4] = {0};
        int t = 0;
        for (int i = 0; i < 4; ++i) {
            if (line[i] != 0)
                temp[t++] = line[i];
        }
        for (int i = 0; i < 3; ++i) {
            if (temp[i] != 0 && temp[i] == temp[i + 1]) {
                temp[i] *= 2;
                score_2048_ += temp[i];
                if (temp[i] == 2048 && !won_2048_)
                    won_2048_ = true;
                temp[i + 1] = 0;
                i++;
            }
        }
        int res[4] = {0};
        t = 0;
        for (int i = 0; i < 4; ++i) {
            if (temp[i] != 0)
                res[t++] = temp[i];
        }
        bool line_changed = false;
        for (int i = 0; i < 4; ++i) {
            if (line[i] != res[i])
                line_changed = true;
            line[i] = res[i];
        }
        return line_changed;
    };

    int prev_score = score_2048_;
    bool prev_won = won_2048_;

    if (dir == 0) {  // UP
        for (int c = 0; c < 4; ++c) {
            int line[4] = {board_2048_[0][c], board_2048_[1][c], board_2048_[2][c],
                           board_2048_[3][c]};
            if (slide_line(line))
                changed = true;
            for (int r = 0; r < 4; ++r)
                board_2048_[r][c] = line[r];
        }
    } else if (dir == 1) {  // DOWN
        for (int c = 0; c < 4; ++c) {
            int line[4] = {board_2048_[3][c], board_2048_[2][c], board_2048_[1][c],
                           board_2048_[0][c]};
            if (slide_line(line))
                changed = true;
            for (int r = 0; r < 4; ++r)
                board_2048_[3 - r][c] = line[r];
        }
    } else if (dir == 2) {  // LEFT
        for (int r = 0; r < 4; ++r) {
            int line[4] = {board_2048_[r][0], board_2048_[r][1], board_2048_[r][2],
                           board_2048_[r][3]};
            if (slide_line(line))
                changed = true;
            for (int c = 0; c < 4; ++c)
                board_2048_[r][c] = line[c];
        }
    } else if (dir == 3) {  // RIGHT
        for (int r = 0; r < 4; ++r) {
            int line[4] = {board_2048_[r][3], board_2048_[r][2], board_2048_[r][1],
                           board_2048_[r][0]};
            if (slide_line(line))
                changed = true;
            for (int c = 0; c < 4; ++c)
                board_2048_[r][3 - c] = line[c];
        }
    }

    if (changed) {
        if (score_2048_ > best_2048_) {
            best_2048_ = score_2048_;
            Settings prefs("game_2048", true);
            prefs.SetInt("best", best_2048_);
        }
        SpawnTile2048();
        if (!CanMove2048()) {
            game_over_2048_ = true;
        }

        if (game_over_2048_) {
            PlaySound(SOUND_LOSE_BALL);
        } else if (!prev_won && won_2048_) {
            PlaySound(SOUND_WIN);
        } else if (score_2048_ > prev_score) {
            PlaySound(SOUND_CAR_SCORE);
        } else {
            PlaySound(SOUND_PADDLE_HIT);
        }

        UpdateUI2048();
    }
    return changed;
}

void NovaDisplay::UpdateUI2048() {
    if (score_lbl_2048_) {
        char s_buf[32];
        snprintf(s_buf, sizeof(s_buf), "SKOR: %d", score_2048_);
        lv_label_set_text(score_lbl_2048_, s_buf);
    }
    if (best_lbl_2048_) {
        char b_buf[32];
        snprintf(b_buf, sizeof(b_buf), "REKOR: %d", best_2048_);
        lv_label_set_text(best_lbl_2048_, b_buf);
    }

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!tiles_2048_[r][c] || !tile_labels_2048_[r][c])
                continue;
            int v = board_2048_[r][c];
            lv_color_t bg = Get2048Color(v);
            lv_obj_set_style_bg_color(tiles_2048_[r][c], bg, LV_PART_MAIN);

            if (v == 0) {
                lv_label_set_text(tile_labels_2048_[r][c], "");
            } else {
                char num_buf[16];
                snprintf(num_buf, sizeof(num_buf), "%d", v);
                lv_label_set_text(tile_labels_2048_[r][c], num_buf);
                const lv_font_t* font = (v >= 1024) ? FONT_10 : ((v >= 128) ? FONT_12 : FONT_14);
                lv_obj_set_style_text_font(tile_labels_2048_[r][c], font, LV_PART_MAIN);
                lv_obj_set_style_text_color(tile_labels_2048_[r][c], C_TEXT, LV_PART_MAIN);
            }
        }
    }

    if (card_2048_) {
        if (game_over_2048_) {
            lv_obj_clear_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
            if (card_title_2048_)
                lv_label_set_text(card_title_2048_, "GAME OVER!");
            if (card_sub_2048_) {
                char sub_buf[48];
                snprintf(sub_buf, sizeof(sub_buf), "Skor Akhir: %d", score_2048_);
                lv_label_set_text(card_sub_2048_, sub_buf);
            }
            if (card_btn_2048_)
                lv_label_set_text(card_btn_2048_, "MAIN LAGI");
        } else if (won_2048_) {
            lv_obj_clear_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
            if (card_title_2048_)
                lv_label_set_text(card_title_2048_, "2048 TERCAPAI!");
            if (card_sub_2048_)
                lv_label_set_text(card_sub_2048_, "Luar biasa! Ingin lanjut?");
            if (card_btn_2048_)
                lv_label_set_text(card_btn_2048_, "LANJUT MAIN");
        } else {
            lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NovaDisplay::Build2048() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "GAME 2048");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left: 170x170 Board at X=10, Y=36
        int board_w = 170;
        int tile_sz = 36;
        int gap = 4;
        int pad = 5;

        lv_obj_t* board_cont = lv_obj_create(root);
        lv_obj_set_pos(board_cont, 10, 36);
        lv_obj_set_size(board_cont, board_w, board_w);
        lv_obj_clear_flag(board_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(board_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(board_cont, lv_color_hex(0x0C1322), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(board_cont, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(board_cont, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(board_cont, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(board_cont, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(board_cont, 0, LV_PART_MAIN);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                tiles_2048_[r][c] = lv_obj_create(board_cont);
                lv_obj_set_pos(tiles_2048_[r][c], pad + c * (tile_sz + gap),
                               pad + r * (tile_sz + gap));
                lv_obj_set_size(tiles_2048_[r][c], tile_sz, tile_sz);
                lv_obj_clear_flag(tiles_2048_[r][c],
                                  (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                lv_obj_set_style_radius(tiles_2048_[r][c], 6, LV_PART_MAIN);
                lv_obj_set_style_border_width(tiles_2048_[r][c], 0, LV_PART_MAIN);

                tile_labels_2048_[r][c] = lv_label_create(tiles_2048_[r][c]);
                lv_obj_center(tile_labels_2048_[r][c]);
            }
        }

        // Gesture & Swipe event on board
        lv_obj_add_event_cb(
            board_cont,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                lv_event_code_t code = lv_event_get_code(e);
                if (code == LV_EVENT_PRESSED) {
                    lv_indev_get_point(lv_indev_active(), &self->touch_start_2048_);
                } else if (code == LV_EVENT_RELEASED) {
                    lv_point_t pt;
                    lv_indev_get_point(lv_indev_active(), &pt);
                    int dx = pt.x - self->touch_start_2048_.x;
                    int dy = pt.y - self->touch_start_2048_.y;
                    if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                        int dir =
                            (std::abs(dx) > std::abs(dy)) ? ((dx > 0) ? 3 : 2) : ((dy > 0) ? 1 : 0);
                        self->Move2048(dir);
                    }
                } else if (code == LV_EVENT_GESTURE) {
                    lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                    if (gdir == LV_DIR_TOP)
                        self->Move2048(0);
                    else if (gdir == LV_DIR_BOTTOM)
                        self->Move2048(1);
                    else if (gdir == LV_DIR_LEFT)
                        self->Move2048(2);
                    else if (gdir == LV_DIR_RIGHT)
                        self->Move2048(3);
                }
            },
            LV_EVENT_ALL, this);

        // Right side: Status and Buttons (X=190, W=120)
        lv_obj_t* sc_panel = MakePanel(root, 190, 36, 120, 26, C_PANEL, C_BORDER, 6);
        score_lbl_2048_ = MakeCenteredLabel(sc_panel, "SKOR: 0", 0, 5, 120, 16, FONT_10, C_CYAN);

        lv_obj_t* best_panel = MakePanel(root, 190, 66, 120, 26, C_PANEL, C_BORDER, 6);
        best_lbl_2048_ = MakeCenteredLabel(best_panel, "REKOR: 0", 0, 5, 120, 16, FONT_10, C_AMBER);

        MakeButton(
            root, "RESET", 190, 96, 120, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Reset2048();
            },
            this);

        // 4 D-Pad buttons for accessible navigation
        MakeButton(
            root, "^", 236, 128, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(0);
            },
            this);
        MakeButton(
            root, "<", 202, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(2);
            },
            this);
        MakeButton(
            root, "v", 236, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(1);
            },
            this);
        MakeButton(
            root, ">", 270, 158, 30, 26, C_PANEL, C_BORDER, C_CYAN, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->Move2048(3);
            },
            this);

        // Game Over Overlay
        card_2048_ = MakePanel(root, 20, 60, 150, 100, C_PANEL, C_CYAN, 10);
        lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        card_title_2048_ =
            MakeCenteredLabel(card_2048_, "GAME OVER!", 0, 10, 150, 18, FONT_12, C_TEXT);
        card_sub_2048_ = MakeCenteredLabel(card_2048_, "Skor: 0", 0, 32, 150, 14, FONT_10, C_MUTED);
        lv_obj_t* btn = MakeButton(
            card_2048_, "MAIN LAGI", 25, 58, 100, 26, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Reset2048();
            },
            this);
        card_btn_2048_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status row (Y=36, H=28)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 70, 28, C_PANEL, C_BORDER, 8);
        score_lbl_2048_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 6, 70, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 85, 36, 85, 28, C_PANEL, C_BORDER, 8);
        best_lbl_2048_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 6, 85, 16, FONT_10, C_AMBER);

        MakeButton(
            root, "RESET", 175, 36, 55, 28, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Reset2048();
            },
            this);

        // 200x200 Board at X=20, Y=68
        int board_w = 200;
        int tile_sz = 44;
        int gap = 4;
        int pad = 5;

        lv_obj_t* board_cont = lv_obj_create(root);
        lv_obj_set_pos(board_cont, 20, 68);
        lv_obj_set_size(board_cont, board_w, board_w);
        lv_obj_clear_flag(board_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(board_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(board_cont, lv_color_hex(0x0C1322), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(board_cont, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(board_cont, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(board_cont, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(board_cont, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(board_cont, 0, LV_PART_MAIN);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                tiles_2048_[r][c] = lv_obj_create(board_cont);
                lv_obj_set_pos(tiles_2048_[r][c], pad + c * (tile_sz + gap),
                               pad + r * (tile_sz + gap));
                lv_obj_set_size(tiles_2048_[r][c], tile_sz, tile_sz);
                lv_obj_clear_flag(tiles_2048_[r][c],
                                  (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                lv_obj_set_style_radius(tiles_2048_[r][c], 6, LV_PART_MAIN);
                lv_obj_set_style_border_width(tiles_2048_[r][c], 0, LV_PART_MAIN);

                tile_labels_2048_[r][c] = lv_label_create(tiles_2048_[r][c]);
                lv_obj_center(tile_labels_2048_[r][c]);
            }
        }

        // Gesture & Swipe event on board
        lv_obj_add_event_cb(
            board_cont,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                lv_event_code_t code = lv_event_get_code(e);
                if (code == LV_EVENT_PRESSED) {
                    lv_indev_get_point(lv_indev_active(), &self->touch_start_2048_);
                } else if (code == LV_EVENT_RELEASED) {
                    lv_point_t pt;
                    lv_indev_get_point(lv_indev_active(), &pt);
                    int dx = pt.x - self->touch_start_2048_.x;
                    int dy = pt.y - self->touch_start_2048_.y;
                    if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                        int dir =
                            (std::abs(dx) > std::abs(dy)) ? ((dx > 0) ? 3 : 2) : ((dy > 0) ? 1 : 0);
                        self->Move2048(dir);
                    }
                } else if (code == LV_EVENT_GESTURE) {
                    lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                    if (gdir == LV_DIR_TOP)
                        self->Move2048(0);
                    else if (gdir == LV_DIR_BOTTOM)
                        self->Move2048(1);
                    else if (gdir == LV_DIR_LEFT)
                        self->Move2048(2);
                    else if (gdir == LV_DIR_RIGHT)
                        self->Move2048(3);
                }
            },
            LV_EVENT_ALL, this);

        MakeCenteredLabel(root, "Geser layar untuk menggabungkan angka", 0, 272, 240, 14, FONT_10,
                          C_MUTED);

        // Game Over Overlay Card
        card_2048_ = MakePanel(root, 30, 110, 180, 110, C_PANEL, C_CYAN, 12);
        lv_obj_add_flag(card_2048_, LV_OBJ_FLAG_HIDDEN);
        card_title_2048_ =
            MakeCenteredLabel(card_2048_, "GAME OVER!", 0, 12, 180, 20, FONT_14, C_TEXT);
        card_sub_2048_ =
            MakeCenteredLabel(card_2048_, "Skor Akhir: 0", 0, 36, 180, 16, FONT_10, C_MUTED);
        lv_obj_t* btn = MakeButton(
            card_2048_, "MAIN LAGI", 35, 66, 110, 28, C_CYAN, C_CYAN, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self)
                    self->Reset2048();
            },
            this);
        card_btn_2048_ = lv_obj_get_child(btn, 0);
    }

    Reset2048();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- RETRO SNAKE IMPLEMENTATION ---
// ============================================================================

static void OnSnakeTimer(lv_timer_t* tmr) {
    auto* self = static_cast<NovaDisplay*>(lv_timer_get_user_data(tmr));
    if (self)
        self->SnakeTick();
}

void NovaDisplay::ResetSnake() {
    snake_state_ = SNAKE_IDLE;
    snake_score_ = 0;
    snake_len_ = 3;
    snake_dir_ = SNAKE_RIGHT;
    snake_next_dir_ = SNAKE_RIGHT;

    Settings prefs("game_snake", true);
    snake_best_ = prefs.GetInt("best", 0);

    snake_body_[0] = {6, 7};
    snake_body_[1] = {5, 7};
    snake_body_[2] = {4, 7};

    SpawnSnakeFood();
    UpdateSnakeUI();
}

void NovaDisplay::StartSnake() {
    if (snake_state_ == SNAKE_RUNNING)
        return;
    snake_state_ = SNAKE_RUNNING;
    if (snake_card_)
        lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);

    if (snake_timer_) {
        lv_timer_del(snake_timer_);
    }
    snake_timer_ = lv_timer_create(OnSnakeTimer, 160, this);
}

void NovaDisplay::SpawnSnakeFood() {
    bool on_body = true;
    int tries = 0;
    while (on_body && tries < 100) {
        snake_food_.x = rand() % SNAKE_GRID_W;
        snake_food_.y = rand() % SNAKE_GRID_H;
        on_body = false;
        for (int i = 0; i < snake_len_; ++i) {
            if (snake_body_[i].x == snake_food_.x && snake_body_[i].y == snake_food_.y) {
                on_body = true;
                break;
            }
        }
        tries++;
    }
}

void NovaDisplay::ChangeSnakeDir(SnakeDir new_dir) {
    if ((new_dir == SNAKE_UP && snake_dir_ == SNAKE_DOWN) ||
        (new_dir == SNAKE_DOWN && snake_dir_ == SNAKE_UP) ||
        (new_dir == SNAKE_LEFT && snake_dir_ == SNAKE_RIGHT) ||
        (new_dir == SNAKE_RIGHT && snake_dir_ == SNAKE_LEFT)) {
        return;
    }
    snake_next_dir_ = new_dir;
    PlaySound(SOUND_CAR_STEER);
    if (snake_state_ == SNAKE_IDLE) {
        StartSnake();
    }
}

void NovaDisplay::SnakeTick() {
    if (snake_state_ != SNAKE_RUNNING)
        return;

    snake_dir_ = snake_next_dir_;
    SnakePoint head = snake_body_[0];

    if (snake_dir_ == SNAKE_UP)
        head.y--;
    else if (snake_dir_ == SNAKE_DOWN)
        head.y++;
    else if (snake_dir_ == SNAKE_LEFT)
        head.x--;
    else if (snake_dir_ == SNAKE_RIGHT)
        head.x++;

    // Wall collision
    if (head.x < 0 || head.x >= SNAKE_GRID_W || head.y < 0 || head.y >= SNAKE_GRID_H) {
        snake_state_ = SNAKE_OVER;
        if (snake_timer_) {
            lv_timer_del(snake_timer_);
            snake_timer_ = nullptr;
        }
        PlaySound(SOUND_CAR_CRASH);
        UpdateSnakeUI();
        return;
    }

    // Self bite collision
    for (int i = 0; i < snake_len_ - 1; ++i) {
        if (head.x == snake_body_[i].x && head.y == snake_body_[i].y) {
            snake_state_ = SNAKE_OVER;
            if (snake_timer_) {
                lv_timer_del(snake_timer_);
                snake_timer_ = nullptr;
            }
            PlaySound(SOUND_CAR_CRASH);
            UpdateSnakeUI();
            return;
        }
    }

    // Food collision
    bool ate = (head.x == snake_food_.x && head.y == snake_food_.y);
    if (ate) {
        snake_score_ += 10;
        PlaySound(SOUND_BRICK_HIT);
        if (snake_score_ > snake_best_) {
            snake_best_ = snake_score_;
            Settings prefs("game_snake", true);
            prefs.SetInt("best", snake_best_);
        }
        if (snake_len_ < SNAKE_MAX_SEGS) {
            snake_len_++;
        }
        SpawnSnakeFood();
    }

    for (int i = snake_len_ - 1; i > 0; --i) {
        snake_body_[i] = snake_body_[i - 1];
    }
    snake_body_[0] = head;

    UpdateSnakeUI();
}

void NovaDisplay::UpdateSnakeUI() {
    if (snake_score_lbl_) {
        char s_buf[32];
        snprintf(s_buf, sizeof(s_buf), "SKOR: %d", snake_score_);
        lv_label_set_text(snake_score_lbl_, s_buf);
    }
    if (snake_best_lbl_) {
        char b_buf[32];
        snprintf(b_buf, sizeof(b_buf), "REKOR: %d", snake_best_);
        lv_label_set_text(snake_best_lbl_, b_buf);
    }

    if (snake_food_obj_) {
        lv_obj_set_pos(snake_food_obj_, snake_food_.x * 10, snake_food_.y * 10);
    }

    for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
        if (!snake_segs_[i])
            continue;
        if (i < snake_len_) {
            lv_obj_clear_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(snake_segs_[i], snake_body_[i].x * 10, snake_body_[i].y * 10);
            lv_color_t col = (i == 0) ? C_CYAN : C_MINT;
            lv_obj_set_style_bg_color(snake_segs_[i], col, LV_PART_MAIN);
        } else {
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (snake_card_) {
        if (snake_state_ == SNAKE_OVER) {
            lv_obj_clear_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
            if (snake_card_title_)
                lv_label_set_text(snake_card_title_, "GAME OVER!");
            if (snake_card_sub_) {
                char sub_buf[48];
                snprintf(sub_buf, sizeof(sub_buf), "Skor Akhir: %d", snake_score_);
                lv_label_set_text(snake_card_sub_, sub_buf);
            }
            if (snake_card_btn_)
                lv_label_set_text(snake_card_btn_, "MAIN LAGI");
        } else {
            lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NovaDisplay::BuildSnake() {
    lv_obj_t* root = NewRoot();
    MakeHeader(root, "RETRO SNAKE");

    bool is_land = (width_ > 240);

    if (is_land) {
        // === LANDSCAPE LAYOUT (320x240) ===
        // Left: 190x160 Arena at X=10, Y=36
        snake_arena_ = lv_obj_create(root);
        lv_obj_set_pos(snake_arena_, 10, 36);
        lv_obj_set_size(snake_arena_, 190, 160);
        lv_obj_clear_flag(snake_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(snake_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(snake_arena_, lv_color_hex(0x070B14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(snake_arena_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(snake_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(snake_arena_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(snake_arena_, 0, LV_PART_MAIN);

        // Food Object
        snake_food_obj_ = lv_obj_create(snake_arena_);
        lv_obj_set_size(snake_food_obj_, 10, 10);
        lv_obj_clear_flag(snake_food_obj_,
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_bg_color(snake_food_obj_, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_set_style_radius(snake_food_obj_, 5, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_food_obj_, 0, LV_PART_MAIN);

        // Pre-create body segments
        for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
            snake_segs_[i] = lv_obj_create(snake_arena_);
            lv_obj_set_size(snake_segs_[i], 10, 10);
            lv_obj_clear_flag(snake_segs_[i],
                              (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_radius(snake_segs_[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(snake_segs_[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(snake_segs_[i], (i == 0) ? C_CYAN : C_MINT, LV_PART_MAIN);
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Swipe & Gesture in Arena
        lv_obj_add_event_cb(
            snake_arena_,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                lv_event_code_t code = lv_event_get_code(e);
                if (code == LV_EVENT_PRESSED) {
                    lv_indev_get_point(lv_indev_active(), &self->touch_start_snake_);
                } else if (code == LV_EVENT_RELEASED) {
                    lv_point_t pt;
                    lv_indev_get_point(lv_indev_active(), &pt);
                    int dx = pt.x - self->touch_start_snake_.x;
                    int dy = pt.y - self->touch_start_snake_.y;
                    if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                        if (std::abs(dx) > std::abs(dy)) {
                            self->ChangeSnakeDir((dx > 0) ? SNAKE_RIGHT : SNAKE_LEFT);
                        } else {
                            self->ChangeSnakeDir((dy > 0) ? SNAKE_DOWN : SNAKE_UP);
                        }
                    }
                } else if (code == LV_EVENT_GESTURE) {
                    lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                    if (gdir == LV_DIR_TOP)
                        self->ChangeSnakeDir(SNAKE_UP);
                    else if (gdir == LV_DIR_BOTTOM)
                        self->ChangeSnakeDir(SNAKE_DOWN);
                    else if (gdir == LV_DIR_LEFT)
                        self->ChangeSnakeDir(SNAKE_LEFT);
                    else if (gdir == LV_DIR_RIGHT)
                        self->ChangeSnakeDir(SNAKE_RIGHT);
                }
            },
            LV_EVENT_ALL, this);

        // Right side: Status and D-Pad
        lv_obj_t* p1 = MakePanel(root, 210, 36, 100, 26, C_PANEL, C_BORDER, 6);
        snake_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 100, 16, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 210, 66, 100, 26, C_PANEL, C_BORDER, 6);
        snake_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 100, 16, FONT_10, C_AMBER);

        MakeButton(
            root, "RESET", 210, 96, 100, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetSnake();
                    self->StartSnake();
                }
            },
            this);

        // D-Pad buttons
        MakeButton(
            root, "^", 244, 126, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_UP);
            },
            this);
        MakeButton(
            root, "<", 208, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_LEFT);
            },
            this);
        MakeButton(
            root, "v", 244, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_DOWN);
            },
            this);
        MakeButton(
            root, ">", 280, 158, 32, 28, C_PANEL, C_BORDER, C_MINT, FONT_12,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_RIGHT);
            },
            this);

        // Game Over Card Overlay
        snake_card_ = MakePanel(root, 25, 66, 160, 96, C_PANEL, C_RED, 10);
        lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        snake_card_title_ =
            MakeCenteredLabel(snake_card_, "GAME OVER!", 0, 10, 160, 18, FONT_12, C_RED);
        snake_card_sub_ =
            MakeCenteredLabel(snake_card_, "Skor: 0", 0, 32, 160, 14, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            snake_card_, "MAIN LAGI", 30, 56, 100, 26, C_RED, C_RED, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetSnake();
                    self->StartSnake();
                }
            },
            this);
        snake_card_btn_ = lv_obj_get_child(btn, 0);
    } else {
        // === PORTRAIT LAYOUT (240x320) ===
        // Top status row (Y=36, H=26)
        lv_obj_t* p1 = MakePanel(root, 10, 36, 70, 26, C_PANEL, C_BORDER, 8);
        snake_score_lbl_ = MakeCenteredLabel(p1, "SKOR: 0", 0, 5, 70, 15, FONT_10, C_CYAN);

        lv_obj_t* p2 = MakePanel(root, 85, 36, 85, 26, C_PANEL, C_BORDER, 8);
        snake_best_lbl_ = MakeCenteredLabel(p2, "REKOR: 0", 0, 5, 85, 15, FONT_10, C_AMBER);

        MakeButton(
            root, "RESET", 175, 36, 55, 26, C_PANEL_2, C_BORDER, C_TEXT, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetSnake();
                    self->StartSnake();
                }
            },
            this);

        // Arena: 210x150 at X=15, Y=64 (Grid: 21x15)
        snake_arena_ = lv_obj_create(root);
        lv_obj_set_pos(snake_arena_, 15, 64);
        lv_obj_set_size(snake_arena_, 210, 150);
        lv_obj_clear_flag(snake_arena_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(snake_arena_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(snake_arena_, lv_color_hex(0x070B14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(snake_arena_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(snake_arena_, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_arena_, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(snake_arena_, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(snake_arena_, 0, LV_PART_MAIN);

        // Food Object
        snake_food_obj_ = lv_obj_create(snake_arena_);
        lv_obj_set_size(snake_food_obj_, 10, 10);
        lv_obj_clear_flag(snake_food_obj_,
                          (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_style_bg_color(snake_food_obj_, lv_color_hex(0xEF4444), LV_PART_MAIN);
        lv_obj_set_style_radius(snake_food_obj_, 5, LV_PART_MAIN);
        lv_obj_set_style_border_width(snake_food_obj_, 0, LV_PART_MAIN);

        // Pre-create body segments
        for (int i = 0; i < SNAKE_MAX_SEGS; ++i) {
            snake_segs_[i] = lv_obj_create(snake_arena_);
            lv_obj_set_size(snake_segs_[i], 10, 10);
            lv_obj_clear_flag(snake_segs_[i],
                              (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_style_radius(snake_segs_[i], 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(snake_segs_[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(snake_segs_[i], (i == 0) ? C_CYAN : C_MINT, LV_PART_MAIN);
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Swipe & Gesture in Arena
        lv_obj_add_event_cb(
            snake_arena_,
            [](lv_event_t* e) {
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (!self)
                    return;
                lv_event_code_t code = lv_event_get_code(e);
                if (code == LV_EVENT_PRESSED) {
                    lv_indev_get_point(lv_indev_active(), &self->touch_start_snake_);
                } else if (code == LV_EVENT_RELEASED) {
                    lv_point_t pt;
                    lv_indev_get_point(lv_indev_active(), &pt);
                    int dx = pt.x - self->touch_start_snake_.x;
                    int dy = pt.y - self->touch_start_snake_.y;
                    if (std::abs(dx) > 16 || std::abs(dy) > 16) {
                        if (std::abs(dx) > std::abs(dy)) {
                            self->ChangeSnakeDir((dx > 0) ? SNAKE_RIGHT : SNAKE_LEFT);
                        } else {
                            self->ChangeSnakeDir((dy > 0) ? SNAKE_DOWN : SNAKE_UP);
                        }
                    }
                } else if (code == LV_EVENT_GESTURE) {
                    lv_dir_t gdir = lv_indev_get_gesture_dir(lv_indev_active());
                    if (gdir == LV_DIR_TOP)
                        self->ChangeSnakeDir(SNAKE_UP);
                    else if (gdir == LV_DIR_BOTTOM)
                        self->ChangeSnakeDir(SNAKE_DOWN);
                    else if (gdir == LV_DIR_LEFT)
                        self->ChangeSnakeDir(SNAKE_LEFT);
                    else if (gdir == LV_DIR_RIGHT)
                        self->ChangeSnakeDir(SNAKE_RIGHT);
                }
            },
            LV_EVENT_ALL, this);

        // Ergonomic D-Pad at Y=218 to 286
        MakeButton(
            root, "^", 102, 218, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))->ChangeSnakeDir(SNAKE_UP);
            },
            this);
        MakeButton(
            root, "<", 62, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_LEFT);
            },
            this);
        MakeButton(
            root, "v", 102, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_DOWN);
            },
            this);
        MakeButton(
            root, ">", 142, 252, 36, 32, C_PANEL, C_BORDER, C_MINT, FONT_14,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) == LV_EVENT_CLICKED)
                    static_cast<NovaDisplay*>(lv_event_get_user_data(e))
                        ->ChangeSnakeDir(SNAKE_RIGHT);
            },
            this);

        // Game Over Card Overlay
        snake_card_ = MakePanel(root, 25, 88, 190, 102, C_PANEL, C_RED, 12);
        lv_obj_add_flag(snake_card_, LV_OBJ_FLAG_HIDDEN);
        snake_card_title_ =
            MakeCenteredLabel(snake_card_, "GAME OVER!", 0, 10, 190, 20, FONT_12, C_RED);
        snake_card_sub_ =
            MakeCenteredLabel(snake_card_, "Skor Akhir: 0", 0, 34, 190, 16, FONT_10, C_TEXT);
        lv_obj_t* btn = MakeButton(
            snake_card_, "MAIN LAGI", 45, 62, 100, 28, C_RED, C_RED, C_BG, FONT_10,
            [](lv_event_t* e) {
                if (lv_event_get_code(e) != LV_EVENT_CLICKED)
                    return;
                auto* self = static_cast<NovaDisplay*>(lv_event_get_user_data(e));
                if (self) {
                    self->ResetSnake();
                    self->StartSnake();
                }
            },
            this);
        snake_card_btn_ = lv_obj_get_child(btn, 0);
    }

    ResetSnake();
    StartSnake();
    MakeBottomNav(root, PAGE_APPS);
}

// ============================================================================
// --- GAME BRICK BREAKER (ARKANOID) IMPLEMENTATION ---
// ============================================================================
