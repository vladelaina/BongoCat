#include "test.h"
#include "bongo_cat/shortcut.h"

#include <string.h>

static BongoCatInputEvent key(BongoCatInputKind kind, const char *name) {
    BongoCatInputEvent event = {.kind = kind};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

void test_shortcut(void) {
    static const struct { const char *stored; const char *shown; } labels[] = {
        {"Control+A", "Ctrl+A"}, {"Ctrl+Shift+A", "Ctrl+Shift+A"},
        {"control+Comma", "Ctrl+,"}, {"Alt+PageUp", "Alt+PgUp"},
        {"Control+PageDown", "Ctrl+PgDn"}, {"PrintScreen", "PrtSc"},
        {"Escape", "Esc"}, {"Delete", "Del"}, {"Insert", "Ins"},
        {"Control+BracketLeft", "Ctrl+["}, {"Alt+Backslash", "Alt+\\"},
        {"Control+ArrowLeft", "Ctrl+\xE2\x86\x90"},
        {"ArrowUp", "\xE2\x86\x91"}, {"ArrowDown", "\xE2\x86\x93"},
        {"ArrowRight", "\xE2\x86\x92"},
        {"UpArrow", "\xE2\x86\x91"}, {"DownArrow", "\xE2\x86\x93"},
        {"LeftArrow", "\xE2\x86\x90"}, {"RightArrow", "\xE2\x86\x92"},
        {"Shift+KpPlus", "Shift+Num +"},
        {"F12", "F12"}, {"UnknownKey", "UnknownKey"},
        {"Gamepad:South", "Gamepad:South"}, {"", ""}, {NULL, ""},
#if defined(_WIN32)
        {"Meta+A", "Win+A"}, {"Super+A", "Win+A"},
#elif defined(__APPLE__)
        {"Meta+A", "Cmd+A"}, {"Command+A", "Cmd+A"},
#else
        {"Meta+A", "Super+A"},
#endif
    };
    char shown[BONGO_CAT_SHORTCUT_CAP * 2];
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        bongo_cat_shortcut_format(labels[i].stored, shown, sizeof(shown));
        CHECK(strcmp(shown, labels[i].shown) == 0);
    }
    char small[5];
    bongo_cat_shortcut_format("Control+A", small, sizeof(small));
    CHECK(strcmp(small, "Ctrl") == 0);
    bongo_cat_shortcut_format("Control+A", small, 1);
    CHECK(small[0] == '\0');
    small[0] = 'x';
    bongo_cat_shortcut_format("Control+A", small, 0);
    CHECK(small[0] == 'x');
    bongo_cat_shortcut_format("Control+A", NULL, 0);

    BongoCatShortcutState state;
    bongo_cat_shortcut_init(&state);
    BongoCatInputEvent control = key(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    BongoCatInputEvent letter = key(BONGO_CAT_INPUT_KEY_DOWN, "KeyB");
    CHECK(!bongo_cat_shortcut_update(&state, &control));
    CHECK(bongo_cat_shortcut_update(&state, &letter));
    CHECK(bongo_cat_shortcut_matches(&state, &letter, "Control+B"));
    CHECK(!bongo_cat_shortcut_matches(&state, &letter, "Control+Shift+B"));
    CHECK(!bongo_cat_shortcut_update(&state, &letter));
    BongoCatInputEvent up = key(BONGO_CAT_INPUT_KEY_UP, "KeyB");
    bongo_cat_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_shortcut_update(&state, &control);
    BongoCatInputEvent function = key(BONGO_CAT_INPUT_KEY_DOWN, "F1");
    CHECK(bongo_cat_shortcut_update(&state, &function));
    CHECK(bongo_cat_shortcut_matches(&state, &function, "F1"));
    BongoCatInputEvent comma = key(BONGO_CAT_INPUT_KEY_DOWN, "Comma");
    up = key(BONGO_CAT_INPUT_KEY_UP, "F1");
    bongo_cat_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_INPUT_KEY_DOWN;
    bongo_cat_shortcut_update(&state, &control);
    CHECK(bongo_cat_shortcut_update(&state, &comma));
    CHECK(bongo_cat_shortcut_matches(&state, &comma, "Control+Comma"));
    up = key(BONGO_CAT_INPUT_KEY_UP, "Comma");
    bongo_cat_shortcut_update(&state, &up);
    control.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_shortcut_update(&state, &control);
    BongoCatInputEvent alt = key(BONGO_CAT_INPUT_KEY_DOWN, "Alt");
    BongoCatInputEvent digit = key(BONGO_CAT_INPUT_KEY_DOWN, "Num1");
    CHECK(!bongo_cat_shortcut_update(&state, &alt));
    CHECK(bongo_cat_shortcut_update(&state, &digit));
    CHECK(bongo_cat_shortcut_matches(&state, &digit, "Alt+1"));

    BongoCatInputEvent gamepad = {
        .kind = BONGO_CAT_INPUT_GAMEPAD_BUTTON, .value = 1.0f
    };
    snprintf(gamepad.name, sizeof(gamepad.name), "South");
    CHECK(bongo_cat_shortcut_matches(&state, &gamepad, "Gamepad:South"));
    CHECK(!bongo_cat_shortcut_matches(&state, &gamepad, "South"));
    gamepad.value = 0.0f;
    CHECK(!bongo_cat_shortcut_matches(&state, &gamepad, "Gamepad:South"));
}
