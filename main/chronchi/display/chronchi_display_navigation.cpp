#include "chronchi_display_internal.h"

// ---- NAVIGATION ICON DRAWING (Google Maps style) ----
static inline void put_pixel(uint8_t* buf, int x, int y, uint16_t color) {
    if (x < 0 || x >= 48 || y < 0 || y >= 48) return;
    int idx = (y * 48 + x) * 2;
    buf[idx] = color & 0xFF;
    buf[idx + 1] = (color >> 8) & 0xFF;
}

static void draw_filled_circle(uint8_t* buf, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                put_pixel(buf, cx+dx, cy+dy, color);
}

// Modern arrow up
static void draw_arrow_up(uint8_t* buf, uint16_t color) {
    for (int y = 24; y <= 42; y++)
        for (int x = 21; x <= 26; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 24, 42, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            put_pixel(buf, x, 6 + i, color);
    }
}

// Modern arrow left
static void draw_arrow_left(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 12; x <= 34; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 34, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 6 + i, y, color);
    }
}

// Modern arrow right
static void draw_arrow_right(uint8_t* buf, uint16_t color) {
    for (int y = 21; y <= 26; y++)
        for (int x = 13; x <= 35; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 13, 24, 3, color);
    for (int i = 0; i < 16; i++) {
        int half = 2 + (i * 8) / 16;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 41 - i, y, color);
    }
}

// Modern slight left
static void draw_arrow_slight_left(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 32 - i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            put_pixel(buf, x + d, y, color);
            put_pixel(buf, x, y + d, color);
        }
    }
    draw_filled_circle(buf, 32, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 12 - i/2, cy = 14 - i/2;
        for (int d = -half; d <= half; d++)
            put_pixel(buf, cx + d, cy, color);
    }
}

// Modern slight right
static void draw_arrow_slight_right(uint8_t* buf, uint16_t color) {
    for (int i = 0; i < 20; i++) {
        int x = 15 + i;
        int y = 34 - i;
        for (int d = -2; d <= 2; d++) {
            put_pixel(buf, x + d, y, color);
            put_pixel(buf, x, y + d, color);
        }
    }
    draw_filled_circle(buf, 15, 34, 3, color);
    for (int i = 0; i < 12; i++) {
        int half = 2 + (i * 5) / 12;
        int cx = 35 + i/2, cy = 14 - i/2;
        for (int d = -half; d <= half; d++)
            put_pixel(buf, cx + d, cy, color);
    }
}

// Modern roundabout
static void draw_roundabout(uint8_t* buf, uint16_t color) {
    int r = 14;
    for (int a = 0; a < 360; a += 2) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -1; t <= 1; t++) {
            int x = 24 + (int)((r + t) * cosf(rad));
            int y = 24 + (int)((r + t) * sinf(rad));
            put_pixel(buf, x, y, color);
        }
    }
    for (int y = 21; y <= 26; y++)
        for (int x = 30; x <= 42; x++)
            put_pixel(buf, x, y, color);
    for (int i = 0; i < 8; i++) {
        int half = 1 + (i * 3) / 8;
        for (int y = 24 - half; y <= 24 + half; y++)
            put_pixel(buf, 42 - i, y, color);
    }
}

// Modern arrive (Google Maps pin)
static void draw_arrive(uint8_t* buf, uint16_t color, uint16_t cutout) {
    draw_filled_circle(buf, 24, 14, 9, color);
    draw_filled_circle(buf, 24, 14, 4, cutout);
    for (int i = 0; i < 16; i++) {
        int half = 9 - (i * 6) / 16;
        for (int x = 24 - half; x <= 24 + half; x++)
            put_pixel(buf, x, 23 + i, color);
    }
    put_pixel(buf, 24, 39, color);
    put_pixel(buf, 24, 40, color);
}

