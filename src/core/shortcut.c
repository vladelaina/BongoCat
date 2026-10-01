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
    if (strcmp(name, "Meta") == 0) { state->meta = value; return true; }
    return false;
}

bool bongo_cat_shortcut_update(BongoCatShortcutState *state, const BongoCatInputEvent *event) {
    if (!state || !event || (event->kind != BONGO_CAT_INPUT_KEY_DOWN &&
        event->kind != BONGO_CAT_INPUT_KEY_UP)) return false;
    bool down = event->kind == BONGO_CAT_INPUT_KEY_DOWN;
    if (modifier(state, event->name, down)) return false;
    if (!down) {
        if (strcmp(state->pressed, event->name) == 0) state->pressed[0] = '\0';
        return false;
    }
    if (strcmp(state->pressed, event->name) == 0) return false;
    snprintf(state->pressed, sizeof(state->pressed), "%s", event->name);
    return true;
}

static bool equal_ci(const char *left, const char *right) {
    while (*left && *right) {
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++)) return false;
    }
    return *left == *right;
}

static const char *canonical_key(const char *name, char output[16]) {
    if (strncmp(name, "Key", 3) == 0 && name[3] && !name[4]) {
        output[0] = name[3]; output[1] = '\0'; return output;
    }
    if (strncmp(name, "Num", 3) == 0 && name[3] && !name[4]) {
        output[0] = name[3]; output[1] = '\0'; return output;
    }
    if (strcmp(name, "UpArrow") == 0) return "ArrowUp";
    if (strcmp(name, "DownArrow") == 0) return "ArrowDown";
    if (strcmp(name, "LeftArrow") == 0) return "ArrowLeft";
    if (strcmp(name, "RightArrow") == 0) return "ArrowRight";
    if (strcmp(name, "Return") == 0) return "Enter";
    if (strcmp(name, "Minus") == 0) return "-";
    if (strcmp(name, "Equal") == 0) return "=";
    return name;
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
    if (equal_ci(value, "Control") || equal_ci(value, "Ctrl")) *control = true;
    else if (equal_ci(value, "Shift")) *shift = true;
    else if (equal_ci(value, "Alt")) *alt = true;
    else if (equal_ci(value, "Super") || equal_ci(value, "Command") ||
        equal_ci(value, "Meta")) *meta = true;
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
    if (event->kind != BONGO_CAT_INPUT_KEY_DOWN) return false;
    bool control = false, shift = false, alt = false, meta = false;
    const char *primary = NULL;
    size_t primary_length = 0;
    const char *cursor = shortcut;
    while (*cursor) {
        const char *plus = strchr(cursor, '+');
        size_t length = plus ? (size_t)(plus - cursor) : strlen(cursor);
        if (!modifier_token(cursor, length, &control, &shift, &alt, &meta)) {
            primary = cursor; primary_length = length;
        }
        if (!plus) break;
        cursor = plus + 1;
    }
    if (!primary || control != (state->control != 0) || shift != (state->shift != 0) ||
        alt != (state->alt != 0) || meta != (state->meta != 0)) return false;
    char expected[32], key[16];
    if (primary_length >= sizeof(expected)) return false;
    memcpy(expected, primary, primary_length); expected[primary_length] = '\0';
    return equal_ci(expected, canonical_key(event->name, key));
}

bool bongo_cat_shortcut_release_matches(const BongoCatInputEvent *event,
    const char *shortcut) {
    if (!event || !shortcut || !*shortcut) return false;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON)
        return event->value <= 0.5f && strncmp(shortcut, "Gamepad:", 8) == 0 &&
            equal_ci(shortcut + 8, event->name);
    if (event->kind != BONGO_CAT_INPUT_KEY_UP) return false;
    const char *primary = strrchr(shortcut, '+');
    primary = primary ? primary + 1 : shortcut;
    char key[16];
    return equal_ci(primary, canonical_key(event->name, key));
}
