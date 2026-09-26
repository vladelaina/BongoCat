#include "preferences_model_card.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"
#include "bongo_cat/i18n.h"

#include <stdio.h>

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

/* Segmented view switcher shown while models are hidden: mirrors the
   motion/expression segment style of the behavior dialog. Lives on the
   section row above the grid so it never overlaps the import card. */
static void draw_hidden_segments(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, BongoCatUIPalette p) {
    if (!value->app->settings.hidden_model_count) return;
    struct nk_rect wrapper = nk_rect(bounds.x,
        bounds.y + (bounds.h - 36.0f) * .5f, 220.0f, 36.0f);
    nk_fill_rect(canvas, wrapper, 10, p.field);
    const char *labels[] = {tr(value, "pages.preference.model.title",
        "Model"), tr(value, "pages.preference.model.labels.hiddenModels",
        "Hidden")};
    float width = (wrapper.w - 6.0f) * .5f;
    for (int i = 0; i < 2; ++i) {
        struct nk_rect button = nk_rect(wrapper.x + 3.0f + width * i,
            wrapper.y + 3.0f, width, wrapper.h - 6.0f);
        bool hover = nk_input_is_mouse_hovering_rect(&context->input, button);
        char id[48];
        snprintf(id, sizeof(id), "model-view-segment-%d", i);
        float active = bongo_cat_ui_animate_eased(context, id,
            (value->model_show_hidden ? 1 : 0) == i ? 1.0f : 0.0f, 200.0f,
            BONGO_CAT_UI_EASE_STANDARD);
        struct nk_color background = bongo_cat_ui_color_mix(
            hover ? p.hover : p.field, p.accent, active);
        if (active > .01f && p.effects) bongo_cat_ui_paint_shadow(context,
            button, 8, 0, 2, 10, 0, nk_rgba(p.accent.r, p.accent.g,
            p.accent.b, (nk_byte)(89.0f * active)));
        nk_fill_rect(canvas, button, 8, background);
        float text_width = value->ui.caption_font->width(
            value->ui.caption_font->userdata, value->ui.caption_font->height,
            labels[i], nk_strlen(labels[i]));
        nk_draw_text(canvas, nk_rect(button.x + (button.w - text_width) * .5f,
            button.y + (button.h - value->ui.caption_font->height) * .5f,
            text_width + 1.0f, value->ui.caption_font->height), labels[i],
            nk_strlen(labels[i]), value->ui.caption_font,
            nk_rgba(0, 0, 0, 0), bongo_cat_ui_color_mix(p.muted,
            nk_rgb(255, 255, 255), active));
        if (hover) bongo_cat_ui_cursor_hover_rect(context, button,
            BONGO_CAT_UI_CURSOR_POINTER);
        if (hover && nk_input_is_mouse_click_in_rect(&context->input,
                NK_BUTTON_LEFT, button) &&
            value->model_show_hidden != (i == 1)) {
            value->model_show_hidden = i == 1;
            value->render_dirty = true;
        }
    }
}

bool bongo_cat_preferences_model_section(BongoCatPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    const struct nk_user_font *action_font = bongo_cat_ui_caption_font(context);
    const char *label = tr(value,
        "pages.preference.model.tooltips.moreModels", "More Models");
    float label_width = action_font->width(action_font->userdata,
        action_font->height, label, nk_strlen(label));
    float action_width = label_width + 22;
    float action_x = bounds.x + bounds.w - action_width;
    struct nk_rect action = nk_rect(action_x,
        bounds.y, action_width, bounds.h);
    bool action_hover = nk_input_is_mouse_hovering_rect(&context->input, action);
    float hover_amount = bongo_cat_ui_animate_eased(context,
        "model-more-link-hover", action_hover ? 1.0f : 0.0f, 180.0f,
        BONGO_CAT_UI_EASE_STANDARD);
    struct nk_color action_color = p.danger;
    nk_draw_text(canvas, nk_rect(action.x, action.y +
        (action.h - action_font->height) * .5f, label_width + 1,
        action_font->height), label, nk_strlen(label), action_font,
        nk_rgba(0, 0, 0, 0), action_color);
    float arrow_y = action.y + action.h * .5f;
    float arrow_offset = 2.0f * hover_amount;
    float first_arrow_x = action.x + label_width + 11 + arrow_offset;
    float second_arrow_x = action.x + label_width + 17 + arrow_offset;
    struct nk_color first_arrow_color = bongo_cat_ui_color_alpha(
        action_color, 0.5f);
    nk_stroke_line(canvas, first_arrow_x - 4, arrow_y - 4,
        first_arrow_x, arrow_y, 1.5f, first_arrow_color);
    nk_stroke_line(canvas, first_arrow_x, arrow_y,
        first_arrow_x - 4, arrow_y + 4, 1.5f, first_arrow_color);
    nk_stroke_line(canvas, second_arrow_x - 4, arrow_y - 4,
        second_arrow_x, arrow_y, 1.5f, action_color);
    nk_stroke_line(canvas, second_arrow_x, arrow_y,
        second_arrow_x - 4, arrow_y + 4, 1.5f, action_color);
    if (action_hover) bongo_cat_ui_cursor_hover_rect(context, action,
        BONGO_CAT_UI_CURSOR_POINTER);
    draw_hidden_segments(value, context, canvas, bounds, p);
    return action_hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, action);
}
