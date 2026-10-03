#include "preferences_controls.h"
#include "preferences_number_edit.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct RepeatState {
    struct nk_context *context;
    char id[64];
    int direction;
    uint64_t next_ns;
} RepeatState;

typedef struct SliderDragState {
    struct nk_context *context;
    char id[64];
    bool captured;
} SliderDragState;

static RepeatState repeat_state;
static SliderDragState slider_drag_state;

static bool slider_drag_active(struct nk_context *context, const char *id) {
    return slider_drag_state.context == context &&
        strcmp(slider_drag_state.id, id) == 0;
}

static void slider_drag_stop(struct nk_context *context) {
    if (slider_drag_state.context != context) return;
    if (slider_drag_state.captured) SDL_CaptureMouse(false);
    memset(&slider_drag_state, 0, sizeof(slider_drag_state));
}

static bool slider_drag_begin(struct nk_context *context, const char *id,
    struct nk_rect hit) {
    bool active = slider_drag_active(context, id);
    bool pressed = nk_input_is_mouse_click_down_in_rect(&context->input,
        NK_BUTTON_LEFT, hit, nk_true) != 0;
    if (!active && pressed) {
        if (slider_drag_state.context)
            slider_drag_stop(slider_drag_state.context);
        slider_drag_state.context = context;
        snprintf(slider_drag_state.id, sizeof(slider_drag_state.id), "%s", id);
        slider_drag_state.captured = SDL_CaptureMouse(true);
        active = true;
    }
    return active;
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height,
        text, nk_strlen(text));
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, width + 1, font->height);
    nk_draw_text(canvas, target, text, nk_strlen(text), font,
        nk_rgba(0, 0, 0, 0), color);
}

static int repeat_direction(struct nk_context *context, const char *id,
    struct nk_rect minus, struct nk_rect plus) {
    bool down = nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT);
    bool same = repeat_state.context == context &&
        strcmp(repeat_state.id, id) == 0;
    if (!down) {
        if (repeat_state.context == context) memset(&repeat_state, 0,
            sizeof(repeat_state));
        return 0;
    }
    int clicked_direction = nk_input_is_mouse_click_down_in_rect(&context->input,
        NK_BUTTON_LEFT, minus, nk_true) ? -1 :
        (nk_input_is_mouse_click_down_in_rect(&context->input,
        NK_BUTTON_LEFT, plus, nk_true) ? 1 : 0);
    uint64_t now = SDL_GetTicksNS();
    if (clicked_direction && (!same || repeat_state.direction != clicked_direction)) {
        repeat_state.context = context;
        snprintf(repeat_state.id, sizeof(repeat_state.id), "%s", id);
        repeat_state.direction = clicked_direction;
        repeat_state.next_ns = now + 360000000ULL;
        return clicked_direction;
    }
    struct nk_rect active = repeat_state.direction < 0 ? minus : plus;
    if (!same || !nk_input_is_mouse_hovering_rect(&context->input, active) ||
        now < repeat_state.next_ns) return 0;
    repeat_state.next_ns = now + 60000000ULL;
    return repeat_state.direction;
}

bool bongo_cat_pref_controls_animating(struct nk_context *context) {
    bool down = nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT);
    if (!down && repeat_state.context == context)
        memset(&repeat_state, 0, sizeof(repeat_state));
    if (!down) slider_drag_stop(context);
    return (repeat_state.context == context && down) ||
        slider_drag_state.context == context;
}

void bongo_cat_pref_controls_reset(struct nk_context *context) {
    bongo_cat_pref_number_edit_reset(context);
    if (repeat_state.context == context)
        memset(&repeat_state, 0, sizeof(repeat_state));
    slider_drag_stop(context);
}

