#include "test.h"
#include "runtime.h"
#include "bongo_cat/overlay.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int bongo_cat_test_failures;
static BongoCatApp app;
static bool south, dpad, keyboard, effect;
static unsigned shortcut_calls;
static float left_hand, right_hand;

bool bongo_cat_live2d_set_parameter(BongoCatLive2D *live2d,
    const char *id, float value) {
    (void)live2d;
    if (!strcmp(id, "CatParamLeftHandDown")) left_hand = value;
    if (!strcmp(id, "CatParamRightHandDown")) right_hand = value;
    return true;
}

bool bongo_cat_live2d_parameter(BongoCatLive2D *live2d,
    const char *id, BongoCatParameterRange *range) {
    (void)live2d; (void)id;
    *range = (BongoCatParameterRange){-1.0f, 1.0f, 0.0f};
    return true;
}

int bongo_cat_overlay_key(BongoCatOverlay *overlay,
    const char *name, bool pressed) {
    (void)overlay;
    if (!strcmp(name, "South")) { south = pressed; return 1; }
    if (!strcmp(name, "DPadLeft")) { dpad = pressed; return 0; }
    if (!strcmp(name, "KeyA")) { keyboard = pressed; return 0; }
    return -1;
}

bool bongo_cat_overlay_hand_active(const BongoCatOverlay *overlay, bool right) {
    (void)overlay;
    return right ? south : dpad || keyboard;
}

bool bongo_cat_overlay_effect(BongoCatOverlay *overlay, const char *path) {
    (void)overlay;
    effect = path != NULL;
    return true;
}

void bongo_cat_app_shortcuts(BongoCatApp *value, const BongoCatInputEvent *event) {
    shortcut_calls++;
    bongo_cat_sound_shortcut_update(&value->sound_shortcut_state, event);
}

static SDL_JoystickID attach_gamepad(SDL_Joystick **joystick) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    desc.name = "BongoCat state synchronization test";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    CHECK(id != 0);
    *joystick = id ? SDL_OpenJoystick(id) : NULL;
    CHECK(*joystick != NULL);
    if (*joystick) {
        CHECK(SDL_SetJoystickVirtualAxis(*joystick,
            SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN));
        CHECK(SDL_SetJoystickVirtualAxis(*joystick,
            SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN));
    }
    return id;
}

static void dispatch_events(void) {
    SDL_UpdateGamepads();
    SDL_Event event;
    unsigned count = 0;
    while (count < 1000 && SDL_PollEvent(&event)) {
        bongo_cat_gamepad_event(&app, &event);
        count++;
    }
    CHECK(count < 1000);
}

static void check_neutral(void) {
    CHECK(app.left_stick_x == 0.0f && app.left_stick_y == 0.0f);
    CHECK(app.right_stick_x == 0.0f && app.right_stick_y == 0.0f);
    CHECK(!app.left_stick_pressed && !app.right_stick_pressed);
    CHECK(!south && !dpad && !keyboard);
    CHECK(left_hand == 0.0f && right_hand == 0.0f);
}

