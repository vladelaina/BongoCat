#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PointerAudit {
    bool ran;
    bool mver;
    bool has_mouse;
    float angle_x[4];
    float angle_y[4];
    float mouse_x[4];
    float mouse_y[4];
    float maximum_step;
} PointerAudit;

static PointerAudit pointer_audit;

/* Viewer 5.3.03: (7.2727275 / 30) target speed * 30-degree range * 1.2 gain. */
#define POINTER_VISIBLE_STEP_LIMIT 8.8f

static bool value(BongoCatApp *app, const char *id, float *output) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return false;
    if (output) *output = range.value;
    return true;
}

static void input(BongoCatApp *app, BongoCatInputKind kind, const char *name, float value) {
    BongoCatInputEvent event = {.kind = kind, .value = value};
    snprintf(event.name, sizeof(event.name), "%s", name);
    bongo_cat_app_shortcuts(app, &event);
    bongo_cat_app_apply_input(app, &event);
}

static bool pointer_test_center(BongoCatApp *app, SDL_Rect *bounds,
    double *center_x, double *center_y) {
    int window_x, window_y, width, height;
    if (!app || !app->window || !bounds || !center_x || !center_y ||
        !SDL_GetWindowPosition(app->window, &window_x, &window_y) ||
        !SDL_GetWindowSize(app->window, &width, &height)) return false;
    *center_x = window_x + width * 0.5;
    *center_y = window_y + height * 0.5;
    SDL_Point point = {(int)*center_x, (int)*center_y};
    SDL_DisplayID display = SDL_GetDisplayForPoint(&point);
    return display && SDL_GetDisplayBounds(display, bounds) &&
        bounds->w > 0 && bounds->h > 0;
}

static bool motion(BongoCatApp *app, const char *scenario) {
    if (strcmp(scenario, "motion-0") == 0)
        return bongo_cat_live2d_start_motion(app->live2d, "CAT_motion", 0);
    if (strcmp(scenario, "motion-1") == 0)
        return bongo_cat_live2d_start_motion(app->live2d, "CAT_motion", 1);
    if (strncmp(scenario, "expression-", 11) == 0)
        return bongo_cat_live2d_set_expression(app->live2d, atoi(scenario + 11));
    return true;
}

static bool pointer(BongoCatApp *app, bool mirror) {
    SDL_Rect bounds;
    double center_x, center_y;
    if (!pointer_test_center(app, &bounds, &center_x, &center_y)) return false;
    app->config.model.mouse_mirror = mirror;
    app->left_mouse_down = false;
    bongo_cat_app_apply_mouse_position(app, center_x + bounds.w * 0.4,
        center_y - bounds.h * 0.4, 1.0f / 60.0f);
    for (int frame = 0; frame < 90; ++frame)
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    return true;
}

static bool reverse_pointer(BongoCatApp *app) {
    SDL_Rect bounds;
    double center_x, center_y;
    if (!pointer_test_center(app, &bounds, &center_x, &center_y)) return false;
    pointer_audit = (PointerAudit){.ran = true,
        .mver = app->model_render_options.mver_projection};
    app->config.model.mouse_mirror = false;
    app->left_mouse_down = false;
    const float ratios[4][2] = {
        {-0.4f, -0.4f}, {0.4f, -0.4f}, {-0.4f, 0.4f}, {0.4f, 0.4f}};
    float previous_x = 0.0f;
    bool previous_ready = value(app, "ParamAngleX", &previous_x);
    for (int corner = 0; corner < 4; ++corner) {
        bongo_cat_app_apply_mouse_position(app,
            center_x + bounds.w * ratios[corner][0],
            center_y + bounds.h * ratios[corner][1], 1.0f / 60.0f);
        for (int frame = 0; frame < 90; ++frame) {
            bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
            float current_x = previous_x;
            if (!value(app, "ParamAngleX", &current_x)) return false;
            if (previous_ready) {
                float step = SDL_fabsf(current_x - previous_x);
                if (step > pointer_audit.maximum_step)
                    pointer_audit.maximum_step = step;
            }
            previous_x = current_x;
            previous_ready = true;
        }
        if (!value(app, "ParamAngleX", &pointer_audit.angle_x[corner]) ||
            !value(app, "ParamAngleY", &pointer_audit.angle_y[corner])) return false;
        bool mouse_x = value(app, "ParamMouseX", &pointer_audit.mouse_x[corner]);
        bool mouse_y = value(app, "ParamMouseY", &pointer_audit.mouse_y[corner]);
        if (corner == 0) pointer_audit.has_mouse = mouse_x && mouse_y;
        else if (pointer_audit.has_mouse != (mouse_x && mouse_y)) return false;
    }
    return true;
}

