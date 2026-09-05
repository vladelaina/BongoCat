#include "runtime.h"
#include <SDL3/SDL_opengl.h>
#include <stdio.h>

static bool set_gl_attributes(int samples) {
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
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 0 ? 1 : 0) &&
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
}

static bool try_window(BongoCatApp *app, bool transparent, int samples,
    char *failure, size_t capacity) {
    if (!set_gl_attributes(samples)) {
        snprintf(failure, capacity, "OpenGL attributes: %s", SDL_GetError()); return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (transparent) flags |= SDL_WINDOW_TRANSPARENT;
    app->window = SDL_CreateWindow(BONGO_CAT_PET_WINDOW_TITLE,
        app->session.window.width,
        app->session.window.height, flags);
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
#ifdef _WIN32
    /* Transparent OpenGL windows must never receive SDL's default black
       WM_ERASEBKGND fill before the first frame is submitted. */
    SDL_SetHint(SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "SDL initialization failed: %s", SDL_GetError());
        return BONGO_CAT_ERROR_PLATFORM;
    }
    /* Keep a lower-cost MSAA path for drivers that cannot provide 4 samples. */
    const int options[][2] = {{true, 4}, {true, 2}, {true, 0}, {false, 0}};
    char failure[256] = {0};
    bool force_fallback = SDL_getenv("BONGO_CAT_TEST_GL_FALLBACK") != NULL;
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        if (force_fallback && options[i][1] > 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Test requested OpenGL fallback"); continue;
        }
        if (try_window(app, options[i][0], options[i][1], failure, sizeof(failure))) {
            int sample_buffers = 0, sample_count = 0;
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sample_buffers);
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &sample_count);
            const GLubyte *vendor = glGetString(GL_VENDOR);
            const GLubyte *renderer = glGetString(GL_RENDERER);
            const GLubyte *version = glGetString(GL_VERSION);
            SDL_Log("[runtime] OpenGL window ready (transparent=%d, MSAA=%d, "
                "sample_buffers=%d, sample_count=%d)", options[i][0], options[i][1],
                sample_buffers, sample_count);
            SDL_Log("[runtime] OpenGL context: vendor=%s renderer=%s version=%s",
                vendor ? (const char *)vendor : "unknown",
                renderer ? (const char *)renderer : "unknown",
                version ? (const char *)version : "unknown");
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
    BongoCatWindowPreferences *preferences = &app->settings.window;
    BongoCatWindowState *state = &app->session.window;
    bongo_cat_platform_set_opacity(&app->platform,
        state->opacity_percent / 100.0f);
    SDL_SetWindowSize(app->window, state->width, state->height);
    if (state->position_known)
        SDL_SetWindowPosition(app->window, state->x, state->y);
    SDL_SyncWindow(app->window);
    if (preferences->keep_in_screen) bongo_cat_window_clamp_to_display(app);
    else bongo_cat_window_recover_to_display(app);
    SDL_SyncWindow(app->window);
    /* A visible session is revealed by the first successful frame. Keeping
       the native window hidden while loading avoids exposing an uninitialised
       (and on some drivers black) back buffer. */
    bongo_cat_platform_set_visible(&app->platform,
        state->visible && !app->startup_visibility_pending);
    bongo_cat_window_sync_click_through(app);
    bongo_cat_platform_set_always_on_top(&app->platform,
        preferences->always_on_top);
}

static bool event_targets_main_window(BongoCatApp *app,
    const SDL_Event *event) {
    SDL_WindowID id = SDL_GetWindowID(app->window);
    if (event->type >= SDL_EVENT_WINDOW_FIRST &&
        event->type <= SDL_EVENT_WINDOW_LAST)
        return event->window.windowID == id;
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        return event->motion.windowID == id || app->window_drag_active ||
            app->drag_candidate || app->resize_gesture;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return event->button.windowID == id;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event->button.windowID == id || app->window_drag_active ||
            app->drag_candidate || app->resize_gesture;
    case SDL_EVENT_MOUSE_WHEEL:
        return event->wheel.windowID == id;
    default:
        return true;
    }
}

bool bongo_cat_window_event(BongoCatApp *app, const SDL_Event *event) {
    bongo_cat_window_display_event(app, event);
    if (!event_targets_main_window(app, event)) return true;
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
        app->session.window.width = event->window.data1;
        app->session.window.height = event->window.data2;
        bongo_cat_window_content_size(app, event->window.data1,
            event->window.data2, &app->session.window.content_width,
            &app->session.window.content_height);
        bongo_cat_window_clamp_to_display(app);
        app->dirty = true;
    }
    if (event->type == SDL_EVENT_WINDOW_EXPOSED ||
        event->type == SDL_EVENT_WINDOW_SHOWN ||
        event->type == SDL_EVENT_WINDOW_RESTORED) {
        if (event->type != SDL_EVENT_WINDOW_EXPOSED)
            app->window_minimized = false;
        bongo_cat_app_reset_pointer_tracking(app);
        /* DWM can discard the transparent redirection surface after an
           Explorer/display refresh. Repaint even when the model is idle so
           the restored alpha surface is submitted immediately. */
        app->dirty = true;
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
        app->session.window.x = event->window.data1;
        app->session.window.y = event->window.data2;
        app->session.window.position_known = true;
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
        else bongo_cat_window_show_context_menu(app);
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
