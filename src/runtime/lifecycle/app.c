#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/file.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void scan_models(BongoCatApp *app) {
    bongo_cat_app_rescan_models(app);
}
static bool load_selected_model(BongoCatApp *app, BongoCatError *error) {
    const char *candidates[] = {app->session.active_model_id, "standard", "keyboard", "gamepad"};
    for (size_t i = 0; i < 4; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j)
            if (strcmp(candidates[i], candidates[j]) == 0) duplicate = true;
        if (!duplicate && bongo_cat_models_find(&app->models, candidates[i]) &&
            bongo_cat_app_select_model(app, candidates[i])) {
            if (i) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Selected model was unavailable; loaded fallback model %s", candidates[i]);
            return true;
        }
    }
    for (size_t i = 0; i < app->models.count; ++i)
        if (bongo_cat_app_select_model(app, app->models.entries[i].id)) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "No usable Live2D model could be loaded");
    return false;
}
bool bongo_cat_app_initialize(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error) {
    memset(app, 0, sizeof(*app));
    if (argc > 0 && argv && argv[0])
        snprintf(app->executable_path, sizeof(app->executable_path),
            "%s", argv[0]);
    app->smoke_language = -1;
    app->smoke_theme = -1;
    app->smoke_preference_page = -1;
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    bongo_cat_input_init(&app->input);
    bongo_cat_shortcut_init(&app->shortcut_state);
    bongo_cat_models_init(&app->models);
    if (!bongo_cat_startup_prepare(app, argc, argv, error)) return false;
    bongo_cat_config_store_load(app);
    if (app->secondary_pet) {
        snprintf(app->session.active_model_id,
            sizeof(app->session.active_model_id), "%s",
            app->secondary_model_id);
        bongo_cat_session_clear_additional_models(&app->session);
        if (!app->session_store_valid && app->secondary_origin_known) {
            app->session.window.position_known = true;
            app->session.window.x = app->secondary_origin_x;
            app->session.window.y = app->secondary_origin_y;
        }
        /* The control file reveals the child on its first update. Starting
           hidden prevents a stale child session from flashing on screen. */
        app->session.window.visible = false;
    } else if (!app->settings.model.multiple_pets)
        bongo_cat_session_clear_additional_models(&app->session);
    if (app->smoke_language >= 0)
        app->settings.app.language = (BongoCatLanguage)app->smoke_language;
    if (app->smoke_theme >= 0) app->settings.app.theme = (BongoCatTheme)app->smoke_theme;
    if (app->smoke_pass_through) app->settings.window.pass_through = true;
    if (app->smoke_model[0])
        snprintf(app->session.active_model_id, sizeof(app->session.active_model_id),
            "%s", app->smoke_model);
    if (!app->secondary_pet && !app->autostart_launch)
        app->session.window.visible = true;
    /* Keep a normally visible session hidden until the renderer has produced
       its first complete frame. The native window is still configured and
       sized below, but no platform gets a chance to expose an empty back
       buffer during model/driver startup. */
    app->startup_visibility_pending = app->session.window.visible;
    bongo_cat_startup_stage(app, "configuration-ready");
    if (bongo_cat_app_locate_assets(app, error) != BONGO_CAT_OK) return false;
    bongo_cat_startup_stage(app, "assets-ready");
    BongoCatError optional = {0};
    app->i18n = bongo_cat_i18n_create(app->locale_root, app->settings.app.language, &optional);
    if (!app->i18n) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    if (bongo_cat_window_create(app, error) != BONGO_CAT_OK) return false;
    bongo_cat_startup_stage(app, "window-ready");
    if (bongo_cat_platform_init(&app->platform, app->window, &app->input, error) != BONGO_CAT_OK) return false;
    bongo_cat_window_apply(app);
    bongo_cat_startup_stage(app, "platform-ready");
    app->live2d = bongo_cat_live2d_create(app->asset_root, error);
    if (!app->live2d) return false;
    optional = (BongoCatError){0}; app->overlay = bongo_cat_overlay_create(&optional);
    if (!app->overlay) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Overlay disabled: %s", optional.message);
    optional = (BongoCatError){0}; app->audio = bongo_cat_audio_create(&optional);
    if (!app->audio) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", optional.message);
    else bongo_cat_audio_set_enabled(app->audio, true);
    scan_models(app);
    if (!load_selected_model(app, error)) return false;
    bongo_cat_startup_stage(app, "model-ready");
    if (app->smoke_import_path[0]) {
        BongoCatError import_error = {0};
        if (bongo_cat_app_import_model(app, app->smoke_import_path, &import_error) != BONGO_CAT_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", import_error.message);
            bongo_cat_startup_ci_failure(app, &import_error);
        } else if (app->smoke_remove_imported) {
            char imported[BONGO_CAT_PATH_CAP];
            snprintf(imported, sizeof(imported), "%s", app->session.active_model_id);
            if (bongo_cat_app_remove_model(app, imported, &import_error) != BONGO_CAT_OK)
                bongo_cat_startup_ci_failure(app, &import_error);
        }
    }
    app->running = true;
    optional = (BongoCatError){0}; app->tray = app->secondary_pet ? NULL :
        bongo_cat_tray_create(app, &optional);
    if (!app->secondary_pet && !app->tray && app->settings.app.tray_visible)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    app->preferences = app->secondary_pet ? NULL :
        bongo_cat_preferences_create(app);
    if (!app->secondary_pet && !app->preferences)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Preferences are unavailable because their state could not be allocated");
    app->update = app->secondary_pet ? NULL : bongo_cat_update_create(app);
    if (!app->secondary_pet && !app->update)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "The update checker could not be initialized");
    bongo_cat_update_start_automatic(app->update);
    if (!app->secondary_pet && !app->tray && !app->session.window.visible) {
        bongo_cat_window_set_visible(app, true);
    }
    bongo_cat_live2d_audit_run(app);
    if (app->smoke_context_menu) bongo_cat_window_show_context_menu(app);
    if (app->smoke_shortcuts && !bongo_cat_app_shortcuts_self_test(app)) {
        BongoCatError shortcut_error = {0};
        bongo_cat_error_set(&shortcut_error, BONGO_CAT_ERROR_PLATFORM, "Shortcut action self-test failed");
        bongo_cat_startup_ci_failure(app, &shortcut_error);
    }
    if (app->smoke_menu) {
        bool menu = bongo_cat_window_menu_self_test(app);
        bool geometry = bongo_cat_window_geometry_self_test(app);
        bool display = bongo_cat_window_display_self_test(app);
        bool wheel = bongo_cat_window_wheel_self_test(app);
        bool tray = bongo_cat_tray_self_test(app->tray);
        bool wait = bongo_cat_window_wait_timeout_self_test();
        SDL_Log("Menu self-test: menu=%d geometry=%d display=%d wheel=%d tray=%d wait=%d",
            menu, geometry, display, wheel, tray, wait);
        if (!menu || !geometry || !display || !wheel || !tray || !wait) {
            BongoCatError menu_error = {0};
            bongo_cat_error_set(&menu_error, BONGO_CAT_ERROR_PLATFORM,
                "Context menu action self-test failed (menu=%d geometry=%d display=%d wheel=%d tray=%d wait=%d)",
                menu, geometry, display, wheel, tray, wait);
            bongo_cat_startup_ci_failure(app, &menu_error);
        }
    }
    if (app->smoke_preferences) bongo_cat_preferences_show(app->preferences);
    app->last_frame_ns = SDL_GetTicksNS();
    app->dirty = true;
    app->startup_raise_due_ns = !app->secondary_pet && !app->smoke &&
        !app->autostart_launch && app->session.window.visible ?
        app->last_frame_ns + 250000000ull : 0;
    if (app->smoke_deadline_ns) app->smoke_deadline_ns += app->last_frame_ns;
    if (!app->session.window.visible) bongo_cat_startup_ready(app);
    return true;
}
