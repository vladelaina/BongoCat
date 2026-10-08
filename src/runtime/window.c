#include "runtime.h"
#include "window_menu.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>

void bongo_cat_window_set_icon(BongoCatApp *app, SDL_Window *window) {
    char path[BONGO_CAT_PATH_CAP];
    BongoCatImage icon = {0};
    BongoCatError error = {0};
    if (!window || !bongo_cat_path_join(path, sizeof(path), app->asset_root,
        "logo.png")) return;
    if (bongo_cat_image_load(path, &icon, &error) != BONGO_CAT_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return;
    }
    if (!SDL_SetWindowIcon(window, icon.surface))
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Window icon failed: %s", SDL_GetError());
    bongo_cat_image_free(&icon);
}

static bool set_gl_attributes(bool multisampling) {
    SDL_GL_ResetAttributes();
#ifdef __APPLE__
    const int major = 4, minor = 1, profile = SDL_GL_CONTEXT_PROFILE_CORE;
#elif defined(BONGO_CAT_HAS_CUBISM)
    const int major = 3, minor = 3, profile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
#else
    const int major = 3, minor = 3, profile = SDL_GL_CONTEXT_PROFILE_CORE;
#endif
    return SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major) &&
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor) &&
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile) &&
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) &&
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0) &&
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8) &&
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, multisampling ? 1 : 0) &&
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisampling ? 4 : 0);
}

static bool try_window(BongoCatApp *app, bool transparent, bool multisampling,
    char *failure, size_t capacity) {
    if (!set_gl_attributes(multisampling)) {
        snprintf(failure, capacity, "OpenGL attributes: %s", SDL_GetError()); return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (transparent) flags |= SDL_WINDOW_TRANSPARENT;
    app->window = SDL_CreateWindow(BONGO_CAT_PET_WINDOW_TITLE,
        app->config.window.width,
        app->config.window.height, flags);
    if (!app->window) {
        snprintf(failure, capacity, "Window creation: %s", SDL_GetError()); return false;
    }
    app->gl_context = SDL_GL_CreateContext(app->window);
    if (!app->gl_context || !SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        snprintf(failure, capacity, "OpenGL context: %s", SDL_GetError());
        if (app->gl_context) SDL_GL_DestroyContext(app->gl_context);
        SDL_DestroyWindow(app->window); app->gl_context = NULL; app->window = NULL;
        return false;
    }
    return true;
}

BongoCatResult bongo_cat_window_create(BongoCatApp *app, BongoCatError *error) {
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "SDL initialization failed: %s", SDL_GetError());
        return BONGO_CAT_ERROR_PLATFORM;
    }
    const bool options[][2] = {{true, true}, {true, false}, {false, false}};
    char failure[256] = {0};
    bool force_fallback = SDL_getenv("BONGO_CAT_TEST_GL_FALLBACK") != NULL;
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        if (force_fallback && i == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Test requested OpenGL fallback"); continue;
        }
        if (try_window(app, options[i][0], options[i][1], failure, sizeof(failure))) {
            int sample_buffers = 0, sample_count = 0;
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sample_buffers);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &sample_count);
            SDL_Log("OpenGL window ready (transparent=%d, MSAA=%d, "
                "sample_buffers=%d, sample_count=%d)", options[i][0], options[i][1],
                sample_buffers, sample_count);
            if (!SDL_GL_SetSwapInterval(1)) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Vertical sync unavailable: %s", SDL_GetError());
            return BONGO_CAT_OK;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "OpenGL attempt %llu failed: %s",
            (unsigned long long)(i + 1), failure);
    }
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Window and OpenGL initialization failed after compatibility retries: %s", failure);
    return BONGO_CAT_ERROR_PLATFORM;
}

void bongo_cat_window_apply(BongoCatApp *app) {
    BongoCatWindowOptions *value = &app->config.window;
    bongo_cat_platform_set_opacity(&app->platform,
        value->opacity_percent / 100.0f);
    SDL_SetWindowSize(app->window, value->width, value->height);
    if (value->x || value->y) SDL_SetWindowPosition(app->window, value->x, value->y);
    SDL_SyncWindow(app->window);
    if (value->keep_in_screen) bongo_cat_window_clamp_to_display(app);
    else bongo_cat_window_recover_to_display(app);
    SDL_SyncWindow(app->window);
    bongo_cat_platform_set_visible(&app->platform, value->visible);
    bongo_cat_window_sync_click_through(app);
    bongo_cat_platform_set_always_on_top(&app->platform, value->always_on_top);
}

