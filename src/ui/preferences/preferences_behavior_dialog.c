#include "preferences_state.h"
#include "preferences_overlay.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

static struct nk_color alpha(struct nk_color color, float amount) {
    return bongo_cat_preferences_overlay_alpha(color, amount);
}
static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}
static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height, value,
        nk_strlen(value));
    text(canvas, nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, width + 1, font->height),
        value, font, color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds, bool enabled) {
    return enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}
static bool matches(const BongoCatPreferences *value,
    const BongoCatBehaviorEntry *entry, int tab) {
    if (tab == 0) return entry->kind == BONGO_CAT_BEHAVIOR_MOTION &&
        bongo_cat_live2d_motion_visible(value->app->live2d,
            entry->group, entry->index);
    if (tab == 1) return entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION;
    return entry->kind == BONGO_CAT_BEHAVIOR_SOUND && (entry->sound[0] || entry->sound_clear);
}

static bool tab_available(const BongoCatPreferences *value, int tab) {
    for (size_t i = 0; i < value->app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &value->app->behaviors.entries[i];
        if (matches(value, entry, tab) && (tab != 2 || entry->sound[0])) return true;
    }
    return false;
}

static size_t row_count(const BongoCatPreferences *value) {
    if (!tab_available(value, value->behavior_tab)) return 0;
    size_t count = 0;
    for (size_t i = 0; i < value->app->behaviors.count; ++i)
        if (matches(value, &value->app->behaviors.entries[i],
            value->behavior_tab))
            count++;
    return count;
}

bool bongo_cat_preferences_behavior_dialog_active(
    const BongoCatPreferences *value) {
    return value && value->behavior_dialog;
}

void bongo_cat_preferences_behavior_dialog_open(
    BongoCatPreferences *value) {
    if (!value) return;
    SDL_Log("Preferences behavior dialog opened with %llu behaviors",
        (unsigned long long)value->app->behaviors.count);
    value->behavior_dialog = true;
    value->behavior_dialog_input_armed = false;
    value->behavior_dialog_opened_ns = SDL_GetTicksNS();
    value->behavior_dialog_closing_ns = 0;
    value->render_dirty = true;
}

void bongo_cat_preferences_behavior_dialog_close(
    BongoCatPreferences *value) {
    if (!value || !value->behavior_dialog || value->behavior_dialog_closing_ns)
        return;
    value->behavior_dialog_closing_ns = SDL_GetTicksNS();
    bongo_cat_preferences_scrollbar_reset(&value->behavior_scrollbar);
    bongo_cat_preferences_behavior_rename_finish(value, true);
    bongo_cat_preferences_shortcut_cancel(value);
    value->render_dirty = true;
}

static bool draw_header(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatUIPalette p, float opacity, bool enabled) {
    text(canvas, nk_rect(panel.x + 20, panel.y + 21, panel.w - 74, 24),
        tr(value, "pages.preference.model.behaviorModal.title",
        "Motions, expressions and audio"), value->ui.label_font, alpha(p.text, opacity));
    struct nk_rect close = nk_rect(panel.x + panel.w - 52, panel.y + 17, 32, 32);
    return bongo_cat_ui_close_button(context, canvas, close,
        alpha(p.muted, opacity), alpha(p.accent, opacity), enabled);
}

static void draw_segments(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect panel, BongoCatUIPalette p, float opacity, bool enabled) {
    struct nk_rect wrapper = nk_rect(panel.x + 20, panel.y + 85,
        panel.w - 40, 43);
    nk_fill_rect(canvas, wrapper, 10, alpha(p.field, opacity));
    const char *labels[] = {tr(value,
        "pages.preference.model.behaviorModal.labels.motion", "Motions"), tr(value,
        "pages.preference.model.behaviorModal.labels.expression", "Expressions"), tr(value,
        "pages.preference.model.behaviorModal.labels.audio", "Audio")};
    int count = 0, position = 0;
    for (int i = 0; i < 3; ++i) count += tab_available(value, i);
    if (!count) return;
    float width = (wrapper.w - 6) / count;
    for (int i = 0; i < 3; ++i) {
        if (!tab_available(value, i)) continue;
        struct nk_rect button = nk_rect(wrapper.x + 3 + width * position++,
            wrapper.y + 3, width, 37);
        bool hover = enabled && nk_input_is_mouse_hovering_rect(
            &context->input, button);
        char id[32]; snprintf(id, sizeof(id), "behavior-segment-%d", i);
        float active = bongo_cat_ui_animate_eased(context, id,
            value->behavior_tab == i ? 1.0f : 0.0f, 200,
            BONGO_CAT_UI_EASE_STANDARD);
        struct nk_color background = bongo_cat_ui_color_mix(
            hover ? p.hover : p.field, p.accent, active);
        if (active > .01f && p.effects) bongo_cat_ui_paint_shadow(context,
            button, 8, 0, 2, 10, 0, alpha(nk_rgba(p.accent.r,
            p.accent.g, p.accent.b, 89), opacity * active));
        nk_fill_rect(canvas, button, 8, alpha(background, opacity));
        centered(canvas, button, labels[i], value->ui.caption_font,
            alpha(bongo_cat_ui_color_mix(p.muted,
            nk_rgb(255, 255, 255), active), opacity));
        if (hover) bongo_cat_ui_cursor_hover_rect(context, button,
            BONGO_CAT_UI_CURSOR_POINTER);
        if (hit(context, button, enabled) && value->behavior_tab != i) {
            value->behavior_tab = i;
            bongo_cat_preferences_scrollbar_reset(
                &value->behavior_scrollbar);
            value->behavior_tab_transition_ns = SDL_GetTicksNS();
            bongo_cat_preferences_shortcut_cancel(value);
        }
    }
}

