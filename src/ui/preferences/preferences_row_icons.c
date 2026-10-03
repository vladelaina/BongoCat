#include "preferences_icons_internal.h"

static void line(struct nk_command_buffer *canvas, struct nk_rect b,
    float x1, float y1, float x2, float y2, struct nk_color color) {
    nk_stroke_line(canvas, b.x + x1, b.y + y1, b.x + x2, b.y + y2,
        1.5f, color);
}

static void dot(struct nk_command_buffer *canvas, struct nk_rect b,
    float x, float y, float size, struct nk_color color) {
    nk_fill_circle(canvas, nk_rect(b.x + x - size * .5f,
        b.y + y - size * .5f, size, size), color);
}

static void multiple_models(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 1, b.y + 1, 11, 11), 2, 1.5f, color);
    nk_stroke_rect(c, nk_rect(b.x + 6, b.y + 6, 11, 11), 2, 1.5f, color);
    dot(c, b, 4.5f, 4.5f, 2.0f, color);
    dot(c, b, 9.5f, 9.5f, 2.0f, color);
}

static void pass_through(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 1, b.y + 3, 12, 12), 2, 1.5f, color);
    line(c, b, 5, 9, 17, 9, color);
    line(c, b, 14, 6, 17, 9, color);
    line(c, b, 14, 12, 17, 9, color);
}

static void always_on_top(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 3, 15, 15, 15, color);
    line(c, b, 5, 12, 13, 12, color);
    line(c, b, 9, 12, 9, 2, color);
    line(c, b, 5, 6, 9, 2, color);
    line(c, b, 13, 6, 9, 2, color);
}

static void keep_in_screen(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 2, 7, 2, 2, color); line(c, b, 2, 2, 7, 2, color);
    line(c, b, 11, 2, 16, 2, color); line(c, b, 16, 2, 16, 7, color);
    line(c, b, 2, 11, 2, 16, color); line(c, b, 2, 16, 7, 16, color);
    line(c, b, 11, 16, 16, 16, color); line(c, b, 16, 16, 16, 11, color);
}

static void solid_background(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 1, b.y + 1, 16, 16), 2, 1.5f, color);
    nk_fill_rect(c, nk_rect(b.x + 5, b.y + 5, 8, 8), 2, color);
}

static void window_size(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 3, 15, 15, 3, color);
    line(c, b, 3, 10, 3, 15, color); line(c, b, 3, 15, 8, 15, color);
    line(c, b, 10, 3, 15, 3, color); line(c, b, 15, 3, 15, 8, color);
}

static void window_corners(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 1, b.y + 1, 16, 16), 5, 1.5f, color);
}

static void opacity(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 9, 1, 4, 9, color); line(c, b, 9, 1, 14, 9, color);
    nk_stroke_circle(c, nk_rect(b.x + 4, b.y + 6, 10, 10), 1.5f, color);
    line(c, b, 6, 12, 12, 12, color);
}

static void random_expression(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 1, b.y + 1, 16, 16), 1.5f, color);
    dot(c, b, 6, 7, 2, color); dot(c, b, 12, 7, 2, color);
    nk_stroke_curve(c, b.x + 5, b.y + 10, b.x + 6, b.y + 15,
        b.x + 12, b.y + 15, b.x + 13, b.y + 10, 1.5f, color);
}

static void random_motion(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 10, b.y + 1, 4, 4), 1.5f, color);
    line(c, b, 10, 6, 8, 11, color);
    line(c, b, 10, 6, 6, 6, color); line(c, b, 6, 6, 3, 9, color);
    line(c, b, 10, 6, 13, 9, color); line(c, b, 13, 9, 16, 9, color);
    line(c, b, 8, 11, 12, 13, color); line(c, b, 12, 13, 11, 17, color);
    line(c, b, 8, 11, 5, 16, color); line(c, b, 5, 16, 2, 16, color);
}

static void mirror(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 9, 1, 9, 17, color);
    nk_stroke_triangle(c, b.x + 2, b.y + 9, b.x + 7, b.y + 4,
        b.x + 7, b.y + 14, 1.5f, color);
    nk_stroke_triangle(c, b.x + 16, b.y + 9, b.x + 11, b.y + 4,
        b.x + 11, b.y + 14, 1.5f, color);
}

static void mouse_mirror(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_triangle(c, b.x + 1, b.y + 2, b.x + 2, b.y + 14,
        b.x + 7, b.y + 10, 1.5f, color);
    nk_stroke_triangle(c, b.x + 17, b.y + 2, b.x + 16, b.y + 14,
        b.x + 11, b.y + 10, 1.5f, color);
    line(c, b, 9, 2, 9, 6, color); line(c, b, 9, 9, 9, 13, color);
}

static void mouse_centered(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 3, b.y + 3, 12, 12), 1.5f, color);
    line(c, b, 9, 0, 9, 6, color); line(c, b, 9, 12, 9, 18, color);
    line(c, b, 0, 9, 6, 9, color); line(c, b, 12, 9, 18, 9, color);
    dot(c, b, 9, 9, 2.5f, color);
}

static void ignore_mouse(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_triangle(c, b.x + 2, b.y + 1, b.x + 3, b.y + 15,
        b.x + 8, b.y + 11, 1.5f, color);
    line(c, b, 7, 10, 11, 16, color);
    line(c, b, 1, 17, 17, 1, color);
}