static const char *tr(BongoCatApp *app, const char *key, const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static void context_menu(BongoCatApp *app) {
    BongoCatWindowMenuPreview preview;
    bongo_cat_window_menu_preview_init(&preview, app);
    const char *model_names[BONGO_CAT_MODEL_CAP];
    size_t current_model = app->models.count;
    for (size_t i = 0; i < app->models.count; ++i) {
        model_names[i] = app->models.entries[i].id;
        if (strcmp(app->models.entries[i].id, app->config.current_model) == 0)
            current_model = i;
    }
    const char *motion_names[BONGO_CAT_BEHAVIOR_CAP];
    const char *expression_names[BONGO_CAT_BEHAVIOR_CAP];
    size_t motion_count, expression_count, current_expression;
    bongo_cat_window_behavior_labels(app, motion_names, &motion_count,
        expression_names, &expression_count, &current_expression);
    bool dark_theme = app->config.app.theme == BONGO_CAT_THEME_DARK ||
        (app->config.app.theme == BONGO_CAT_THEME_AUTO &&
            SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
    BongoCatMenuLabels labels = {
        tr(app, "composables.useAppMenu.labels.preference", "Preferences"),
        tr(app, "composables.useAppMenu.labels.hideCat", "Hide Cat"),
        tr(app, "composables.useAppMenu.labels.passThrough", "Pass Through"),
        tr(app, "composables.useAppMenu.labels.alwaysOnTop", "Always on top"),
        tr(app, "composables.useAppMenu.labels.windowSize", "Window Size"),
        tr(app, "composables.useAppMenu.labels.opacity", "Opacity"),
        tr(app, "composables.useAppMenu.labels.model", "Model"),
        tr(app, "composables.useAppMenu.labels.addModel", "Add Model"),
        tr(app, "composables.useAppMenu.labels.quitApp", "Exit"),
        tr(app, "composables.useAppMenu.labels.wheelSizeHint", "Wheel: resize"),
        tr(app, "composables.useAppMenu.labels.wheelOpacityHint", "Ctrl+Wheel: opacity"),
        tr(app, "composables.useAppMenu.labels.motion", "Motions"),
        tr(app, "composables.useAppMenu.labels.expression", "Expressions"),
        model_names, motion_names, expression_names,
        app->models.count, current_model, motion_count, expression_count,
        current_expression,
        app->config.window.scale_percent, app->config.window.opacity_percent,
        app->config.window.pass_through, app->config.window.always_on_top,
        dark_theme,
        bongo_cat_window_menu_preview, bongo_cat_window_menu_preview_tick,
        bongo_cat_window_menu_restore, &preview};
    BongoCatMenuAction action = bongo_cat_platform_context_menu(&app->platform, &labels);
    bongo_cat_window_menu_action(app, action);
}

void bongo_cat_window_show_context_menu(BongoCatApp *app) { context_menu(app); }

void bongo_cat_window_menu_action(BongoCatApp *app, BongoCatMenuAction action) {
    if (action == BONGO_CAT_MENU_PREFERENCES) bongo_cat_preferences_show(app->preferences);
    else if (action == BONGO_CAT_MENU_MODEL_ADD)
        bongo_cat_preferences_open_model_import(app->preferences, app->window);
    else if (action == BONGO_CAT_MENU_HIDE)
        bongo_cat_window_set_visible(app, false);
    else if (action == BONGO_CAT_MENU_PASS_THROUGH) {
        app->config.window.pass_through = !app->config.window.pass_through;
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    } else if (action == BONGO_CAT_MENU_ALWAYS_ON_TOP) {
        app->config.window.always_on_top = !app->config.window.always_on_top;
        bongo_cat_platform_set_always_on_top(&app->platform, app->config.window.always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
    } else if (action >= BONGO_CAT_MENU_SCALE_50 && action <= BONGO_CAT_MENU_SCALE_200) {
        bongo_cat_window_cancel_wheel_animation(app);
        bongo_cat_window_set_scale(app,
            (float)(50 + 10 * (action - BONGO_CAT_MENU_SCALE_50)));
    } else if (action >= BONGO_CAT_MENU_OPACITY_10 && action <= BONGO_CAT_MENU_OPACITY_100) {
        bongo_cat_window_cancel_wheel_animation(app);
        app->config.window.opacity_percent = (float)(10 * (action-BONGO_CAT_MENU_OPACITY_10+1));
        bongo_cat_platform_set_opacity(&app->platform,
            app->config.window.opacity_percent / 100.0f);
    } else if (bongo_cat_window_behavior_action(app, action)) {
        bongo_cat_app_render_now(app);
    } else if (action >= BONGO_CAT_MENU_MODEL_FIRST &&
        action < BONGO_CAT_MENU_MODEL_FIRST + BONGO_CAT_MODEL_CAP) {
        size_t index = (size_t)(action - BONGO_CAT_MENU_MODEL_FIRST);
        if (index < app->models.count)
            bongo_cat_app_select_model(app, app->models.entries[index].id);
    } else if (action == BONGO_CAT_MENU_EXIT) app->running = false;
    bongo_cat_preferences_invalidate(app->preferences);
}

bool bongo_cat_window_menu_self_test(BongoCatApp *app) {
    if (!app || !app->preferences) return false;
    app->config.window.pass_through = false;
    app->config.window.always_on_top = false;
    app->config.window.scale_percent = 100.0f;
    app->config.window.opacity_percent = 100.0f;
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_PASS_THROUGH);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_ALWAYS_ON_TOP);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_SCALE_120);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_OPACITY_50);
    bongo_cat_window_menu_action(app, BONGO_CAT_MENU_PREFERENCES);
    bool behavior = bongo_cat_window_behavior_self_test(app);
    bool result = app->config.window.pass_through && app->config.window.always_on_top &&
        app->config.window.scale_percent == 120.0f &&
        app->config.window.opacity_percent == 50.0f &&
        bongo_cat_preferences_visible(app->preferences) && behavior;
    bongo_cat_preferences_close(app->preferences);
    return result;
}

