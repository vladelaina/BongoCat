#include "preferences_internal.h"
#include "preferences_state.h"
#include "preferences_theme.h"
#include "preferences_widgets.h"
#include "preferences_notice.h"
#include "ui_tooltip.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/log.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"
#include "runtime.h"
#ifdef _WIN32
#include "windows_game_compatibility.h"
#endif

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key, const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static void section_gap(struct nk_context *context, float pixels) {
    context->current->layout->at_y += pixels;
}

static void page_display(BongoCatApp *app, struct nk_context *context) {
    BongoCatModelPreferences *model = &app->settings.model;
    BongoCatWindowPreferences *window = &app->settings.window;
    BongoCatWindowState *window_state = &app->session.window;
    bongo_cat_pref_section_icon(context, tr(app,
        "pages.preference.cat.labels.windowSettings",
        "Window"),
        BONGO_CAT_PREF_ICON_SECTION_WINDOW);
#ifdef _WIN32
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_ADMINISTRATOR);
    bool game_compatibility = app->settings.app.game_compatibility;
    if (bongo_cat_pref_toggle_help(context, "game-compatibility", tr(app,
        "pages.preference.general.labels.gameCompatibility", "Game Compatibility Mode"),
        tr(app, "pages.preference.cat.hints.gameCompatibility",
            "Enable when the desktop pet cannot respond in games."),
        tr(app, "pages.preference.cat.hints.gameCompatibilityHelp",
            "Enable to restart BongoCat with administrator privileges. Disable to return to normal privileges."),
        &game_compatibility)) {
        BongoCatError compatibility_error = {0};
        if (!bongo_cat_windows_game_compatibility_set(app,
                game_compatibility, &compatibility_error)) {
            char message[1024];
            snprintf(message, sizeof(message), "%s\n%s", tr(app,
                "pages.preference.cat.hints.gameCompatibilityFailed",
                "Unable to change game compatibility mode."), compatibility_error.message);
            bongo_cat_preferences_notice_show(app, message, true);
        }
    }
