#include "model_import_tauri_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bongo_cat_tauri_key_code(const char *filename) {
    char name[BONGO_CAT_ID_CAP];
    snprintf(name, sizeof(name), "%s", filename ? filename : "");
    char *dot = strrchr(name, '.');
    if (!dot || SDL_strcasecmp(dot, ".png") != 0) return -1;
    *dot = '\0';
    char *end = NULL;
    long numeric = strtol(name, &end, 10);
    if (name[0] && end && !end[0] && numeric >= 0 && numeric <= 255)
        return (int)numeric;
    if (strlen(name) == 4 && SDL_strncasecmp(name, "Key", 3) == 0 &&
        name[3] >= 'A' && name[3] <= 'Z') return name[3];
    if (strlen(name) == 4 && SDL_strncasecmp(name, "Num", 3) == 0 &&
        name[3] >= '0' && name[3] <= '9') return name[3];
    if ((name[0] == 'F' || name[0] == 'f') && strlen(name) <= 3) {
        long value = strtol(name + 1, &end, 10);
        if (end != name + 1 && !*end && value >= 1 && value <= 12)
            return 111 + (int)value;
    }
    static const struct { const char *name; int code; } map[] = {
        {"Backspace", 8}, {"BackSpace", 8}, {"Tab", 9}, {"Return", 13},
        {"Pause", 19}, {"CapsLock", 20}, {"Escape", 27}, {"Space", 32},
        {"PageUp", 33}, {"PageDown", 34}, {"End", 35}, {"Home", 36},
        {"LeftArrow", 37}, {"UpArrow", 38}, {"RightArrow", 39},
        {"DownArrow", 40}, {"PrintScreen", 44}, {"Insert", 45},
        {"Delete", 46}, {"Meta", 91}, {"MetaLeft", 91}, {"MetaRight", 92},
        {"Apps", 93}, {"Kp0", 96}, {"Kp1", 97}, {"Kp2", 98},
        {"Kp3", 99}, {"Kp4", 100}, {"Kp5", 101}, {"Kp6", 102},
        {"Kp7", 103}, {"Kp8", 104}, {"Kp9", 105},
        {"KpMultiply", 106}, {"KpPlus", 107}, {"KpMinus", 109},
        {"KpDecimal", 110}, {"KpDivide", 111}, {"NumLock", 144},
        {"ScrollLock", 145}, {"Semicolon", 186}, {"SemiColon", 186},
        {"Equal", 187}, {"Comma", 188}, {"Minus", 189},
        {"Period", 190}, {"Dot", 190}, {"Slash", 191},
        {"BackQuote", 192}, {"BracketLeft", 219}, {"LeftBracket", 219},
        {"Backslash", 220}, {"BracketRight", 221}, {"RightBracket", 221},
        {"Quote", 222}, {"Shift", 16}, {"ShiftLeft", 16},
        {"ShiftRight", 16}, {"Control", 17}, {"ControlLeft", 17},
        {"ControlRight", 17}, {"Alt", 18}, {"AltGr", 18}
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (SDL_strcasecmp(name, map[i].name) == 0) return map[i].code;
    static const struct { const char *name; int code; } gamepad[] = {
        {"South", 0}, {"East", 1}, {"West", 2}, {"North", 3},
        {"LeftTrigger", 4}, {"RightTrigger", 5}, {"LeftTrigger2", 6},
        {"RightTrigger2", 7}, {"LeftThumb", 8}, {"RightThumb", 9},
        {"DPadLeft", 10}, {"DPadRight", 11}, {"DPadUp", 12},
        {"DPadDown", 13}, {"Start", 14}, {"Select", 15}
    };
    for (size_t i = 0; i < sizeof(gamepad) / sizeof(gamepad[0]); ++i)
        if (SDL_strcasecmp(name, gamepad[i].name) == 0) return gamepad[i].code;
    return -1;
}
