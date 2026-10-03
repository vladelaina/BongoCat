#include "runtime.h"
#include "bongo_cat/log.h"
#include "bongo_cat/overlay.h"

#include <stdio.h>
#include <string.h>

const char *bongo_cat_gamepad_axis_name(Uint8 axis) {
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX: return "LeftStickX";
    case SDL_GAMEPAD_AXIS_LEFTY: return "LeftStickY";
    case SDL_GAMEPAD_AXIS_RIGHTX: return "RightStickX";
    case SDL_GAMEPAD_AXIS_RIGHTY: return "RightStickY";
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "LeftTrigger2";
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "RightTrigger2";
    default: return "UnknownAxis";
    }
}

const char *bongo_cat_gamepad_button_name(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "South";
    case SDL_GAMEPAD_BUTTON_EAST: return "East";
    case SDL_GAMEPAD_BUTTON_WEST: return "West";
    case SDL_GAMEPAD_BUTTON_NORTH: return "North";
    case SDL_GAMEPAD_BUTTON_BACK: return "Select";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Mode";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LeftThumb";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RightThumb";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LeftTrigger";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RightTrigger";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "DPadUp";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "DPadDown";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "DPadLeft";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "DPadRight";
    case SDL_GAMEPAD_BUTTON_MISC1: return "Misc1";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "RightPaddle1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "LeftPaddle1";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "RightPaddle2";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "LeftPaddle2";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    case SDL_GAMEPAD_BUTTON_MISC2: return "Misc2";
    case SDL_GAMEPAD_BUTTON_MISC3: return "Misc3";
    case SDL_GAMEPAD_BUTTON_MISC4: return "Misc4";
    case SDL_GAMEPAD_BUTTON_MISC5: return "Misc5";
    case SDL_GAMEPAD_BUTTON_MISC6: return "Misc6";
    default: return "UnknownButton";
    }
}

static void close_inactive_gamepads(BongoCatApp *app, const SDL_JoystickID *ids,
    int count) {
    for (int i = 0; ids && i < count; ++i) {
        SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[i]);
        if (gamepad && ids[i] != app->active_gamepad) SDL_CloseGamepad(gamepad);
    }
}

static void log_gamepad(BongoCatApp *app, const char *reason) {
    SDL_Gamepad *gamepad = app->active_gamepad ? SDL_GetGamepadFromID(app->active_gamepad) : NULL;
    const char *name = gamepad ? SDL_GetGamepadName(gamepad) : NULL;
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] gamepad-device reason=%s model=%s id=%u name=%s "
        "vendor=0x%04x product=0x%04x sdl_virtual=%d",
        reason, app->loaded_model, (unsigned)app->active_gamepad,
        name ? name : "none", gamepad ? (unsigned)SDL_GetGamepadVendor(gamepad) : 0u,
        gamepad ? (unsigned)SDL_GetGamepadProduct(gamepad) : 0u,
        gamepad && SDL_IsJoystickVirtual(app->active_gamepad));
}

static float axis_value(Sint16 value) {
    return value < 0 ? value / 32768.0f : value / 32767.0f;
}

static void synchronize_gamepad(BongoCatApp *app, const char *reason) {
    Sint16 axes[SDL_GAMEPAD_AXIS_COUNT] = {0};
    bool buttons[SDL_GAMEPAD_BUTTON_COUNT] = {0};
    /* Loading pumps SDL but does not dispatch controller events. Replace the
       old input backlog and cached pose with one coherent current snapshot.
       Keep device events (especially removal) and all non-controller events. */
    SDL_LockJoysticks();
    SDL_UpdateGamepads();
    SDL_Gamepad *gamepad = SDL_GetGamepadFromID(app->active_gamepad);
    bool connected = gamepad && SDL_GamepadConnected(gamepad);
    if (connected) {
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
            axes[i] = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)i);
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
            buttons[i] = SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)i);
    }
    SDL_FlushEvents(SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_UP);
    SDL_UnlockJoysticks();

    /* Use the normal input path so snapshots also receive stick deadzones.
       Zero values release stale buttons/axes before the model's first frame. */
    BongoCatInputEvent input = {.timestamp_ms = SDL_GetTicks()};
    input.kind = BONGO_CAT_INPUT_GAMEPAD_AXIS;
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
        snprintf(input.name, sizeof(input.name), "%s",
            bongo_cat_gamepad_axis_name((Uint8)i));
        if (!strncmp(input.name, "Unknown", 7)) continue;
        input.value = axis_value(axes[i]);
        bongo_cat_app_apply_input(app, &input);
    }
    unsigned pressed = 0;
    input.kind = BONGO_CAT_INPUT_GAMEPAD_BUTTON;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
        snprintf(input.name, sizeof(input.name), "%s",
            bongo_cat_gamepad_button_name((Uint8)i));
        if (!strncmp(input.name, "Unknown", 7)) continue;
        input.value = buttons[i] ? 1.0f : 0.0f;
        pressed += buttons[i] ? 1u : 0u;
        bongo_cat_app_apply_input(app, &input);
        /* Remember held chords without replaying sounds or toggle actions. */
        bongo_cat_sound_shortcut_update(&app->sound_shortcut_state, &input);
    }
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        BongoCatBehaviorEntry *behavior = &app->behaviors.entries[i];
        const BongoCatBehaviorShortcut *binding =
            bongo_cat_app_behavior_binding(app, behavior->id);
        if (!binding || !strstr(binding->shortcut, "Gamepad:")) continue;
        bool down = !binding->shortcut_disabled &&
            bongo_cat_sound_shortcut_down(&app->sound_shortcut_state, binding->shortcut);
        if (!down && behavior->shortcut_active && behavior->momentary &&
            behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT)
            bongo_cat_overlay_effect(app->overlay, NULL);
        behavior->shortcut_active = down;
    }
    /* Only on model/device transitions, never for individual input events.
       Input totals include the axes/buttons sampled above. */
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] gamepad-snapshot reason=%s model=%s id=%u connected=%d "
        "sampled_axes=%d sampled_buttons=%d buttons_down=%u "
        "sticks_raw=%.3f,%.3f,%.3f,%.3f sticks=%.3f,%.3f,%.3f,%.3f",
        reason, app->loaded_model, (unsigned)app->active_gamepad, connected,
        (int)SDL_GAMEPAD_AXIS_COUNT, (int)SDL_GAMEPAD_BUTTON_COUNT, pressed,
        axis_value(axes[SDL_GAMEPAD_AXIS_LEFTX]), axis_value(axes[SDL_GAMEPAD_AXIS_LEFTY]),
        axis_value(axes[SDL_GAMEPAD_AXIS_RIGHTX]), axis_value(axes[SDL_GAMEPAD_AXIS_RIGHTY]),
        app->left_stick_x, app->left_stick_y, app->right_stick_x, app->right_stick_y);
}

