#include "runtime.h"
#include "mouse_internal.h"

#include <math.h>

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
    BongoCatMouseProjection projection;
    if (!app) return false;
    bool centered = app->settings.model.mouse_centered;
    app->settings.model.mouse_centered = true;
    if (!bongo_cat_app_mouse_projection(app, 0.0, 0.0, &projection) ||
        !projection.centered) {
        app->settings.model.mouse_centered = centered;
        return false;
    }
    BongoCatMverPointerBounds bounds = projection.bounds;
    BongoCatParameterRange tl_x = {0}, tl_y = {0}, br_x = {0}, br_y = {0};
    bongo_cat_app_apply_mouse_position(app, bounds.left, bounds.top, 0.0f);
    bool passed = bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &tl_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &tl_y);
    bongo_cat_app_apply_mouse_position(app, bounds.left + bounds.width - 1,
        bounds.top + bounds.height - 1, 0.0f);
    passed = bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &br_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &br_y) && passed;
    app->settings.model.mouse_centered = centered;
    return passed && tl_x.value < -20.0f && tl_y.value > 20.0f &&
        br_x.value > 20.0f && br_y.value < -20.0f;
}
