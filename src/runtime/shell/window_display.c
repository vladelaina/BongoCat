#include "runtime.h"

#define DISPLAY_RECOVERY_DELAY_NS 750000000ull
#define DISPLAY_RECT_CAP 32

static bool window_rect(BongoCatApp *app, SDL_Rect *rect) {
    return app && app->window && rect &&
        SDL_GetWindowPosition(app->window, &rect->x, &rect->y) &&
        SDL_GetWindowSize(app->window, &rect->w, &rect->h) &&
        rect->w > 0 && rect->h > 0;
}

static bool intersects_bounds(const SDL_Rect *window,
    const SDL_Rect *bounds, int count) {
    if (!window || !bounds || count < 1) return false;
    for (int i = 0; i < count; ++i)
        if (SDL_HasRectIntersection(window, &bounds[i])) return true;
    return false;
}

static bool intersects_available_display(const SDL_Rect *window, bool *known) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    *known = displays && count > 0;
    bool intersects = false;
    for (int i = 0; displays && i < count && !intersects; ++i) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(displays[i], &bounds))
            intersects = SDL_HasRectIntersection(window, &bounds);
    }
    SDL_free(displays);
    return intersects;
}

static SDL_Point fitted_position(const SDL_Rect *window,
    const SDL_Rect *bounds) {
    int max_x = SDL_max(bounds->x, bounds->x + bounds->w - window->w);
    int max_y = SDL_max(bounds->y, bounds->y + bounds->h - window->h);
    SDL_Point point = {SDL_clamp(window->x, bounds->x, max_x),
        SDL_clamp(window->y, bounds->y, max_y)};
    return point;
}

static bool clipped_rect(const SDL_Rect *window, const SDL_Rect *bounds,
    SDL_Rect *clip) {
    int right = SDL_min(window->x + window->w, bounds->x + bounds->w);
    int bottom = SDL_min(window->y + window->h, bounds->y + bounds->h);
    clip->x = SDL_max(window->x, bounds->x);
    clip->y = SDL_max(window->y, bounds->y);
    clip->w = right - clip->x;
    clip->h = bottom - clip->y;
    return clip->w > 0 && clip->h > 0;
}

static void sort_edges(int *values, int count) {
    for (int i = 1; i < count; ++i) {
        int value = values[i], j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1]; --j;
        }
        values[j] = value;
    }
}

static void sort_clips(SDL_Rect *values, int count) {
    for (int i = 1; i < count; ++i) {
        SDL_Rect value = values[i]; int j = i;
        while (j > 0 && values[j - 1].y > value.y) {
            values[j] = values[j - 1]; --j;
        }
        values[j] = value;
    }
}

static bool bounds_cover_window(const SDL_Rect *window,
    const SDL_Rect *bounds, int count) {
    if (!window || !bounds || window->w <= 0 || window->h <= 0 ||
        count < 1 || count > DISPLAY_RECT_CAP) return false;
    SDL_Rect clips[DISPLAY_RECT_CAP];
    int edges[DISPLAY_RECT_CAP * 2 + 2];
    int clip_count = 0, edge_count = 2;
    edges[0] = window->x; edges[1] = window->x + window->w;
    for (int i = 0; i < count; ++i) {
        SDL_Rect clip;
        if (!clipped_rect(window, &bounds[i], &clip)) continue;
        clips[clip_count++] = clip;
        edges[edge_count++] = clip.x;
        edges[edge_count++] = clip.x + clip.w;
    }
    if (!clip_count) return false;
    sort_edges(edges, edge_count); sort_clips(clips, clip_count);
    int window_bottom = window->y + window->h;
    for (int edge = 1; edge < edge_count; ++edge) {
        int left = edges[edge - 1], right = edges[edge];
        if (left == right) continue;
        int covered_to = window->y;
        for (int i = 0; i < clip_count; ++i) {
            int clip_right = clips[i].x + clips[i].w;
            if (clips[i].x > left || clip_right < right) continue;
            if (clips[i].y > covered_to) break;
            covered_to = SDL_max(covered_to, clips[i].y + clips[i].h);
            if (covered_to >= window_bottom) break;
        }
        if (covered_to < window_bottom) return false;
    }
    return true;
}

static int available_bounds(SDL_Rect bounds[DISPLAY_RECT_CAP]) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (!displays || count < 1 || count > DISPLAY_RECT_CAP) {
        SDL_free(displays); return 0;
    }
    int found = 0;
    for (int i = 0; i < count; ++i)
        if (SDL_GetDisplayUsableBounds(displays[i], &bounds[found]) ||
            SDL_GetDisplayBounds(displays[i], &bounds[found])) ++found;
    SDL_free(displays);
    return found;
}