static double stepper(struct nk_context *context, const char *id,
    double minimum, double value, double maximum, double step,
    double default_value, bool integer, bool *changed) {
    struct nk_rect bounds;
    *changed = false;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return value;
    const float effect_margin = 2.0f;
    bounds = nk_rect(bounds.x + bounds.w - 122.0f - effect_margin,
        bounds.y, 122.0f, bounds.h);
    struct nk_rect interaction = nk_rect(bounds.x, bounds.y,
        bounds.w + effect_margin, bounds.h);
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect minus = nk_rect(bounds.x, bounds.y, 36, bounds.h);
    struct nk_rect plus = nk_rect(bounds.x + bounds.w - 36, bounds.y, 36, bounds.h);
    struct nk_rect plus_hit = nk_rect(plus.x, plus.y,
        plus.w + effect_margin, plus.h);
    struct nk_rect number_box = nk_rect(bounds.x + 36, bounds.y, bounds.w - 72,
        bounds.h);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, interaction);
    bool minus_hover = nk_input_is_mouse_hovering_rect(&context->input, minus);
    bool plus_hover = nk_input_is_mouse_hovering_rect(&context->input, plus_hit);
    char hover_id[80], minus_id[80], plus_id[80];
    snprintf(hover_id, sizeof(hover_id), "stepper-hover-%s", id);
    snprintf(minus_id, sizeof(minus_id), "stepper-minus-%s", id);
    snprintf(plus_id, sizeof(plus_id), "stepper-plus-%s", id);
    float hover_amount = bongo_cat_ui_animate_eased(context, hover_id,
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    float minus_amount = bongo_cat_ui_animate_eased(context, minus_id,
        minus_hover && value > minimum ? 1.0f : 0.0f, 150,
        BONGO_CAT_UI_EASE_STANDARD);
    float plus_amount = bongo_cat_ui_animate_eased(context, plus_id,
        plus_hover && value < maximum ? 1.0f : 0.0f, 150,
        BONGO_CAT_UI_EASE_STANDARD);
    nk_fill_rect(canvas, bounds, 11, p.field);
    if (minus_amount > 0) nk_fill_rect(canvas, minus, 10,
        bongo_cat_ui_color_mix(p.field, p.accent, minus_amount));
    if (plus_amount > 0) nk_fill_rect(canvas, plus, 10,
        bongo_cat_ui_color_mix(p.field, p.accent, plus_amount));
    nk_stroke_rect(canvas, bounds, 11, 1.0f,
        bongo_cat_ui_color_mix(p.border_subtle, p.accent, hover_amount));
    struct nk_color minus_color = value <= minimum ? p.border_subtle :
        bongo_cat_ui_color_mix(p.muted, nk_rgb(255, 255, 255), minus_amount);
    struct nk_color plus_color = value >= maximum ? p.border_subtle :
        bongo_cat_ui_color_mix(p.muted, nk_rgb(255, 255, 255), plus_amount);
    float cy = bounds.y + bounds.h * .5f;
    nk_stroke_line(canvas, minus.x + 13, cy, minus.x + 27, cy, 2, minus_color);
    nk_stroke_line(canvas, plus.x + 13, cy, plus.x + 27, cy, 2, plus_color);
    nk_stroke_line(canvas, plus.x + 20, cy - 7,
        plus.x + 20, cy + 7, 2, plus_color);
    if (minus_hover || plus_hover) bongo_cat_ui_cursor_hover_rect(context, interaction,
        BONGO_CAT_UI_CURSOR_POINTER);
    double before = value;
    bool reset_default = nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_DOUBLE, number_box) != 0;
    if (reset_default) {
        value = NK_CLAMP(minimum, default_value, maximum);
        bongo_cat_pref_number_edit_reset(context);
        nk_edit_unfocus(context);
    }
    char number[32];
    if (integer) snprintf(number, sizeof(number), "%.0f", value);
    else if (step < .1) snprintf(number, sizeof(number), "%.2f", value);
    else if (step < 1.0) snprintf(number, sizeof(number), "%.1f", value);
    else snprintf(number, sizeof(number), "%.0f", value);
    bool editing = !reset_default && bongo_cat_pref_number_edit(context, id,
        number_box, number, integer, minimum, maximum, &value);
    if (!editing) centered(canvas, number_box, number,
        bongo_cat_ui_body_font(context), p.text);
    int direction = repeat_direction(context, id, minus, plus_hit);
    float wheel = context->input.mouse.scroll_delta.y;
    if (!editing && nk_input_is_mouse_hovering_rect(&context->input, number_box) && wheel != 0) {
        direction = wheel > 0 ? 1 : -1;
        context->input.mouse.scroll_delta.y = 0;
    }
    if (!reset_default)
        value = NK_CLAMP(minimum, value + direction * step, maximum);
    *changed = value != before;
    return value;
}

