#include "bongo_cat/shortcut.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void bongo_cat_shortcut_init(BongoCatShortcutState *state) {
    if (state) memset(state, 0, sizeof(*state));
}

static bool modifier(BongoCatShortcutState *state, const char *name, bool down) {
    uint8_t value = down ? 1 : 0;
    if (strcmp(name, "ControlLeft") == 0) { state->control = (state->control & 2) | value; return true; }
    if (strcmp(name, "ControlRight") == 0) { state->control = (state->control & 1) | (value << 1); return true; }
    if (strcmp(name, "ShiftLeft") == 0) { state->shift = (state->shift & 2) | value; return true; }
    if (strcmp(name, "ShiftRight") == 0) { state->shift = (state->shift & 1) | (value << 1); return true; }
    if (strcmp(name, "Alt") == 0) { state->alt = (state->alt & 2) | value; return true; }
    if (strcmp(name, "AltGr") == 0) { state->alt = (state->alt & 1) | (value << 1); return true; }
    if (strcmp(name, "MetaLeft") == 0) { state->meta = (state->meta & 6) | value; return true; }
    if (strcmp(name, "MetaRight") == 0) { state->meta = (state->meta & 5) | (value << 1); return true; }
    if (strcmp(name, "Meta") == 0) { state->meta = (state->meta & 3) | (value << 2); return true; }
    return false;
}

bool bongo_cat_shortcut_update(BongoCatShortcutState *state, const BongoCatInputEvent *event) {
    if (!state || !event || (event->kind != BONGO_CAT_INPUT_KEY_DOWN &&
        event->kind != BONGO_CAT_INPUT_KEY_UP)) return false;
    bool down = event->kind == BONGO_CAT_INPUT_KEY_DOWN;
    state->changed = bongo_cat_sound_shortcut_update(&state->held, event);
    if (modifier(state, event->name, down)) return down && state->changed;
    if (!down) {
        if (strcmp(state->pressed, event->name) == 0) state->pressed[0] = '\0';
        return false;
    }
    if (!state->changed) return false;
    snprintf(state->pressed, sizeof(state->pressed), "%s", event->name);
    return true;
}

static bool equal_ci(const char *left, const char *right) {
    while (*left && *right) {
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++)) return false;
    }
    return *left == *right;
}

static const char *display_key(const char *token, size_t length) {
    static const struct { const char *name; const char *label; } names[] = {
        {"Control", "Ctrl"}, {"Ctrl", "Ctrl"},
#if defined(_WIN32)
        {"Meta", "Win"}, {"Super", "Win"}, {"Command", "Win"},
#elif defined(__APPLE__)
        {"Meta", "Cmd"}, {"Super", "Cmd"}, {"Command", "Cmd"},
#else
        {"Meta", "Super"}, {"Command", "Super"},
#endif
        {"Escape", "Esc"}, {"Delete", "Del"}, {"Insert", "Ins"},
        {"PageUp", "PgUp"}, {"PageDown", "PgDn"},
        {"PrintScreen", "PrtSc"}, {"Return", "Enter"},
        {"ArrowUp", "\xE2\x86\x91"}, {"ArrowDown", "\xE2\x86\x93"},
        {"ArrowLeft", "\xE2\x86\x90"}, {"ArrowRight", "\xE2\x86\x92"},
        {"UpArrow", "\xE2\x86\x91"}, {"DownArrow", "\xE2\x86\x93"},
        {"LeftArrow", "\xE2\x86\x90"}, {"RightArrow", "\xE2\x86\x92"},
        {"BackQuote", "`"}, {"Minus", "-"}, {"Equal", "="},
        {"BracketLeft", "["}, {"BracketRight", "]"},
        {"Backslash", "\\"}, {"Semicolon", ";"}, {"Quote", "'"},
        {"Comma", ","}, {"Period", "."}, {"Slash", "/"},
        {"Kp0", "Num 0"}, {"Kp1", "Num 1"}, {"Kp2", "Num 2"},
        {"Kp3", "Num 3"}, {"Kp4", "Num 4"}, {"Kp5", "Num 5"},
        {"Kp6", "Num 6"}, {"Kp7", "Num 7"}, {"Kp8", "Num 8"},
        {"Kp9", "Num 9"}, {"KpMultiply", "Num *"}, {"KpPlus", "Num +"},
        {"KpMinus", "Num -"}, {"KpDecimal", "Num ."}, {"KpDivide", "Num /"}
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strlen(names[i].name) != length) continue;
        size_t j = 0;
        while (j < length && tolower((unsigned char)token[j]) ==
            tolower((unsigned char)names[i].name[j])) ++j;
        if (j == length) return names[i].label;
    }
    return NULL;
}

