#include "preferences_state.h"
#include "preferences_overlay.h"
#include "preferences_controls.h"
#include "runtime.h"
#include "ui_backend.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

static struct nk_color alpha(struct nk_color color, float amount) {
    return bongo_cat_preferences_overlay_alpha(color, amount);
}

static bool hit(struct nk_context *context, struct nk_rect bounds, bool enabled) {
    return enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static bool candidate(BongoCatApp *app, const BongoCatBehaviorEntry *entry,
    BongoCatBehaviorKind kind) {
    return entry->kind == kind && (kind != BONGO_CAT_BEHAVIOR_MOTION ||
        !app->live2d ||
        bongo_cat_live2d_motion_visible(app->live2d, entry->group, entry->index));
}

static size_t candidate_count(BongoCatApp *app, BongoCatBehaviorKind kind) {
    size_t count = 0;
    for (size_t i = 0; i < app->behaviors.count; ++i)
        if (candidate(app, &app->behaviors.entries[i], kind)) count++;
    return count;
}

bool bongo_cat_preferences_random_dialog_active(
    const BongoCatPreferences *value) {
    return value && value->random_dialog;
}

void bongo_cat_preferences_random_dialog_open(BongoCatPreferences *value,
    BongoCatBehaviorKind kind) {
    if (!value) return;
    bongo_cat_preferences_shortcut_cancel(value);
    value->random_dialog_kind = kind;
    value->random_dialog_scroll = 0;
    bongo_cat_preferences_scrollbar_reset(&value->random_dialog_scrollbar);
    value->random_dialog = true;
    value->random_dialog_input_armed = false;
    value->random_dialog_opened_ns = SDL_GetTicksNS();
    value->random_dialog_closing_ns = 0;
    value->render_dirty = true;
}

void bongo_cat_preferences_random_dialog_close(BongoCatPreferences *value) {
    if (!value || !value->random_dialog) return;
    /* Dismiss instantly: a fade-out over a tall panel reads as lag when the
       user asks for a close. The open animation keeps its fade-in. */
    value->random_dialog = false;
    value->random_dialog_input_armed = false;
    value->random_dialog_opened_ns = 0;
    value->random_dialog_closing_ns = 0;
    value->random_dialog_scroll = 0;
    bongo_cat_preferences_scrollbar_reset(&value->random_dialog_scrollbar);
    value->render_dirty = true;
}

static bool draw_header(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatUIPalette p, float opacity, bool enabled) {
    const char *title = value->random_dialog_kind == BONGO_CAT_BEHAVIOR_MOTION ?
        tr(value, "pages.preference.cat.labels.randomMotion", "Random Motions") :
        tr(value, "pages.preference.cat.labels.randomExpression",
        "Random Expressions");
    struct nk_rect bounds = nk_rect(panel.x + 20, panel.y + 21, panel.w - 74, 24);
    nk_draw_text(canvas, bounds, title, nk_strlen(title), value->ui.label_font,
        nk_rgba(0, 0, 0, 0), alpha(nk_rgb(247, 125, 170), opacity));
    struct nk_rect close = nk_rect(panel.x + panel.w - 52, panel.y + 17, 32, 32);
    return bongo_cat_ui_close_button(context, canvas, close,
        alpha(p.muted, opacity), alpha(p.accent, opacity), enabled);
}

static void draw_rows(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatUIPalette p, float opacity, bool enabled, size_t count) {
    struct nk_rect viewport = nk_rect(panel.x + 20, panel.y + 72,
        panel.w - 40, panel.h - 92);
    float content_height = count * 56.0f;
    float maximum = NK_MAX(0.0f, content_height - viewport.h);
    BongoCatPreferencesScrollbarResult scroll =
        bongo_cat_preferences_scrollbar_draw(context, canvas, viewport,
            content_height, value->random_dialog_scroll,
            &value->random_dialog_scrollbar, p.accent, opacity, enabled);
    if (scroll.changed) {
        value->random_dialog_scroll = scroll.offset;
        value->render_dirty = true;
    }
    float offset = NK_CLAMP(0.0f, scroll.offset, maximum);
    float row_width = bongo_cat_preferences_scrollbar_content_width(
        viewport, content_height);
    nk_push_scissor(canvas, viewport);
    size_t shown = 0;
    BongoCatApp *app = value->app;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (!candidate(app, entry, value->random_dialog_kind)) continue;
        struct nk_rect row = nk_rect(viewport.x,
            viewport.y + shown++ * 56.0f - offset, row_width, 56);
        if (row.y + row.h < viewport.y || row.y > viewport.y + viewport.h)
            continue;
        struct nk_rect toggle = nk_rect(row.x + row.w - 78, row.y + 10, 80, 36);
        struct nk_rect name = nk_rect(row.x + 8, row.y + 9,
            NK_MAX(48.0f, toggle.x - row.x - 16), 38);
        const char *label = bongo_cat_app_behavior_label(app, entry->id);
        nk_draw_text(canvas, name, label && label[0] ? label : entry->label,
            nk_strlen(label && label[0] ? label : entry->label),
            value->ui.caption_font, nk_rgba(0, 0, 0, 0),
            alpha(p.text, opacity));
        bool state = bongo_cat_settings_random_enabled(&app->settings,
            entry->id);
        char id[BONGO_CAT_BEHAVIOR_ID_CAP + 16];
        snprintf(id, sizeof(id), "random-%.*s", (int)sizeof(id) - 8,
            entry->id);
        if (bongo_cat_pref_control_toggle_rect(context, id, &state, toggle,
                enabled) &&
            bongo_cat_settings_random_set_enabled(&app->settings, entry->id,
                state))
            value->render_dirty = true;
    }
    nk_push_scissor(canvas, nk_window_get_content_region(context));
}

void bongo_cat_preferences_random_dialog_draw(
    BongoCatPreferences *value, struct nk_context *context) {
    if (!bongo_cat_preferences_random_dialog_active(value)) return;
    bongo_cat_ui_cursor_reset(context);
    struct nk_rect region = nk_window_get_bounds(context);
    size_t count = candidate_count(value->app, value->random_dialog_kind);
    float width = NK_MIN(540.0f, region.w - 48.0f);
    float height = NK_MIN(92.0f + count * 56.0f, region.h - 48.0f);
    BongoCatOverlayFrame frame = bongo_cat_preferences_overlay_frame(
        region, width, height, value->random_dialog_opened_ns,
        value->random_dialog_closing_ns);
    if (frame.finished) {
        value->random_dialog = false;
        value->random_dialog_opened_ns = value->random_dialog_closing_ns = 0;
        return;
    }
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bongo_cat_preferences_overlay_draw(context, region, &frame, p);
    bool closing = value->random_dialog_closing_ns != 0;
    bool input_ready = bongo_cat_preferences_overlay_input_ready(context,
        &value->random_dialog_input_armed);
    float opacity = closing ? frame.visibility : 1.0f;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, frame.panel, 18, alpha(p.surface, opacity));
    nk_stroke_rect(canvas, frame.panel, 18, 1, alpha(p.border, opacity));
    bool close = draw_header(value, context, canvas, frame.panel, p,
        opacity, !closing && input_ready);
    draw_rows(value, context, canvas, frame.panel, p, opacity,
        !closing && input_ready, count);
    bool outside = hit(context, region, !closing && input_ready) &&
        !nk_input_is_mouse_hovering_rect(&context->input, frame.panel);
    if (close || outside) bongo_cat_preferences_random_dialog_close(value);
    if (frame.visibility < 1.0f || closing) value->render_dirty = true;
}