#endif
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_PASS_THROUGH);
    if (bongo_cat_pref_toggle(context, "pass-through", tr(app,
        "composables.useAppMenu.labels.passThrough", "Pass Through"), tr(app,
        "pages.preference.cat.hints.passThrough",
        "You can also turn this off by right-clicking the tray icon"),
        &window->pass_through)) {
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_ALWAYS_ON_TOP);
    if (bongo_cat_pref_toggle(context, "always-top", tr(app,
        "composables.useAppMenu.labels.alwaysOnTop", "Always on Top"), "",
        &window->always_on_top)) {
        bongo_cat_platform_set_always_on_top(&app->platform, window->always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    }
    if (app->platform.hover_hide_unavailable) {
        bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_SHORTCUT_VISIBILITY);
        bongo_cat_pref_status(context, "hover-unavailable", tr(app,
            "pages.preference.cat.labels.hideOnHover", "Hide on Hover"), tr(app,
            "pages.preference.cat.hints.hoverUnavailable",
            "Hover hiding is unavailable in the current desktop environment."));
    } else {
        bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_SHORTCUT_VISIBILITY);
        bool hover_available = window->pass_through && window->always_on_top;
        bool hide_on_hover = window->hide_on_hover && hover_available;
        if (bongo_cat_pref_toggle_float_detail(context, "hide-on-hover", tr(app,
            "pages.preference.cat.labels.hideOnHover", "Hide on Hover"),
            tr(app, "pages.preference.cat.labels.secondsUnit", "s"),
            &hide_on_hover, 0.0f, &window->hide_fade_seconds,
            BONGO_CAT_MAX_HIDE_FADE_SECONDS, 0.1f,
            BONGO_CAT_DEFAULT_HIDE_FADE_SECONDS, tr(app,
                "pages.preference.cat.hints.hoverRequires",
                "Requires both [Pass Through] and [Always on Top] above to be enabled."),
            hover_available)) {
            window->hide_on_hover = hide_on_hover;
            bongo_cat_app_update_hover(app, SDL_GetTicksNS());
        }
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_SOLID_BACKGROUND);
    if (bongo_cat_pref_obs_background(context, "obs-background", tr(app,
        "pages.preference.cat.labels.obsBackground", "Solid Background"), tr(app,
        "pages.preference.cat.hints.obsBackground", "Window capture is black?"), tr(app,
        "pages.preference.cat.hints.obsBackgroundHelp", "OBS: enable this option, use "
        "the Windows 7 compatibility method, and remove the background with a "
        "color key filter."),
        &window->obs_background, &window->obs_background_color))
        app->dirty = true;
    float old_scale = window_state->scale_percent;
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_WINDOW_SIZE);
    bool reset_position = bongo_cat_pref_float_action(context, "window-size", tr(app,
        "pages.preference.cat.labels.windowSize", "Window Size"), tr(app,
        "pages.preference.cat.hints.windowSize", "[Scroll] to resize or [hold the right mouse button] and drag right to enlarge, left to shrink"),
        10.0f, &window_state->scale_percent, 500.0f, 1.0f,
        BONGO_CAT_DEFAULT_WINDOW_SCALE_PERCENT, tr(app,
            "pages.preference.cat.labels.resetPosition", "Reset"));
    if (old_scale != window_state->scale_percent && old_scale > 0.0f) {
        float requested_scale = window_state->scale_percent;
        window_state->scale_percent = old_scale;
        bongo_cat_window_cancel_wheel_animation(app);
        bongo_cat_window_set_scale(app, requested_scale);
    }
    if (reset_position) bongo_cat_window_reset_position(app);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_WINDOW_CORNERS);
    /* Display the fraction of maximum rounding; keep saved radii in their
       original units (percent of the short edge) for existing settings. */
    float corner_roundness = window->corner_radius_percent * 2.0f;
    if (bongo_cat_pref_toggle_float(context, "window-corners", tr(app,
        "pages.preference.cat.labels.windowCorners", "Window Corners"), "%",
        &window->rounded_corners, 0.0f, &corner_roundness,
        100.0f, 1.0f, BONGO_CAT_DEFAULT_WINDOW_CORNER_PERCENT * 2.0f)) {
        window->corner_radius_percent = corner_roundness * 0.5f;
        app->dirty = true;
        bongo_cat_window_mark_hit_dirty(app);
    }
    float old_opacity = window_state->opacity_percent;
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_OPACITY);
    bongo_cat_pref_slider(context, "opacity", tr(app,
        "pages.preference.cat.labels.opacity", "Opacity"), tr(app,
        "composables.useAppMenu.labels.wheelOpacityHint", "Ctrl+Wheel: opacity"),
        10.0f, &window_state->opacity_percent, 100.0f, 1.0f,
        BONGO_CAT_DEFAULT_WINDOW_OPACITY_PERCENT);
    if (old_opacity != window_state->opacity_percent)
        bongo_cat_window_cancel_wheel_animation(app);
    if (old_opacity != window_state->opacity_percent && !app->hover_hidden) {
        bongo_cat_app_cancel_hover_fade(app);
        bongo_cat_platform_set_opacity(&app->platform,
            window_state->opacity_percent / 100.0f);
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_RANDOM_EXPRESSION);
    bongo_cat_pref_toggle_float(context, "random-expression", tr(app,
        "pages.preference.cat.labels.randomExpression", "Random Expressions"),
        tr(app, "pages.preference.cat.labels.secondsUnit", "s"),
        &window->random_expression, 1.0f,
        &window->random_expression_interval_seconds, 3600.0f, 1.0f,
        BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_RANDOM_MOTION);
    bongo_cat_pref_toggle_float(context, "random-motion", tr(app,
        "pages.preference.cat.labels.randomMotion", "Random Motions"),
        tr(app, "pages.preference.cat.labels.secondsUnit", "s"),
        &window->random_motion, 1.0f,
        &window->random_motion_interval_seconds, 3600.0f, 1.0f,
        BONGO_CAT_DEFAULT_RANDOM_MOTION_SECONDS);

    section_gap(context, 10);
    bongo_cat_pref_section_icon(context, tr(app,
        "pages.preference.cat.labels.modelSettings", "Model"),
        BONGO_CAT_PREF_ICON_SECTION_MODEL);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_GAMEPAD_FOUR_HANDS);
    if (bongo_cat_pref_toggle(context, "gamepad-four-hands", tr(app,
        "pages.preference.cat.labels.gamepadFourHands", "Gamepad Four-Hand Mode"), tr(app,
        "pages.preference.cat.hints.gamepadFourHands",
        "Keep two extra hands visible in gamepad mode. Appearance depends on the model"),
        &model->gamepad_four_hands))
        bongo_cat_app_refresh_hands(app);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MIRROR);
    if (bongo_cat_pref_toggle(context, "mirror", tr(app,
        "pages.preference.cat.labels.mirrorMode", "Mirror Mode"), "",
        &model->mirror)) {
        app->model_pointer_anchor_ready = false;
        app->pointer_known = false;
        app->dirty = true;
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_VERTICAL_FLIP);
    if (bongo_cat_pref_toggle(context, "vertical-flip", tr(app,
        "pages.preference.cat.labels.verticalFlip", "Hang Upside Down"), "",
        &model->vertical_flip))
        bongo_cat_app_reset_pointer_tracking(app);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MOUSE_MIRROR);
    if (bongo_cat_pref_toggle(context, "mouse-mirror", tr(app,
        "pages.preference.cat.labels.mouseMirror", "Mouse Horizontal Flip"), "",
        &model->mouse_mirror)) {
        app->pointer_known = false;
        app->dirty = true;
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MOUSE_VERTICAL_FLIP);
    if (bongo_cat_pref_toggle(context, "mouse-vertical-flip", tr(app,
        "pages.preference.cat.labels.mouseVerticalFlip", "Mouse Vertical Flip"), "",
        &model->mouse_vertical_flip))
        bongo_cat_app_reset_pointer_tracking(app);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MOUSE_CENTERED);
    bool disable_mouse_centered = !model->mouse_centered;
    if (bongo_cat_pref_toggle(context, "mouse-centered", tr(app,
        "pages.preference.cat.labels.mouseCentered",
        "Disable Mouse Centering on Desktop Pet"), "", &disable_mouse_centered)) {
        model->mouse_centered = !disable_mouse_centered;
        bongo_cat_app_reset_pointer_tracking(app);
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_IGNORE_MOUSE);
    if (bongo_cat_pref_toggle(context, "ignore-mouse", tr(app,
        "pages.preference.cat.labels.ignoreMouse", "Ignore Mouse Events"), "",
        &model->ignore_mouse)) {
        app->pointer_known = false;
        app->dirty = true;
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_MAX_FPS);
    model->max_fps = bongo_cat_pref_fps(context, "max-fps", tr(app,
        "pages.preference.cat.labels.maxFPS", "Max Frame Rate"), model->max_fps,
        app->startup_display_fps);
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_RENDER_QUALITY);
    float next_quality = bongo_cat_pref_render_quality(context, "render-quality",
        tr(app, "pages.preference.cat.labels.renderQuality",
            "Model Quality (%)"),
        tr(app, "pages.preference.cat.hints.renderQuality",
            "Try to find a visual balance and save memory."),
        model->render_quality_percent);
    if (next_quality != model->render_quality_percent) {
        float old_quality = model->render_quality_percent;
        model->render_quality_percent = next_quality;
        BongoCatError reload_error = {0};
        bool reloaded = !app->loaded_model[0] ||
            bongo_cat_live2d_try_reuse_texture_quality(app->live2d, next_quality) ||
            bongo_cat_app_reload_model_with_error(app, &reload_error);
        if (!reloaded) {
            model->render_quality_percent = old_quality;
            char message[1024];
            snprintf(message, sizeof(message), "%s\n%s", tr(app,
                "pages.preference.cat.hints.dynamicTextureResolutionFailed",
                "Unable to reload the model with the selected texture mode."),
                reload_error.message);
            bongo_cat_preferences_notice_show(app, message, true);
        } else app->dirty = true;
    }
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_TEXTURE_RESOLUTION);
    bool old_dynamic_texture_resolution = model->dynamic_texture_resolution;
    bool disable_dynamic_texture_resolution = !model->dynamic_texture_resolution;
    if (bongo_cat_pref_toggle(context, "dynamic-texture-resolution", tr(app,
        "pages.preference.cat.labels.dynamicTextureResolution",
        "Disable Dynamic Texture Resolution"), tr(app,
        "pages.preference.cat.hints.dynamicTextureResolution",
        "Dynamically adjusts texture resolution to the actual display size, "
        "preserving visual detail while optimizing video memory usage. "
        "Enabling this option is generally not recommended."),
        &disable_dynamic_texture_resolution)) {
        model->dynamic_texture_resolution = !disable_dynamic_texture_resolution;
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[memory] texture-mode-change previous=%d requested=%d",
            old_dynamic_texture_resolution, model->dynamic_texture_resolution);
        BongoCatError reload_error = {0};
        bool reloaded = !app->loaded_model[0] ||
            bongo_cat_app_reload_model_with_error(app, &reload_error);
        if (!reloaded) {
            model->dynamic_texture_resolution = old_dynamic_texture_resolution;
            char message[1024];
            snprintf(message, sizeof(message), "%s\n%s", tr(app,
                "pages.preference.cat.hints.dynamicTextureResolutionFailed",
                "Unable to reload the model with the selected texture mode."),
                reload_error.message);
            bongo_cat_preferences_notice_show(app, message, true);
        } else app->dirty = true;
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[memory] texture-mode-result reloaded=%d active=%d",
            reloaded, model->dynamic_texture_resolution);
    }
}

