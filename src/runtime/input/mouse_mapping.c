#include "runtime.h"
#include "mouse_internal.h"
#include "bongo_cat/overlay.h"

#include <string.h>

bool bongo_cat_app_map_pointer(BongoCatApp *app,
    const BongoCatMouseProjection *projection, bool relative_requested,
    double absolute_x, double absolute_y, double *x, double *y, bool *changed) {
    if (!app || !projection || !x || !y || !changed) return false;
    const BongoCatMverPointerBounds *bounds = &projection->bounds;
    double relative_x = 0.0, relative_y = 0.0;
    bool use_relative = relative_requested &&
        bongo_cat_platform_relative_pointer(&app->platform,
            &relative_x, &relative_y);
    bool initialized = app->mver_pointer.initialized;
    double previous_x = app->mver_pointer.x, previous_y = app->mver_pointer.y;
    /* Preserve the virtual position while relative samples are unavailable. */
    if (relative_requested && initialized && !use_relative) {
        absolute_x = previous_x;
        absolute_y = previous_y;
    }
    if (!bongo_cat_mver_pointer_update(&app->mver_pointer,
        absolute_x, absolute_y, relative_x, relative_y, use_relative,
        bounds, x, y)) return false;
    *changed = !initialized || previous_x != *x || previous_y != *y;
    return true;
}

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

void bongo_cat_app_apply_mouse_coordinates(BongoCatApp *app,
    const BongoCatMouseProjection *projection, double hand_x, double hand_y,
    double gaze_x, double gaze_y) {
    if (!app || !projection) return;
    const BongoCatMverPointerBounds *bounds = &projection->bounds;
    float hand_x_ratio, hand_y_ratio;
    if (!bongo_cat_mver_pointer_ratios(hand_x, hand_y, bounds,
        &hand_x_ratio, &hand_y_ratio)) return;
    float gaze_x_ratio = hand_x_ratio, gaze_y_ratio = hand_y_ratio;
    if (projection->centered) {
        gaze_x_ratio = bongo_cat_mouse_centered_ratio(gaze_x, projection->center_x,
            bounds->left, bounds->left + bounds->width);
        gaze_y_ratio = bongo_cat_mouse_centered_ratio(gaze_y, projection->center_y,
            bounds->top, bounds->top + bounds->height);
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
    bongo_cat_platform_relative_pointer_release(&app->platform);
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
    BongoCatMouseProjection projection;
    if (bongo_cat_app_mouse_projection(app, x, y, &projection))
        bongo_cat_app_apply_mouse_coordinates(app, &projection, x, y, x, y);
}
