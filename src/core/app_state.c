#include "bongo_cat/app.h"
#include "bongo_cat/overlay.h"

#include <math.h>
#include <string.h>

static size_t active_input_index(const BongoCatApp *app,
    BongoCatInputKind kind, const char *name) {
    if (!app || !name) return app ? app->active_input_count : 0;
    for (size_t i = 0; i < app->active_input_count; ++i) {
        const BongoCatInputEvent *active = &app->active_inputs[i];
        if (active->kind == kind && strcmp(active->name, name) == 0) return i;
    }
    return app->active_input_count;
}

static void active_input_remove(BongoCatApp *app, size_t index) {
    if (!app || index >= app->active_input_count) return;
    if (index + 1 < app->active_input_count)
        memmove(&app->active_inputs[index], &app->active_inputs[index + 1],
            (app->active_input_count - index - 1) * sizeof(app->active_inputs[0]));
    memset(&app->active_inputs[--app->active_input_count], 0,
        sizeof(app->active_inputs[0]));
}

static void active_input_update(BongoCatApp *app,
    const BongoCatInputEvent *event) {
    if (!app || !event || !event->name[0]) return;
    bool tracked = event->kind == BONGO_CAT_INPUT_KEY_DOWN ||
        event->kind == BONGO_CAT_INPUT_KEY_UP ||
        event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
        event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS;
    if (!tracked) return;
    bool active = event->kind == BONGO_CAT_INPUT_KEY_DOWN ||
        (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON && event->value > 0.5f) ||
        (event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS && fabsf(event->value) > .001f);
    BongoCatInputKind kind = event->kind == BONGO_CAT_INPUT_KEY_DOWN ||
        event->kind == BONGO_CAT_INPUT_KEY_UP
        ? BONGO_CAT_INPUT_KEY_DOWN : event->kind;
    size_t index = active_input_index(app, kind, event->name);
    if (!active) { active_input_remove(app, index); return; }
    if (index == app->active_input_count) {
        if (app->active_input_count >=
            sizeof(app->active_inputs) / sizeof(app->active_inputs[0])) return;
        index = app->active_input_count++;
    }
    app->active_inputs[index] = *event;
    app->active_inputs[index].kind = kind;
}

static bool stick_active(float x, float y, bool pressed) {
    return pressed || fabsf(x) > 0.001f || fabsf(y) > 0.001f;
}

static void update_hands(BongoCatApp *app) {
    bool left_stick = stick_active(app->left_stick_x,
        app->left_stick_y, app->left_stick_pressed);
    bool right_stick = stick_active(app->right_stick_x,
        app->right_stick_y, app->right_stick_pressed);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowLeftHand", left_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowRightHand", right_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamLeftHandDown",
        (left_stick || bongo_cat_overlay_hand_active(app->overlay, false)) ? 1.0f : 0.0f);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamRightHandDown",
        (right_stick || bongo_cat_overlay_hand_active(app->overlay, true)) ? 1.0f : 0.0f);
}

static void apply_key(BongoCatApp *app, const char *name, bool pressed) {
    int hand = bongo_cat_overlay_key(app->overlay, name, pressed);
    if (hand < 0) {
        app->input_key_unsupported++;
        return;
    }
    app->input_key_supported++;
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
    if (strcmp(event->name, "LeftStickX") == 0) {
        id = "CatParamStickLX"; app->left_stick_x = event->value;
    } else if (strcmp(event->name, "LeftStickY") == 0) {
        id = "CatParamStickLY"; app->left_stick_y = event->value;
    } else if (strcmp(event->name, "RightStickX") == 0) {
        id = "CatParamStickRX"; app->right_stick_x = event->value;
    } else if (strcmp(event->name, "RightStickY") == 0) {
        id = "CatParamStickRY"; app->right_stick_y = event->value;
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
    if (id) set_axis(app, id, event->value);
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
    for (size_t i = app->active_input_count; i > 0; --i)
        if (app->active_inputs[i - 1].kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
            app->active_inputs[i - 1].kind == BONGO_CAT_INPUT_GAMEPAD_AXIS)
            active_input_remove(app, i - 1);
    update_hands(app);
    app->dirty = true;
}

void bongo_cat_app_apply_input(BongoCatApp *app, const BongoCatInputEvent *event) {
    if (!app || !event || !app->live2d) return;
    active_input_update(app, event);
    switch (event->kind) {
    case BONGO_CAT_INPUT_KEY_DOWN: apply_key(app, event->name, true); break;
    case BONGO_CAT_INPUT_KEY_UP: apply_key(app, event->name, false); break;
    case BONGO_CAT_INPUT_MOUSE_DOWN:
    case BONGO_CAT_INPUT_MOUSE_UP: {
        bool left = strcmp(event->name, "Left") == 0;
        bool right = strcmp(event->name, "Right") == 0;
        bool side = strcmp(event->name, "Back") == 0 ||
            strcmp(event->name, "Forward") == 0;
        if (!left && !right && !side)
            break;
        bool down = event->kind == BONGO_CAT_INPUT_MOUSE_DOWN;
        bool changed;
        if (left) {
            changed = app->left_mouse_down != down;
            app->left_mouse_down = down;
        } else if (right) {
            changed = app->right_mouse_down != down;
            app->right_mouse_down = down;
        } else {
            changed = app->side_mouse_down != down;
            app->side_mouse_down = down;
        }
        if (changed) app->mouse_button_event_pending = true;
        if (!down) app->pointer_hit_dirty = true;
        if (side) {
            app->input_mouse_applied++;
            app->dirty = true;
            break;
        }
        const char *id = left
            ? "ParamMouseLeftDown" : "ParamMouseRightDown";
        bongo_cat_live2d_set_parameter(app->live2d, id, down ? 1.0f : 0.0f);
        app->input_mouse_applied++;
        app->dirty = true;
        break;
    }
    case BONGO_CAT_INPUT_GAMEPAD_BUTTON:
    case BONGO_CAT_INPUT_GAMEPAD_AXIS: apply_gamepad(app, event); break;
    default: break;
    }
}

void bongo_cat_app_reapply_input(BongoCatApp *app) {
    if (!app || !app->live2d) return;
    bongo_cat_live2d_set_parameter(app->live2d, "ParamMouseLeftDown",
        app->left_mouse_down ? 1.0f : 0.0f);
    bongo_cat_live2d_set_parameter(app->live2d, "ParamMouseRightDown",
        app->right_mouse_down ? 1.0f : 0.0f);
    for (size_t i = 0; i < app->active_input_count; ++i) {
        BongoCatInputEvent *event = &app->active_inputs[i];
        if (event->kind == BONGO_CAT_INPUT_KEY_DOWN)
            apply_key(app, event->name, true);
        else if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
            event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS)
            apply_gamepad(app, event);
    }
    update_hands(app);
    app->dirty = true;
}
