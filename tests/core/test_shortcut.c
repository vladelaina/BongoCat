#include "test.h"
#include "bongo_cat/shortcut.h"

#include <string.h>

static BongoCatInputEvent key(BongoCatInputKind kind, const char *name) {
    BongoCatInputEvent event = {.kind = kind};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

static void punctuation_shortcuts(void) {
    static const struct { const char *input; const char *binding; } cases[] = {
        {"Equal", "Alt+="},
        {"Minus", "Alt+-"},
        {"BracketLeft", "Alt+BracketLeft"},
        {"BracketRight", "Alt+BracketRight"},
        {"Backslash", "Alt+Backslash"},
        {"Semicolon", "Alt+Semicolon"},
        {"Quote", "Alt+Quote"},
        {"Comma", "Alt+Comma"},
        {"Period", "Alt+Period"},
        {"Slash", "Alt+Slash"},
        {"BackQuote", "Alt+BackQuote"},
        {"KpPlus", "Alt+KpPlus"},
        {"KpMinus", "Alt+KpMinus"},
        {"KpMultiply", "Alt+KpMultiply"},
        {"KpDivide", "Alt+KpDivide"},
        {"KpDecimal", "Alt+KpDecimal"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (int reverse = 0; reverse < 2; ++reverse) {
            BongoCatShortcutState state;
            bongo_cat_shortcut_init(&state);
            BongoCatInputEvent first = key(BONGO_CAT_INPUT_KEY_DOWN,
                reverse ? cases[i].input : "Alt");
            BongoCatInputEvent second = key(BONGO_CAT_INPUT_KEY_DOWN,
                reverse ? "Alt" : cases[i].input);
            CHECK(bongo_cat_shortcut_update(&state, &first));
            CHECK(!bongo_cat_shortcut_matches(&state, &first, cases[i].binding));
            CHECK(!bongo_cat_sound_shortcut_down(&state.held, cases[i].binding));
            CHECK(bongo_cat_shortcut_update(&state, &second));
            CHECK(bongo_cat_shortcut_matches(&state, &second, cases[i].binding));
            CHECK(bongo_cat_sound_shortcut_down(&state.held, cases[i].binding));
            CHECK(!bongo_cat_shortcut_update(&state, &second));
            CHECK(!bongo_cat_shortcut_matches(&state, &second, cases[i].binding));
            first.kind = BONGO_CAT_INPUT_KEY_UP;
            bongo_cat_shortcut_update(&state, &first);
            CHECK(bongo_cat_shortcut_release_matches(&first, cases[i].binding));
            CHECK(!bongo_cat_sound_shortcut_down(&state.held, cases[i].binding));
        }
    }
}

void test_shortcut(void) {
    punctuation_shortcuts();
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
    CHECK(bongo_cat_shortcut_update(&state, &control));
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
    CHECK(bongo_cat_shortcut_update(&state, &alt));
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

    /* Every ordinary key is required, regardless of press order. */
    for (int reverse = 0; reverse < 2; ++reverse) {
        bongo_cat_shortcut_init(&state);
        BongoCatInputEvent first = key(BONGO_CAT_INPUT_KEY_DOWN, reverse ? "KeyB" : "KeyA");
        BongoCatInputEvent second = key(BONGO_CAT_INPUT_KEY_DOWN, reverse ? "KeyA" : "KeyB");
        CHECK(bongo_cat_shortcut_update(&state, &first));
        CHECK(!bongo_cat_shortcut_matches(&state, &first, "A+B"));
        CHECK(bongo_cat_shortcut_update(&state, &second));
        CHECK(bongo_cat_shortcut_matches(&state, &second, "A+B"));
        CHECK(!bongo_cat_shortcut_matches(&state, &second, "Control+A+B"));
        CHECK(!bongo_cat_shortcut_matches(&state, &second, "A+B+"));
        CHECK(!bongo_cat_shortcut_matches(&state, &second, "A++B"));
        CHECK(!bongo_cat_shortcut_update(&state, &first));
        CHECK(!bongo_cat_shortcut_matches(&state, &first, "A+B"));
        first.kind = BONGO_CAT_INPUT_KEY_UP;
        bongo_cat_shortcut_update(&state, &first);
        CHECK(bongo_cat_shortcut_release_matches(&first, "A+B"));
        CHECK(!bongo_cat_shortcut_update(&state, &second));
        CHECK(!bongo_cat_shortcut_matches(&state, &second, "A+B"));
        first.kind = BONGO_CAT_INPUT_KEY_DOWN;
        CHECK(bongo_cat_shortcut_update(&state, &first));
        CHECK(bongo_cat_shortcut_matches(&state, &first, "A+B"));
        BongoCatInputEvent other = key(BONGO_CAT_INPUT_KEY_DOWN, "KeyC");
        CHECK(bongo_cat_shortcut_update(&state, &other));
        CHECK(!bongo_cat_shortcut_matches(&state, &other, "A+B"));
    }
    control.kind = BONGO_CAT_INPUT_KEY_UP;
    CHECK(bongo_cat_shortcut_release_matches(&control, "Control+B"));
    CHECK(!bongo_cat_shortcut_release_matches(&control, "Control+B+"));
    bongo_cat_shortcut_init(&state);
    letter = key(BONGO_CAT_INPUT_KEY_DOWN, "KeyB");
    CHECK(bongo_cat_shortcut_update(&state, &letter));
    CHECK(!bongo_cat_shortcut_matches(&state, &letter, "Control+B"));
    control.kind = BONGO_CAT_INPUT_KEY_DOWN;
    CHECK(bongo_cat_shortcut_update(&state, &control));
    CHECK(bongo_cat_shortcut_matches(&state, &control, "B+Ctrl"));
    BongoCatInputEvent right = key(BONGO_CAT_INPUT_KEY_DOWN, "ControlRight");
    CHECK(bongo_cat_shortcut_update(&state, &right));
    CHECK(!bongo_cat_shortcut_matches(&state, &right, "B+Control"));
    control.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_shortcut_update(&state, &control);
    CHECK(state.control != 0);
    CHECK(!bongo_cat_shortcut_update(&state, &letter));
    CHECK(!bongo_cat_shortcut_matches(&state, &letter, "B+Control"));
    bongo_cat_shortcut_init(&state);
    BongoCatInputEvent meta_left = key(BONGO_CAT_INPUT_KEY_DOWN, "MetaLeft");
    BongoCatInputEvent meta_right = key(BONGO_CAT_INPUT_KEY_DOWN, "MetaRight");
    bongo_cat_shortcut_update(&state, &meta_left);
    bongo_cat_shortcut_update(&state, &meta_right);
    meta_left.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_shortcut_update(&state, &meta_left);
    CHECK(bongo_cat_shortcut_update(&state, &letter));
    CHECK(bongo_cat_shortcut_matches(&state, &letter, "Meta+B"));
    meta_right.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_shortcut_update(&state, &meta_right);
    CHECK(!state.meta);
}