static void update_autostart(BongoCatApp *app, bool old_value, bool old_admin) {
    BongoCatError error = {0};
    if (bongo_cat_platform_set_autostart(app->settings.app.autostart,
        app->settings.app.game_compatibility, &error) == BONGO_CAT_OK) {
        app->settings.app.autostart_admin = app->settings.app.game_compatibility;
        return;
    }
    app->settings.app.autostart = old_value;
    app->settings.app.autostart_admin = old_admin;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    char message[1024];
    snprintf(message, sizeof(message), "%s\n%s", tr(app,
        "pages.preference.general.hints.autostartFailed",
        "Unable to update launch-on-startup settings"), error.message);
    bongo_cat_preferences_notice_show(app, message, true);
}

#ifdef __APPLE__
/* System Settings keeps Input Monitoring under Privacy & Security. */
#define BONGO_CAT_INPUT_MONITORING_SETTINGS_URI \
    "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"

void bongo_cat_preferences_input_monitoring_refresh(BongoCatPreferences *value) {
    if (!value) return;
    /* The refresh points are user actions, not frames: one read here. */
    bool authorized = bongo_cat_platform_input_monitoring_authorized();
    /* A button drawn from the old reading stays on screen until something
       repaints, so a reading that changed - or arrives for the first time -
       schedules the next frame; an unchanged one must not, or every refresh
       point would keep the window rendering. */
    if (value->input_monitoring_valid &&
        value->input_monitoring_authorized == authorized) return;
    value->input_monitoring_authorized = authorized;
    value->input_monitoring_valid = true;
    value->render_dirty = true;
}

