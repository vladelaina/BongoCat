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
static size_t overlay_key_calls;
int bongo_cat_test_failures;

static float parameter(const char *id) {
    for (size_t i = parameter_count; i > 0; --i)
        if (strcmp(parameters[i - 1].id, id) == 0) return parameters[i - 1].value;
    return -999.0f;
}

bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d, const char *id, float value) {
    (void)live2d;
    for (size_t i = 0; i < parameter_count; ++i)
        if (!strcmp(parameters[i].id, id)) {
            parameters[i].value = value;
            return true;
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
    overlay_key_calls++;
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
    BongoCatBehaviorEntry entries[3] = {0};
    app->behaviors.entries = entries;
    app->behaviors.capacity = 3;
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
    app->behaviors = (BongoCatBehaviorCatalog){0};
}

static void check_input_modes(void) {
    static BongoCatApp app;
    app.live2d = (BongoCatLive2D *)(uintptr_t)1;
    app.overlay = (BongoCatOverlay *)(uintptr_t)1;
    const BongoCatModelMode modes[] = {BONGO_CAT_MODE_STANDARD,
        BONGO_CAT_MODE_KEYBOARD, BONGO_CAT_MODE_GAMEPAD};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        app.loaded_mode = modes[i];
        parameter_count = overlay_key_calls = 0;
        BongoCatInputEvent event = input(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
        bongo_cat_app_apply_input(&app, &event);
        CHECK(left_hand == (modes[i] != BONGO_CAT_MODE_GAMEPAD));
        CHECK(overlay_key_calls == (modes[i] == BONGO_CAT_MODE_GAMEPAD ? 0 : 1));
        CHECK(app.active_input_count == 1);
        event.kind = BONGO_CAT_INPUT_KEY_UP;
        bongo_cat_app_apply_input(&app, &event);
        CHECK(!left_hand && app.active_input_count == 0);
        /* A quick press/release must remain visible in the next log summary. */
        CHECK(app.input_diagnostics.pending);
        CHECK(app.input_diagnostics.hands_seen ==
            (modes[i] == BONGO_CAT_MODE_GAMEPAD ? 0u : 1u));
        app.input_diagnostics.hands_seen = 0;
        CHECK(overlay_key_calls == (modes[i] == BONGO_CAT_MODE_GAMEPAD ? 0 : 2));
        size_t calls = overlay_key_calls;
        event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1.0f);
        bongo_cat_app_apply_input(&app, &event);
        CHECK(right_hand == (modes[i] == BONGO_CAT_MODE_GAMEPAD));
        CHECK(overlay_key_calls == calls + (modes[i] == BONGO_CAT_MODE_GAMEPAD ? 1 : 0));
        event.value = 0.0f;
        bongo_cat_app_apply_input(&app, &event);
        CHECK(!right_hand && app.active_input_count == 0);
        CHECK(app.input_diagnostics.hands_seen ==
            (modes[i] == BONGO_CAT_MODE_GAMEPAD ? 2u : 0u));
        app.input_diagnostics.hands_seen = 0;
    }
    CHECK(app.input_diagnostics.mode_blocked_keys == 2);
    CHECK(app.input_diagnostics.keyboard_overlays == 2);
    CHECK(app.input_diagnostics.gamepad_overlays == 1);
    CHECK(app.input_diagnostics.gamepad_buttons == 6);
    CHECK(app.input_diagnostics.ignored_gamepad == 4);
    /* A key held while switching models must not restore keyboard art in
       gamepad mode, but must still be available when switching back. */
    app.loaded_mode = BONGO_CAT_MODE_KEYBOARD;
    BongoCatInputEvent event = input(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    left_hand = false; /* Loading another model clears its overlay. */
    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
    overlay_key_calls = 0;
    bongo_cat_app_reapply_input(&app);
    CHECK(!left_hand && overlay_key_calls == 0);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    app.loaded_mode = BONGO_CAT_MODE_KEYBOARD;
    bongo_cat_app_reapply_input(&app);
    CHECK(left_hand);
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_app_apply_input(&app, &event);

    /* Preserve an explicitly authored Mver keyboard-simulated controller. */
    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
    app.loaded_gamepad_keyboard = true;
    event.kind = BONGO_CAT_INPUT_KEY_DOWN;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(left_hand);
    left_hand = false;
    bongo_cat_app_reapply_input(&app);
    CHECK(left_hand);
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!left_hand && app.active_input_count == 0);
    parameter_count = overlay_key_calls = 0;
}