bool bongo_cat_pref_control_float(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step,
    float default_value) {
    bool changed;
    double result = stepper(context, id, minimum, *value, maximum, step,
        default_value, false, &changed);
    changed = changed && *value != (float)result;
    if (changed) *value = (float)result;
    return changed;
}

bool bongo_cat_pref_control_int(struct nk_context *context, const char *id,
    int minimum, int *value, int maximum, int step, int default_value) {
    bool changed;
    double result = stepper(context, id, minimum, *value, maximum, step,
        default_value, true, &changed);
    if (changed) *value = (int)result;
    return changed;
}

bool bongo_cat_pref_control_slider(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step,
    float default_value) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    bounds = nk_rect(bounds.x + bounds.w - 220.0f,
        bounds.y + 2.0f, 220.0f, bounds.h - 4.0f);
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect value_box = nk_rect(bounds.x + bounds.w - 49,
        bounds.y, 49, bounds.h);
    struct nk_rect track = nk_rect(bounds.x + 2,
        bounds.y + bounds.h * .5f - 3, 154, 6);
    struct nk_rect hit = nk_rect(track.x - 3, bounds.y,
        track.w + 6, bounds.h);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, hit);
    bool value_hover = nk_input_is_mouse_hovering_rect(&context->input, value_box);
    bool dragging = slider_drag_begin(context, id, hit);
    bool mouse_down = nk_input_is_mouse_down(&context->input, NK_BUTTON_LEFT);
    char hover_id[80];
    snprintf(hover_id, sizeof(hover_id), "slider-hover-%s", id);
    float hover_amount = bongo_cat_ui_animate_eased(context, hover_id,
        hover || (dragging && mouse_down) ? 1.0f : 0.0f, 150,
        BONGO_CAT_UI_EASE_STANDARD);
    float before = *value;
    if (dragging) {
        float pointer_x = mouse_down ? context->input.mouse.pos.x :
            context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.x;
        float ratio = (pointer_x - track.x) / track.w;
        ratio = NK_CLAMP(0.0f, ratio, 1.0f);
        float raw = minimum + ratio * (maximum - minimum);
        *value = minimum + roundf((raw - minimum) / step) * step;
        *value = NK_CLAMP(minimum, *value, maximum);
    }
    if (dragging && !mouse_down) slider_drag_stop(context);
    float wheel = context->input.mouse.scroll_delta.y;
    if ((hover || value_hover) && wheel != 0.0f) {
        *value = NK_CLAMP(minimum, *value + (wheel > 0 ? step : -step), maximum);
        context->input.mouse.scroll_delta.y = 0;
    }
    if (nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_DOUBLE, value_box))
        *value = NK_CLAMP(minimum, default_value, maximum);
    float ratio = (*value - minimum) / (maximum - minimum);
    nk_fill_rect(canvas, track, 3, p.field);
    nk_fill_rect(canvas, nk_rect(track.x, track.y, track.w * ratio, track.h),
        3, p.accent);
    float thumb_x = track.x + track.w * ratio;
    float thumb_width = 4.0f + 4.0f * hover_amount;
    float thumb_rounding = thumb_width * .5f;
    struct nk_rect thumb = nk_rect(thumb_x - thumb_width * .5f,
        track.y - 9, thumb_width, 24);
    if (p.effects) bongo_cat_ui_paint_shadow(context, thumb, thumb_rounding,
        0, 1, 4.0f + 3.0f * hover_amount, 0,
        nk_rgba(p.accent.r, p.accent.g, p.accent.b, 89));
    nk_fill_rect(canvas, thumb, thumb_rounding, p.accent);
    nk_fill_rect(canvas, value_box, 8, p.field);
    nk_stroke_rect(canvas, value_box, 8, 1, p.border_subtle);
    char number[24]; snprintf(number, sizeof(number), "%.0f%%", *value);
    centered(canvas, value_box, number, bongo_cat_ui_body_font(context), p.text);
    if (hover || value_hover) bongo_cat_ui_cursor_hover_rect(context,
        hover ? hit : value_box,
        hover ? BONGO_CAT_UI_CURSOR_RESIZE_EW :
        BONGO_CAT_UI_CURSOR_POINTER);
    return before != *value;
}
