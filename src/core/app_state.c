#include "bongo_cat/app.h"
#include "bongo_cat/overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool gamepad_stick_axis(const char *name) {
    return !strcmp(name, "LeftStickX") || !strcmp(name, "LeftStickY") ||
        !strcmp(name, "RightStickX") || !strcmp(name, "RightStickY");
}

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
    /* A visual preference, independent of physical input and its deadzone. */
    bool gamepad = app->loaded_mode == BONGO_CAT_MODE_GAMEPAD;
    bool four_hands = gamepad &&
        app->settings.model.gamepad_four_hands;
    bool left_overlay = bongo_cat_overlay_hand_active(app->overlay, false);
    bool right_overlay = bongo_cat_overlay_hand_active(app->overlay, true);
    /* In the normal pose, an overlay paw takes priority over the stick paw
       on the same side. The explicit four-hands preference keeps both. */
    bool left_stick = gamepad && (four_hands ||
        (!left_overlay && stick_active(app->left_stick_x,
            app->left_stick_y, app->left_stick_pressed)));
    bool right_stick = gamepad && (four_hands ||
        (!right_overlay && stick_active(app->right_stick_x,
            app->right_stick_y, app->right_stick_pressed)));
    /* HandDown hides the resting paw so its input overlay can replace it.
       Four-hands mode keeps those resting paws alongside the stick hands;
       only a real button overlay should hide a resting paw in this mode. */
    bool left = (!four_hands && left_stick) || left_overlay;
    bool right = (!four_hands && right_stick) || right_overlay;
    app->input_diagnostics.hands_seen |= (left ? 1u : 0u) | (right ? 2u : 0u);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowLeftHand", left_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickShowRightHand", right_stick);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamLeftHandDown",
        left ? 1.0f : 0.0f);
    bongo_cat_live2d_set_parameter(app->live2d, "CatParamRightHandDown",
        right ? 1.0f : 0.0f);
}

void bongo_cat_app_refresh_hands(BongoCatApp *app) {
    if (!app || !app->live2d) return;
    update_hands(app);
    app->input_diagnostics.pending = true;
    app->dirty = true;
}

static bool apply_key(BongoCatApp *app, const char *name, bool pressed) {
    int hand = bongo_cat_overlay_key(app->overlay, name, pressed);
    if (hand < 0) return false;
    update_hands(app);
    app->dirty = true;
    return true;
}

static bool keyboard_hands_enabled(const BongoCatApp *app) {
    /* Authored keyboard simulation is the only gamepad-mode exception.
       Do not infer input support from leftover/shared overlay textures. */
    return app->loaded_mode != BONGO_CAT_MODE_GAMEPAD ||
        app->loaded_gamepad_keyboard;
}

static void set_axis(BongoCatApp *app, const char *id, float input) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return;
    float value = input * range.maximum;
    if (value < range.minimum) value = range.minimum;
    if (value > range.maximum) value = range.maximum;
    bongo_cat_live2d_set_parameter(app->live2d, id, value);
}

static void apply_gamepad_key(BongoCatApp *app, const char *name, bool pressed) {
    if (apply_key(app, name, pressed) && pressed)
        app->input_diagnostics.gamepad_overlays++;
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
        apply_gamepad_key(app, event->name, app->left_stick_pressed);
        bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickLeftDown",
            app->left_stick_pressed);
    } else if (strcmp(event->name, "RightThumb") == 0) {
        app->right_stick_pressed = event->value > 0.0f;
        apply_gamepad_key(app, event->name, app->right_stick_pressed);
        bongo_cat_live2d_set_parameter(app->live2d, "CatParamStickRightDown",
            app->right_stick_pressed);
    } else apply_gamepad_key(app, event->name, event->value > 0.05f);
    /* SDL reports stick up as negative Y; the authored model uses positive Y
       for upward movement. Keep the raw input state for replay/diagnostics. */
    if (id) set_axis(app, id, invert_y ? -event->value : event->value);
    update_hands(app);
    app->dirty = true;
}