static void enable_input_monitoring(BongoCatApp *app) {
    BongoCatPreferences *value = app->preferences;
    if (!value) return;
    /* The permission can change while the page is open, so the button reads it
       once more before acting: an already granted permission hides the button
       instead of asking again or sending the user to System Settings. */
    bongo_cat_preferences_input_monitoring_refresh(value);
    if (value->input_monitoring_authorized) return;
    /* The system prompt belongs to this button and to a missing permission; a
       refusal is an answer, not a failure. */
    (void)bongo_cat_platform_input_monitoring_request();
    /* SDL_OpenURL is the whole entry point, so a failure is where the path has
       to be spelled out: the item itself only says when to restart. */
    if (!SDL_OpenURL(BONGO_CAT_INPUT_MONITORING_SETTINGS_URI))
        bongo_cat_preferences_notice_show(app, tr(app,
            "pages.preference.general.status.openSettingsFailed",
            "Cannot open System Settings. Open Privacy & Security → Input "
            "Monitoring."), true);
    bongo_cat_preferences_input_monitoring_refresh(value);
}

static void input_monitoring_section(BongoCatApp *app,
    struct nk_context *context) {
    /* The page is drawn through the preferences that hold the cached reading; a
       page drawn before the first refresh initializes it once, never per frame. */
    BongoCatPreferences *value = app->preferences;
    if (!value) return;
    if (!value->input_monitoring_valid)
        bongo_cat_preferences_input_monitoring_refresh(value);
    bongo_cat_pref_section_icon(context, tr(app,
        "pages.preference.general.labels.permissionsSettings",
        "Permissions Settings"), BONGO_CAT_PREF_ICON_SECTION_APPLICATION);
    const char *title = tr(app,
        "pages.preference.general.labels.inputMonitoringPermission",
        "Input Monitoring Permission");
    /* Granted is a reading without an action; only a missing permission offers
       the button, and nothing here asks for authorization twice. */
    if (value->input_monitoring_authorized) {
        bongo_cat_pref_status(context, "input-monitoring", title, tr(app,
            "pages.preference.general.status.authorized", "Authorized"));
        return;
    }
    if (bongo_cat_pref_button(context, "input-monitoring", title, tr(app,
        "pages.preference.general.hints.inputMonitoringPermission",
        "Restart after granting it."), tr(app,
        "pages.preference.general.status.authorize", "Go to Enable")))
        enable_input_monitoring(app);
}
#endif

