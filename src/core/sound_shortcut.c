#include "bongo_cat/sound_shortcut.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    }
    return *a == *b;
}

static const char *canonical(const char *key) {
    if (strlen(key) == 4) {
        char prefix[4] = {key[0], key[1], key[2], '\0'};
        if (equal(prefix, "Key") || equal(prefix, "Num")) return key + 3;
    }
    if (equal(key, "UpArrow")) return "ArrowUp";
    if (equal(key, "DownArrow")) return "ArrowDown";
    if (equal(key, "LeftArrow")) return "ArrowLeft";
    if (equal(key, "RightArrow")) return "ArrowRight";
    if (equal(key, "Return")) return "Enter";
    if (equal(key, "Ctrl")) return "Control";
    if (equal(key, "Super") || equal(key, "Command")) return "Meta";
    if (equal(key, "Minus")) return "-";
    if (equal(key, "Equal")) return "=";
    return key;
}

typedef struct ShortcutTokens {
    char text[BONGO_CAT_SHORTCUT_CAP];
    const char *keys[BONGO_CAT_SHORTCUT_CAP / 2];
    size_t count;
} ShortcutTokens;

static bool parse(const char *shortcut, ShortcutTokens *tokens) {
    tokens->count = 0;
    if (!shortcut || !*shortcut || strlen(shortcut) >= sizeof(tokens->text)) return false;
    snprintf(tokens->text, sizeof(tokens->text), "%s", shortcut);
    char *cursor = tokens->text;
    for (;;) {
        char *plus = strchr(cursor, '+');
        if (plus) *plus = '\0';
        if (!*cursor || tokens->count >= sizeof(tokens->keys) / sizeof(tokens->keys[0]))
            return false;
        const char *key = canonical(cursor);
        /* A+A (or A+KeyA) must not turn a two-key binding into a single key. */
        for (size_t i = 0; i < tokens->count; ++i)
            if (equal(tokens->keys[i], key)) return false;
        tokens->keys[tokens->count++] = key;
        if (!plus) return true;
        cursor = plus + 1;
    }
}

bool bongo_cat_shortcut_equal(const char *left, const char *right) {
    ShortcutTokens a, b;
    if (!parse(left, &a) || !parse(right, &b) || a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i) {
        bool found = false;
        for (size_t j = 0; j < b.count; ++j) found = found || equal(a.keys[i], b.keys[j]);
        if (!found) return false;
    }
    return true;
}

static bool token_matches(const char *token, const char *held) {
    if (equal(canonical(token), canonical(held))) return true;
    if (equal(token, "Control") || equal(token, "Ctrl"))
        return equal(held, "ControlLeft") || equal(held, "ControlRight");
    if (equal(token, "Shift"))
        return equal(held, "ShiftLeft") || equal(held, "ShiftRight");
    if (equal(token, "Alt")) return equal(held, "Alt") || equal(held, "AltGr");
    if (equal(token, "Meta") || equal(token, "Super") || equal(token, "Command"))
        return equal(held, "Meta") || equal(held, "MetaLeft") || equal(held, "MetaRight");
    if (equal(token, "-")) return equal(held, "Minus");
    if (equal(token, "=")) return equal(held, "Equal");
    return false;
}

bool bongo_cat_sound_shortcut_update(BongoCatSoundShortcutState *state,
    const BongoCatInputEvent *event) {
    if (!state || !event || !event->name[0]) return false;
    bool down;
    char name[BONGO_CAT_ID_CAP];
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON) {
        down = event->value > 0.5f;
        if (strlen(event->name) + 8 >= sizeof(name)) return false;
        snprintf(name, sizeof(name), "Gamepad:%s", event->name);
    } else {
        if (event->kind != BONGO_CAT_INPUT_KEY_DOWN && event->kind != BONGO_CAT_INPUT_KEY_UP &&
            event->kind != BONGO_CAT_INPUT_MOUSE_DOWN && event->kind != BONGO_CAT_INPUT_MOUSE_UP)
            return false;
        down = event->kind == BONGO_CAT_INPUT_KEY_DOWN || event->kind == BONGO_CAT_INPUT_MOUSE_DOWN;
        snprintf(name, sizeof(name), "%s", event->name);
    }
    for (size_t i = 0; i < state->count; ++i) {
        if (strcmp(state->held[i], name) != 0) continue;
        if (down) return false;
        --state->count;
        if (i != state->count) memcpy(state->held[i], state->held[state->count], sizeof(name));
        return true;
    }
    if (!down || state->count >= BONGO_CAT_INPUT_KEY_STATE_CAP) return false;
    snprintf(state->held[state->count++], sizeof(name), "%s", name);
    return true;
}

static bool chord_down(const BongoCatSoundShortcutState *state,
    const ShortcutTokens *tokens, const char *exclude) {
    for (size_t token = 0; token < tokens->count; ++token) {
        bool found = false;
        for (size_t i = 0; i < state->count && !found; ++i)
            if (!exclude || strcmp(exclude, state->held[i]))
                found = token_matches(tokens->keys[token], state->held[i]);
        if (!found) return false;
    }
    return true;
}

bool bongo_cat_sound_shortcut_down(const BongoCatSoundShortcutState *state,
    const char *shortcut) {
    ShortcutTokens tokens;
    return state && parse(shortcut, &tokens) && chord_down(state, &tokens, NULL);
}

bool bongo_cat_sound_shortcut_pressed(const BongoCatSoundShortcutState *state,
    const BongoCatInputEvent *event, const char *shortcut) {
    if (!state || !event || (event->kind != BONGO_CAT_INPUT_KEY_DOWN &&
        event->kind != BONGO_CAT_INPUT_MOUSE_DOWN &&
        !(event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON && event->value > 0.5f))) return false;
    char name[BONGO_CAT_ID_CAP];
    int length = snprintf(name, sizeof(name), "%s%s",
        event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ? "Gamepad:" : "", event->name);
    if (length <= 0 || (size_t)length >= sizeof(name)) return false;
    ShortcutTokens tokens;
    return parse(shortcut, &tokens) && chord_down(state, &tokens, NULL) &&
        !chord_down(state, &tokens, name);
}