static void draw_rows(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatUIPalette p, float opacity, bool enabled, size_t count) {
    struct nk_rect viewport = nk_rect(panel.x + 20, panel.y + 144,
        panel.w - 40, panel.h - 164);
    float content_height = count * 56.0f;
    float maximum = NK_MAX(0.0f, content_height - viewport.h);
    float saved_offset = value->behavior_scroll[value->behavior_tab];
    BongoCatPreferencesScrollbarResult scroll =
        bongo_cat_preferences_scrollbar_draw(context, canvas, viewport,
            content_height, saved_offset, &value->behavior_scrollbar, p.accent,
            opacity, enabled);
    if (scroll.changed) {
        value->behavior_scroll[value->behavior_tab] = scroll.offset;
        value->render_dirty = true;
    }
    float offset = NK_CLAMP(0.0f, scroll.offset, maximum);
    float render_offset = offset;
    float row_width = bongo_cat_preferences_scrollbar_content_width(
        viewport, content_height);
    float content_opacity = opacity;
    if (value->behavior_tab_transition_ns) {
        float progress = (float)(SDL_GetTicksNS() -
            value->behavior_tab_transition_ns) / 180000000.0f;
        if (progress >= 1.0f) value->behavior_tab_transition_ns = 0;
        else {
            float eased = bongo_cat_ui_ease(BONGO_CAT_UI_EASE_SWIFT,
                NK_CLAMP(0.0f, progress, 1.0f));
            content_opacity *= eased;
            render_offset += 5.0f * (1.0f - eased);
            value->render_dirty = true;
        }
    }
    nk_push_scissor(canvas, viewport);
    size_t shown = 0;
    for (size_t i = 0; i < value->app->behaviors.count; ++i) {
        BongoCatBehaviorEntry *entry = &value->app->behaviors.entries[i];
        if (!matches(value, entry, value->behavior_tab)) continue;
        struct nk_rect row = nk_rect(viewport.x,
            viewport.y + shown++ * 56.0f - render_offset, row_width, 56);
        if (row.y + row.h >= viewport.y && row.y <= viewport.y + viewport.h)
            bongo_cat_preferences_behavior_row_draw(value, context, canvas,
                row, entry, p,
                content_opacity, enabled);
    }
    nk_push_scissor(canvas, nk_window_get_content_region(context));
    if (!shown) centered(canvas, viewport, tr(value, "native.noBehaviors",
        "No items"), value->ui.caption_font, alpha(p.muted, content_opacity));
}

void bongo_cat_preferences_behavior_dialog_draw(
    BongoCatPreferences *value, struct nk_context *context) {
    if (!bongo_cat_preferences_behavior_dialog_active(value)) return;
    if (!tab_available(value, value->behavior_tab))
        for (int i = 0; i < 3; ++i) if (tab_available(value, i)) {
            value->behavior_tab = i; break;
        }
    bongo_cat_ui_cursor_reset(context);
    struct nk_rect region = nk_window_get_bounds(context);
    size_t count = row_count(value);
    float width = NK_MIN(540.0f, region.w - 48.0f);
    float height = NK_MIN(165.0f + count * 56.0f, region.h - 48.0f);
    BongoCatOverlayFrame frame = bongo_cat_preferences_overlay_frame(
        region, width, height, value->behavior_dialog_opened_ns,
        value->behavior_dialog_closing_ns);
    if (frame.finished) {
        value->behavior_dialog = false;
        value->behavior_dialog_opened_ns = value->behavior_dialog_closing_ns = 0;
        return;
    }
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bongo_cat_preferences_overlay_draw(context, region, &frame, p);
    bool closing = value->behavior_dialog_closing_ns != 0;
    bool input_ready = bongo_cat_preferences_overlay_input_ready(context,
        &value->behavior_dialog_input_armed);
    float opacity = closing ? frame.visibility : 1.0f;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, frame.panel, 18, alpha(p.surface, opacity));
    nk_stroke_rect(canvas, frame.panel, 18, 1, alpha(p.border, opacity));
    bool close = draw_header(value, context, canvas, frame.panel, p,
        opacity, !closing && input_ready);
    draw_segments(value, context, canvas, frame.panel, p, opacity,
        !closing && input_ready);
    draw_rows(value, context, canvas, frame.panel, p, opacity,
        !closing && input_ready, count);
    bool outside = hit(context, region, !closing && input_ready) &&
        !nk_input_is_mouse_hovering_rect(&context->input, frame.panel);
    if (close || outside) bongo_cat_preferences_behavior_dialog_close(value);
    if (frame.visibility < 1.0f || closing) value->render_dirty = true;
}