static bool select_first_gamepad(BongoCatApp *app) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_JoystickID selected = 0;
    for (int i = 0; ids && i < count && !selected; ++i) {
        SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[i]);
        if (!gamepad) gamepad = SDL_OpenGamepad(ids[i]);
        if (gamepad) selected = ids[i];
    }
    app->active_gamepad = selected;
    log_gamepad(app, "select");
    close_inactive_gamepads(app, ids, count);
    SDL_free(ids);
    if (selected) synchronize_gamepad(app, "select");
    return selected != 0;
}

void bongo_cat_gamepads_set_enabled(BongoCatApp *app, bool enabled) {
    if (!app) return;
    bool initialized = (SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0;
    if (enabled && !initialized) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                "Gamepad initialization failed: %s", SDL_GetError());
            app->active_gamepad = 0;
            bongo_cat_app_reset_gamepad(app);
            return;
        }
        initialized = true;
    }
    int count = 0;
    SDL_JoystickID *ids = initialized ? SDL_GetGamepads(&count) : NULL;
    bool active_connected = false;
    if (!enabled && app->active_gamepad) log_gamepad(app, "disable");
    for (int i = 0; ids && i < count; ++i) {
        if (ids[i] == app->active_gamepad && SDL_GetGamepadFromID(ids[i]))
            active_connected = true;
        if (!enabled) {
            SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[i]);
            if (gamepad) SDL_CloseGamepad(gamepad);
        }
    }
    SDL_free(ids);
    if (!enabled) {
        app->active_gamepad = 0;
        bongo_cat_app_reset_gamepad(app);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    } else if (active_connected) {
        ids = SDL_GetGamepads(&count);
        close_inactive_gamepads(app, ids, count);
        SDL_free(ids);
        synchronize_gamepad(app, "model-selected");
    } else {
        app->active_gamepad = 0;
        bongo_cat_app_reset_gamepad(app);
        select_first_gamepad(app);
    }
}

void bongo_cat_gamepad_event(BongoCatApp *app, const void *raw) {
    const SDL_Event *event = raw;
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        if (app->loaded_mode == BONGO_CAT_MODE_GAMEPAD &&
            !app->active_gamepad) {
            SDL_Gamepad *gamepad = SDL_GetGamepadFromID(event->gdevice.which);
            if (!gamepad) gamepad = SDL_OpenGamepad(event->gdevice.which);
            if (gamepad) {
                app->active_gamepad = event->gdevice.which;
                log_gamepad(app, "added");
                synchronize_gamepad(app, "added");
            }
        }
        return;
    }
    if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (event->gdevice.which == app->active_gamepad) log_gamepad(app, "removed");
        SDL_Gamepad *gamepad = SDL_GetGamepadFromID(event->gdevice.which);
        if (gamepad) SDL_CloseGamepad(gamepad);
        if (event->gdevice.which == app->active_gamepad) {
            app->active_gamepad = 0;
            bongo_cat_app_reset_gamepad(app);
            if (app->loaded_mode == BONGO_CAT_MODE_GAMEPAD)
                select_first_gamepad(app);
        }
        return;
    }
    if (app->loaded_mode != BONGO_CAT_MODE_GAMEPAD) return;
    if (event->type == SDL_EVENT_GAMEPAD_REMAPPED) {
        if (app->active_gamepad && event->gdevice.which == app->active_gamepad)
            synchronize_gamepad(app, "remapped");
        return;
    }
    BongoCatInputEvent input = {0};
    SDL_JoystickID source;
    if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        source = event->gaxis.which;
        input.kind = BONGO_CAT_INPUT_GAMEPAD_AXIS;
        input.value = axis_value(event->gaxis.value);
        snprintf(input.name, sizeof(input.name), "%s", bongo_cat_gamepad_axis_name(event->gaxis.axis));
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        source = event->gbutton.which;
        input.kind = BONGO_CAT_INPUT_GAMEPAD_BUTTON;
        input.value = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? 1.0f : 0.0f;
        snprintf(input.name, sizeof(input.name), "%s", bongo_cat_gamepad_button_name(event->gbutton.button));
    } else return;
    if (!app->active_gamepad || source != app->active_gamepad) return;
    if (strncmp(input.name, "Unknown", 7) == 0) return;
    input.timestamp_ms = SDL_GetTicks();
    if (input.kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON)
        bongo_cat_app_shortcuts(app, &input);
    bongo_cat_app_apply_input(app, &input);
}
