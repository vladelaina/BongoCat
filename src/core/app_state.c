#include "bongo_cat/app.h"
#include "bongo_cat/overlay.h"

#include <math.h>
#include <string.h>

static bool stick_active(float x, float y, bool pressed) {
    return pressed || fabsf(x) > 0.001f || fabsf(y) > 0.001f;
}

static void update_hands(BongoCatApp *app) {
    bool left_overlay = bongo_cat_overlay_hand_active(app->overlay, false);
    bool right_overlay = bongo_cat_overlay_hand_active(app->overlay, true);
    /* An input overlay replaces the stick paw on the same side. */
    bool left_stick = !left_overlay && stick_active(app->left_stick_x,
        app->left_stick_y, app->left_stick_pressed);
    bool right_stick = !right_overlay && stick_active(app->right_stick_x,
        app->right_stick_y, app->right_stick_pressed);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowLeftHand", left_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowRightHand", right_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamLeftHandDown",
        (left_stick || left_overlay) ? 1.0f : 0.0f);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamRightHandDown",
        (right_stick || right_overlay) ? 1.0f : 0.0f);
}

static void apply_key(BongoCatApp *app, const char *name, bool pressed) {
    if (bongo_cat_overlay_key(app->overlay, name, pressed) < 0) return;
    update_hands(app);
    app->dirty = true;
}

static void set_axis(BongoCatApp *app, const char *id, float input) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return;
    float value = input * range.maximum;
    if (value < range.minimum) value = range.minimum;
    if (value > range.maximum) value = range.maximum;
    bongo_cat_live2d_set_parameter(app->live2d, id, value);
}

static void apply_gamepad(BongoCatApp *app, const BongoCatInputEvent *event) {
    const char *id = NULL;
    bool invert_y = false;
    if (strcmp(event->name, "LeftStickX") == 0) {
        id = "CatParamStickLX"; app->left_stick_x = event->value;
    } else if (strcmp(event->name, "LeftStickY") == 0) {
        id = "CatParamStickLY"; app->left_stick_y = event->value;
        invert_y = true;
    } else if (strcmp(event->name, "RightStickX") == 0) {
        id = "CatParamStickRX"; app->right_stick_x = event->value;
    } else if (strcmp(event->name, "RightStickY") == 0) {
        id = "CatParamStickRY"; app->right_stick_y = event->value;
        invert_y = true;
    } else if (strcmp(event->name, "LeftThumb") == 0) {
        app->left_stick_pressed = event->value > 0.0f;
        apply_key(app, event->name, app->left_stick_pressed);
        bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickLeftDown",
            app->left_stick_pressed);
    } else if (strcmp(event->name, "RightThumb") == 0) {
        app->right_stick_pressed = event->value > 0.0f;
        apply_key(app, event->name, app->right_stick_pressed);
        bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickRightDown",
            app->right_stick_pressed);
    } else apply_key(app, event->name, event->value > 0.05f);
    /* SDL up is negative Y; the model uses positive Y for upward movement.
       Keep the raw axis state unchanged. */
    if (id) set_axis(app, id, invert_y ? -event->value : event->value);
    update_hands(app);
    app->dirty = true;
}

void bongo_cat_app_reset_gamepad(BongoCatApp *app) {
    if (!app || !app->live2d) return;
    app->left_stick_x = app->left_stick_y = 0.0f;
    app->right_stick_x = app->right_stick_y = 0.0f;
    app->left_stick_pressed = app->right_stick_pressed = false;
    const char *parameters[] = {"CatParamStickLX", "CatParamStickLY",
        "CatParamStickRX", "CatParamStickRY", "CatParamStickLeftDown",
        "CatParamStickRightDown"};
    for (size_t i = 0; i < sizeof(parameters) / sizeof(parameters[0]); ++i)
        bongo_cat_live2d_set_parameter(app->live2d, parameters[i], 0.0f);
    const char *buttons[] = {"South", "East", "West", "North", "Select", "Mode",
        "Start", "LeftTrigger", "RightTrigger", "LeftTrigger2", "RightTrigger2",
        "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "Misc1", "Misc2",
        "Misc3", "Misc4", "Misc5", "Misc6", "RightPaddle1", "LeftPaddle1",
        "RightPaddle2", "LeftPaddle2", "Touchpad", "LeftThumb", "RightThumb"};
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        apply_key(app, buttons[i], false);
    update_hands(app);
    app->dirty = true;
}

void bongo_cat_app_apply_input(BongoCatApp *app, const BongoCatInputEvent *event) {
    if (!app || !event || !app->live2d) return;
    switch (event->kind) {
    case BONGO_CAT_INPUT_KEY_DOWN: apply_key(app, event->name, true); break;
    case BONGO_CAT_INPUT_KEY_UP: apply_key(app, event->name, false); break;
    case BONGO_CAT_INPUT_MOUSE_DOWN:
    case BONGO_CAT_INPUT_MOUSE_UP: {
        if (strcmp(event->name, "Left") != 0 && strcmp(event->name, "Right") != 0)
            break;
        bool down = event->kind == BONGO_CAT_INPUT_MOUSE_DOWN;
        if (strcmp(event->name, "Left") == 0) app->left_mouse_down = down;
        else app->right_mouse_down = down;
        if (!down) app->pointer_hit_dirty = true;
        const char *id = strcmp(event->name, "Left") == 0
            ? "ParamMouseLeftDown" : "ParamMouseRightDown";
        bongo_cat_live2d_set_parameter(app->live2d, id, down ? 1.0f : 0.0f);
        app->dirty = true;
        break;
    }
    case BONGO_CAT_INPUT_GAMEPAD_BUTTON:
    case BONGO_CAT_INPUT_GAMEPAD_AXIS: apply_gamepad(app, event); break;
    default: break;
    }
}
