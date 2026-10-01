#include "preferences_widgets.h"
#include "preferences_widgets_internal.h"
#include "preferences_controls.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"
#include "ui_tooltip.h"
static struct nk_context *section_context;
static bool section_first;
static const float description_line_height = 19.0f;
static bool has_line_break(const char *text) {
    if (!text) return false;
    while (*text) {
        if (*text++ == '\n') return true;
    }
    return false;
}
static int wrapped_line_count(const struct nk_user_font *font,
    const char *text, int length, float width) {
    if (length <= 0) return 1;
    float measured = font->width(font->userdata, font->height, text, length);
    return (int)(measured / width) + 1;
}
int bongo_cat_pref_detail_lines(const struct nk_context *context,
    const char *text) {
    if (!text || !text[0]) return 0;
    const struct nk_user_font *font = bongo_cat_ui_caption_font(context);
    float width = nk_window_get_content_region(context).w -
        (has_line_break(text) ? 30.0f : 310.0f);
    if (width < 220.0f) width = 220.0f;
    int lines = 0;
    const char *segment = text;
    for (;;) {
        const char *end = segment;
        while (*end && *end != '\n') ++end;
        lines += wrapped_line_count(font, segment, (int)(end - segment), width);
        if (!*end) break;
        segment = end + 1;
    }
    return NK_CLAMP(1, lines, 3);
}
static void restore_style(struct nk_context *context, const FormStyle *saved) {
    context->style.window.fixed_background = saved->background;
    context->style.window.background = saved->window_color;
    context->style.window.group_border_color = saved->border_color;
    context->style.window.group_padding = saved->padding;
    context->style.window.spacing = saved->spacing;
    context->style.window.group_border = saved->border;
}
bool bongo_cat_pref_form_begin(struct nk_context *context, const char *id,
    int lines, FormStyle *saved) {
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
    float height = lines ? 70.0f + description_line_height * lines : 76.0f;
    if (section_context == context && section_first && !lines) height = 69.0f;
    if (section_context == context) section_first = false;
    nk_layout_row_dynamic(context, height, 1);
    if (!nk_group_begin(context, id, NK_WINDOW_NO_SCROLLBAR)) {
        bongo_cat_pref_row_icon_clear(context);
        restore_style(context, saved);
        return false;
    }
    return true;
}
void bongo_cat_pref_form_end(struct nk_context *context,
    const FormStyle *saved) {
    bongo_cat_pref_row_icon_clear(context);
    nk_group_end(context); restore_style(context, saved); }
#define form_begin bongo_cat_pref_form_begin
#define form_end bongo_cat_pref_form_end
static float left_width(struct nk_context *context) {
    return NK_MAX(220.0f, nk_window_get_content_region(context).w - 228.0f);
}
static void form_title(struct nk_context *context, const char *title) {
    bongo_cat_pref_form_title_sized(context, title, 220.0f); }
