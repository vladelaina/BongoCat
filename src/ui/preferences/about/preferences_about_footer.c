#include "preferences_about_footer.h"
#include "preferences_notice.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_icons.h"
#include "ui_paint.h"
#include "update_service.h"

#include <SDL3/SDL.h>
#include <stdio.h>

#define SUPPORT_LOGS_SCALE (4.0f / 5.0f)
#define SUPPORT_LOGS_ICON_SIZE (14.0f * SUPPORT_LOGS_SCALE)
#define SUPPORT_LOGS_GAP 4.0f

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

static float width(const struct nk_user_font *font, const char *value) {
    return font->width(font->userdata, font->height, value, nk_strlen(value));
}

static const struct nk_user_font *logs_font(BongoCatPreferences *value) {
    value->support_logs_font = *value->ui.caption_font;
    value->support_logs_font.height =
        value->ui.caption_font->height * SUPPORT_LOGS_SCALE;
    return &value->support_logs_font;
}

static float logs_content_width(const struct nk_user_font *font,
    const char *label) {
    return SUPPORT_LOGS_ICON_SIZE + SUPPORT_LOGS_GAP + width(font, label);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    float text_width = width(font, value);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - text_width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f,
        NK_MIN(text_width + 1.0f, bounds.w), font->height), value,
        nk_strlen(value), font, nk_rgba(0, 0, 0, 0), color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static void link_cursor(struct nk_context *context, struct nk_rect bounds) {
    if (nk_input_is_mouse_hovering_rect(&context->input, bounds))
        bongo_cat_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_UI_CURSOR_POINTER);
}

static struct nk_color link_color(struct nk_context *context,
    struct nk_rect bounds, const char *animation_id, BongoCatUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    float amount = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    return bongo_cat_ui_color_mix(p.accent, p.pink, amount);
}

static void open_logs(BongoCatPreferences *value) {
    if (bongo_cat_platform_open_directory(value->app->log_root)) return;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot open logs directory: %s", value->app->log_root);
    bongo_cat_preferences_notice_show(value->app, tr(value,
        "native.support.openLogsFailed", "Unable to open logs folder"), true);
}

static void draw_update(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *label, float content_width,
    BongoCatUIPalette p, const BongoCatUpdateSnapshot *snapshot) {
    if (p.effects) bongo_cat_ui_paint_shadow(context, bounds, 10,
        0, 4, 14, 0, nk_rgba(p.accent.r, p.accent.g, p.accent.b, 89));
    nk_fill_rect(canvas, bounds, 10, p.accent);
    if (snapshot->status == BONGO_CAT_UPDATE_AVAILABLE ||
            snapshot->status == BONGO_CAT_UPDATE_INSTALLED ||
            (snapshot->status == BONGO_CAT_UPDATE_ERROR &&
                snapshot->release.version[0])) {
        /* Mark the actionable update without obscuring its label. */
        nk_fill_circle(canvas, nk_rect(bounds.x + bounds.w - 9.0f,
            bounds.y + 5.0f, 6.0f, 6.0f), p.danger);
    }
    float content_x = bounds.x + (bounds.w - content_width) * .5f;
    bongo_cat_preferences_icon_draw(value, canvas, BONGO_CAT_UI_ICON_SYNC,
        nk_rect(content_x, bounds.y + 10, 16, 16), nk_rgb(255, 255, 255));
    centered(canvas, nk_rect(content_x + 24, bounds.y,
        content_width - 23, bounds.h), label, value->ui.caption_font,
        nk_rgb(255, 255, 255));
    link_cursor(context, bounds);
    if (hit(context, bounds)) {
        if (snapshot->status == BONGO_CAT_UPDATE_CHECKING ||
            snapshot->status == BONGO_CAT_UPDATE_DOWNLOADING ||
            snapshot->status == BONGO_CAT_UPDATE_INSTALLED) return;
        if (snapshot->status == BONGO_CAT_UPDATE_AVAILABLE ||
                (snapshot->status == BONGO_CAT_UPDATE_ERROR &&
                    snapshot->release.version[0])) {
            if (!bongo_cat_update_refresh_and_open(value->app->update))
                bongo_cat_preferences_notice_show(value->app, tr(value,
                    "native.support.updateFailed",
                    "Unable to check for updates"), true);
        } else if (snapshot->status == BONGO_CAT_UPDATE_STORE ||
            snapshot->status == BONGO_CAT_UPDATE_UNSUPPORTED) {
            if (!bongo_cat_update_open(value->app->update))
                {
                    char detail[384];
                    const char *reason = SDL_GetError();
                    snprintf(detail, sizeof(detail), "%s: %s", tr(value,
                        "native.support.openUpdateFailed",
                        "Unable to open the update page"),
                        reason && reason[0] ? reason : "system rejected the URL");
                    bongo_cat_preferences_notice_show(value->app, detail, true);
                }
        } else if (!bongo_cat_update_check(value->app->update, true)) {
            bongo_cat_preferences_notice_show(value->app, tr(value,
                "native.support.updateFailed",
                "Unable to check for updates"), true);
        } else value->render_dirty = true;
    }
}