void bongo_cat_app_reset_gamepad(BongoCatApp *app) {
    if (!app || !app->live2d) return;
    for (size_t i = app->sound_shortcut_state.count; i > 0; --i) {
        if (strncmp(app->sound_shortcut_state.held[i - 1], "Gamepad:", 8)) continue;
        --app->sound_shortcut_state.count;
        if (i - 1 != app->sound_shortcut_state.count)
            memcpy(app->sound_shortcut_state.held[i - 1],
                app->sound_shortcut_state.held[app->sound_shortcut_state.count], BONGO_CAT_ID_CAP);
    }
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorShortcut *binding = bongo_cat_app_behavior_binding(
            app, app->behaviors.entries[i].id);
        if (binding && strstr(binding->shortcut, "Gamepad:"))
            app->behaviors.entries[i].shortcut_active = false;
    }
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
    if (!app || !event) return;
    bool keyboard = event->kind == BONGO_CAT_INPUT_KEY_DOWN ||
        event->kind == BONGO_CAT_INPUT_KEY_UP;
    if (keyboard) app->input_diagnostics.received_keys++;
    else if (event->kind == BONGO_CAT_INPUT_MOUSE_DOWN ||
        event->kind == BONGO_CAT_INPUT_MOUSE_UP) app->input_diagnostics.mouse_buttons++;
    else if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON)
        app->input_diagnostics.gamepad_buttons++;
    else if (event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS)
        app->input_diagnostics.gamepad_axes++;
    else return;
    app->input_diagnostics.pending = true;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
        event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS) {
        /* Store only the last controller control, never a keyboard history. */
        memcpy(app->input_diagnostics.last_gamepad, event->name, sizeof(event->name));
        app->input_diagnostics.last_gamepad[sizeof(event->name) - 1] = '\0';
        app->input_diagnostics.last_gamepad_value = event->value;
    }
    if (!app->live2d) {
        if (keyboard) app->input_diagnostics.no_model_keys++;
        return;
    }
    BongoCatInputEvent filtered;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS &&
        gamepad_stick_axis(event->name) &&
        fabsf(event->value) <= BONGO_CAT_GAMEPAD_STICK_DEADZONE) {
        /* Filter before tracking held inputs AND applying model parameters.
           Returning to center must release the hand even if the hardware
           never reports exactly zero. Keep triggers and stick clicks intact. */
        filtered = *event;
        filtered.value = 0.0f;
        if (event->value != 0.0f) app->input_diagnostics.stick_deadzone_events++;
        event = &filtered;
    }
    active_input_update(app, event);
    switch (event->kind) {
    case BONGO_CAT_INPUT_KEY_DOWN:
    case BONGO_CAT_INPUT_KEY_UP:
        if (!keyboard_hands_enabled(app)) {
            app->input_diagnostics.ignored_keys++;
            app->input_diagnostics.mode_blocked_keys++;
            break;
        }
        if (apply_key(app, event->name, event->kind == BONGO_CAT_INPUT_KEY_DOWN)) {
            app->input_diagnostics.mapped_keys++;
            if (event->kind == BONGO_CAT_INPUT_KEY_DOWN)
                app->input_diagnostics.keyboard_overlays++;
        } else app->input_diagnostics.unmapped_keys++;
        break;
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
            if (strcmp(event->name, "Back") == 0) app->back_mouse_down = down;
            else app->forward_mouse_down = down;
            bool side_down = app->back_mouse_down || app->forward_mouse_down;
            changed = app->side_mouse_down != side_down;
            app->side_mouse_down = side_down;
        }
        if (changed) app->mouse_button_event_pending = true;
        if (!down) app->pointer_hit_dirty = true;
        if (side) {
            app->dirty = true;
            break;
        }
        const char *id = left
            ? "ParamMouseLeftDown" : "ParamMouseRightDown";
        bongo_cat_live2d_set_parameter(app->live2d, id, down ? 1.0f : 0.0f);
        app->dirty = true;
        break;
    }
    case BONGO_CAT_INPUT_GAMEPAD_BUTTON:
    case BONGO_CAT_INPUT_GAMEPAD_AXIS:
        if (app->loaded_mode == BONGO_CAT_MODE_GAMEPAD) apply_gamepad(app, event);
        else app->input_diagnostics.ignored_gamepad++;
        break;
    default: break;
    }
}

void bongo_cat_app_reapply_input(BongoCatApp *app) {
    if (!app || !app->live2d) return;
    app->input_diagnostics.replays++;
    app->input_diagnostics.pending = true;
    bongo_cat_live2d_set_parameter(app->live2d, "ParamMouseLeftDown",
        app->left_mouse_down ? 1.0f : 0.0f);
    bongo_cat_live2d_set_parameter(app->live2d, "ParamMouseRightDown",
        app->right_mouse_down ? 1.0f : 0.0f);
    for (size_t i = 0; i < app->active_input_count; ++i) {
        BongoCatInputEvent *event = &app->active_inputs[i];
        if (event->kind == BONGO_CAT_INPUT_KEY_DOWN) {
            if (!keyboard_hands_enabled(app)) app->input_diagnostics.replay_blocked_keys++;
            else if (apply_key(app, event->name, true)) app->input_diagnostics.keyboard_overlays++;
        } else if (app->loaded_mode == BONGO_CAT_MODE_GAMEPAD &&
            (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
             event->kind == BONGO_CAT_INPUT_GAMEPAD_AXIS))
            apply_gamepad(app, event);
    }
    update_hands(app);
    app->dirty = true;
}

const BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding(
    const BongoCatApp *app, const char *id) {
    if (!app || !id) return NULL;
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next) {
        size_t length = strlen(cache->model_id);
        if (strncmp(id, cache->model_id, length) || id[length] != ':') continue;
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next)
            if (!strcmp(node->binding.id, id)) return &node->binding;
        /* A loaded Mver model must never fall back to stale private bindings. */
        return NULL;
    }
    for (size_t i = 0; i < app->models.count; ++i) {
        const BongoCatModelEntry *model = &app->models.entries[i];
        size_t length = strlen(model->id);
        if ((model->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
            model->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH) &&
            !strncmp(id, model->id, length) && id[length] == ':') return NULL;
    }
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i)
        if (!app->settings.behavior_shortcuts[i].shortcut_external &&
            !strcmp(app->settings.behavior_shortcuts[i].id, id))
            return &app->settings.behavior_shortcuts[i];
    return NULL;
}
BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding_mut(BongoCatApp *app, const char *id) {
    return (BongoCatBehaviorShortcut *)bongo_cat_app_behavior_binding(app, id);
}
BongoCatBehaviorShortcut *bongo_cat_app_behavior_binding_target(BongoCatApp *app, const char *target) {
    if (!app || !target) return NULL;
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next)
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next)
            if (node->binding.shortcut == target) return &node->binding;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i)
        if (app->settings.behavior_shortcuts[i].shortcut == target)
            return &app->settings.behavior_shortcuts[i];
    return NULL;
}
const char *bongo_cat_app_behavior_label(const BongoCatApp *app, const char *id) {
    if (!app || !id) return NULL;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        if (!strcmp(value->id, id) && value->label[0]) return value->label;
    }
    const BongoCatBehaviorShortcut *value = bongo_cat_app_behavior_binding(app, id);
    return value && value->label[0] ? value->label : NULL;
}
static void free_shortcut_cache(BongoCatModelShortcutCache *cache) {
    while (cache->bindings) {
        BongoCatModelShortcutNode *node = cache->bindings;
        cache->bindings = node->next;
        free(node);
    }
    free(cache);
}
void bongo_cat_app_model_shortcuts_clear(BongoCatApp *app) {
    if (!app) return;
    while (app->model_shortcuts) {
        BongoCatModelShortcutCache *cache = app->model_shortcuts;
        app->model_shortcuts = cache->next;
        free_shortcut_cache(cache);
    }
}
/* Call after cancelling UI capture: removed models no longer own live pointers. */
void bongo_cat_app_model_shortcuts_prune(BongoCatApp *app) {
    if (!app) return;
    BongoCatModelShortcutCache **link = &app->model_shortcuts;
    while (*link) {
        BongoCatModelShortcutCache *cache = *link;
        bool present = !strcmp(cache->model_id, app->loaded_model);
        for (size_t i = 0; i < app->models.count; ++i) {
            const BongoCatModelEntry *model = &app->models.entries[i];
            if (!strcmp(model->id, cache->model_id) &&
                (model->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
                 model->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH)) {
                present = true; break;
            }
        }
        if (present) link = &cache->next;
        else { *link = cache->next; free_shortcut_cache(cache); }
    }
}
bool bongo_cat_app_shortcut_conflicts(BongoCatApp *app,
    const char *shortcut, const char *exclude) {
    if (!app || !shortcut || !*shortcut) return false;
    const BongoCatShortcutPreferences *global = &app->settings.shortcuts;
    const char *keys[] = {global->toggle_pet_visibility, global->visible_preferences,
        global->mirror, global->pass_through, global->always_on_top,
        global->open_menu};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (keys[i] != exclude && bongo_cat_shortcut_equal(keys[i], shortcut)) return true;
    /* Model actions may intentionally share a key. */
    if (bongo_cat_app_behavior_binding_target(app, exclude)) return false;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        if (!value->shortcut_external && !value->shortcut_disabled &&
            bongo_cat_shortcut_equal(value->shortcut, shortcut)) return true;
    }
    for (BongoCatModelShortcutCache *cache = app->model_shortcuts; cache; cache = cache->next)
        for (BongoCatModelShortcutNode *node = cache->bindings; node; node = node->next)
            if (!node->binding.shortcut_disabled && bongo_cat_shortcut_equal(node->binding.shortcut, shortcut)) return true;
    return false;
}

void bongo_cat_app_reset_sound_bindings(BongoCatApp *app) {
    if (app) for (size_t i = 0; i < app->behaviors.count; ++i)
        app->behaviors.entries[i].shortcut_active = false;
}
