#include "runtime.h"
#include "live2d_audit_scenario.h"
#include "live2d_pointer_audit.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define input bongo_cat_live2d_audit_input
#define motion bongo_cat_live2d_audit_motion
#define parameter_override_scenario bongo_cat_live2d_audit_parameter_override

static bool value(BongoCatApp *app, const char *id, float *output) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return false;
    if (output) *output = range.value;
    return true;
}

/* Viewer 5.3.03: (7.2727275 / 30) target speed * 30-degree range * 1.2 gain. */
#define POINTER_VISIBLE_STEP_LIMIT 8.8f

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
    if (strcmp(scenario, "mirror") == 0) app->settings.model.mirror = true;
    else if (strcmp(scenario, "mouse-move") == 0)
        return bongo_cat_live2d_pointer_audit_run(app, false);
    else if (strcmp(scenario, "mouse-move-mirror") == 0)
        return bongo_cat_live2d_pointer_audit_run(app, true);
    else if (strcmp(scenario, "mouse-reverse") == 0)
        return bongo_cat_live2d_pointer_reverse_audit_run(app);
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

static bool inactive(BongoCatApp *app, const char *id) {
    float current = 0.0f;
    return value(app, id, &current) && current <= 0.5f;
}

static bool signed_value(BongoCatApp *app, const char *id, bool positive) {
    float current = 0.0f;
    return value(app, id, &current) && (positive ? current > 0.05f : current < -0.05f);
}

static bool assertions(BongoCatApp *app, const char *scenario, bool operation) {
    if (!operation) return false;
    if (strncmp(scenario, "switch:", 7) == 0)
        return strcmp(app->session.active_model_id, scenario + 7) == 0;
    if (strcmp(scenario, "idle") == 0 || strcmp(scenario, "mouse-screen") == 0 || strcmp(scenario, "mouse-hand-screen") == 0 || strcmp(scenario, "mirror") == 0 ||
        strcmp(scenario, "visual-consistency") == 0 ||
        strcmp(scenario, "viewer-sequence") == 0 ||
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
    if (strcmp(scenario, "key-left-release") == 0)
        return inactive(app, "CatParamLeftHandDown");
    if (strcmp(scenario, "keys-both-release") == 0 ||
        strcmp(scenario, "key-stress") == 0)
        return inactive(app, "CatParamLeftHandDown") &&
            inactive(app, "CatParamRightHandDown");
    if (strcmp(scenario, "mouse-left") == 0)
        return active(app, "ParamMouseLeftDown");
    if (strcmp(scenario, "mouse-right") == 0)
        return active(app, "ParamMouseRightDown");
    if (strcmp(scenario, "mouse-move") == 0) {
        float angle_z = 0.0f;
        bool z_valid = !value(app, "ParamAngleZ", &angle_z) ||
            angle_z < -0.05f;
        return signed_value(app, "ParamAngleX", true) &&
            signed_value(app, "ParamAngleY", true) && z_valid;
    }
    if (strcmp(scenario, "mouse-move-mirror") == 0)
        return signed_value(app, "ParamAngleX", true) &&
            signed_value(app, "ParamAngleY", true);
    if (strcmp(scenario, "mouse-reverse") == 0) {
        bool horizontal =
            bongo_cat_pointer_audit.angle_x[0] < -5.0f &&
            bongo_cat_pointer_audit.angle_x[1] > 5.0f &&
            bongo_cat_pointer_audit.angle_x[2] < -5.0f &&
            bongo_cat_pointer_audit.angle_x[3] > 5.0f;
        bool direction = bongo_cat_pointer_audit.ran && horizontal &&
            bongo_cat_pointer_audit.angle_y[0] > 5.0f &&
            bongo_cat_pointer_audit.angle_y[1] > 5.0f &&
            bongo_cat_pointer_audit.angle_y[2] < -5.0f &&
            bongo_cat_pointer_audit.angle_y[3] < -5.0f;
        return direction &&
            bongo_cat_pointer_audit.maximum_step < POINTER_VISIBLE_STEP_LIMIT;
    }
    if (strcmp(scenario, "gamepad-sticks") == 0)
        return signed_value(app, "CatParamStickLX", true) &&
            signed_value(app, "CatParamStickLY", true) &&
            signed_value(app, "CatParamStickRX", false) &&
            signed_value(app, "CatParamStickRY", false) &&
            active(app, "CatParamLeftHandDown") &&
            active(app, "CatParamRightHandDown");
    return false;
}

void bongo_cat_live2d_audit_run(BongoCatApp *app) {
    if (!app || !app->smoke_live2d_scenario[0]) return;
    uint64_t started = SDL_GetTicksNS();
    bool result = apply(app, app->smoke_live2d_scenario);
    if (result && parameter_override_scenario(app->smoke_live2d_scenario))
        for (int frame = 0; frame < 4; ++frame)
            bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
    double duration_ms = (SDL_GetTicksNS() - started) / 1000000.0;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->state_root,
        "live2d-audit.txt")) return;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return;
    bool verified = assertions(app, app->smoke_live2d_scenario, result);
    fprintf(file, "scenario=%s\nmodel=%s\nmode=%s\noperation=%s\nassertions=%s\n"
        "duration_ms=%.3f\n",
        app->smoke_live2d_scenario, app->session.active_model_id,
        bongo_cat_mode_name(app->loaded_mode), result ? "accepted" : "rejected",
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
    if (bongo_cat_pointer_audit.ran) {
        static const char *names[] = {"tl", "tr", "bl", "br"};
        fprintf(file, "pointer.maximum_step=%.4f\npointer.has_mouse=%d\n",
            bongo_cat_pointer_audit.maximum_step,
            bongo_cat_pointer_audit.has_mouse);
        for (int corner = 0; corner < 4; ++corner)
            fprintf(file, "pointer.%s.angle_x=%.4f\n"
                "pointer.%s.angle_y=%.4f\n"
                "pointer.%s.mouse_x=%.4f\n"
                "pointer.%s.mouse_y=%.4f\n", names[corner],
                bongo_cat_pointer_audit.angle_x[corner], names[corner],
                bongo_cat_pointer_audit.angle_y[corner], names[corner],
                bongo_cat_pointer_audit.mouse_x[corner], names[corner],
                bongo_cat_pointer_audit.mouse_y[corner]);
    }
    fclose(file);
}
