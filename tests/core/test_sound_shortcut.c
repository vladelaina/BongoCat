#include "bongo_cat/sound_shortcut.h"
#include "test.h"

#include <string.h>

static bool edge(BongoCatSoundShortcutState *state, BongoCatInputKind kind,
    const char *name) {
    BongoCatInputEvent event = {.kind = kind};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return bongo_cat_sound_shortcut_update(state, &event);
}

void test_sound_shortcut(void) {
    BongoCatSoundShortcutState state = {0};
    CHECK(!bongo_cat_sound_shortcut_down(&state, "A"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "KeyA"));
    CHECK(!edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "KeyA"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "A"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "A+B"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "B+A"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "KeyA"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Control+A"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "Control+A"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "A"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "ControlRight"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_UP, "ControlLeft"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "Ctrl+A"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "KeyB"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "A+B+Control"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_UP, "KeyA"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "A+B+Control"));
    CHECK(edge(&state, BONGO_CAT_INPUT_MOUSE_DOWN, "Left"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "Control+Left"));
    CHECK(edge(&state, BONGO_CAT_INPUT_MOUSE_UP, "Left"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Control+Left"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, ""));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Control+"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "+Control"));
    CHECK(bongo_cat_shortcut_equal("Ctrl+KeyA", "a+control"));
    CHECK(bongo_cat_shortcut_equal("Command+Return", "Enter+Meta"));
    CHECK(bongo_cat_shortcut_equal("Minus+UpArrow", "ArrowUp+-"));
    CHECK(!bongo_cat_shortcut_equal("A+B", "A"));
    CHECK(!bongo_cat_shortcut_equal("ControlLeft+A", "ControlRight+A"));
    CHECK(!bongo_cat_shortcut_equal("A+A", "A"));
    CHECK(!bongo_cat_shortcut_equal("A+", "A"));
    CHECK(!bongo_cat_shortcut_equal("", ""));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "B+KeyB"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Control+Ctrl+B"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "MetaLeft"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "MetaRight"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_UP, "MetaLeft"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "Meta+B"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_UP, "MetaRight"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Meta+B"));
    memset(&state, 0, sizeof(state));
    BongoCatInputEvent button = {.kind = BONGO_CAT_INPUT_GAMEPAD_BUTTON, .value = 1.0f};
    snprintf(button.name, sizeof(button.name), "South");
    CHECK(bongo_cat_sound_shortcut_update(&state, &button));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Gamepad:South+Gamepad:East"));
    snprintf(button.name, sizeof(button.name), "East");
    CHECK(bongo_cat_sound_shortcut_update(&state, &button));
    CHECK(bongo_cat_sound_shortcut_down(&state, "Gamepad:South+Gamepad:East"));
    CHECK(bongo_cat_sound_shortcut_pressed(&state, &button, "Gamepad:South+Gamepad:East"));
    button.value = 0.0f;
    CHECK(bongo_cat_sound_shortcut_update(&state, &button));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "Gamepad:South+Gamepad:East"));
    memset(&state, 0, sizeof(state));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft"));
    CHECK(!bongo_cat_sound_shortcut_down(&state, "ControlLeft+ControlRight"));
    CHECK(edge(&state, BONGO_CAT_INPUT_KEY_DOWN, "ControlRight"));
    CHECK(bongo_cat_sound_shortcut_down(&state, "ControlLeft+ControlRight"));
}