void bongo_cat_shortcut_format(const char *shortcut, char *output, size_t capacity) {
    if (!output || !capacity) return;
    output[0] = '\0';
    if (!shortcut) return;
    if (strncmp(shortcut, "Gamepad:", 8) == 0) {
        snprintf(output, capacity, "%s", shortcut);
        return;
    }
    size_t used = 0;
    while (*shortcut && used < capacity - 1) {
        const char *plus = strchr(shortcut, '+');
        size_t length = plus ? (size_t)(plus - shortcut) : strlen(shortcut);
        const char *label = display_key(shortcut, length);
        const char *shown = label ? label : shortcut;
        size_t count = label ? strlen(label) : length;
        if (count > capacity - 1 - used) count = capacity - 1 - used;
        memcpy(output + used, shown, count);
        used += count;
        if (!plus) break;
        if (used < capacity - 1) output[used++] = '+';
        shortcut = plus + 1;
    }
    output[used] = '\0';
}

static bool modifier_token(const char *token, size_t length, bool *control,
    bool *shift, bool *alt, bool *meta) {
    char value[24];
    if (!length || length >= sizeof(value)) return false;
    memcpy(value, token, length); value[length] = '\0';
    if (equal_ci(value, "Control") || equal_ci(value, "Ctrl") ||
        equal_ci(value, "ControlLeft") || equal_ci(value, "ControlRight")) *control = true;
    else if (equal_ci(value, "Shift") || equal_ci(value, "ShiftLeft") ||
        equal_ci(value, "ShiftRight")) *shift = true;
    else if (equal_ci(value, "Alt") || equal_ci(value, "AltGr")) *alt = true;
    else if (equal_ci(value, "Super") || equal_ci(value, "Command") ||
        equal_ci(value, "Meta") || equal_ci(value, "MetaLeft") ||
        equal_ci(value, "MetaRight")) *meta = true;
    else return false;
    return true;
}

bool bongo_cat_shortcut_matches(const BongoCatShortcutState *state,
    const BongoCatInputEvent *event, const char *shortcut) {
    if (!state || !event || !shortcut || !*shortcut)
        return false;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON)
        return event->value > 0.5f && strncmp(shortcut, "Gamepad:", 8) == 0 &&
            equal_ci(shortcut + 8, event->name);
    if (event->kind != BONGO_CAT_INPUT_KEY_DOWN || !state->changed) return false;
    bool control = false, shift = false, alt = false, meta = false;
    bool primary = false;
    const char *cursor = shortcut;
    while (*cursor) {
        const char *plus = strchr(cursor, '+');
        size_t length = plus ? (size_t)(plus - cursor) : strlen(cursor);
        if (!length || (plus && !plus[1])) return false;
        if (!modifier_token(cursor, length, &control, &shift, &alt, &meta)) {
            primary = true;
        }
        if (!plus) break;
        cursor = plus + 1;
    }
    if (!primary || control != (state->control != 0) || shift != (state->shift != 0) ||
        alt != (state->alt != 0) || meta != (state->meta != 0)) return false;
    return bongo_cat_sound_shortcut_pressed(&state->held, event, shortcut);
}

bool bongo_cat_shortcut_release_matches(const BongoCatInputEvent *event,
    const char *shortcut) {
    if (!event || !shortcut || !*shortcut) return false;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON)
        return event->value <= 0.5f && strncmp(shortcut, "Gamepad:", 8) == 0 &&
            equal_ci(shortcut + 8, event->name);
    if (event->kind != BONGO_CAT_INPUT_KEY_UP) return false;
    BongoCatSoundShortcutState released = {0};
    BongoCatInputEvent down = *event;
    down.kind = BONGO_CAT_INPUT_KEY_DOWN;
    bongo_cat_sound_shortcut_update(&released, &down);
    bool matches = false;
    for (const char *cursor = shortcut; *cursor;) {
        const char *plus = strchr(cursor, '+');
        size_t length = plus ? (size_t)(plus - cursor) : strlen(cursor);
        char token[BONGO_CAT_ID_CAP];
        if (!length || length >= sizeof(token) || (plus && !plus[1])) return false;
        memcpy(token, cursor, length); token[length] = '\0';
        matches = matches || bongo_cat_sound_shortcut_down(&released, token);
        if (!plus) break;
        cursor = plus + 1;
    }
    return matches;
}
