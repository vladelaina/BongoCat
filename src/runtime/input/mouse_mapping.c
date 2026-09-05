#include "runtime.h"
#include "bongo_cat/mver_pointer.h"
#include "bongo_cat/overlay.h"

#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void set_parameter(BongoCatApp *app, const char *id,
    float x_ratio, float y_ratio, bool horizontal_mirror) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return;
    size_t length = strlen(id);
    char axis = length ? id[length - 1] : 'X';
    float value = bongo_cat_mouse_parameter_value(range.minimum, range.maximum,
        x_ratio, y_ratio, axis, horizontal_mirror);
    bongo_cat_live2d_set_parameter(app->live2d, id, value);
}

static bool mver_pointer_bounds(const BongoCatApp *app, SDL_Rect *bounds) {
    const BongoCatLive2DRenderOptions *options = &app->model_render_options;
    if (!options->mver_projection || !bounds) return false;
    if (options->custom_pointer_bounds) {
        *bounds = (SDL_Rect){options->pointer_left, options->pointer_top,
            options->pointer_right - options->pointer_left,
            options->pointer_bottom - options->pointer_top};
        return bounds->w > 0 && bounds->h > 0;
    }
#ifdef _WIN32
    typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextFn)(
        DPI_AWARENESS_CONTEXT);
    FARPROC set_thread_dpi_proc = GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    SetThreadDpiAwarenessContextFn set_thread_dpi = NULL;
    memcpy(&set_thread_dpi, &set_thread_dpi_proc, sizeof(set_thread_dpi));
    DPI_AWARENESS_CONTEXT previous = set_thread_dpi
        ? set_thread_dpi(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) : NULL;
    RECT desktop = {0};
    bool available = GetWindowRect(GetDesktopWindow(), &desktop) != FALSE;
    if (previous && set_thread_dpi) set_thread_dpi(previous);
    if (available && desktop.right > 0 && desktop.bottom > 0) {
        /* Mver 0.1.6 uses a zero origin and stores right/bottom as dimensions. */
        *bounds = (SDL_Rect){0, 0, desktop.right, desktop.bottom};
        return true;
    }
#endif
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    SDL_Rect display;
    if (!primary || !SDL_GetDisplayBounds(primary, &display)) return false;
    *bounds = (SDL_Rect){0, 0, display.w, display.h};
    return bounds->w > 0 && bounds->h > 0;
}

static bool tracked_pointer_bounds(BongoCatApp *app, double absolute_x,
    double absolute_y, SDL_Rect *bounds) {
    if (!app || !bounds) return false;
    if (app->model_render_options.mver_projection)
        return mver_pointer_bounds(app, bounds);
    SDL_Point point = {(int)absolute_x, (int)absolute_y};
    SDL_DisplayID window_display = app->window
        ? SDL_GetDisplayForWindow(app->window) : 0;
    SDL_DisplayID display = app->settings.model.mouse_centered
        ? window_display : SDL_GetDisplayForPoint(&point);
    if (!display) display = window_display;
    return display && SDL_GetDisplayBounds(display, bounds) &&
        bounds->w > 0 && bounds->h > 0;
}

bool bongo_cat_app_map_pointer(BongoCatApp *app, bool relative_requested,
    double absolute_x, double absolute_y, double *x, double *y, bool *changed) {
    SDL_Rect bounds;
    if (!tracked_pointer_bounds(app, absolute_x, absolute_y, &bounds)) return false;
    BongoCatMverPointerBounds pointer_bounds = {
        bounds.x, bounds.y, bounds.w, bounds.h
    };
    double relative_x = 0.0, relative_y = 0.0;
    bool use_relative = relative_requested &&
        bongo_cat_platform_relative_pointer(&app->platform,
            &relative_x, &relative_y);
    bool initialized = app->mver_pointer.initialized;
    double previous_x = app->mver_pointer.x, previous_y = app->mver_pointer.y;
    /* A temporary device read failure must not snap a locked game pointer
       back to the fixed screen coordinate. Keep the virtual position until
       relative samples resume; the first sample still seeds from reality. */
    if (relative_requested && initialized && !use_relative) {
        absolute_x = previous_x;
        absolute_y = previous_y;
    }
    if (!bongo_cat_mver_pointer_update(&app->mver_pointer,
        absolute_x, absolute_y, relative_x, relative_y, use_relative,
        &pointer_bounds, x, y)) return false;
    *changed = !initialized || previous_x != *x || previous_y != *y;
    return true;
}

