#include "preferences_widgets.h"
#include "preferences_widgets_internal.h"
#include "preferences_controls.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_icons.h"
#include <stdio.h>

/* Measure and draw with the same UTF-8 wrapping, omitting highlight markers. */
int bongo_cat_pref_detail_text(struct nk_context *context, const char *text,
    struct nk_rect bounds, bool draw) {
    if (!text || !text[0]) return 0;
    const struct nk_user_font *font = bongo_cat_ui_caption_font(context);
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    float x = 0.0f;
    int line = 0, remaining = nk_strlen(text);
    bool highlighted = false;
    while (remaining > 0) {
        if (*text == '[' || *text == ']') {
            highlighted = *text == '[';
            ++text;
            --remaining;
            continue;
        }
        nk_rune rune;
        int length = nk_utf_decode(text, &rune, remaining);
        if (length <= 0) length = 1;
        float width = font->width(font->userdata, font->height, text, length);
        if (x > 0 && x + width > bounds.w) {
            x = 0;
            ++line;
        }
        if (draw)
            nk_draw_text(nk_window_get_canvas(context),
                nk_rect(bounds.x + x, bounds.y + line * 19.0f,
                    width + 1.0f, font->height), text, length, font,
                nk_rgba(0, 0, 0, 0),
                highlighted ? nk_rgb(247, 125, 170) : p.muted);
        x += width;
        text += length;
        remaining -= length;
    }
    return line + 1;
}

bool bongo_cat_pref_toggle_float(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value) {
    return bongo_cat_pref_toggle_float_detail(context, id, title, unit, enabled,
        minimum, value, maximum, step, default_value, NULL, true);
}

/* Small rounded icon button placed left of the numeric field; shown only
   while the row is enabled, matching the field's visibility. */
static bool config_button(struct nk_context *context) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    nk_fill_rect(canvas, bounds, 10, hover ? p.hover : p.field);
    nk_stroke_rect(canvas, bounds, 10, 1, hover ? p.accent : p.border_subtle);
    struct nk_rect icon = nk_rect(bounds.x + (bounds.w - 16.0f) * .5f,
        bounds.y + (bounds.h - 16.0f) * .5f, 16.0f, 16.0f);
    bongo_cat_ui_draw_icon(canvas, BONGO_CAT_UI_ICON_SETTINGS, icon,
        hover ? p.accent : p.muted);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    return hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

static bool toggle_float_row(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value, const char *detail,
    bool available, bool configure, bool *config_clicked) {
    float detail_width = NK_MAX(1.0f,
        nk_window_get_content_region(context).w - 26.0f - 33.0f);
    int lines = bongo_cat_pref_detail_text(context, detail, nk_rect(0, 0, detail_width, 0), false);
    FormStyle saved;
    if (!bongo_cat_pref_form_begin(context, id, lines, &saved)) return false;
    float content_width = nk_window_get_content_region(context).w;
    bool configured = configure && *enabled;
    int columns = *enabled ? (configured ? 4 : 3) : 2;
    float input_width = *enabled ? 122.0f : 0.0f;
    float config_width = configured ? 36.0f : 0.0f;
    float spacing = 8.0f * (columns - 1);
    float left = NK_MAX(220.0f,
        content_width - config_width - input_width - 80.0f - spacing);
    nk_layout_row_begin(context, NK_STATIC, 36, columns);
    nk_layout_row_push(context, left);
    char label[512];
    if (unit && unit[0]) {
        snprintf(label, sizeof(label), "%s (%s)", title, unit);
        title = label;
    }
    bongo_cat_pref_form_label(context, title);
    bool changed = false;
    if (configured) {
        nk_layout_row_push(context, config_width);
        if (config_button(context) && config_clicked) *config_clicked = true;
    }
    if (*enabled) {
        nk_layout_row_push(context, input_width);
        changed = bongo_cat_pref_control_float(context, id,
            minimum, value, maximum, step, default_value);
    }
    nk_layout_row_push(context, NK_MAX(80.0f,
        content_width - left - config_width - input_width - spacing));
    changed = bongo_cat_pref_control_toggle_available(context, id, enabled,
        available) || changed;
    nk_layout_row_end(context);
    if (lines) {
        nk_layout_row_dynamic(context, 19.0f * lines, 1);
        struct nk_rect bounds;
        if (nk_widget(&bounds, context) != NK_WIDGET_INVALID) {
            bounds.x += 33.0f;
            bounds.w = detail_width;
            bongo_cat_pref_detail_text(context, detail, bounds, true);
        }
    }
    bongo_cat_pref_form_end(context, &saved);
    return changed;
}

bool bongo_cat_pref_toggle_float_detail(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value, const char *detail,
    bool available) {
    return toggle_float_row(context, id, title, unit, enabled, minimum, value,
        maximum, step, default_value, detail, available, false, NULL);
}

bool bongo_cat_pref_toggle_float_config(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value) {
    bool config_clicked = false;
    toggle_float_row(context, id, title, unit, enabled, minimum, value,
        maximum, step, default_value, NULL, true, true, &config_clicked);
    return config_clicked;
}
