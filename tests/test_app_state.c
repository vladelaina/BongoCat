#include "test.h"
#include "bongo_cat/app.h"
#include "bongo_cat/overlay.h"

#include <stdio.h>
#include <string.h>

typedef struct ParameterValue {
    char id[BONGO_CAT_ID_CAP];
    float value;
} ParameterValue;

static ParameterValue parameters[128];
static size_t parameter_count;
static bool left_hand, right_hand, left_trigger, right_trigger, left_thumb;
static bool left_shoulder, right_shoulder;
int bongo_cat_test_failures;

static float parameter(const char *id) {
    for (size_t i = parameter_count; i > 0; --i)
        if (strcmp(parameters[i - 1].id, id) == 0) return parameters[i - 1].value;
    return -999.0f;
}

bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value) {
    (void)live2d;
    for (size_t i = 0; i < parameter_count; ++i) {
        if (strcmp(parameters[i].id, id) == 0) {
            parameters[i].value = value;
            return true;
        }
    }
    if (parameter_count >= sizeof(parameters) / sizeof(parameters[0])) return false;
    snprintf(parameters[parameter_count].id,
        sizeof(parameters[parameter_count].id), "%s", id);
    parameters[parameter_count++].value = value;
    return true;
}

bool bongo_cat_live2d_parameter(BongoCatLive2D *live2d, const char *id,
    BongoCatParameterRange *range) {
    (void)live2d; (void)id;
    if (!range) return false;
    *range = (BongoCatParameterRange){-1.0f, 1.0f, 0.0f};
    return true;
}

int bongo_cat_overlay_key(BongoCatOverlay *overlay, const char *name, bool pressed) {
    (void)overlay;
    if (strcmp(name, "KeyA") == 0 || strcmp(name, "DPadLeft") == 0)
        left_hand = pressed;
    else if (strcmp(name, "RightArrow") == 0 || strcmp(name, "South") == 0)
        right_hand = pressed;
    else if (strcmp(name, "LeftTrigger2") == 0) left_trigger = pressed;
    else if (strcmp(name, "RightTrigger2") == 0) right_trigger = pressed;
    else if (strcmp(name, "LeftTrigger") == 0) left_shoulder = pressed;
    else if (strcmp(name, "RightTrigger") == 0) right_shoulder = pressed;
    else if (strcmp(name, "LeftThumb") == 0) left_thumb = pressed;
    return 0;
}

bool bongo_cat_overlay_hand_active(const BongoCatOverlay *overlay, bool right) {
    (void)overlay;
    return right ? right_hand || right_trigger || right_shoulder :
        left_hand || left_trigger || left_shoulder;
}

static BongoCatInputEvent input(BongoCatInputKind kind, const char *name, float value) {
    BongoCatInputEvent event = {.kind = kind, .value = value};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

static void apply(BongoCatApp *app, BongoCatInputKind kind,
    const char *name, float value) {
    BongoCatInputEvent event = input(kind, name, value);
    bongo_cat_app_apply_input(app, &event);
}

static void check_gamepad_poses(BongoCatApp *app) {
    bongo_cat_app_reset_gamepad(app);
    const float directions[] = {-0.5f, 0.5f, 0.0f};
    for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); ++i) {
        float y = directions[i];
        apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", y);
        apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickY", y);
        CHECK(app->left_stick_y == y && app->right_stick_y == y);
        CHECK(parameter("CatParamStickLY") == -y);
        CHECK(parameter("CatParamStickRY") == -y);
    }
    apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", 0.5f);
    apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickX", -0.5f);
    const struct { BongoCatInputKind kind; const char *left, *right; } overlays[] = {
        {BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftTrigger", "RightTrigger"},
        {BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftTrigger2", "RightTrigger2"},
        {BONGO_CAT_INPUT_GAMEPAD_BUTTON, "DPadLeft", "South"}
    };
    for (size_t i = 0; i < sizeof(overlays) / sizeof(overlays[0]); ++i) {
        apply(app, overlays[i].kind, overlays[i].left, 0.6f);
        CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
        CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
        CHECK(parameter("CatParamLeftHandDown") == 1.0f);
        /* Further axis events must not bring back an obscured stick paw. */
        apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", -0.5f);
        CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
        apply(app, overlays[i].kind, overlays[i].right, 0.6f);
        CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
        CHECK(parameter("CatParamStickShowRightHand") == 0.0f);
        CHECK(parameter("CatParamRightHandDown") == 1.0f);
        apply(app, overlays[i].kind, overlays[i].left, 0.0f);
        CHECK(parameter("CatParamStickShowLeftHand") == 1.0f);
        CHECK(parameter("CatParamStickShowRightHand") == 0.0f);
        apply(app, overlays[i].kind, overlays[i].right, 0.0f);
        CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
        CHECK(app->left_stick_x == 0.5f && app->right_stick_x == -0.5f);
    }
    /* Releasing the overlay after centering the stick restores the resting paw. */
    apply(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftTrigger", 1.0f);
    apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", 0.0f);
    apply(app, BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", 0.0f);
    apply(app, BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftTrigger", 0.0f);
    CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    bongo_cat_app_reset_gamepad(app);
    CHECK(parameter("CatParamStickShowRightHand") == 0.0f);
    CHECK(parameter("CatParamRightHandDown") == 0.0f);
}

int main(void) {
    /* BongoCatApp contains the input queue and model catalogs and is too
       large for the default 1 MiB Windows test-thread stack. */
    static BongoCatApp app = {0};
    app.live2d = (BongoCatLive2D *)(uintptr_t)1;
    app.overlay = (BongoCatOverlay *)(uintptr_t)1;

    BongoCatInputEvent event = input(BONGO_CAT_INPUT_MOUSE_DOWN, "Middle", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter_count == 0);
    event = input(BONGO_CAT_INPUT_MOUSE_DOWN, "Left", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(app.left_mouse_down);
    CHECK(parameter("ParamMouseLeftDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_MOUSE_DOWN, "Right", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(app.right_mouse_down);
    CHECK(parameter("ParamMouseRightDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_MOUSE_UP, "Right", 0.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!app.right_mouse_down && app.pointer_hit_dirty);
    CHECK(parameter("ParamMouseRightDown") == 0.0f);

    event = input(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamLeftHandDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_KEY_DOWN, "RightArrow", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamRightHandDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    event = input(BONGO_CAT_INPUT_KEY_UP, "RightArrow", 0.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamRightHandDown") == 0.0f);

    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", 0.5f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickLX") == 0.5f);
    CHECK(parameter("CatParamStickShowLeftHand") == 1.0f);
    CHECK(parameter("CatParamLeftHandDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftThumb", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickLeftDown") == 1.0f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickY", -0.5f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickLY") == 0.5f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickX", -0.75f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickRX") == -0.75f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickY", 0.25f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickRY") == -0.25f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(right_hand);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftTrigger2", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightTrigger2", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(left_trigger && right_trigger);
    event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftThumb", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(left_thumb);

    bongo_cat_app_reset_gamepad(&app);
    CHECK(parameter("CatParamStickLX") == 0.0f);
    CHECK(parameter("CatParamStickLeftDown") == 0.0f);
    CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    CHECK(!right_hand && !left_trigger && !right_trigger && !left_thumb);
    check_gamepad_poses(&app);
    return bongo_cat_test_failures ? 1 : 0;
}