static void check_stick_deadzone(void) {
    static BongoCatApp app;
    app.live2d = (BongoCatLive2D *)(uintptr_t)1;
    app.overlay = (BongoCatOverlay *)(uintptr_t)1;
    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
    static const struct { const char *name, *parameter; float drift; } axes[] = {
        {"LeftStickX", "CatParamStickLX", 0.000f},
        {"LeftStickY", "CatParamStickLY", 0.019f},
        {"RightStickX", "CatParamStickRX", 0.028f},
        {"RightStickY", "CatParamStickRY", 0.018f}
    };
    /* Actual idle values from the affected Xbox controller's diagnostic log. */
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        BongoCatInputEvent event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS,
            axes[i].name, axes[i].drift);
        bongo_cat_app_apply_input(&app, &event);
        CHECK(parameter(axes[i].parameter) == 0.0f);
    }
    CHECK(app.active_input_count == 0);
    CHECK(app.input_diagnostics.stick_deadzone_events == 3);
    CHECK(app.input_diagnostics.last_gamepad_value == 0.018f);
    CHECK(app.input_diagnostics.hands_seen == 0);
    bongo_cat_app_reapply_input(&app);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    CHECK(parameter("CatParamRightHandDown") == 0.0f);

    /* Center noise must also release an already moving hand and its held axis. */
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        parameter_count = 0;
        float moved = i % 2 ? -0.5f : 0.5f;
        BongoCatInputEvent event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, axes[i].name, moved);
        bongo_cat_app_apply_input(&app, &event);
        CHECK(parameter(axes[i].parameter) == (i % 2 ? -moved : moved));
        CHECK(app.active_input_count == 1 && app.active_inputs[0].value == moved);
        CHECK(parameter(i < 2 ? "CatParamStickShowLeftHand" : "CatParamStickShowRightHand") == 1.0f);
        event.value = i % 2 ? -0.028f : 0.019f;
        bongo_cat_app_apply_input(&app, &event);
        CHECK(parameter(axes[i].parameter) == 0.0f);
        CHECK(app.active_input_count == 0);
        bongo_cat_app_reapply_input(&app);
        CHECK(parameter("CatParamLeftHandDown") == 0.0f);
        CHECK(parameter("CatParamRightHandDown") == 0.0f);
    }
    const float boundaries[] = {-0.10f, 0.10f, -0.101f, 0.101f};
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        parameter_count = 0;
        BongoCatInputEvent event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS,
            "LeftStickX", boundaries[i]);
        bongo_cat_app_apply_input(&app, &event);
        CHECK(parameter("CatParamStickLX") == (i < 2 ? 0.0f : boundaries[i]));
        CHECK(app.active_input_count == (i < 2 ? 0u : 1u));
    }
    parameter_count = 0;
    BongoCatInputEvent center = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", 0.019f);
    bongo_cat_app_apply_input(&app, &center);
    BongoCatInputEvent event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "LeftThumb", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    bongo_cat_app_apply_input(&app, &center);
    CHECK(parameter("CatParamStickShowLeftHand") == 1.0f && left_thumb);
    event.value = 0.0f;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(parameter("CatParamStickShowLeftHand") == 0.0f && !left_thumb);
    CHECK(app.active_input_count == 0);

    /* Stick centering must not clear a still-held D-pad/button overlay. */
    event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "DPadLeft", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    bongo_cat_app_apply_input(&app, &center);
    CHECK(left_hand && parameter("CatParamLeftHandDown") == 1.0f);
    CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
    event.value = 0.0f;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!left_hand && parameter("CatParamLeftHandDown") == 0.0f);

    /* Analog triggers use their own threshold, not the stick dead zone. */
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftTrigger2", 0.06f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(left_trigger && app.active_input_count == 1);
    event.value = 0.0f;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!left_trigger && app.active_input_count == 0);
    parameter_count = overlay_key_calls = 0;
}

