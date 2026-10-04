#include "preferences_controls.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"
#include "bongo_cat/config.h"

#include <stdio.h>

static bool toggle_at(struct nk_context *context, const char *id,
    bool *value, struct nk_rect cell, bool available) {
    const float effect_margin = 18.0f;
    struct nk_rect track = nk_rect(cell.x + cell.w - 46 - effect_margin,
        cell.y + (cell.h - 24) * .5f, 46, 24);
    struct nk_rect interaction = nk_rect(track.x, track.y,
        track.w + effect_margin, track.h);
    bool hover = available &&
        nk_input_is_mouse_hovering_rect(&context->input, interaction);
    bool changed = hover && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, interaction);
    if (changed) *value = !*value;
    float progress = bongo_cat_ui_animate_eased(context, id,
        *value ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_SPRING);
    char hover_id[80];
    snprintf(hover_id, sizeof(hover_id), "toggle-hover-%s", id);
    float hover_amount = bongo_cat_ui_animate_eased(context, hover_id,
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_STANDARD);
    float scale = 1.0f + .05f * hover_amount;
    track = nk_rect(track.x - track.w * (scale - 1.0f) * .5f,
        track.y - track.h * (scale - 1.0f) * .5f,
        track.w * scale, track.h * scale);
    BongoCatUIPalette p = bongo_cat_ui_palette(
        bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    float state_amount = NK_CLAMP(0.0f, progress, 1.0f);
    if (state_amount > 0.0f && p.effects)
        bongo_cat_ui_paint_shadow(context, track, 12, 0, 2, 8, 0,
            nk_rgba(p.accent.r, p.accent.g, p.accent.b,
            (nk_byte)(89 * state_amount)));
    nk_fill_rect(canvas, track, 12,
        bongo_cat_ui_color_mix(p.field, p.accent, state_amount));
    nk_stroke_rect(canvas, track, 12, 1.0f, bongo_cat_ui_color_mix(
        p.border_subtle, p.accent, state_amount));
    float knob_size = 18.0f * scale;
    float knob_x = track.x + 3.0f * scale +
        (track.w - 6.0f * scale - knob_size) * progress;
    struct nk_rect knob = nk_rect(knob_x,
        track.y + (track.h - knob_size) * .5f, knob_size, knob_size);
    if (p.effects) bongo_cat_ui_paint_shadow(context, knob,
        knob_size * .5f, 0, 2, 5, 0, nk_rgba(0, 0, 0, 51));
    nk_fill_circle(canvas, knob,
        available ? nk_rgb(255, 255, 255) : nk_rgb(245, 248, 252));
    if (hover) bongo_cat_ui_cursor_hover_rect(context, interaction,
        BONGO_CAT_UI_CURSOR_POINTER);
    return changed;
}

bool bongo_cat_pref_control_toggle(struct nk_context *context,
    const char *id, bool *value) {
    return bongo_cat_pref_control_toggle_available(context, id, value, true);
}

bool bongo_cat_pref_control_toggle_available(struct nk_context *context,
    const char *id, bool *value, bool available) {
    struct nk_rect cell;
    if (nk_widget(&cell, context) == NK_WIDGET_INVALID) return false;
    return toggle_at(context, id, value, cell, available);
}

static bool draw_swatches(struct nk_context *context, const char *id,
    struct nk_rect cell, BongoCatObsBackgroundColor *selected) {
    const float slot = 25.0f, switch_width = 64.0f;
    float left = cell.x + cell.w - switch_width -
        BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT * slot - 7.0f;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    bool changed = false;
    for (int i = 0; i < BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT; ++i) {
        BongoCatObsBackgroundColor color = (BongoCatObsBackgroundColor)i;
        struct nk_rect hit = nk_rect(left + i * slot,
            cell.y + (cell.h - 24.0f) * .5f, 24.0f, 24.0f);
        bool hover = nk_input_is_mouse_hovering_rect(&context->input, hit);
        if (nk_input_is_mouse_click_in_rect(&context->input,
            NK_BUTTON_LEFT, hit)) {
            *selected = color;
            changed = true;
        }
        char animation_id[96];
        snprintf(animation_id, sizeof(animation_id),
            "obs-background-%s-%d", id, i);
        float amount = bongo_cat_ui_animate_eased(context, animation_id,
            *selected == color ? 1.0f : 0.0f, 180.0f,
            BONGO_CAT_UI_EASE_STANDARD);
        float size = 14.0f + 6.0f * amount;
        struct nk_rect circle = nk_rect(hit.x + (hit.w - size) * .5f,
            hit.y + (hit.h - size) * .5f, size, size);
        uint32_t rgb = bongo_cat_obs_background_color_rgb(color);
        if (amount > 0.0f && p.effects)
            bongo_cat_ui_paint_shadow(context, circle, size * .5f,
                0, 1, 4, 0, nk_rgba(0, 0, 0, 51));
        nk_fill_circle(canvas, circle, nk_rgb((rgb >> 16) & 255,
            (rgb >> 8) & 255, rgb & 255));
        nk_stroke_circle(canvas, circle, 1.0f, p.border);
        if (hover) bongo_cat_ui_cursor_hover_rect(context, hit,
            BONGO_CAT_UI_CURSOR_POINTER);
    }
    return changed;
}

bool bongo_cat_pref_control_obs_background(struct nk_context *context,
    const char *id, bool *enabled, BongoCatObsBackgroundColor *color) {
    struct nk_rect cell;
    if (nk_widget(&cell, context) == NK_WIDGET_INVALID) return false;
    bool changed = toggle_at(context, id, enabled, cell, true);
    return (*enabled && draw_swatches(context, id, cell, color)) || changed;
}
