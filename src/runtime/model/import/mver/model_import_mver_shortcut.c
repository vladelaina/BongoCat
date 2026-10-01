#include "model_import_mver_internal.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool append(char *output, size_t capacity, const char *value) {
    size_t used = strlen(output), length = strlen(value);
    if (used + length >= capacity) return false;
    memcpy(output + used, value, length + 1);
    return true;
}

static const char *shortcut_key(int code, char generated[16]) {
    BongoCatMverKeyNames names = bongo_cat_mver_device_names(code, 0, 1);
    const char *name = names.items[0];
    if (!name && names.generated[0]) {
        snprintf(generated, 16, "%s", names.generated);
        name = generated;
    }
    if (!name || !*name || bongo_cat_mver_modifier_index(code) >= 0) return NULL;
    if (strncmp(name, "Key", 3) == 0 || strncmp(name, "Num", 3) == 0) {
        generated[0] = name[3]; generated[1] = '\0'; return generated;
    }
    if (strcmp(name, "UpArrow") == 0) return "ArrowUp";
    if (strcmp(name, "DownArrow") == 0) return "ArrowDown";
    if (strcmp(name, "LeftArrow") == 0) return "ArrowLeft";
    if (strcmp(name, "RightArrow") == 0) return "ArrowRight";
    if (strcmp(name, "Return") == 0) return "Enter";
    return name;
}

static bool keyboard_chord(yyjson_val *row, char *output, size_t capacity) {
    output[0] = '\0';
    if (!yyjson_is_arr(row)) return false;
    bool modifiers[3] = {false}, primary = false;
    size_t index, count; yyjson_val *key;
    yyjson_arr_foreach(row, index, count, key) {
        if (!yyjson_is_int(key) && !yyjson_is_uint(key)) return false;
        int code = (int)yyjson_get_int(key);
        int modifier = bongo_cat_mver_modifier_index(code);
        if (modifier >= 0) { modifiers[modifier] = true; continue; }
        char generated[16]; const char *name = shortcut_key(code, generated);
        if (!name || primary) return false;
        primary = append(output, capacity, name);
    }
    if (!primary) return false;
    char key_name[BONGO_CAT_SHORTCUT_CAP];
    snprintf(key_name, sizeof(key_name), "%s", output); output[0] = '\0';
    return (!modifiers[1] || append(output, capacity, "Control+")) &&
        (!modifiers[0] || append(output, capacity, "Shift+")) &&
        (!modifiers[2] || append(output, capacity, "Alt+")) &&
        append(output, capacity, key_name);
}

static bool gamepad_chord(yyjson_val *row, char *output, size_t capacity) {
    if (!yyjson_is_arr(row) || yyjson_arr_size(row) != 1) return false;
    yyjson_val *key = yyjson_arr_get_first(row);
    if (!yyjson_is_int(key) && !yyjson_is_uint(key)) return false;
    BongoCatMverKeyNames names = bongo_cat_mver_gamepad_names(
        (int)yyjson_get_int(key));
    return names.count && names.items[0] &&
        snprintf(output, capacity, "Gamepad:%s", names.items[0]) > 0;
}

bool bongo_cat_mver_chord(const BongoCatImportCandidate *candidate,
    void *raw, char *output, size_t capacity) {
    yyjson_val *row = raw;
    yyjson_val *only = yyjson_is_arr(row) && yyjson_arr_size(row) == 1
        ? yyjson_arr_get_first(row) : NULL;
    int code = (yyjson_is_int(only) || yyjson_is_uint(only))
        ? (int)yyjson_get_int(only) : -1;
    return candidate && candidate->gamepad_buttons && code >= 0 && code <= 15
        ? gamepad_chord(row, output, capacity) : keyboard_chord(row, output, capacity);
}

/* Mver's audio uses Windows keys even when its visual mode uses a gamepad.
   Preserve arbitrary chords, modifier-only keys, and mouse buttons. */
bool bongo_cat_mver_sound_chord(void *raw, char *output, size_t capacity) {
    yyjson_val *row = raw;
    if (!output || !capacity || !yyjson_is_arr(row) || !yyjson_arr_size(row)) return false;
    output[0] = '\0';
    size_t index, count; yyjson_val *key;
    yyjson_arr_foreach(row, index, count, key) {
        if (!yyjson_is_int(key) && !yyjson_is_uint(key)) return false;
        int64_t raw_code = yyjson_get_sint(key);
        if (raw_code <= 0 || raw_code > 255) return false;
        int code = (int)raw_code;
        const char *name = NULL;
        if (code == 1) name = "Left";
        else if (code == 2) name = "Right";
        else if (code == 4) name = "Middle";
        else if (code == 5) name = "Back";
        else if (code == 6) name = "Forward";
        else if (code == 16) name = "Shift";
        else if (code == 17) name = "Control";
        else if (code == 18) name = "Alt";
        else if (code == 160) name = "ShiftLeft";
        else if (code == 161) name = "ShiftRight";
        else if (code == 162) name = "ControlLeft";
        else if (code == 163) name = "ControlRight";
        else if (code == 164) name = "Alt";
        else if (code == 165) name = "AltGr";
        char generated[16];
        if (!name) name = shortcut_key(code, generated);
        if (!name || (index && !append(output, capacity, "+")) ||
            !append(output, capacity, name)) return false;
    }
    return true;
}