static void check_four_hands(void) {
    static BongoCatApp app;
    app.live2d = (BongoCatLive2D *)(uintptr_t)1;
    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
    bongo_cat_app_refresh_hands(&app);
    CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
    app.settings.model.gamepad_four_hands = true;
    bongo_cat_app_refresh_hands(&app);
    CHECK(parameter("CatParamStickShowLeftHand") == 1.0f);
    CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f &&
        parameter("CatParamRightHandDown") == 0.0f && app.active_input_count == 0);
    CHECK(app.dirty && app.input_diagnostics.pending);
    CHECK(overlay_key_calls == 0 && app.input_diagnostics.replays == 0);
    BongoCatInputEvent event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", .019f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(app.left_stick_x == 0.0f && app.active_input_count == 0);
    CHECK(parameter("CatParamStickShowLeftHand") == 1.0f);
    event = input(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!left_hand); /* feature does not enable keyboard overlays */
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_app_apply_input(&app, &event);
    bongo_cat_app_reset_gamepad(&app); /* visible even without a controller */
    CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    CHECK(parameter("CatParamRightHandDown") == 0.0f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1.0f);
    bongo_cat_app_apply_input(&app, &event);
    CHECK(right_hand && parameter("CatParamRightHandDown") == 1.0f);
    CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
    size_t calls = overlay_key_calls;
    bongo_cat_app_refresh_hands(&app);
    CHECK(overlay_key_calls == calls && right_hand && app.active_input_count == 1);
    event.value = 0.0f;
    bongo_cat_app_apply_input(&app, &event);
    CHECK(!right_hand && parameter("CatParamRightHandDown") == 0.0f);
    CHECK(parameter("CatParamStickShowRightHand") == 1.0f);
    app.settings.model.gamepad_four_hands = false;
    bongo_cat_app_refresh_hands(&app);
    CHECK(parameter("CatParamLeftHandDown") == 0.0f);
    CHECK(parameter("CatParamRightHandDown") == 0.0f);
    event = input(BONGO_CAT_INPUT_GAMEPAD_AXIS, "LeftStickX", .5f);
    bongo_cat_app_apply_input(&app, &event);
    app.settings.model.gamepad_four_hands = true;
    bongo_cat_app_refresh_hands(&app);
    app.settings.model.gamepad_four_hands = false;
    bongo_cat_app_refresh_hands(&app);
    CHECK(app.left_stick_x == .5f); /* switching off preserves real input */
    CHECK(parameter("CatParamStickShowLeftHand") == 1.0f);
    CHECK(parameter("CatParamStickShowRightHand") == 0.0f);
    bongo_cat_app_reset_gamepad(&app);
    app.settings.model.gamepad_four_hands = true;
    app.left_stick_x = .5f; /* stale controller state cannot leak to other modes */
    const BongoCatModelMode modes[] = {BONGO_CAT_MODE_STANDARD, BONGO_CAT_MODE_KEYBOARD};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        app.loaded_mode = modes[i];
        bongo_cat_app_refresh_hands(&app);
        CHECK(parameter("CatParamStickShowLeftHand") == 0.0f);
        CHECK(parameter("CatParamStickShowRightHand") == 0.0f);
    }
    parameter_count = overlay_key_calls = 0;
}

int main(void) {
    check_four_hands();
    check_stick_deadzone();
    check_input_modes();
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

    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
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
    check_behavior_state(&app);
    return bongo_cat_test_failures ? 1 : 0;
}