static bool apply(BongoCatApp *app, const char *scenario) {
    if (strncmp(scenario, "switch:", 7) == 0)
        return bongo_cat_app_select_model(app, scenario + 7);
    if (strcmp(scenario, "visual-consistency") == 0)
        return bongo_cat_live2d_visual_audit_run(app);
    if (strcmp(scenario, "viewer-sequence") == 0)
        return bongo_cat_live2d_viewer_audit_run(app);
    if (strcmp(scenario, "mouse-screen") == 0)
        return bongo_cat_app_audit_screen_pointer(app);
    if (strcmp(scenario, "mouse-hand-screen") == 0)
        return bongo_cat_app_audit_display_pointer(app);
    if (strcmp(scenario, "mirror") == 0) app->config.model.mirror = true;
    else if (strcmp(scenario, "mouse-move") == 0) return pointer(app, false);
    else if (strcmp(scenario, "mouse-move-mirror") == 0) return pointer(app, true);
    else if (strcmp(scenario, "mouse-reverse") == 0)
        return reverse_pointer(app);
    else if (strcmp(scenario, "key-left") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
    else if (strcmp(scenario, "key-tab-left") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "Tab", 1.0f);
    else if (strcmp(scenario, "key-right") == 0)
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
    else if (strcmp(scenario, "key-left-release") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
    } else if (strcmp(scenario, "keys-both-release") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
        input(app, BONGO_CAT_INPUT_KEY_UP, "RightArrow", 0.0f);
    } else if (strcmp(scenario, "key-stress") == 0) {
        for (int i = 0; i < 250; ++i) {
            input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
            input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
            input(app, BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
            input(app, BONGO_CAT_INPUT_KEY_UP, "RightArrow", 0.0f);
        }
    }
    else if (strcmp(scenario, "keys-both") == 0) {
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        input(app, BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
    } else if (strcmp(scenario, "mouse-left") == 0)
        input(app, BONGO_CAT_INPUT_MOUSE_DOWN, "Left", 1.0f);
    else if (strcmp(scenario, "mouse-right") == 0)
        input(app, BONGO_CAT_INPUT_MOUSE_DOWN, "Right", 1.0f);
    else if (strcmp(scenario, "gamepad-buttons") == 0) {
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "DPadLeft", 1.0f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1.0f);
    } else if (strcmp(scenario, "gamepad-sticks") == 0) {
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", .75f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", -.5f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickX", -.65f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickY", .5f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftThumb", 1.0f);
        input(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "RightThumb", 1.0f);
    } else return motion(app, scenario);
    app->dirty = true;
    return true;
}

static void parameter(FILE *file, BongoCatApp *app, const char *id) {
    BongoCatParameterRange range;
    if (bongo_cat_live2d_parameter(app->live2d, id, &range))
        fprintf(file, "parameter.%s=%.4f [%.4f,%.4f]\n", id,
            range.value, range.minimum, range.maximum);
    else fprintf(file, "parameter.%s=unavailable\n", id);
}

static bool active(BongoCatApp *app, const char *id) {
    float current = 0.0f;
    return value(app, id, &current) && current > 0.5f;
}

static bool signed_value(BongoCatApp *app, const char *id, bool positive) {
    float current = 0.0f;
    return value(app, id, &current) && (positive ? current > 0.05f : current < -0.05f);
}

static bool assertions(BongoCatApp *app, const char *scenario, bool operation) {
    if (!operation) return false;
    if (strncmp(scenario, "switch:", 7) == 0)
        return strcmp(app->config.current_model, scenario + 7) == 0;
    if (strcmp(scenario, "idle") == 0 || strcmp(scenario, "mouse-screen") == 0 || strcmp(scenario, "mouse-hand-screen") == 0 || strcmp(scenario, "mirror") == 0 ||
        strcmp(scenario, "visual-consistency") == 0 ||
        strcmp(scenario, "viewer-sequence") == 0 ||
        strcmp(scenario, "key-left-release") == 0 ||
        strcmp(scenario, "keys-both-release") == 0 ||
        strcmp(scenario, "key-stress") == 0 ||
        strncmp(scenario, "motion-", 7) == 0) return true;
    if (strncmp(scenario, "expression-", 11) == 0)
        return bongo_cat_live2d_expression(app->live2d) == atoi(scenario + 11);
    if (strcmp(scenario, "key-left") == 0 || strcmp(scenario, "key-tab-left") == 0)
        return active(app, "CatParamLeftHandDown");
    if (strcmp(scenario, "key-right") == 0)
        return active(app, "CatParamRightHandDown");
    if (strcmp(scenario, "keys-both") == 0 ||
        strcmp(scenario, "gamepad-buttons") == 0)
        return active(app, "CatParamLeftHandDown") &&
            active(app, "CatParamRightHandDown");
    if (strcmp(scenario, "mouse-left") == 0)
        return active(app, "ParamMouseLeftDown");
    if (strcmp(scenario, "mouse-right") == 0)
        return active(app, "ParamMouseRightDown");
    if (strcmp(scenario, "mouse-move") == 0)
        return signed_value(app, "ParamAngleX",
                app->model_render_options.mver_projection) &&
            signed_value(app, "ParamAngleY", true);
    if (strcmp(scenario, "mouse-move-mirror") == 0)
        return signed_value(app, "ParamAngleX", true) &&
            signed_value(app, "ParamAngleY", true);
    if (strcmp(scenario, "mouse-reverse") == 0) {
        bool horizontal = pointer_audit.mver ?
            pointer_audit.angle_x[0] < -5.0f &&
            pointer_audit.angle_x[1] > 5.0f &&
            pointer_audit.angle_x[2] < -5.0f &&
            pointer_audit.angle_x[3] > 5.0f :
            pointer_audit.angle_x[0] > 5.0f &&
            pointer_audit.angle_x[1] < -5.0f &&
            pointer_audit.angle_x[2] > 5.0f &&
            pointer_audit.angle_x[3] < -5.0f;
        bool direction = pointer_audit.ran && horizontal &&
            pointer_audit.angle_y[0] > 5.0f &&
            pointer_audit.angle_y[1] > 5.0f &&
            pointer_audit.angle_y[2] < -5.0f &&
            pointer_audit.angle_y[3] < -5.0f;
        bool custom = true;
        if (!pointer_audit.mver && pointer_audit.has_mouse) {
            for (int corner = 0; corner < 4; ++corner)
                custom = SDL_fabsf(pointer_audit.mouse_x[corner]) < 0.001f &&
                    SDL_fabsf(pointer_audit.mouse_y[corner]) < 0.001f && custom;
        }
        return direction && custom &&
            pointer_audit.maximum_step < POINTER_VISIBLE_STEP_LIMIT;
    }
    if (strcmp(scenario, "gamepad-sticks") == 0)
        return signed_value(app, "CatParamStickLX", true) &&
            signed_value(app, "CatParamStickLY", false) &&
            signed_value(app, "CatParamStickRX", false) &&
            signed_value(app, "CatParamStickRY", true) &&
            active(app, "CatParamLeftHandDown") &&
            active(app, "CatParamRightHandDown");
    return false;
}

void bongo_cat_live2d_audit_run(BongoCatApp *app) {
    if (!app || !app->smoke_live2d_scenario[0]) return;
    uint64_t started = SDL_GetTicksNS();
    bool result = apply(app, app->smoke_live2d_scenario);
    double duration_ms = (SDL_GetTicksNS() - started) / 1000000.0;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->data_root, "live2d-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return;
    bool verified = assertions(app, app->smoke_live2d_scenario, result);
    fprintf(file, "scenario=%s\nmodel=%s\nmode=%s\noperation=%s\nassertions=%s\n"
        "duration_ms=%.3f\n",
        app->smoke_live2d_scenario, app->config.current_model,
        bongo_cat_mode_name(app->config.current_mode), result ? "accepted" : "rejected",
        verified ? "passed" : "failed", duration_ms);
#ifdef BONGO_CAT_HAS_CUBISM
    fputs("renderer=cubism-native\n", file);
    if (!verified) app->exit_code = 1;
#else
    fputs("renderer=diagnostic; visual-model-result=blocked\n", file);
#endif
    const char *parameters[] = {"CatParamLeftHandDown", "CatParamRightHandDown",
        "CatParamStickLX", "CatParamStickLY", "CatParamStickRX", "CatParamStickRY",
        "CatParamStickLeftDown", "CatParamStickRightDown",
        "CatParamStickShowLeftHand", "CatParamStickShowRightHand",
        "ParamMouseLeftDown", "ParamMouseRightDown", "ParamMouseX", "ParamMouseY",
        "ParamAngleX", "ParamAngleY", "ParamAngleZ", "ParamBodyAngleX",
        "ParamEyeBallX", "ParamEyeBallY"};
    for (size_t i = 0; i < sizeof(parameters) / sizeof(parameters[0]); ++i)
        parameter(file, app, parameters[i]);
    if (pointer_audit.ran) {
        static const char *names[] = {"tl", "tr", "bl", "br"};
        fprintf(file, "pointer.maximum_step=%.4f\npointer.has_mouse=%d\n",
            pointer_audit.maximum_step, pointer_audit.has_mouse);
        for (int corner = 0; corner < 4; ++corner)
            fprintf(file, "pointer.%s.angle_x=%.4f\n"
                "pointer.%s.angle_y=%.4f\n"
                "pointer.%s.mouse_x=%.4f\n"
                "pointer.%s.mouse_y=%.4f\n", names[corner],
                pointer_audit.angle_x[corner], names[corner],
                pointer_audit.angle_y[corner], names[corner],
                pointer_audit.mouse_x[corner], names[corner],
                pointer_audit.mouse_y[corner]);
    }
    fclose(file);
}