void bongo_cat_pref_description(struct nk_context *context, const char *text,
    int lines) {
    if (!lines) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    float available = nk_window_get_content_region(context).w;
    bool multiline = has_line_break(text);
    float left = multiline ? available : left_width(context);
    float wrap_width = available - (multiline ? 10.0f : 310.0f);
    if (wrap_width < 220.0f) wrap_width = 220.0f;
    const struct nk_user_font *font = bongo_cat_ui_caption_font(context);
    nk_style_push_font(context, bongo_cat_ui_caption_font(context));
    struct nk_vec2 text_padding = context->style.text.padding;
    float row_spacing = context->style.window.spacing.y;
    context->style.text.padding.x += 5.0f;
    context->style.window.spacing.y = 0;
    const char *segment = text;
    int remaining = lines;
    while (remaining > 0) {
        const char *end = segment;
        while (*end && *end != '\n') ++end;
        int segment_lines = wrapped_line_count(font, segment,
            (int)(end - segment), wrap_width);
        segment_lines = NK_MIN(segment_lines, remaining);
        nk_layout_row_begin(context, NK_STATIC,
            description_line_height * segment_lines, multiline ? 1 : 2);
        nk_layout_row_push(context, left);
        nk_text_wrap_colored(context, segment, (int)(end - segment), p.muted);
        if (!multiline) {
            nk_layout_row_push(context, NK_MAX(1.0f, available - left - 8.0f));
            nk_spacing(context, 1);
        }
        nk_layout_row_end(context);
        remaining -= segment_lines;
        if (!*end) break;
        segment = end + 1;
    }
    context->style.window.spacing.y = row_spacing;
    context->style.text.padding = text_padding;
    nk_style_pop_font(context);
}
static bool secondary_button(struct nk_context *context, const char *label) {
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_style_button style = context->style.button;
    style.normal = nk_style_item_color(p.field);
    style.hover = nk_style_item_color(p.selection);
    style.active = nk_style_item_color(p.selection);
    style.border_color = p.border_subtle;
    style.border = 1;
    style.rounding = 10;
    style.text_normal = p.text;
    style.text_hover = p.accent;
    style.text_active = p.accent;
    struct nk_rect bounds = nk_widget_bounds(context);
    if (nk_input_is_mouse_hovering_rect(&context->input, bounds))
        bongo_cat_ui_cursor_hover_rect(context, bounds, BONGO_CAT_UI_CURSOR_POINTER);
    return nk_button_label_styled(context, &style, label) != 0;
}
void bongo_cat_pref_section(struct nk_context *context, const char *title) {
    section_context = context; section_first = true;
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 2, 4, 18), 2, p.pink);
    const struct nk_user_font *font = bongo_cat_ui_label_font(context);
    struct nk_rect text = nk_rect(bounds.x + 14,
        bounds.y + (bounds.h - font->height) * .5f, bounds.w - 14, font->height);
    nk_draw_text(canvas, text, title, nk_strlen(title), font,
        nk_rgba(0, 0, 0, 0), p.text);
}
void bongo_cat_pref_section_icon(struct nk_context *context,
    const char *title, BongoCatPrefIcon icon) {
    section_context = context; section_first = true;
    const struct nk_user_font *font = bongo_cat_ui_label_font(context);
    float width = nk_window_get_content_region(context).w - 42;
    float measured = font->width(font->userdata, font->height,
        title, nk_strlen(title));
    if (width > 0 && measured > width) {
        /* Leave room for word wrapping in longer translations. */
        float height = (float)((int)(measured / width) + 2) *
            NK_MAX(22.0f, font->height + 2.0f);
        struct nk_vec2 spacing = context->style.window.spacing;
        struct nk_vec2 padding = context->style.text.padding;
        context->style.window.spacing.x = 0;
        context->style.text.padding = nk_vec2(0, 0);
        nk_style_push_font(context, font);
        nk_layout_row_begin(context, NK_STATIC, height, 2);
        nk_layout_row_push(context, 42);
        struct nk_rect bounds;
        if (nk_widget(&bounds, context) != NK_WIDGET_INVALID) {
            BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
            struct nk_command_buffer *canvas = nk_window_get_canvas(context);
            nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 2, 4, 18), 2, p.pink);
            bongo_cat_pref_icon_draw(canvas,
                nk_rect(bounds.x + 14, bounds.y + 2, 18, 18), icon, p.accent);
        }
        nk_layout_row_push(context, width);
        nk_text_wrap_colored(context, title, nk_strlen(title),
            bongo_cat_ui_palette(bongo_cat_ui_dark(context)).accent);
        nk_layout_row_end(context);
        nk_style_pop_font(context);
        context->style.window.spacing = spacing;
        context->style.text.padding = padding;
        return;
    }
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 2, 4, 18), 2, p.pink);
    struct nk_rect icon_bounds = nk_rect(bounds.x + 14,
        bounds.y + 2, 18, 18);
    bongo_cat_pref_icon_draw(canvas, icon_bounds, icon, p.accent);
    struct nk_rect text = nk_rect(bounds.x + 42,
        bounds.y + (bounds.h - font->height) * .5f,
        bounds.w - 42, font->height);
    nk_draw_text(canvas, text, title, nk_strlen(title), font,
        nk_rgba(0, 0, 0, 0), p.accent);
}
bool bongo_cat_pref_toggle(struct nk_context *context, const char *id,
    const char *title, const char *detail, bool *value) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    bongo_cat_pref_form_title_sized(context, title, 80.0f);
    bool changed = bongo_cat_pref_control_toggle(context, id, value);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return changed;
}
bool bongo_cat_pref_obs_background(struct nk_context *context, const char *id,
    const char *title, const char *question, const char *reply, bool *enabled,
    BongoCatObsBackgroundColor *color) {
    int lines = question && question[0] ? 1 : 0; FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    bongo_cat_pref_form_title_sized(context, title, 230.0f);
    bool changed = bongo_cat_pref_control_obs_background(context, id, enabled, color);
    nk_layout_row_end(context);
    bongo_cat_ui_question_tooltip(context, question, reply);
    form_end(context, &saved); return changed;
}
bool bongo_cat_pref_float(struct nk_context *context, const char *id,
    const char *title, const char *detail, float minimum, float *value,
    float maximum, float step, float default_value) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    form_title(context, title);
    bool changed = bongo_cat_pref_control_float(context, id,
        minimum, value, maximum, step, default_value);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return changed;
}
bool bongo_cat_pref_int(struct nk_context *context, const char *id,
    const char *title, const char *detail, int minimum, int *value,
    int maximum, int step, int default_value) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    form_title(context, title);
    bool changed = bongo_cat_pref_control_int(context, id,
        minimum, value, maximum, step, default_value);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return changed;
}
bool bongo_cat_pref_slider(struct nk_context *context, const char *id,
    const char *title, const char *detail, float minimum, float *value,
    float maximum, float step, float default_value) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    form_title(context, title);
    bool changed = bongo_cat_pref_control_slider(context, id,
        minimum, value, maximum, step, default_value);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return changed;
}
int bongo_cat_pref_combo(struct nk_context *context, const char *id,
    const char *title, const char *detail, const char *const *items,
    int count, int selected) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return selected;
    bongo_cat_pref_form_title_sized(context, title, 176.0f);
    selected = bongo_cat_pref_control_combo(context, id,
        items, count, selected);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return selected;
}
bool bongo_cat_pref_button(struct nk_context *context, const char *id,
    const char *title, const char *detail, const char *button) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return false;
    form_title(context, title); bool result = secondary_button(context, button);
    nk_layout_row_end(context); bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved); return result;
}
void bongo_cat_pref_status(struct nk_context *context, const char *id,
    const char *title, const char *detail) {
    int lines = bongo_cat_pref_detail_lines(context, detail); FormStyle saved;
    if (!form_begin(context, id, lines, &saved)) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    nk_layout_row_dynamic(context, 44, 1);
    struct nk_rect row; nk_widget(&row, context);
    nk_fill_circle(nk_window_get_canvas(context),
        nk_rect(row.x, row.y + (row.h - 10) * .5f, 10, 10), p.accent);
    const struct nk_user_font *font = bongo_cat_ui_label_font(context);
    struct nk_rect text = nk_rect(row.x + 20,
        row.y + (row.h - font->height) * .5f, row.w - 20, font->height);
    nk_draw_text(nk_window_get_canvas(context), text, title, nk_strlen(title),
        font, nk_rgba(0, 0, 0, 0), p.text);
    bongo_cat_pref_description(context, detail, lines);
    form_end(context, &saved);
}