bool bongo_cat_window_event(BongoCatApp *app, const SDL_Event *event) {
    bongo_cat_window_display_event(app, event);
    if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST &&
        event->window.windowID != SDL_GetWindowID(app->window)) return true;
    if (event->type == SDL_EVENT_QUIT) return false;
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        bongo_cat_window_set_visible(app, false);
        return true;
    }
    if (event->type == SDL_EVENT_WINDOW_HIDDEN) {
        bongo_cat_window_drag_end(app);
    }
    if (event->type == SDL_EVENT_WINDOW_MINIMIZED) {
        app->window_minimized = true;
        bongo_cat_window_drag_end(app);
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        app->config.window.width = event->window.data1;
        app->config.window.height = event->window.data2;
        bongo_cat_window_clamp_to_display(app);
        app->dirty = true;
    }
    if (event->type == SDL_EVENT_WINDOW_EXPOSED ||
        event->type == SDL_EVENT_WINDOW_SHOWN ||
        event->type == SDL_EVENT_WINDOW_RESTORED) {
        if (event->type != SDL_EVENT_WINDOW_EXPOSED)
            app->window_minimized = false;
        bongo_cat_app_reset_pointer_tracking(app);
    }
    if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
        event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        bongo_cat_app_reset_pointer_tracking(app);
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetWindowSizeInPixels(app->window,
            &app->resize_pixel_width, &app->resize_pixel_height);
        app->resize_pending = true;
        bongo_cat_window_mark_hit_dirty(app);
    } else if (event->type == SDL_EVENT_WINDOW_MOVED) {
        app->config.window.x = event->window.data1;
        app->config.window.y = event->window.data2;
        app->pointer_known = false;
        if (!app->window_drag_active) bongo_cat_window_clamp_to_display(app);
        bongo_cat_window_mark_hit_dirty(app);
    } else if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        bongo_cat_app_reset_pointer_tracking(app);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        bongo_cat_window_drag_begin(app, &event->button);
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        bongo_cat_window_resize_by_pointer(app, event);
        bongo_cat_window_drag_motion(app, &event->motion);
    } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        bongo_cat_window_wheel(app, &event->wheel);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_LEFT) {
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_drag_end(app);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_RIGHT) {
        bongo_cat_window_mark_hit_dirty(app);
        if (app->resize_gesture) app->resize_gesture = false;
        else context_menu(app);
    }
    return true;
}

void bongo_cat_window_destroy(BongoCatApp *app) {
    bongo_cat_window_drag_end(app);
    if (app->gl_context) SDL_GL_DestroyContext(app->gl_context);
    if (app->window) SDL_DestroyWindow(app->window);
    app->gl_context = NULL;
    app->window = NULL;
    SDL_Quit();
}
