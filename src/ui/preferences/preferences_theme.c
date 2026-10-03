#include "preferences_theme.h"
#include "bongo_cat/config.h"
#include "preferences_widgets_internal.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"

#include <math.h>
#include <stdio.h>

typedef struct ThemeFormStyle {
    struct nk_style_item background;
    struct nk_color window_color;
    struct nk_color border_color;
    struct nk_vec2 padding;
    struct nk_vec2 spacing;
    float border;
} ThemeFormStyle;

static bool form_begin(struct nk_context *context, const char *id,
    ThemeFormStyle *saved) {
    saved->background = context->style.window.fixed_background;
    saved->window_color = context->style.window.background;
    saved->border_color = context->style.window.group_border_color;
    saved->padding = context->style.window.group_padding;
    saved->spacing = context->style.window.spacing;
    saved->border = context->style.window.group_border;
    struct nk_color clear = nk_rgba(0, 0, 0, 0);
    context->style.window.fixed_background = nk_style_item_color(clear);
    context->style.window.background = clear;
    context->style.window.group_border_color = clear;
    context->style.window.group_padding = nk_vec2(13, 11);
    context->style.window.spacing = nk_vec2(8, 3);
    context->style.window.group_border = 0;
    nk_layout_row_dynamic(context, 76, 1);
    if (nk_group_begin(context, id, NK_WINDOW_NO_SCROLLBAR)) return true;
    context->style.window.fixed_background = saved->background;
    context->style.window.background = saved->window_color;
    context->style.window.group_border_color = saved->border_color;
    context->style.window.group_padding = saved->padding;
    context->style.window.spacing = saved->spacing;
    context->style.window.group_border = saved->border;
    bongo_cat_pref_row_icon_clear(context);
    return false;
}

static void form_end(struct nk_context *context, const ThemeFormStyle *saved) {
    bongo_cat_pref_row_icon_clear(context);
    nk_group_end(context);
    context->style.window.fixed_background = saved->background;
    context->style.window.background = saved->window_color;
    context->style.window.group_border_color = saved->border_color;
    context->style.window.group_padding = saved->padding;
    context->style.window.spacing = saved->spacing;
    context->style.window.group_border = saved->border;
}

static void draw_computer(struct nk_command_buffer *canvas,
    struct nk_rect bounds, struct nk_color color) {
    float x = bounds.x + bounds.w * .5f, y = bounds.y + bounds.h * .5f;
    nk_stroke_rect(canvas, nk_rect(x - 8, y - 7, 16, 11), 2, 1.7f, color);
    nk_stroke_line(canvas, x, y + 4, x, y + 7, 1.7f, color);
    nk_stroke_line(canvas, x - 5, y + 7, x + 5, y + 7, 1.7f, color);
}

static void draw_sun(struct nk_command_buffer *canvas,
    struct nk_rect bounds, struct nk_color color) {
    float x = bounds.x + bounds.w * .5f, y = bounds.y + bounds.h * .5f;
    nk_stroke_circle(canvas, nk_rect(x - 4, y - 4, 8, 8), 1.7f, color);
    const float rays[][4] = {
        {0, -9, 0, -7}, {0, 7, 0, 9}, {-9, 0, -7, 0}, {7, 0, 9, 0},
        {-6.4f, -6.4f, -5, -5}, {5, 5, 6.4f, 6.4f},
        {-6.4f, 6.4f, -5, 5}, {5, -5, 6.4f, -6.4f}
    };
    for (size_t i = 0; i < sizeof(rays) / sizeof(rays[0]); ++i)
        nk_stroke_line(canvas, x + rays[i][0], y + rays[i][1],
            x + rays[i][2], y + rays[i][3], 1.7f, color);
}

static void draw_moon(struct nk_command_buffer *canvas,
    struct nk_rect bounds, struct nk_color color,
    struct nk_color background) {
    float x = bounds.x + bounds.w * .5f;
    float y = bounds.y + bounds.h * .5f;
    nk_fill_circle(canvas, nk_rect(x - 7.5f, y - 7.5f, 15, 15), color);
    nk_fill_circle(canvas, nk_rect(x - 3.4f, y - 8.3f, 12.4f, 12.4f),
        background);
}

static void draw_icon(struct nk_command_buffer *canvas, int icon,
    struct nk_rect bounds, struct nk_color color,
    struct nk_color background) {
    if (icon == 0) draw_computer(canvas, bounds, color);
    else if (icon == 1) draw_sun(canvas, bounds, color);
    else draw_moon(canvas, bounds, color, background);
}

