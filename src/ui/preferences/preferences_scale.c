#include "preferences_state.h"
#include "preferences_fonts.h"
#include "preferences_gl.h"
#include "ui_catime.h"
#include "bongo_cat/memory_policy.h"
#include <SDL3/SDL_opengl.h>
#include <math.h>

#define PREF_WIDTH 900.0f
#define PREF_HEIGHT 680.0f
#define PREF_MIN_WIDTH 720.0f
#define PREF_MIN_HEIGHT 560.0f
#define PREF_SCREEN_MARGIN 48.0f

static SDL_DisplayID window_display(SDL_Window *window) {
    SDL_DisplayID display = window ? SDL_GetDisplayForWindow(window) : 0;
    return display ? display : SDL_GetPrimaryDisplay();
}

static int scaled(float value, float scale) {
    return SDL_max(1, (int)lroundf(value * scale));
}

static void fit_size(SDL_DisplayID display, float layout_scale,
    float logical_width, float logical_height, int *width, int *height,
    int *minimum_width, int *minimum_height) {
    *width = scaled(logical_width, layout_scale);
    *height = scaled(logical_height, layout_scale);
    *minimum_width = scaled(PREF_MIN_WIDTH, layout_scale);
    *minimum_height = scaled(PREF_MIN_HEIGHT, layout_scale);
    SDL_Rect usable;
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable)) return;
    int margin = scaled(PREF_SCREEN_MARGIN, layout_scale);
    int maximum_width = SDL_max(1, usable.w - margin);
    int maximum_height = SDL_max(1, usable.h - margin);
    *minimum_width = SDL_min(*minimum_width, maximum_width);
    *minimum_height = SDL_min(*minimum_height, maximum_height);
    *width = SDL_clamp(*width, *minimum_width, maximum_width);
    *height = SDL_clamp(*height, *minimum_height, maximum_height);
}

static void fit_position(SDL_Window *window, SDL_DisplayID display,
    int width, int height) {
    SDL_Rect usable;
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable)) return;
    int x = 0, y = 0;
    SDL_GetWindowPosition(window, &x, &y);
    int maximum_x = usable.x + SDL_max(0, usable.w - width);
    int maximum_y = usable.y + SDL_max(0, usable.h - height);
    int next_x = SDL_clamp(x, usable.x, maximum_x);
    int next_y = SDL_clamp(y, usable.y, maximum_y);
    if (next_x != x || next_y != y)
        SDL_SetWindowPosition(window, next_x, next_y);
}

static SDL_HitTestResult SDLCALL preference_hit_test(SDL_Window *window,
    const SDL_Point *point, void *data) {
    BongoCatPreferences *value = data;
    if (!value || !point || bongo_cat_preferences_remove_dialog_active(value->app))
        return SDL_HITTEST_NORMAL;
    float scale = value->ui.layout_scale > 0.0f ? value->ui.layout_scale : 1.0f;
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    int edge = scaled(7.0f, scale);
#ifdef __APPLE__
    /* AppKit owns the traffic-light controls in the native titlebar. Do not
       classify their hit area as a resize corner from SDL's borderless hit
       test callback. */
    if (point->x < scaled(100.0f, scale) && point->y < scaled(40.0f, scale))
        return SDL_HITTEST_NORMAL;
#endif
    bool left = point->x < edge, right = point->x >= width - edge;
    bool top = point->y < edge, bottom = point->y >= height - edge;
    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    // Title dragging is handled from SDL events so the application loop keeps
    // advancing the desktop pet instead of entering a native modal move loop.
    return SDL_HITTEST_NORMAL;
}

static void discard_window(BongoCatPreferences *value) {
    bongo_cat_preferences_gl_destroy(value);
    if (value->window) SDL_DestroyWindow(value->window);
    value->window = NULL;
    value->transparent_window = false;
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
}

static bool create_window(BongoCatPreferences *value, int width, int height,
    SDL_WindowFlags flags, bool transparent) {
    const char *title = bongo_cat_i18n_get(value->app->i18n,
        "native.preferencesWindowTitle", "BongoCat - Settings");
    value->window = SDL_CreateWindow(title,
        width, height,
        flags | (transparent ? SDL_WINDOW_TRANSPARENT : 0));
    value->transparent_window = value->window && transparent &&
        (SDL_GetWindowFlags(value->window) & SDL_WINDOW_TRANSPARENT) != 0;
    return value->window != NULL;
}