static void page_general(BongoCatApp *app, struct nk_context *context) {
    BongoCatApplicationPreferences *options = &app->settings.app;
    // Keep each option in its own native language so the list is recognizable
    // regardless of the language currently used by the settings window.
    const char *ui_languages[] = {"简体中文", "繁體中文", "English",
        "Français", "Deutsch", "日本語", "한국어", "Português",
        "Русский", "Español"};
    bongo_cat_pref_section_icon(context, tr(app,
        "pages.preference.general.labels.appSettings", "Application"),
        BONGO_CAT_PREF_ICON_SECTION_APPLICATION);
    bool old_autostart = options->autostart;
    bool old_admin = options->autostart_admin;
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_AUTOSTART);
    if (bongo_cat_pref_toggle(context, "autostart", tr(app,
        "pages.preference.general.labels.launchOnStartup", "Launch on Startup"), "",
        &options->autostart)) update_autostart(app, old_autostart, old_admin);
    section_gap(context, 7);
    bongo_cat_pref_section_icon(context, tr(app,
        "pages.preference.general.labels.appearanceSettings", "Appearance"),
        BONGO_CAT_PREF_ICON_SECTION_APPEARANCE);
    section_gap(context, 6);
    const int language_to_ui[] = {2, 0, 1, 3, 4, 5, 6, 7, 8, 9};
    const BongoCatLanguage ui_to_language[] = {
        BONGO_CAT_LANG_ZH_CN, BONGO_CAT_LANG_ZH_HANT,
        BONGO_CAT_LANG_EN_US, BONGO_CAT_LANG_FR_FR,
        BONGO_CAT_LANG_DE_DE, BONGO_CAT_LANG_JA_JP,
        BONGO_CAT_LANG_KO_KR, BONGO_CAT_LANG_PT_BR,
        BONGO_CAT_LANG_RU_RU, BONGO_CAT_LANG_ES_ES};
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_LANGUAGE);
    int selected = bongo_cat_pref_combo(context,
        "language", tr(app, "pages.preference.general.labels.language",
        "Language"), "", ui_languages, BONGO_CAT_LANG_COUNT,
        language_to_ui[options->language]);
    options->language = ui_to_language[selected];
    const char *themes[] = {
        tr(app, "pages.preference.general.options.auto", "System"),
        tr(app, "pages.preference.general.options.lightMode", "Light"),
        tr(app, "pages.preference.general.options.darkMode", "Dark")};
    bongo_cat_pref_row_icon(context, BONGO_CAT_PREF_ICON_THEME);
    options->theme = (BongoCatTheme)bongo_cat_pref_theme(context,
        "theme", tr(app, "pages.preference.general.labels.themeMode",
        "Theme"), themes, options->theme);
#ifdef __APPLE__
    section_gap(context, 7);
    input_monitoring_section(app, context);
#endif
}

void bongo_cat_preferences_page_settings(BongoCatApp *app,
    struct nk_context *context) {
    page_display(app, context);
    section_gap(context, 10);
    page_general(app, context);
}