static float clamp_ratio(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static bool model_pointer_center(BongoCatApp *app, double *x, double *y) {
    int window_x, window_y, width, height;
    if (!app || !app->window || !x || !y ||
        !SDL_GetWindowPosition(app->window, &window_x, &window_y) ||
        !SDL_GetWindowSize(app->window, &width, &height) ||
        width <= 0 || height <= 0) return false;
    if (!app->model_pointer_anchor_ready) {
        BongoCatLive2DVisualState state = {0};
        if (bongo_cat_live2d_visual_state(app->live2d, &state) && state.visible) {
            float center_x = (state.visible_min_x + state.visible_max_x) * 0.5f;
            float center_y = (state.visible_min_y + state.visible_max_y) * 0.5f;
            if (isfinite(center_x) && isfinite(center_y)) {
                app->model_pointer_anchor_x = clamp_ratio(center_x * 0.5f + 0.5f);
                app->model_pointer_anchor_y = clamp_ratio(0.5f - center_y * 0.5f);
                app->model_pointer_anchor_ready = true;
            }
        }
    }
    float anchor_x = app->model_pointer_anchor_ready
        ? app->model_pointer_anchor_x : 0.5f;
    float anchor_y = app->model_pointer_anchor_ready
        ? app->model_pointer_anchor_y : 0.5f;
    *x = window_x + (double)width * anchor_x;
    *y = window_y + (double)height * anchor_y;
    return true;
}

static bool model_pointer_bounds(BongoCatApp *app, SDL_Rect *bounds,
    double *center_x, double *center_y) {
    double visible_center_x, visible_center_y;
    if (!bounds || !model_pointer_center(app, &visible_center_x,
        &visible_center_y)) return false;
    if (center_x) *center_x = visible_center_x;
    if (center_y) *center_y = visible_center_y;
    SDL_Point point = {(int)visible_center_x, (int)visible_center_y};
    SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
    return display && SDL_GetDisplayBounds(display, bounds) &&
        bounds->w > 0 && bounds->h > 0;
}

void bongo_cat_app_apply_mouse_coordinates(BongoCatApp *app, double hand_x,
    double hand_y, double gaze_x, double gaze_y) {
    SDL_Point point = {(int)hand_x, (int)hand_y}; SDL_Rect bounds = {0};
    double model_center_x = 0.0, model_center_y = 0.0;
    bool screen_mapped = app->settings.model.mouse_centered;
    bool centered_bounds = screen_mapped && model_pointer_bounds(app, &bounds,
        &model_center_x, &model_center_y);
    bool mapped_bounds = centered_bounds || (!screen_mapped &&
        mver_pointer_bounds(app, &bounds));
    if (!mapped_bounds) {
        SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
        if (!display || !SDL_GetDisplayBounds(display, &bounds)) return;
    }
    BongoCatMverPointerBounds pointer_bounds = {
        bounds.x, bounds.y, bounds.w, bounds.h
    };
    float hand_x_ratio, hand_y_ratio;
    if (!bongo_cat_mver_pointer_ratios(hand_x, hand_y, &pointer_bounds,
        &hand_x_ratio, &hand_y_ratio)) return;
    float gaze_x_ratio = hand_x_ratio, gaze_y_ratio = hand_y_ratio;
    if (centered_bounds) {
        gaze_x_ratio = bongo_cat_mouse_centered_ratio(gaze_x, model_center_x,
            bounds.x, bounds.x + bounds.w);
        gaze_y_ratio = bongo_cat_mouse_centered_ratio(gaze_y, model_center_y,
            bounds.y, bounds.y + bounds.h);
    }
    bool exact_pointer = bongo_cat_overlay_mver_pointer_enabled(app->overlay);
    bool overlay_left_handed = exact_pointer &&
        bongo_cat_overlay_mver_pointer_left_handed(app->overlay);
    bool left_handed = app->model_render_options.pointer_left_handed ||
        overlay_left_handed;
    bool horizontal_mirror = left_handed != app->settings.model.mouse_mirror;
    float drag_x = 0.0f, drag_y = 0.0f;
    bongo_cat_mouse_drag_coordinates(gaze_x_ratio, gaze_y_ratio,
        horizontal_mirror, &drag_x, &drag_y);
    bool overlay_mirror = horizontal_mirror != overlay_left_handed;
    float overlay_x_ratio = overlay_mirror
        ? 1.0f - hand_x_ratio : hand_x_ratio;
    bongo_cat_overlay_set_mver_pointer(app->overlay, overlay_x_ratio, hand_y_ratio,
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down);
    if (!exact_pointer) {
        set_parameter(app, "ParamMouseX", hand_x_ratio, hand_y_ratio,
            horizontal_mirror);
        set_parameter(app, "ParamMouseY", hand_x_ratio, hand_y_ratio,
            horizontal_mirror);
    }
    /* Pointer and window messages are not atomic during a native window move. */
    if (!app->settings.model.mouse_centered || !app->window_drag_active) {
        if (app->settings.model.mouse_centered)
            bongo_cat_live2d_set_centered_dragging(app->live2d, drag_x, drag_y);
        else bongo_cat_live2d_set_dragging(app->live2d, drag_x, drag_y);
    }
    app->dirty = true;
}

void bongo_cat_app_reset_pointer_tracking(BongoCatApp *app) {
    if (!app) return;
    if (app->pointer_relative_active)
        bongo_cat_platform_relative_pointer_reset(&app->platform);
    else bongo_cat_platform_relative_pointer_release(&app->platform);
    app->mver_pointer = (BongoCatMverPointerState){0};
    app->model_pointer_anchor_ready = false;
    app->pointer_known = false;
    app->mouse_last_ns = 0;
    app->pointer_relative_active = false;
    app->pointer_cursor_locked = false;
    app->mouse_button_event_pending = false;
    app->dirty = true;
}

void bongo_cat_app_apply_mouse_position(BongoCatApp *app, double x, double y,
    float elapsed_seconds) {
    if (!app || app->settings.model.ignore_mouse) return;
    (void)elapsed_seconds;
    bongo_cat_app_apply_mouse_coordinates(app, x, y, x, y);
}

bool bongo_cat_app_audit_screen_pointer(BongoCatApp *app) {
    SDL_Rect bounds;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (!app || !display || !SDL_GetDisplayBounds(display, &bounds)) return false;
    bool mouse_centered = app->settings.model.mouse_centered;
    app->settings.model.mouse_centered = false;
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w * 0.5,
        bounds.y + bounds.h * 0.5, 1.0f / 60.0f);
    for (int frame = 0; frame < 90; ++frame)
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    BongoCatParameterRange x, y, z;
    bool passed = bongo_cat_live2d_parameter(app->live2d, "ParamAngleX", &x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamAngleY", &y) &&
        fabsf(x.value) < 0.5f && fabsf(y.value) < 0.5f;
    bool has_z = bongo_cat_live2d_parameter(app->live2d, "ParamAngleZ", &z);
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w * 0.8,
        bounds.y + bounds.h * 0.2, 1.0f / 60.0f);
    for (int frame = 0; frame < 90; ++frame)
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    passed = (!has_z || (bongo_cat_live2d_parameter(app->live2d,
        "ParamAngleZ", &z) && fabsf(z.value) < 0.25f)) && passed;
    app->settings.model.mouse_centered = mouse_centered;
    return passed;
}

bool bongo_cat_app_audit_display_pointer(BongoCatApp *app) {
    SDL_Rect bounds;
    if (!app) return false;
    bool centered = app->settings.model.mouse_centered;
    app->settings.model.mouse_centered = true;
    if (!model_pointer_bounds(app, &bounds, NULL, NULL)) {
        app->settings.model.mouse_centered = centered;
        return false;
    }
    BongoCatParameterRange tl_x = {0}, tl_y = {0}, br_x = {0}, br_y = {0};
    bongo_cat_app_apply_mouse_position(app, bounds.x, bounds.y, 0.0f);
    bool passed = bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &tl_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &tl_y);
    bongo_cat_app_apply_mouse_position(app, bounds.x + bounds.w - 1,
        bounds.y + bounds.h - 1, 0.0f);
    passed = bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &br_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &br_y) && passed;
    app->settings.model.mouse_centered = centered;
    return passed && tl_x.value < -20.0f && tl_y.value > 20.0f &&
        br_x.value > 20.0f && br_y.value < -20.0f;
}
