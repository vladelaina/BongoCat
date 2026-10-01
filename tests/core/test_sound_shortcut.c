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
}