// Modern car icon
static void draw_car(uint8_t* buf, uint16_t color, uint16_t cutout) {
    for (int y = 24; y <= 36; y++)
        for (int x = 10; x <= 37; x++)
            put_pixel(buf, x, y, color);
    for (int y = 16; y <= 24; y++)
        for (int x = 15; x <= 32; x++)
            put_pixel(buf, x, y, color);
    for (int y = 17; y <= 23; y++)
        for (int x = 16; x <= 22; x++)
            put_pixel(buf, x, y, cutout);
    for (int y = 17; y <= 23; y++)
        for (int x = 25; x <= 31; x++)
            put_pixel(buf, x, y, cutout);
    draw_filled_circle(buf, 15, 37, 4, color);
    draw_filled_circle(buf, 32, 37, 4, color);
    put_pixel(buf, 10, 28, 0xFFE0);
    put_pixel(buf, 10, 29, 0xFFE0);
    put_pixel(buf, 37, 28, 0xFFE0);
    put_pixel(buf, 37, 29, 0xFFE0);
}

// Modern U-turn arrow
static void draw_arrow_uturn(uint8_t* buf, uint16_t color) {
    for (int y = 20; y <= 40; y++)
        for (int x = 27; x <= 32; x++)
            put_pixel(buf, x, y, color);
    draw_filled_circle(buf, 29, 40, 2, color);
    for (int a = 0; a <= 180; a += 3) {
        float rad = a * 3.14159f / 180.0f;
        for (int t = -2; t <= 2; t++) {
            int x = 20 + (int)((9 + t) * cosf(rad));
            int y = 20 - (int)((9 + t) * sinf(rad));
            put_pixel(buf, x, y, color);
        }
    }
    for (int y = 20; y <= 32; y++)
        for (int x = 9; x <= 14; x++)
            put_pixel(buf, x, y, color);
    for (int i = 0; i < 12; i++) {
        int half = 1 + (i * 7) / 12;
        for (int x = 11 - half; x <= 11 + half; x++)
            put_pixel(buf, x, 32 + i, color);
    }
}

void ChronchiDisplay::setNavIcon(const char* maneuver) {
    if (!nav_icon_buf_) return;
    if (maneuver) last_maneuver_ = maneuver;
    const auto& th = PRESET_THEMES[current_layout_ % NUM_THEMES];
    uint16_t clear_color = hex_to_rgb565(th.card_bg);
    for (int i = 0; i < 48 * 48; i++) {
        nav_icon_buf_[i * 2] = clear_color & 0xFF;
        nav_icon_buf_[i * 2 + 1] = (clear_color >> 8) & 0xFF;
    }
    uint16_t fg = hex_to_rgb565(th.accent);
    if (fg == clear_color) {
        fg = light_mode_ ? 0x0290 : 0x07FF;
    }
    uint16_t cutout = clear_color;

    const char* m = last_maneuver_.c_str();
    if (strcmp(m, "STRAIGHT") == 0) draw_arrow_up(nav_icon_buf_, fg);
    else if (strcmp(m, "LEFT") == 0) draw_arrow_left(nav_icon_buf_, fg);
    else if (strcmp(m, "RIGHT") == 0) draw_arrow_right(nav_icon_buf_, fg);
    else if (strcmp(m, "SLIGHT_LEFT") == 0) draw_arrow_slight_left(nav_icon_buf_, fg);
    else if (strcmp(m, "SLIGHT_RIGHT") == 0) draw_arrow_slight_right(nav_icon_buf_, fg);
    else if (strcmp(m, "UTURN") == 0 || strcmp(m, "U_TURN") == 0 || strcmp(m, "PUTAR_BALIK") == 0) draw_arrow_uturn(nav_icon_buf_, fg);
    else if (strcmp(m, "ROUNDABOUT") == 0) draw_roundabout(nav_icon_buf_, fg);
    else if (strcmp(m, "ARRIVE") == 0) draw_arrive(nav_icon_buf_, fg, cutout);
    else draw_car(nav_icon_buf_, fg, cutout);

    if (nav_icon_img_) lv_image_set_src(nav_icon_img_, &nav_icon_dsc_);
}