static const char *update_label(BongoCatPreferences *value,
    const BongoCatUpdateSnapshot *snapshot, char *buffer, size_t capacity) {
    switch (snapshot->status) {
    case BONGO_CAT_UPDATE_CHECKING:
        return tr(value, "native.support.checkingUpdate", "Checking...");
    case BONGO_CAT_UPDATE_DOWNLOADING:
        return tr(value, "native.support.downloadingUpdate",
            "Downloading update...");
    case BONGO_CAT_UPDATE_INSTALLED:
        snprintf(buffer, capacity, tr(value, "native.support.updateInstalled",
            "Restart to apply v%s"), snapshot->release.version);
        return buffer;
    case BONGO_CAT_UPDATE_AVAILABLE:
        snprintf(buffer, capacity, tr(value,
            "native.support.downloadUpdate", "Update to v%s"),
            snapshot->release.version);
        return buffer;
    case BONGO_CAT_UPDATE_ERROR:
        if (snapshot->release.version[0]) {
            snprintf(buffer, capacity, tr(value,
                "native.support.downloadUpdate", "Update to v%s"),
                snapshot->release.version);
            return buffer;
        }
        return tr(value, "native.support.checkUpdate", "Check for updates");
    case BONGO_CAT_UPDATE_STORE:
        return tr(value, "native.support.openStore", "Open Microsoft Store");
    case BONGO_CAT_UPDATE_UNSUPPORTED:
        return tr(value, "native.support.viewReleases", "View releases");
    default:
        return tr(value, "native.support.checkUpdate", "Check for updates");
    }
}

static void draw_links(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect feedback, struct nk_rect logs, const char *feedback_label,
    const char *logs_label, BongoCatUIPalette p) {
    const struct nk_user_font *small_font = logs_font(value);
    float feedback_hover = bongo_cat_ui_animate_eased(context, "support-feedback-hover",
        nk_input_is_mouse_hovering_rect(&context->input, feedback) ? 1.0f : 0.0f,
        200, BONGO_CAT_UI_EASE_STANDARD);
    struct nk_color feedback_color = bongo_cat_ui_color_mix(nk_rgb(111, 204, 225),
        nk_rgb(78, 172, 192), feedback_hover);
    centered(canvas, feedback, feedback_label, value->ui.caption_font,
        feedback_color);
    link_cursor(context, feedback);
    if (hit(context, feedback) && !SDL_OpenURL("https://bongocat.pet/?feedback=1"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot open feedback page: %s", SDL_GetError());

    struct nk_color logs_color = link_color(context, logs,
        "support-logs-hover", p);
    float content_width = logs_content_width(small_font, logs_label);
    float content_x = logs.x + (logs.w - content_width) * .5f;
    bongo_cat_preferences_icon_draw(value, canvas, BONGO_CAT_UI_ICON_FOLDER,
        nk_rect(content_x, logs.y + (logs.h - SUPPORT_LOGS_ICON_SIZE) * .5f,
        SUPPORT_LOGS_ICON_SIZE, SUPPORT_LOGS_ICON_SIZE), logs_color);
    centered(canvas, nk_rect(content_x + SUPPORT_LOGS_ICON_SIZE +
        SUPPORT_LOGS_GAP, logs.y, width(small_font, logs_label) + 1,
        logs.h), logs_label, small_font, logs_color);
    link_cursor(context, logs);
    if (hit(context, logs)) open_logs(value);
}

void bongo_cat_preferences_about_footer(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, BongoCatUIPalette p) {
    const char *version_label = tr(value,
        "native.support.version", "App version");
    BongoCatUpdateSnapshot update_snapshot;
    bongo_cat_update_snapshot(value->app->update, &update_snapshot);
    char update_buffer[128];
    const char *update = update_label(value, &update_snapshot,
        update_buffer, sizeof(update_buffer));
    const char *feedback = tr(value, "native.support.feedback", "Feedback");
    const char *logs = tr(value, "native.support.openLogs", "Open logs");
    const struct nk_user_font *small_font = logs_font(value);
    const char *version = BONGO_CAT_VERSION_DISPLAY;
    float label_width = width(value->ui.caption_font, version_label) + 2;
    float version_width = width(value->ui.label_font, version) + 2;
    float update_text_width = width(value->ui.caption_font, update);
    float update_content_width = 24.0f + update_text_width;
    float update_width = NK_MAX(110.0f, update_content_width + 28.0f);
    float feedback_width = NK_MAX(56.0f,
        width(value->ui.caption_font, feedback) + 4.0f);
    float logs_width = logs_content_width(small_font, logs) + 4.0f;
    float links_width = feedback_width + 20.0f + logs_width;
    float actions_width = update_width + 14.0f + links_width;
    float info_width = label_width + version_width + 8.0f;
    float total = info_width + actions_width + 14.0f;
    bool stacked = total > bounds.w;
    float info_x = bounds.x + (bounds.w -
        (stacked ? info_width : total)) * .5f;
    float info_y = bounds.y + (stacked ? -8.0f : 0.0f);
    centered(canvas, nk_rect(info_x, info_y, label_width, 36), version_label,
        value->ui.caption_font, p.muted);
    centered(canvas, nk_rect(info_x + label_width + 8, info_y,
        version_width, 36), version, value->ui.label_font, p.text);

    float actions_x = stacked ? bounds.x +
        (bounds.w - actions_width) * .5f : info_x + info_width + 14.0f;
    float actions_y = bounds.y + (stacked ? 22.0f : 0.0f);
    struct nk_rect update_button = nk_rect(actions_x, actions_y,
        update_width, 36);
    draw_update(value, context, canvas, update_button, update,
        update_content_width, p, &update_snapshot);
    struct nk_rect feedback_link = nk_rect(actions_x + update_width + 14,
        actions_y, feedback_width, 36);
    struct nk_rect logs_link = nk_rect(feedback_link.x + feedback_width + 20,
        actions_y, logs_width, 36);
    draw_links(value, context, canvas, feedback_link, logs_link,
        feedback, logs, p);
}
