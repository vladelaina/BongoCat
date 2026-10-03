#include "preferences_widgets.h"
#include "bongo_cat/shortcut.h"
#include "preferences_widgets_internal.h"
#include "preferences_shortcut_clear.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

int bongo_cat_pref_edit(struct nk_context *context, const char *id,
    const char *title, const char *detail, const char *value,
    bool recording, const char *idle_hint, const char *record_hint) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!bongo_cat_pref_form_begin(context, id, lines, &saved)) return false;
    char shortcut_label[BONGO_CAT_SHORTCUT_CAP * 2];
    bongo_cat_shortcut_format(value, shortcut_label, sizeof(shortcut_label));
    const char *shown = recording ? record_hint :
        (value && value[0] ? shortcut_label : idle_hint);
    const struct nk_user_font *font = bongo_cat_ui_body_font(context);
    float width = font->width(font->userdata, font->height, shown,
        nk_strlen(shown));
    float control_width = NK_CLAMP(180.0f, width + 64.0f, 260.0f);
    bongo_cat_pref_form_title_sized(context, title, control_width);
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) {
        nk_layout_row_end(context);
        bongo_cat_pref_form_end(context, &saved);
        return false;
    }
    const float effect_margin = 6.0f;
    bounds = nk_rect(bounds.x + bounds.w - control_width, bounds.y,
        control_width - effect_margin, bounds.h);
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char hover_id[80];
    snprintf(hover_id, sizeof(hover_id), "shortcut-hover-%s", id);
    float hover_amount = bongo_cat_ui_animate_eased(context, hover_id,
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 10, recording ? p.hover_pink :
        bongo_cat_ui_color_mix(p.field, p.hover, hover_amount));
    nk_stroke_rect(canvas, bounds, 10, recording ? 2.0f : 1.0f,
        recording ? p.pink : bongo_cat_ui_color_mix(
        p.border_subtle, p.accent, hover_amount));
    if (recording) {
        float phase = (float)(SDL_GetTicksNS() % 1500000000ULL) /
            1500000000.0f;
        float pulse = .5f - .5f * cosf(phase * 6.2831853f);
        struct nk_rect glow = nk_rect(bounds.x - pulse * 5,
            bounds.y - pulse * 5, bounds.w + pulse * 10,
            bounds.h + pulse * 10);
        nk_stroke_rect(canvas, glow, 12, 2,
            nk_rgba(p.pink.r, p.pink.g, p.pink.b,
            (nk_byte)(150 * (1.0f - pulse))));
    }
    bool has_shortcut = value && value[0];
    bool can_clear = !recording && has_shortcut;
    bool show_keyboard = recording || !has_shortcut;
    float icon_width = show_keyboard ? 26.0f : 0.0f;
    float text_width = NK_MIN(width, bounds.w - icon_width -
        (can_clear ? 34.0f : 16.0f));
    float group_width = icon_width + text_width + (can_clear ? 20.0f : 0.0f);
    float group_x = bounds.x + NK_MAX(14.0f, (bounds.w - group_width) * .5f);
    float text_x = group_x + icon_width;
    struct nk_color icon_color = recording ? p.pink :
        bongo_cat_ui_color_mix(p.muted, p.accent, hover_amount);
    if (show_keyboard) {
        struct nk_rect keyboard = nk_rect(group_x, bounds.y + 10, 18, 18);
        if (!bongo_cat_ui_draw_icon(canvas, BONGO_CAT_UI_ICON_KEYBOARD,
            keyboard, icon_color)) {
            nk_stroke_rect(canvas, keyboard, 3, 1.5f, icon_color);
            for (int i = 0; i < 3; ++i)
                nk_stroke_line(canvas, keyboard.x + 3 + i * 3,
                    keyboard.y + 4, keyboard.x + 4 + i * 3,
                    keyboard.y + 4, 1, icon_color);
            nk_stroke_line(canvas, keyboard.x + 3, keyboard.y + 8,
                keyboard.x + 10, keyboard.y + 8, 1, icon_color);
        }
    }
    struct nk_rect text = nk_rect(text_x,
        bounds.y + (bounds.h - font->height) * .5f,
        NK_MIN(width + 1, bounds.x + bounds.w - text_x -
        (can_clear ? 24.0f : 8.0f)), font->height);
    nk_draw_text(canvas, text, shown, nk_strlen(shown), font,
        nk_rgba(0, 0, 0, 0), recording ? p.pink :
        (has_shortcut ? p.text :
        bongo_cat_ui_color_mix(p.muted, p.accent, hover_amount)));
    struct nk_rect clear_bounds = nk_rect(bounds.x + bounds.w - 25,
        bounds.y + 8, 17, 20);
    bool clear = bongo_cat_pref_shortcut_clear(context, canvas, id,
        clear_bounds, p, 1.0f, can_clear);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    bool clicked = !clear && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds) != 0;
    nk_layout_row_end(context);
    bongo_cat_pref_description(context, detail, lines);
    bongo_cat_pref_form_end(context, &saved);
    return clear ? -1 : clicked;
}
