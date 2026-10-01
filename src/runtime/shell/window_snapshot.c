#include "runtime.h"

#ifdef _WIN32
#include "windows_snapshot.h"
#endif

#define SNAPSHOT_IDLE_NS 180000000ull

void bongo_cat_window_snapshot_begin(BongoCatApp *app) {
    if (!app) return;
    app->snapshot_deadline_ns = SDL_GetTicksNS() + SNAPSHOT_IDLE_NS;
    if (app->window_snapshot || app->snapshot_blocked) return;
#ifdef _WIN32
    if (!app->window || !app->live2d || !app->session.window.visible ||
        app->startup_visibility_pending || app->window_minimized || app->hover_hidden ||
        app->hover_fade_active ||
        app->settings.window.pass_through ||
        !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_TRANSPARENT)) return;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    if (!SDL_GL_MakeCurrent(app->window, app->gl_context)) return;
    /* Capture a complete, up-to-date frame before changing native geometry. */
    bongo_cat_app_render_now(app);
    if (!app->render_retry_ns) {
        app->window_snapshot = bongo_cat_windows_snapshot_create(app->window,
            app->platform.window_opacity);
        if (!app->window_snapshot) {
            app->snapshot_blocked = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Interaction snapshot unavailable; using live rendering: %s", SDL_GetError());
        }
    }
    SDL_GetGlobalMouseState(&app->snapshot_pointer_x, &app->snapshot_pointer_y);
    if (previous_window && previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
#endif
}

void bongo_cat_window_snapshot_discard(BongoCatApp *app) {
    if (!app || !app->window_snapshot) return;
#ifdef _WIN32
    bongo_cat_windows_snapshot_destroy(app->window_snapshot);
#endif
    app->window_snapshot = NULL;
    app->last_frame_ns = SDL_GetTicksNS();
    app->dirty = true;
    bongo_cat_window_mark_hit_dirty(app);
}

void bongo_cat_window_snapshot_end(BongoCatApp *app) {
    if (!app || !app->window_snapshot) return;
    void *snapshot = app->window_snapshot;
    app->window_snapshot = NULL;
    app->snapshot_blocked = true;
    app->last_frame_ns = SDL_GetTicksNS();
    app->dirty = true;
    /* Keep the preview visible until the final real frame is submitted. */
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bongo_cat_app_render_now(app);
#ifdef _WIN32
    bongo_cat_windows_snapshot_destroy(snapshot);
#else
    (void)snapshot;
#endif
    if (previous_window && previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
    bongo_cat_window_mark_hit_dirty(app);
}

void bongo_cat_window_snapshot_update(BongoCatApp *app, uint64_t now) {
    if (!app) return;
    if (!app->window_snapshot) {
        if (!app->window_drag_active && !app->wheel_animation_active &&
            now >= app->snapshot_deadline_ns) app->snapshot_blocked = false;
        return;
    }
#ifdef _WIN32
    bongo_cat_windows_snapshot_pointer(app->window_snapshot);
#endif
    bool finish = !app->session.window.visible || app->window_minimized ||
        app->settings.window.pass_through || app->hover_hidden;
    if (!app->window_drag_active) {
        float x, y;
        SDL_GetGlobalMouseState(&x, &y);
        float dx = x - app->snapshot_pointer_x, dy = y - app->snapshot_pointer_y;
        finish = finish || dx * dx + dy * dy >= 4.0f ||
            (!app->wheel_animation_active && now >= app->snapshot_deadline_ns);
    }
    if (finish) bongo_cat_window_snapshot_end(app);
    else if (app->window_drag_active &&
        !(SDL_GetGlobalMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
        bongo_cat_window_drag_end(app);
}

void bongo_cat_window_snapshot_geometry(BongoCatApp *app, const SDL_FRect *rect) {
#ifdef _WIN32
    if (app) bongo_cat_windows_snapshot_geometry(app->window_snapshot, rect);
#else
    (void)app; (void)rect;
#endif
}

void bongo_cat_window_snapshot_present(BongoCatApp *app) {
    if (!app || !app->window_snapshot) return;
#ifdef _WIN32
    if (!bongo_cat_windows_snapshot_present(app->window_snapshot)) {
        app->snapshot_blocked = true;
        bongo_cat_window_snapshot_discard(app);
        return;
    }
#endif
    app->dirty = false;
}

bool bongo_cat_window_snapshot_hit(BongoCatApp *app, float x, float y) {
#ifdef _WIN32
    return app && bongo_cat_windows_snapshot_hit(app->window_snapshot, x, y);
#else
    (void)app; (void)x; (void)y;
    return false;
#endif
}
