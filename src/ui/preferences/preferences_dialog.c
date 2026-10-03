#include "preferences_state.h"
#include "preferences_notice.h"
#include "preferences_overlay.h"
#include "ui_backend.h"
#include "ui_icons.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct RemoveDialog {
    BongoCatApp *app;
    char model_id[BONGO_CAT_ID_CAP];
    char model_name[BONGO_CAT_ID_CAP];
    uint64_t opened_ns;
    uint64_t closing_ns;
    bool input_armed;
} RemoveDialog;

static RemoveDialog remove_dialog;

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
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

bool bongo_cat_preferences_remove_dialog_active(const BongoCatApp *app) {
    return app && remove_dialog.app == app && remove_dialog.model_id[0];
}

void bongo_cat_preferences_remove_dialog_open(BongoCatApp *app,
    const char *id) {
    if (!app || !id || !id[0]) return;
    remove_dialog.app = app;
    snprintf(remove_dialog.model_id, sizeof(remove_dialog.model_id), "%s", id);
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    snprintf(remove_dialog.model_name, sizeof(remove_dialog.model_name), "%s",
        bongo_cat_model_name(&app->settings, entry));
    remove_dialog.opened_ns = SDL_GetTicksNS();
    remove_dialog.closing_ns = 0;
    remove_dialog.input_armed = false;
    if (app->preferences) app->preferences->render_dirty = true;
}

void bongo_cat_preferences_remove_dialog_close(BongoCatApp *app) {
    if (!bongo_cat_preferences_remove_dialog_active(app) ||
        remove_dialog.closing_ns) return;
    remove_dialog.closing_ns = SDL_GetTicksNS();
    if (app->preferences) app->preferences->render_dirty = true;
}

void bongo_cat_preferences_remove_dialog_clear(const BongoCatApp *app) {
    if (!app || remove_dialog.app == app)
        memset(&remove_dialog, 0, sizeof(remove_dialog));
}

static bool button(BongoCatApp *app, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds, const char *label,
    bool danger, BongoCatUIPalette p, float opacity, bool enabled) {
    bool hover = enabled && nk_input_is_mouse_hovering_rect(&context->input, bounds);
    struct nk_color background = danger ? p.danger :
        (hover ? p.hover : p.field);
    struct nk_color foreground = danger ? nk_rgb(255, 255, 255) :
        (hover ? p.accent : p.text);
    nk_fill_rect(canvas, bounds, 10, alpha(background, opacity));
    if (danger) nk_stroke_rect(canvas, bounds, 10, 1,
        alpha(p.danger, opacity));
    centered(canvas, bounds, label, app->preferences->ui.caption_font,
        alpha(foreground, opacity));
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    return hit(context, bounds, enabled);
}

static bool close_button(struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect panel,
    BongoCatUIPalette p, float opacity, bool enabled) {
    struct nk_rect bounds = nk_rect(panel.x + panel.w - 52,
        panel.y + 17, 32, 32);
    return bongo_cat_ui_close_button(context, canvas, bounds,
        alpha(p.muted, opacity), alpha(p.accent, opacity), enabled);
}

static void remove_model(BongoCatApp *app) {
    BongoCatError error = {0};
    if (bongo_cat_app_remove_model(app, remove_dialog.model_id,
        &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Model removal failed: %s", error.message);
        bongo_cat_preferences_notice_show(app, tr(app,
            "pages.preference.model.hints.deleteFailed",
            "Unable to delete this model"), true);
    } else {
        char message[sizeof(remove_dialog.model_name) + 128];
        snprintf(message, sizeof(message), tr(app,
            "pages.preference.model.hints.deleteSuccess",
            "Deleted \"%s\""), remove_dialog.model_name);
        bongo_cat_preferences_notice_show(app, message, false);
    }
    bongo_cat_preferences_remove_dialog_close(app);
}

void bongo_cat_preferences_remove_dialog_draw(BongoCatApp *app,
    struct nk_context *context) {
    if (!bongo_cat_preferences_remove_dialog_active(app) ||
        !app->preferences) return;
    bongo_cat_ui_cursor_reset(context);
    struct nk_rect region = nk_window_get_bounds(context);
    float width = NK_MIN(420.0f, region.w - 48.0f), height = 202.0f;
    BongoCatOverlayFrame frame = bongo_cat_preferences_overlay_frame(
        region, width, height, remove_dialog.opened_ns, remove_dialog.closing_ns);
    if (frame.finished) {
        bongo_cat_preferences_remove_dialog_clear(app);
        return;
    }
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    bongo_cat_preferences_overlay_draw(context, region, &frame, p);
    bool closing = remove_dialog.closing_ns != 0;
    float opacity = closing ? frame.visibility : 1.0f;
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, frame.panel, 18, alpha(p.surface, opacity));
    nk_stroke_rect(canvas, frame.panel, 18, 1, alpha(p.border, opacity));
    text(canvas, nk_rect(frame.panel.x + 20, frame.panel.y + 21,
        frame.panel.w - 74, 24), tr(app, "native.delete", "Delete"),
        app->preferences->ui.label_font, alpha(p.text, opacity));
    char prompt[sizeof(remove_dialog.model_name) + 128];
    snprintf(prompt, sizeof(prompt), tr(app,
        "pages.preference.model.hints.deleteModel",
        "Are you sure you want to delete \"%s\"?"), remove_dialog.model_name);
    text(canvas, nk_rect(frame.panel.x + 20, frame.panel.y + 75,
        frame.panel.w - 40, 44), prompt, app->preferences->ui.body_font,
        alpha(p.text, opacity));
    const char *cancel = tr(app, "native.cancel", "Cancel");
    const char *remove = tr(app, "native.delete", "Delete");
    struct nk_rect remove_bounds = nk_rect(frame.panel.x + frame.panel.w - 108,
        frame.panel.y + frame.panel.h - 56, 88, 36);
    struct nk_rect cancel_bounds = nk_rect(remove_bounds.x - 98,
        remove_bounds.y, 88, 36);
    bool enabled = !closing && bongo_cat_preferences_overlay_input_ready(
        context, &remove_dialog.input_armed);
    bool close = close_button(context, canvas, frame.panel,
        p, opacity, enabled);
    if (button(app, context, canvas, cancel_bounds, cancel, false,
        p, opacity, enabled)) close = true;
    if (button(app, context, canvas, remove_bounds, remove, true,
        p, opacity, enabled)) remove_model(app);
    bool outside = hit(context, region, enabled) &&
        !nk_input_is_mouse_hovering_rect(&context->input, frame.panel);
    if (close || outside) bongo_cat_preferences_remove_dialog_close(app);
    if (app->preferences && (frame.visibility < 1.0f || closing))
        app->preferences->render_dirty = true;
}
