#include "shortcut_test_cases.h"

#include <string.h>

static void resize_while_held(size_t count, const char *primary) {
    CHECK(strcmp(shortcut_app->shortcut_state.pressed, primary) == 0);
    shortcut_app->behaviors.count = count;
}

void shortcut_live_edit_cases(void) {
    /* Initialize spare entries once; never reset state during the stream. */
    shortcut_reset(33, 3);
    shortcut_app->behaviors.count = 31;
    shortcut_app->behaviors.entries[30].kind = BONGO_CAT_BEHAVIOR_EFFECT;
    shortcut_app->behaviors.entries[31].kind = BONGO_CAT_BEHAVIOR_SOUND;
    shortcut_app->behaviors.entries[32].kind = BONGO_CAT_BEHAVIOR_EXPRESSION;
    for (size_t i = 0; i < 3; ++i) {
        shortcut_app->behaviors.entries[30 + i].momentary = true;
        shortcut_binding(shortcut_app, i,
            shortcut_app->behaviors.entries[30 + i].id, "Ctrl+A");
    }
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "E");
    CHECK(shortcut_app->shortcut_state.control == 1);
    resize_while_held(32, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ES");
    resize_while_held(33, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ESQ");
    resize_while_held(32, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ES");
    resize_while_held(31, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "E");
    resize_while_held(32, "KeyA");

    /* Same-count binding and catalog edits must affect the very next event. */
    strcpy(shortcut_app->config.behavior_shortcuts[1].shortcut, "Ctrl+B");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "E");
    strcpy(shortcut_app->config.behavior_shortcuts[0].id, "edited-live-id");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "");
    strcpy(shortcut_app->behaviors.entries[30].id, "edited-live-id");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "E");
    strcpy(shortcut_app->config.behavior_shortcuts[1].shortcut, "Ctrl+A");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ES");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "ControlRight", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "ControlLeft", 0, "");
    CHECK(shortcut_app->shortcut_state.control == 2);
    resize_while_held(0, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "");
    resize_while_held(33, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "ESQ");
    resize_while_held(32, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "ControlRight", 0, "");
    CHECK(shortcut_app->shortcut_state.control == 0);
    resize_while_held(31, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "E");
    CHECK(shortcut_app->shortcut_state.pressed[0] == '\0');
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "KeyA", 0, "E");
    resize_while_held(32, "KeyA");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "KeyA", 0, "EX");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "ControlLeft", 0, "");

    /* Continue without resetting: aliases and stale overrides across counts. */
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Alt", 0, "");
    shortcut_app->behaviors.count = 31;
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "MS");
    resize_while_held(32, "Num1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "MS");
    resize_while_held(33, "Num1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "MS");
    resize_while_held(0, "Num1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "");
    resize_while_held(33, "Num1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "MS");
    CHECK(shortcut_app->shortcut_state.alt == 1);
    shortcut_app->behaviors.entries[0].kind = BONGO_CAT_BEHAVIOR_SOUND;
    shortcut_app->behaviors.entries[0].sound[0] = '\0';
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "X");
    strcpy(shortcut_app->config.behavior_shortcuts[0].shortcut, "Alt+1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "E");
    strcpy(shortcut_app->config.behavior_shortcuts[0].id, "stale-live-id");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num1", 0, "X");
    resize_while_held(1, "Num1");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num0", 0, "");
    resize_while_held(10, "Num0");
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Num0", 0, "Q");
    resize_while_held(0, "Num0");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "Num0", 0, "");
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "Alt", 0, "");
    CHECK(shortcut_app->shortcut_state.pressed[0] == '\0');
    CHECK(shortcut_app->shortcut_state.alt == 0);
}