bool bongo_cat_preferences_open_window(BongoCatPreferences *value) {
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    float layout_scale = bongo_cat_ui_display_layout_scale(display);
    int width, height, minimum_width, minimum_height;
    fit_size(display, layout_scale, PREF_WIDTH, PREF_HEIGHT, &width, &height,
        &minimum_width, &minimum_height);
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE, "0");
#endif
    if (!create_window(value, width, height, flags, true)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Transparent preferences window unavailable: %s", SDL_GetError());
        if (!create_window(value, width, height, flags, false)) return false;
    }
    bongo_cat_platform_configure_preferences_window(value->window);
    SDL_SetWindowPosition(value->window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SyncWindow(value->window);
    bongo_cat_platform_configure_preferences_window(value->window);
    display = window_display(value->window);
    float raster_scale = 1.0f;
    bongo_cat_ui_query_window_scale(value->window, &layout_scale, &raster_scale);
    fit_size(display, layout_scale, PREF_WIDTH, PREF_HEIGHT, &width, &height,
        &minimum_width, &minimum_height);
    SDL_SetWindowMinimumSize(value->window, minimum_width, minimum_height);
    SDL_SetWindowSize(value->window, width, height);
    SDL_SetWindowPosition(value->window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SyncWindow(value->window);
    bongo_cat_platform_configure_preferences_window(value->window);
    bool context_ready = bongo_cat_preferences_gl_create(value);
    if (!context_ready && value->transparent_window) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Transparent preferences OpenGL context unavailable: %s",
            SDL_GetError());
        discard_window(value);
        if (!create_window(value, width, height, flags, false)) return false;
        SDL_SetWindowMinimumSize(value->window, minimum_width, minimum_height);
        SDL_SetWindowPosition(value->window,
            SDL_WINDOWPOS_CENTERED_DISPLAY(display),
            SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        SDL_SyncWindow(value->window);
        bongo_cat_platform_configure_preferences_window(value->window);
        context_ready = bongo_cat_preferences_gl_create(value);
    }
    if (!context_ready) return false;
    BongoCatPreferenceFonts fonts;
    bongo_cat_preferences_fonts_resolve(value, &fonts);
    BongoCatError error = {0};
    if (!bongo_cat_ui_init(&value->ui, value->window, fonts.body,
        fonts.body_fallback, fonts.body_korean_fallback, fonts.heading,
        fonts.heading_fallback, fonts.heading_korean_fallback, fonts.ranges,
        layout_scale, raster_scale, &error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    value->ui_initialized = true;
    SDL_SetWindowHitTest(value->window, preference_hit_test, value);
    value->native_drag = false;
    bongo_cat_preferences_assets_load(value);
    bongo_cat_memory_policy_ui_loaded();
    value->style_theme = -1;
    value->font_language = value->app->settings.app.language;
    int pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSizeInPixels(value->window, &pixel_width, &pixel_height);
    SDL_Log("Preferences GL ready: dedicated=%d transparent=%d pixels=%dx%d layout=%.2f raster=%.2f",
        value->owns_gl_context, value->transparent_window,
        pixel_width, pixel_height,
        layout_scale, raster_scale);
    if (!SDL_GL_SetSwapInterval(0)) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Preferences VSync disable unavailable: %s", SDL_GetError());
    SDL_StartTextInput(value->window);
    value->render_dirty = true;
    bongo_cat_preferences_live_resize_install(value);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    return true;
}

bool bongo_cat_preferences_scale_event(BongoCatPreferences *value,
    const SDL_Event *event) {
    if (!value || !value->window || !event ||
        (event->type != SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED &&
        event->type != SDL_EVENT_WINDOW_DISPLAY_CHANGED)) return false;
    float old_layout = value->ui.layout_scale > 0.0f ? value->ui.layout_scale : 1.0f;
    float old_raster = value->ui.raster_scale > 0.0f ? value->ui.raster_scale : 1.0f;
    int current_width = 1, current_height = 1;
    SDL_GetWindowSize(value->window, &current_width, &current_height);
    float logical_width = current_width / old_layout;
    float logical_height = current_height / old_layout;
    float layout_scale = 1.0f, raster_scale = 1.0f;
    bongo_cat_ui_query_window_scale(value->window, &layout_scale, &raster_scale);
    SDL_DisplayID display = window_display(value->window);
    int width, height, minimum_width, minimum_height;
    fit_size(display, layout_scale, logical_width, logical_height,
        &width, &height, &minimum_width, &minimum_height);
    bool was_rendering = value->live_resize_rendering;
    value->live_resize_rendering = true;
    bool context_ready = SDL_GL_MakeCurrent(value->window, value->gl_context);
    value->ui.layout_scale = layout_scale;
    if (fabsf(raster_scale - old_raster) > 0.01f) {
        value->pending_raster_scale = raster_scale;
        value->raster_retry_ns = 0;
        if (context_ready) bongo_cat_preferences_refresh_raster(value);
        else SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Preferences GL context unavailable during scale change: %s",
            SDL_GetError());
    } else {
        value->pending_raster_scale = 0.0f;
        value->raster_retry_ns = 0;
        value->ui.raster_scale = raster_scale;
    }
    SDL_SetWindowMinimumSize(value->window, minimum_width, minimum_height);
    SDL_SetWindowSize(value->window, width, height);
    fit_position(value->window, display, width, height);
    SDL_SyncWindow(value->window);
    bongo_cat_platform_configure_preferences_window(value->window);
    value->live_resize_rendering = was_rendering;
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    value->render_dirty = true;
    SDL_Log("Preferences scale changed: layout %.2f->%.2f raster %.2f->%.2f logical=%.0fx%.0f",
        old_layout, layout_scale, old_raster, raster_scale,
        logical_width, logical_height);
    return true;
}