int main(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) return 1;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_free(ids);
    if (count) {
        fprintf(stderr, "Skipping virtual gamepad test: physical gamepad present.\n");
        SDL_Quit();
        return 77;
    }
    SDL_Joystick *joystick = NULL;
    SDL_JoystickID id = attach_gamepad(&joystick);
    if (!joystick) { SDL_Quit(); return 1; }
    app.live2d = (BongoCatLive2D *)&app;
    app.loaded_mode = BONGO_CAT_MODE_GAMEPAD;
    BongoCatBehaviorEntry behavior = {
        .kind = BONGO_CAT_BEHAVIOR_EFFECT, .momentary = true};
    snprintf(behavior.id, sizeof(behavior.id), "test:effect");
    app.behaviors.entries = &behavior;
    app.behaviors.count = 1;
    app.settings.behavior_shortcut_count = 1;
    BongoCatBehaviorShortcut *binding = &app.settings.behavior_shortcuts[0];
    snprintf(binding->id, sizeof(binding->id), "%s", behavior.id);
    snprintf(binding->shortcut, sizeof(binding->shortcut), "Gamepad:South");

    /* Startup must see held buttons immediately, but ignore center drift and
       avoid treating the snapshot as a fresh shortcut press. */
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 623));
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 918));
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, 590));
    CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    bongo_cat_gamepads_set_enabled(&app, true);
    CHECK(app.active_gamepad == id && south && right_hand == 1.0f);
    CHECK(app.left_stick_y == 0.0f && app.right_stick_x == 0.0f);
    CHECK(app.right_stick_y == 0.0f && left_hand == 0.0f);
    CHECK(shortcut_calls == 0 && behavior.shortcut_active);
    CHECK(bongo_cat_sound_shortcut_down(&app.sound_shortcut_state, "Gamepad:South"));
    dispatch_events();
    CHECK(shortcut_calls == 0);

    /* Simulate a held pose before loading, followed by a release while the
       loader pumps SDL but leaves the input events queued. */
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 24000));
    dispatch_events();
    CHECK(app.left_stick_x > 0.7f);
    BongoCatInputEvent key = {.kind = BONGO_CAT_INPUT_KEY_DOWN};
    snprintf(key.name, sizeof(key.name), "KeyA");
    bongo_cat_app_apply_input(&app, &key);
    bongo_cat_sound_shortcut_update(&app.sound_shortcut_state, &key);
    CHECK(!keyboard);
    effect = true;
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 500));
    CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false));
    SDL_UpdateGamepads();
    CHECK(SDL_HasEvent(SDL_EVENT_GAMEPAD_BUTTON_UP));
    SDL_Event sentinel = {.type = SDL_EVENT_USER};
    CHECK(SDL_PushEvent(&sentinel));
    sentinel.type = SDL_EVENT_GAMEPAD_REMAPPED;
    sentinel.gdevice.which = id + 100000;
    CHECK(SDL_PushEvent(&sentinel));
    bongo_cat_gamepads_set_enabled(&app, true);
    check_neutral();
    CHECK(!effect && !behavior.shortcut_active);
    CHECK(!SDL_HasEvents(SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_UP));
    CHECK(SDL_HasEvent(SDL_EVENT_USER) && SDL_HasEvent(SDL_EVENT_GAMEPAD_REMAPPED));
    CHECK(!bongo_cat_sound_shortcut_down(&app.sound_shortcut_state, "Gamepad:South"));
    CHECK(bongo_cat_sound_shortcut_down(&app.sound_shortcut_state, "A"));
    CHECK(app.active_input_count == 1); /* held keyboard key is preserved */
    bongo_cat_app_reapply_input(&app);
    check_neutral();
    dispatch_events();
    check_neutral();
    CHECK(shortcut_calls == 0);

    /* Fresh input after a snapshot still reaches both visuals and shortcuts. */
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 16384));
    CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    dispatch_events();
    CHECK(fabsf(app.left_stick_x - 0.5f) < 0.001f && south);
    CHECK(shortcut_calls == 1);

    /* Remapping also refreshes state without replaying button actions. */
    CHECK(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
    CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false));
    CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_LEFT_STICK, true));
    sentinel.type = SDL_EVENT_GAMEPAD_REMAPPED;
    sentinel.gdevice.which = id;
    bongo_cat_gamepad_event(&app, &sentinel);
    CHECK(app.left_stick_x == 0.0f && app.left_stick_pressed && !south);
    CHECK(shortcut_calls == 1);
    dispatch_events();
    CHECK(shortcut_calls == 1);

    SDL_CloseJoystick(joystick);
    CHECK(SDL_DetachVirtualJoystick(id));
    dispatch_events();
    CHECK(app.active_gamepad == 0);
    check_neutral();

    /* A newly connected controller takes over with its current held state. */
    id = attach_gamepad(&joystick);
    if (joystick) {
        unsigned before = shortcut_calls;
        CHECK(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true));
        dispatch_events();
        CHECK(app.active_gamepad == id && dpad && left_hand == 1.0f);
        CHECK(shortcut_calls == before);
        SDL_CloseJoystick(joystick);
        CHECK(SDL_DetachVirtualJoystick(id));
        dispatch_events();
        check_neutral();
    }
    bongo_cat_gamepads_set_enabled(&app, false);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