static bool available_displays_cover(BongoCatApp *app,
    const SDL_Rect *window) {
    if (app && app->window_drag_active && app->drag_display_bounds &&
        app->drag_display_count > 0)
        return bounds_cover_window(window, app->drag_display_bounds,
            app->drag_display_count);
    SDL_Rect bounds[DISPLAY_RECT_CAP];
    int count = available_bounds(bounds);
    return bounds_cover_window(window, bounds, count);
}

void bongo_cat_window_drag_bounds_refresh(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_window_drag_bounds_clear(app);
    SDL_Rect *bounds = SDL_malloc(sizeof(*bounds) * DISPLAY_RECT_CAP);
    if (!bounds) return;
    int count = available_bounds(bounds);
    if (!count) { SDL_free(bounds); return; }
    app->drag_display_bounds = bounds;
    app->drag_display_count = count;
}

void bongo_cat_window_drag_bounds_clear(BongoCatApp *app) {
    if (!app) return;
    SDL_free(app->drag_display_bounds);
    app->drag_display_bounds = NULL;
    app->drag_display_count = 0;
}

static SDL_DisplayID target_display(BongoCatApp *app, const SDL_Rect *rect) {
    if (app->window_drag_active || app->resize_gesture) {
        float pointer_x = 0.0f, pointer_y = 0.0f;
        SDL_GetGlobalMouseState(&pointer_x, &pointer_y);
        SDL_Point pointer = {(int)pointer_x, (int)pointer_y};
        SDL_DisplayID display = SDL_GetDisplayForPoint(&pointer);
        if (display) return display;
    }
    SDL_DisplayID display = SDL_GetDisplayForWindow(app->window);
    if (!display && rect) display = SDL_GetDisplayForRect(rect);
    return display ? display : SDL_GetPrimaryDisplay();
}

static bool fit_to_display(BongoCatApp *app, SDL_DisplayID display,
    const SDL_Rect *window) {
    SDL_Rect bounds;
    if (!display || (!SDL_GetDisplayUsableBounds(display, &bounds) &&
        !SDL_GetDisplayBounds(display, &bounds))) return false;
    SDL_Point next = fitted_position(window, &bounds);
    if (next.x == window->x && next.y == window->y) return false;
    if (!SDL_SetWindowPosition(app->window, next.x, next.y)) return false;
    app->session.window.x = next.x;
    app->session.window.y = next.y;
    app->session.window.position_known = true;
    bongo_cat_window_mark_hit_dirty(app);
    return true;
}

void bongo_cat_window_clamp_to_display(BongoCatApp *app) {
    SDL_Rect rect;
    if (!app || !app->settings.window.keep_in_screen || !window_rect(app, &rect)) return;
    if (available_displays_cover(app, &rect)) return;
    fit_to_display(app, target_display(app, &rect), &rect);
}

void bongo_cat_window_drag_to(BongoCatApp *app, int x, int y) {
    int width = 0, height = 0, current_x = 0, current_y = 0;
    if (!app || !app->window || !SDL_GetWindowSize(app->window, &width, &height) ||
        width <= 0 || height <= 0) return;
    SDL_Rect requested = {x, y, width, height};
    SDL_Point next = {x, y};
    if (app->settings.window.keep_in_screen &&
        !available_displays_cover(app, &requested)) {
        SDL_Rect bounds; SDL_DisplayID display = target_display(app, &requested);
        if (display && (SDL_GetDisplayUsableBounds(display, &bounds) ||
            SDL_GetDisplayBounds(display, &bounds))) next = fitted_position(&requested, &bounds);
    }
    if (SDL_GetWindowPosition(app->window, &current_x, &current_y) &&
        current_x == next.x && current_y == next.y) return;
    if (!SDL_SetWindowPosition(app->window, next.x, next.y)) return;
    app->session.window.x = next.x; app->session.window.y = next.y;
    app->session.window.position_known = true;
    bongo_cat_window_mark_hit_dirty(app);
    if (app->window_snapshot) {
        bongo_cat_window_snapshot_geometry(app, NULL);
        app->dirty = true;
    }
}

void bongo_cat_window_reset_position(BongoCatApp *app) {
    SDL_Rect rect, bounds;
    if (!window_rect(app, &rect)) return;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!display || (!SDL_GetDisplayUsableBounds(display, &bounds) &&
        !SDL_GetDisplayBounds(display, &bounds))) return;
    int x = bounds.x + SDL_max(0, bounds.w - rect.w) / 2;
    int y = bounds.y + SDL_max(0, bounds.h - rect.h) / 2;
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_window_snapshot_end(app);
    if (!SDL_SetWindowPosition(app->window, x, y)) return;
    app->session.window.x = x;
    app->session.window.y = y;
    app->session.window.position_known = true;
    bongo_cat_window_mark_hit_dirty(app);
    bongo_cat_app_reset_pointer_tracking(app);
    app->dirty = true;
}

