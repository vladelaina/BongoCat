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
    if ((strncmp(key, "Key", 3) == 0 || strncmp(key, "Num", 3) == 0) &&
        key[3] && !key[4]) return key + 3;
    if (strcmp(key, "UpArrow") == 0) return "ArrowUp";
    if (strcmp(key, "DownArrow") == 0) return "ArrowDown";
    if (strcmp(key, "LeftArrow") == 0) return "ArrowLeft";
    if (strcmp(key, "RightArrow") == 0) return "ArrowRight";
    if (strcmp(key, "Return") == 0) return "Enter";
    return key;
}

static bool token_matches(const char *token, const char *held) {
    if (equal(token, canonical(held))) return true;
    if (equal(token, "Control") || equal(token, "Ctrl"))
        return equal(held, "ControlLeft") || equal(held, "ControlRight");
    if (equal(token, "Shift"))
        return equal(held, "ShiftLeft") || equal(held, "ShiftRight");
    if (equal(token, "Alt")) return equal(held, "Alt") || equal(held, "AltGr");
    if (equal(token, "Super") || equal(token, "Command")) return equal(held, "Meta");
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

bool bongo_cat_sound_shortcut_down(const BongoCatSoundShortcutState *state,
    const char *shortcut) {
    if (!state || !shortcut || !*shortcut) return false;
    const char *cursor = shortcut;
    while (*cursor) {
        const char *plus = strchr(cursor, '+');
        size_t length = plus ? (size_t)(plus - cursor) : strlen(cursor);
        char token[BONGO_CAT_ID_CAP];
        if (!length || length >= sizeof(token)) return false;
        memcpy(token, cursor, length); token[length] = '\0';
        bool found = false;
        for (size_t i = 0; i < state->count && !found; ++i)
            found = token_matches(token, state->held[i]);
        if (!found) return false;
        if (!plus) return true;
        cursor = plus + 1;
    }
    return false;
}
