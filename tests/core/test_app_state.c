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
static bool active_motions[8];
static int active_expression = -1;
static int restored_motion_count;
int bongo_cat_test_failures;

static float parameter(const char *id) {
    for (size_t i = parameter_count; i > 0; --i)
        if (strcmp(parameters[i - 1].id, id) == 0) return parameters[i - 1].value;
    return -999.0f;
}

bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value) {
    (void)live2d;
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

bool bongo_cat_live2d_start_motion(BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d; (void)group;
    if (index < 0 || index >= (int)(sizeof(active_motions) /
        sizeof(active_motions[0]))) return false;
    active_motions[index] = true;
    return true;
}

bool bongo_cat_live2d_restore_motion_state(BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d; (void)group;
    if (index < 0 || index >= (int)(sizeof(active_motions) /
        sizeof(active_motions[0]))) return false;
    active_motions[index] = true;
    restored_motion_count++;
    return true;
}

bool bongo_cat_live2d_motion_selected(const BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d; (void)group;
    return index >= 0 && index < (int)(sizeof(active_motions) /
        sizeof(active_motions[0])) && active_motions[index];
}

bool bongo_cat_live2d_motion_persistent(const BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d; (void)group;
    return index == 1;
}

bool bongo_cat_live2d_motion_visible(const BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d; (void)group; (void)index;
    return true;
}

bool bongo_cat_live2d_motion_same_toggle(const BongoCatLive2D *live2d,
    const char *left_group, int left_index,
    const char *right_group, int right_index) {
    (void)live2d; (void)left_group; (void)left_index;
    (void)right_group; (void)right_index;
    return false;
}

bool bongo_cat_live2d_set_expression(BongoCatLive2D *live2d, int index) {
    (void)live2d;
    active_expression = index;
    return true;
}

int bongo_cat_live2d_expression(const BongoCatLive2D *live2d) {
    (void)live2d;
    return active_expression;
}

int bongo_cat_overlay_key(BongoCatOverlay *overlay, const char *name, bool pressed) {
    (void)overlay;
    if (strcmp(name, "KeyA") == 0 || strcmp(name, "DPadLeft") == 0)
        left_hand = pressed;
    else if (strcmp(name, "RightArrow") == 0 || strcmp(name, "South") == 0)
        right_hand = pressed;
    else if (strcmp(name, "LeftTrigger2") == 0) left_trigger = pressed;
    else if (strcmp(name, "RightTrigger2") == 0) right_trigger = pressed;
    else if (strcmp(name, "LeftThumb") == 0) left_thumb = pressed;
    return 0;
}

bool bongo_cat_overlay_hand_active(const BongoCatOverlay *overlay, bool right) {
    (void)overlay; return right ? right_hand : left_hand;
}

static BongoCatInputEvent input(BongoCatInputKind kind, const char *name, float value) {
    BongoCatInputEvent event = {.kind = kind, .value = value};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

static void check_behavior_state(BongoCatApp *app) {
    snprintf(app->loaded_model, sizeof(app->loaded_model), "model-a");
    app->behaviors.count = 3;
    app->behaviors.entries[0] = (BongoCatBehaviorEntry){
        .kind = BONGO_CAT_BEHAVIOR_MOTION, .index = 1};
    snprintf(app->behaviors.entries[0].id,
        sizeof(app->behaviors.entries[0].id), "model-a:motion:Tap:1");
    snprintf(app->behaviors.entries[0].group,
        sizeof(app->behaviors.entries[0].group), "Tap");
    app->behaviors.entries[1] = (BongoCatBehaviorEntry){
        .kind = BONGO_CAT_BEHAVIOR_MOTION, .index = 2};
    snprintf(app->behaviors.entries[1].id,
        sizeof(app->behaviors.entries[1].id), "model-a:motion:Tap:2");
    snprintf(app->behaviors.entries[1].group,
        sizeof(app->behaviors.entries[1].group), "Tap");
    app->behaviors.entries[2] = (BongoCatBehaviorEntry){
        .kind = BONGO_CAT_BEHAVIOR_EXPRESSION, .index = 3};
    snprintf(app->behaviors.entries[2].id,
        sizeof(app->behaviors.entries[2].id), "model-a:expression:3");

    app->session.active_behavior_count = 1;
    snprintf(app->session.active_behaviors[0].model_id,
        sizeof(app->session.active_behaviors[0].model_id), "model-b");
    snprintf(app->session.active_behaviors[0].behavior_id,
        sizeof(app->session.active_behaviors[0].behavior_id),
        "model-b:expression:1");
    active_motions[1] = true;
    active_motions[2] = true;
    active_expression = 3;
    CHECK(bongo_cat_app_selected_motion_count(app) == 2);
    bongo_cat_app_capture_behavior_state(app);
    CHECK(app->session.active_behavior_count == 3);
    CHECK(strcmp(app->session.active_behaviors[0].model_id,
        "model-b") == 0);
    CHECK(strcmp(app->session.active_behaviors[1].behavior_id,
        "model-a:motion:Tap:1") == 0);
    CHECK(strcmp(app->session.active_behaviors[2].behavior_id,
        "model-a:expression:3") == 0);

    active_motions[1] = false;
    active_motions[2] = false;
    active_expression = -1;
    bongo_cat_app_restore_behavior_state(app, "model-a");
    CHECK(active_motions[1]);
    CHECK(!active_motions[2]);
    CHECK(bongo_cat_app_selected_motion_count(app) == 1);
    CHECK(active_expression == 3);
    CHECK(restored_motion_count == 1);

    active_motions[1] = false;
    active_expression = -1;
    bongo_cat_app_capture_behavior_state(app);
    CHECK(app->session.active_behavior_count == 1);
    CHECK(strcmp(app->session.active_behaviors[0].model_id,
        "model-b") == 0);
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

    event = input(BONGO_CAT_INPUT_MOUSE_DOWN, "Back", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    event = input(BONGO_CAT_INPUT_MOUSE_DOWN, "Forward", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(app.side_mouse_down);
    event = input(BONGO_CAT_INPUT_MOUSE_UP, "Back", 0.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(app.side_mouse_down && app.forward_mouse_down);
    event = input(BONGO_CAT_INPUT_MOUSE_UP, "Forward", 0.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!app.side_mouse_down);

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
    CHECK(parameter("CatParamStickLY") == -0.5f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickX", -0.75f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickRX") == -0.75f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "RightStickY", 0.25f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickRY") == 0.25f);
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
    check_behavior_state(&app);
    return bongo_cat_test_failures ? 1 : 0;
}
