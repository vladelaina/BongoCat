#include "shortcut_test_cases.h"

#include <string.h>

static uint32_t random_state = UINT32_C(0x91734ab5);
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

void shortcut_random_cases(void) {
    static const char *shortcuts[] = {
        "", "A", "a", "Ctrl+A", "Control+Shift+A", "Alt+1", "Alt+0",
        "Command+Enter", "Shift+-", "Super+ArrowUp", "Gamepad:South",
        "gamepad:South", "Gamepad:south", "ControlLeft", "Meta", "Ctrl+",
        "+A", "A+B", "Ctrl+Ctrl+B", "unknown+A", "Ctrl++A", "Ctrl+ A",
        "Control+Shift+Alt+Super+A", "F12", "Num1", "Enter", "ArrowUp",
        "1234567890123456789012345678901234567890", "Comma", "="
    };
    static const char *names[] = {
        "KeyA", "KeyB", "Num1", "Num0", "ControlLeft", "ControlRight",
        "ShiftLeft", "ShiftRight", "Alt", "AltGr", "Meta", "Return",
        "UpArrow", "South", "south", "Minus", "Equal", "F12", "Comma", ""
    };
    static const size_t catalog_sizes[] = {0, 1, 2, 8, 16, 31, 32, 33, 64, 128};
    for (unsigned trial = 0; trial < 256; ++trial) {
        size_t behaviors = catalog_sizes[next_random() %
            (sizeof(catalog_sizes) / sizeof(catalog_sizes[0]))];
        size_t bindings = next_random() % 65;
        shortcut_reset(behaviors, bindings);
        for (size_t i = 0; i < behaviors; ++i) {
            BongoCatBehaviorEntry *entry = &shortcut_app->behaviors.entries[i];
            entry->kind = (BongoCatBehaviorKind)(next_random() % 4);
            entry->momentary = next_random() % 2 != 0;
            if (next_random() % 5 == 0) entry->sound[0] = '\0';
            if (i && next_random() % 3 == 0)
                strcpy(entry->id, shortcut_app->behaviors.entries[i - 1].id);
        }
        for (size_t i = 0; i < bindings; ++i) {
            const char *id = behaviors && next_random() % 4 ?
                shortcut_app->behaviors.entries[next_random() % behaviors].id : "missing";
            shortcut_binding(shortcut_app, i, id,
                shortcuts[next_random() % (sizeof(shortcuts) / sizeof(shortcuts[0]))]);
        }
        shortcut_app->shortcut_state.control = (uint8_t)(next_random() % 4);
        shortcut_app->shortcut_state.shift = (uint8_t)(next_random() % 4);
        shortcut_app->shortcut_state.alt = (uint8_t)(next_random() % 4);
        shortcut_app->shortcut_state.meta = (uint8_t)(next_random() % 2);
        for (unsigned event = 0; event < 64; ++event)
            shortcut_check((BongoCatInputKind)(next_random() % 8),
                names[next_random() % (sizeof(names) / sizeof(names[0]))],
                (float)(next_random() % 5) / 4.0f, NULL);
    }
    /* Capacity boundaries, empty catalogs, and every Alt digit alias. */
    shortcut_reset(BONGO_CAT_BEHAVIOR_CAP, BONGO_CAT_BEHAVIOR_CAP);
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft", 0, NULL);
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "F1", 0, NULL);
    shortcut_check(BONGO_CAT_INPUT_KEY_UP, "F1", 0, NULL);
    shortcut_check(BONGO_CAT_INPUT_MOUSE_MOVE, "", 0, "");
    shortcut_reset(10, 0);
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "Alt", 0, "");
    const char *digit_actions[] = {"Q", "MS", "Q", "S", "E", "MS", "Q", "S", "E", "MS"};
    for (unsigned digit = 0; digit < 10; ++digit) {
        char key[8];
        snprintf(key, sizeof(key), "Num%u", digit);
        shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, key, 0, digit_actions[digit]);
    }
    shortcut_reset(0, BONGO_CAT_BEHAVIOR_CAP);
    shortcut_check(BONGO_CAT_INPUT_KEY_DOWN, "F1", 0, "");
    shortcut_check(BONGO_CAT_INPUT_GAMEPAD_BUTTON, "South", 1, "");
}