static int capsule(struct nk_context *context, const char *id,
    const char *const *labels, int count, bool icons, int selected,
    bool *clicked) {
    struct nk_rect widget;
    if (nk_widget(&widget, context) == NK_WIDGET_INVALID) return selected;
    selected = NK_CLAMP(0, selected, count - 1);
    float width = count == 1 ? widget.w : 156.0f;
    if (!icons && count > 1) {
        const struct nk_user_font *font = context->style.font;
        for (int i = 0; i < count; ++i)
            width = NK_MAX(width, count * (24.0f + font->width(
                font->userdata, font->height, labels[i], nk_strlen(labels[i]))));
    }
    struct nk_rect bounds = nk_rect(widget.x + NK_MAX(0.0f,
        widget.w - width - 6.0f), widget.y - 1, NK_MIN(width, widget.w), 38);
    float segment = bounds.w / count;
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    int hovered = hover ? NK_CLAMP(0, (int)((context->input.mouse.pos.x -
        bounds.x) / segment), count - 1) : -1;
    if (hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds)) {
        selected = hovered;
        if (clicked) *clicked = true;
    }

    char animation_id[96];
    snprintf(animation_id, sizeof(animation_id), "theme-selection-%s", id);
    float position = bongo_cat_ui_animate_eased(context, animation_id,
        (float)selected, 220, BONGO_CAT_UI_EASE_SWIFT);
    snprintf(animation_id, sizeof(animation_id), "theme-hover-%s", id);
    float outer_hover = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 180, BONGO_CAT_UI_EASE_STANDARD);
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 14, p.field);
    for (int i = 0; i < count; ++i) {
        snprintf(animation_id, sizeof(animation_id), "theme-hover-%s-%d", id, i);
        float amount = bongo_cat_ui_animate_eased(context, animation_id,
            hovered == i ? 1.0f : 0.0f, 160, BONGO_CAT_UI_EASE_STANDARD);
        if (amount > .001f) nk_fill_rect(canvas,
            nk_rect(bounds.x + 3 + segment * i, bounds.y + 3,
                segment - 6, bounds.h - 6), 11,
            bongo_cat_ui_color_mix(p.field, p.selection, amount));
    }
    struct nk_rect active = nk_rect(bounds.x + 3 + segment * position,
        bounds.y + 3, segment - 6, bounds.h - 6);
    if (p.effects) bongo_cat_ui_paint_shadow(context, active, 11,
        0, 2, 7, 0, nk_rgba(p.accent.r, p.accent.g, p.accent.b, 76));
    nk_fill_rect(canvas, active, 11, p.accent);
    nk_stroke_rect(canvas, bounds, 14, 1,
        bongo_cat_ui_color_mix(p.border_subtle, p.accent, outer_hover));
    for (int i = 0; i < count; ++i) {
        snprintf(animation_id, sizeof(animation_id), "theme-hover-%s-%d", id, i);
        float amount = bongo_cat_ui_animate_eased(context, animation_id,
            hovered == i ? 1.0f : 0.0f, 160, BONGO_CAT_UI_EASE_STANDARD);
        float distance = fabsf(position - i);
        float active_weight = 1.0f - NK_CLAMP(0.0f,
            (distance - .28f) / .20f, 1.0f);
        struct nk_color base = bongo_cat_ui_color_mix(p.muted, p.accent, amount);
        struct nk_color color = bongo_cat_ui_color_mix(base,
            nk_rgb(255, 255, 255), active_weight);
        struct nk_color segment_background = bongo_cat_ui_color_mix(
            p.field, p.selection, amount);
        segment_background = bongo_cat_ui_color_mix(segment_background,
            p.accent, active_weight);
        float size = 20.0f + amount;
        struct nk_rect icon = nk_rect(bounds.x + segment * (i + .5f) - size * .5f,
            bounds.y + (bounds.h - size) * .5f, size, size);
        if (icons) draw_icon(canvas, i, icon, color, segment_background);
        else {
            const struct nk_user_font *font = context->style.font;
            int length = nk_strlen(labels[i]);
            float text_width = font->width(font->userdata, font->height,
                labels[i], length);
            nk_draw_text(canvas, nk_rect(bounds.x + segment * (i + .5f) -
                text_width * .5f, bounds.y + (bounds.h - font->height) * .5f,
                text_width, font->height), labels[i], length, font,
                nk_rgba(0, 0, 0, 0), color);
        }
    }
    if (hover) {
        bongo_cat_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_UI_CURSOR_POINTER);
    }
    return selected;
}

static int choice_row(struct nk_context *context, const char *id,
    const char *title, const char *const *labels, int count, bool icons,
    int selected, bool *clicked) {
    ThemeFormStyle saved;
    if (!form_begin(context, id, &saved)) return selected;
    float available = nk_window_get_content_region(context).w;
    float choice_width = 176.0f;
    if (!icons && count > 1) {
        const struct nk_user_font *font = context->style.font;
        for (int i = 0; i < count; ++i)
            choice_width = NK_MAX(choice_width, 12.0f + count * (24.0f +
                font->width(font->userdata, font->height, labels[i], nk_strlen(labels[i]))));
    }
    float left = NK_MAX(0.0f, available - choice_width - 8.0f);
    nk_layout_row_begin(context, NK_STATIC, 36, 2);
    nk_layout_row_push(context, left);
    bongo_cat_pref_form_label(context, title);
    nk_layout_row_push(context, NK_MAX(choice_width, available - left - 8.0f));
    selected = capsule(context, id, labels, count, icons, selected, clicked);
    nk_layout_row_end(context);
    form_end(context, &saved);
    return selected;
}

int bongo_cat_pref_theme(struct nk_context *context, const char *id,
    const char *title, const char *const *labels, int selected) {
    return choice_row(context, id, title, labels, 3, true, selected, NULL);
}

int bongo_cat_pref_fps(struct nk_context *context, const char *id,
    const char *title, int fps, int display_fps) {
    char display_label[32];
    snprintf(display_label, sizeof(display_label), "%d FPS", display_fps);
    const char *labels[] = {"30 FPS", "60 FPS", display_label};
    bool high_refresh = display_fps > BONGO_CAT_DEFAULT_MAX_FPS;
    int selected = fps == 30 ? 0 :
        (fps == BONGO_CAT_DISPLAY_MAX_FPS && high_refresh ? 2 : 1);
    bool clicked = false;
    int next = choice_row(context, id, title, labels, high_refresh ? 3 : 2,
        false, selected, &clicked);
    /* Keep the saved display choice when temporarily falling back to 60 FPS. */
    if (!clicked) return fps;
    return next == 0 ? 30 : next == 1 ? 60 : BONGO_CAT_DISPLAY_MAX_FPS;
}

bool bongo_cat_pref_capsule_button(struct nk_context *context,
    const char *id, const char *label) {
    const char *labels[] = {label};
    bool clicked = false;
    capsule(context, id, labels, 1, false, 0, &clicked);
    return clicked;
}