static void texture_resolution(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 2, b.y + 2, 14, 14), 2, 1.5f, color);
    nk_stroke_rect(c, nk_rect(b.x + 6, b.y + 6, 6, 6), 1, 1.5f, color);
    line(c, b, 2, 2, 6, 6, color);
    line(c, b, 16, 16, 12, 12, color);
}

static void max_fps(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 1, b.y + 2, 16, 14), 2, 1.5f, color);
    line(c, b, 4, 13, 4, 10, color);
    line(c, b, 8, 13, 8, 7, color);
    line(c, b, 12, 13, 12, 4, color);
    line(c, b, 3, 4, 15, 4, color);
}

static void autostart(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_triangle(c, b.x + 9, b.y + 1, b.x + 4, b.y + 9,
        b.x + 14, b.y + 9, 1.5f, color);
    line(c, b, 6, 9, 6, 14, color); line(c, b, 12, 9, 12, 14, color);
    line(c, b, 6, 14, 12, 14, color);
    line(c, b, 8, 15, 8, 18, color); line(c, b, 10, 15, 10, 18, color);
}

static void administrator(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 9, 1, 16, 4, color); line(c, b, 16, 4, 15, 11, color);
    line(c, b, 15, 11, 12, 15, color); line(c, b, 12, 15, 9, 17, color);
    line(c, b, 9, 17, 6, 15, color); line(c, b, 6, 15, 3, 11, color);
    line(c, b, 3, 11, 2, 4, color); line(c, b, 2, 4, 9, 1, color);
    line(c, b, 9, 2, 9, 16, color); line(c, b, 3, 8, 15, 8, color);
}

static void language(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 1, b.y + 1, 16, 16), 1.5f, color);
    line(c, b, 1, 9, 17, 9, color); line(c, b, 9, 1, 9, 17, color);
    line(c, b, 4, 4, 14, 4, color); line(c, b, 4, 14, 14, 14, color);
}

static void theme(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 2, b.y + 10, 7, 6), 2, 1.5f, color);
    line(c, b, 7, 11, 14, 2, color); line(c, b, 10, 12, 16, 4, color);
    line(c, b, 14, 2, 16, 4, color); line(c, b, 3, 17, 2, 18, color);
}

static void shortcut_visibility(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 1, 9, 5, 5, color); line(c, b, 5, 5, 9, 4, color);
    line(c, b, 9, 4, 13, 5, color); line(c, b, 13, 5, 17, 9, color);
    line(c, b, 1, 9, 5, 13, color); line(c, b, 5, 13, 9, 14, color);
    line(c, b, 9, 14, 13, 13, color); line(c, b, 13, 13, 17, 9, color);
    dot(c, b, 9, 9, 4, color);
}

static void shortcut_preferences(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    line(c, b, 2, 4, 16, 4, color); dot(c, b, 6, 4, 4, color);
    line(c, b, 2, 9, 16, 9, color); dot(c, b, 12, 9, 4, color);
    line(c, b, 2, 14, 16, 14, color); dot(c, b, 8, 14, 4, color);
}

static void shortcut_menu(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 1, b.y + 1, 16, 16), 1.5f, color);
    nk_stroke_circle(c, nk_rect(b.x + 6, b.y + 6, 6, 6), 1.5f, color);
    line(c, b, 9, 1, 9, 6, color); line(c, b, 9, 12, 9, 17, color);
    line(c, b, 1, 9, 6, 9, color); line(c, b, 12, 9, 17, 9, color);
}

static void hide_fade(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_circle(c, nk_rect(b.x + 1, b.y + 3, 12, 12), 1.5f, color);
    line(c, b, 7, 6, 7, 9, color);
    line(c, b, 7, 9, 9, 10, color);
    struct nk_color trail = color;
    trail.a = (nk_byte)((unsigned int)color.a * 3 / 5);
    line(c, b, 15, 5, 15, 13, trail);
    trail.a = (nk_byte)((unsigned int)color.a * 3 / 10);
    line(c, b, 18, 7, 18, 11, trail);
}

static void gamepad_four_hands(struct nk_command_buffer *c, struct nk_rect b,
    struct nk_color color) {
    nk_stroke_rect(c, nk_rect(b.x + 2, b.y + 5, 14, 9), 3, 1.5f, color);
    line(c, b, 4, 9, 8, 9, color); line(c, b, 6, 7, 6, 11, color);
    dot(c, b, 12, 8, 2, color); dot(c, b, 14, 11, 2, color);
    dot(c, b, 3, 2, 3, color); dot(c, b, 15, 2, 3, color);
    dot(c, b, 3, 17, 3, color); dot(c, b, 15, 17, 3, color);
}

bool bongo_cat_pref_row_icon_draw(struct nk_command_buffer *canvas,
    struct nk_rect bounds, BongoCatPrefIcon icon, struct nk_color color) {
    typedef void (*Draw)(struct nk_command_buffer *, struct nk_rect,
        struct nk_color);
    static const Draw draws[] = {multiple_models, pass_through, always_on_top,
        keep_in_screen, solid_background, window_size, window_corners, opacity,
        random_expression, random_motion, mirror, mouse_mirror, mouse_centered,
        ignore_mouse, texture_resolution,
        max_fps, autostart, administrator, language, theme, shortcut_visibility,
        shortcut_preferences, shortcut_menu, hide_fade, gamepad_four_hands};
    int index = icon - BONGO_CAT_PREF_ICON_MULTIPLE_MODELS;
    int count = (int)(sizeof(draws) / sizeof(draws[0]));
    if (index < 0 || index >= count) return false;
    draws[index](canvas, bounds, color);
    return true;
}