bool bongo_cat_window_recover_to_display(BongoCatApp *app) {
    SDL_Rect rect;
    if (!window_rect(app, &rect)) return false;
    bool known = false;
    if (intersects_available_display(&rect, &known) || !known) return false;
    SDL_DisplayID display = SDL_GetDisplayForRect(&rect);
    if (!display) display = SDL_GetPrimaryDisplay();
    return fit_to_display(app, display, &rect);
}

void bongo_cat_window_display_event(BongoCatApp *app, const SDL_Event *event) {
    if (!app || !event) return;
    if (event->type < SDL_EVENT_DISPLAY_FIRST ||
        event->type > SDL_EVENT_DISPLAY_LAST) return;
    bongo_cat_window_snapshot_end(app);
    if (app->window_drag_active && app->settings.window.keep_in_screen)
        bongo_cat_window_drag_bounds_refresh(app);
    bongo_cat_app_reset_pointer_tracking(app);
    app->display_recovery_due_ns = SDL_GetTicksNS() + DISPLAY_RECOVERY_DELAY_NS;
}

void bongo_cat_window_update_display_recovery(BongoCatApp *app, uint64_t now) {
    if (!app || !app->display_recovery_due_ns ||
        now < app->display_recovery_due_ns) return;
    app->display_recovery_due_ns = 0;
    if (app->settings.window.keep_in_screen) bongo_cat_window_clamp_to_display(app);
    else bongo_cat_window_recover_to_display(app);
}

bool bongo_cat_window_display_self_test(BongoCatApp *app) {
    const SDL_Rect displays[] = {{0, 0, 1920, 1040}, {1920, 0, 1920, 1040}};
    const SDL_Rect gapped[] = {{0, 0, 1920, 1040}, {2020, 0, 1920, 1040}};
    const SDL_Rect negative[] = {{-1920, 0, 1920, 1080}, {0, 0, 2560, 1440}};
    const SDL_Rect stacked[] = {{0, -1200, 1920, 1200}, {0, 0, 1920, 1080},
        {1920, 0, 1280, 1024}};
    SDL_Rect partial = {-80, 100, 320, 240};
    SDL_Rect secondary = {2200, 100, 320, 240};
    SDL_Rect detached = {4200, 100, 320, 240};
    SDL_Rect bridge = {1800, 100, 320, 240};
    SDL_Rect negative_bridge = {-100, 100, 200, 200};
    SDL_Rect vertical_bridge = {100, -100, 200, 200};
    SDL_Rect triple_bridge = {1800, 100, 300, 200};
    SDL_Rect corner_gap = {1800, -100, 240, 200};
    SDL_Point fitted = fitted_position(&detached, &displays[0]);
    bool overlap = intersects_bounds(&partial, displays, 2);
    bool second = intersects_bounds(&secondary, displays, 2);
    bool removed = !intersects_bounds(&secondary, displays, 1);
    bool recovered = fitted.x == 1600 && fitted.y == 100;
    bool adjacent_covered = bounds_cover_window(&bridge, displays, 2);
    bool gap_rejected = !bounds_cover_window(&bridge, gapped, 2);
    bool event_reset = false;
    if (app) {
        bool pointer_known = app->pointer_known;
        bool anchor_ready = app->model_pointer_anchor_ready;
        bool dirty = app->dirty;
        uint64_t recovery_due = app->display_recovery_due_ns;
        app->pointer_known = true; app->model_pointer_anchor_ready = true;
        app->dirty = false; app->display_recovery_due_ns = 0;
        SDL_Event event = {.type = SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED};
        bongo_cat_window_display_event(app, &event);
        event_reset = !app->pointer_known && !app->model_pointer_anchor_ready &&
            app->dirty && app->display_recovery_due_ns > 0;
        app->pointer_known = pointer_known;
        app->model_pointer_anchor_ready = anchor_ready;
        app->dirty = dirty; app->display_recovery_due_ns = recovery_due;
    }
    return overlap && second && removed && recovered && adjacent_covered &&
        gap_rejected && !bounds_cover_window(&partial, displays, 2) &&
        !intersects_bounds(&detached, displays, 2) && event_reset &&
        bounds_cover_window(&negative_bridge, negative, 2) &&
        bounds_cover_window(&vertical_bridge, stacked, 3) &&
        bounds_cover_window(&triple_bridge, stacked, 3) &&
        !bounds_cover_window(&corner_gap, stacked, 3);
}
