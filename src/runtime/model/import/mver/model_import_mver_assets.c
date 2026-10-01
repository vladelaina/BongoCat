#include "model_import_mver_internal.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef BongoCatMverKeyNames KeyNames;

static bool asset_file(const BongoCatImportCandidate *candidate, const char *group,
    const char *name, char *output, size_t capacity) {
    char directory[BONGO_CAT_PATH_CAP];
    if (candidate->overrides[0] &&
        bongo_cat_path_join(directory, sizeof(directory), candidate->overrides, group) &&
        bongo_cat_path_join(output, capacity, directory, name) &&
        bongo_cat_path_is_file(output)) return true;
    return bongo_cat_path_join(directory, sizeof(directory), candidate->assets, group) &&
        bongo_cat_path_join(output, capacity, directory, name) &&
        bongo_cat_path_is_file(output);
}

static bool root_asset_file(const BongoCatImportCandidate *candidate,
    const char *name, char *output, size_t capacity) {
    if (candidate->overrides[0] &&
        bongo_cat_path_join(output, capacity, candidate->overrides, name) &&
        bongo_cat_path_is_file(output)) return true;
    return bongo_cat_path_join(output, capacity, candidate->assets, name) &&
        bongo_cat_path_is_file(output);
}

static bool copy_standard_pointer_assets(const BongoCatImportCandidate *candidate,
    const char *target) {
    static const char *const names[] = {
        "arm.png", "mouse.png", "mouse_left.png", "mouse_right.png",
        "mouse_side.png", "tablet.png", "tablet_left.png", "tablet_right.png"
    };
    char resources[BONGO_CAT_PATH_CAP], output[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resources, sizeof(resources), target, "resources") ||
        !bongo_cat_path_join(output, sizeof(output), resources, "mver-pointer") ||
        !bongo_cat_path_create_directory(output)) return false;
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        char source[BONGO_CAT_PATH_CAP], destination[BONGO_CAT_PATH_CAP];
        if (!root_asset_file(candidate, names[index], source, sizeof(source))) continue;
        int width = 0, height = 0;
        if (!bongo_cat_image_info(source, &width, &height) ||
            (width <= 1 && height <= 1)) continue;
        if (!bongo_cat_path_join(destination, sizeof(destination), output, names[index]) ||
            !bongo_cat_path_copy_file(source, destination)) return false;
    }
    return true;
}

int bongo_cat_mver_modifier_index(int code) {
    return code == 16 ? 0 : code == 17 ? 1 : code == 18 ? 2 : -1;
}

static void count_modifiers(yyjson_val *matrix, size_t counts[3]) {
    size_t row_index, row_count; yyjson_val *row;
    yyjson_arr_foreach(matrix, row_index, row_count, row) {
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(row, key_index, key_count, key) {
            int index = (yyjson_is_int(key) || yyjson_is_uint(key))
                ? bongo_cat_mver_modifier_index((int)yyjson_get_int(key)) : -1;
            if (index >= 0) counts[index]++;
        }
    }
}

KeyNames bongo_cat_mver_device_names(int code, size_t occurrence, size_t total) {
    KeyNames names = {0};
    if (code >= 48 && code <= 57) {
        snprintf(names.generated, sizeof(names.generated), "Num%c", (char)code);
    } else if (code >= 65 && code <= 90) {
        snprintf(names.generated, sizeof(names.generated), "Key%c", (char)code);
    } else if (code >= 112 && code <= 123) {
        snprintf(names.generated, sizeof(names.generated), "F%d", code - 111);
    } else if (bongo_cat_mver_modifier_index(code) >= 0) {
        static const char *const modifiers[][2] = {
            {"ShiftLeft", "ShiftRight"}, {"ControlLeft", "ControlRight"},
            {"Alt", "AltGr"}
        };
        int index = bongo_cat_mver_modifier_index(code);
        if (total > 1) {
            names.items[0] = modifiers[index][occurrence ? 1 : 0];
            names.count = 1;
        } else {
            names.items[0] = modifiers[index][0];
            names.items[1] = modifiers[index][1];
            names.count = 2;
        }
        return names;
    } else {
        static const struct { int code; const char *name; } map[] = {
            {8,"Backspace"},{9,"Tab"},{13,"Return"},{19,"Pause"},{20,"CapsLock"},
            {27,"Escape"},{32,"Space"},{33,"PageUp"},{34,"PageDown"},{35,"End"},
            {36,"Home"},{37,"LeftArrow"},{38,"UpArrow"},{39,"RightArrow"},
            {40,"DownArrow"},{44,"PrintScreen"},{45,"Insert"},{46,"Delete"},
            {91,"Meta"},{92,"Meta"},{93,"Apps"},{96,"Kp0"},{97,"Kp1"},
            {98,"Kp2"},{99,"Kp3"},{100,"Kp4"},{101,"Kp5"},{102,"Kp6"},
            {103,"Kp7"},{104,"Kp8"},{105,"Kp9"},{106,"KpMultiply"},
            {107,"KpPlus"},{109,"KpMinus"},{110,"KpDecimal"},{111,"KpDivide"},
            {144,"NumLock"},{145,"ScrollLock"},{186,"Semicolon"},{187,"Equal"},
            {188,"Comma"},{189,"Minus"},{190,"Period"},{191,"Slash"},
            {192,"BackQuote"},{219,"BracketLeft"},{220,"Backslash"},
            {221,"BracketRight"},{222,"Quote"}
        };
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
            if (map[i].code == code) { names.items[0] = map[i].name; names.count = 1; break; }
        if (!names.count && code > 0 && code <= 255) {
            snprintf(names.generated, sizeof(names.generated), "%d", code);
            names.count = 1;
        }
        return names;
    }
    names.count = 1;
    return names;
}

KeyNames bongo_cat_mver_gamepad_names(int code) {
    static const char *map[] = {"South", "East", "West", "North", "LeftTrigger",
        "RightTrigger", "LeftTrigger2", "RightTrigger2", "LeftThumb", "RightThumb",
        "DPadLeft", "DPadRight", "DPadUp", "DPadDown", "Start", "Select"};
    KeyNames names = {0};
    if (code >= 0 && (size_t)code < sizeof(map) / sizeof(map[0])) {
        names.items[0] = map[code]; names.count = 1;
    }
    return names;
}

static bool keyboard_index(yyjson_val *matrix, int code, size_t *result) {
    if (!yyjson_is_arr(matrix)) return false;
    size_t row_index, row_count; yyjson_val *row;
    yyjson_arr_foreach(matrix, row_index, row_count, row) {
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(row, key_index, key_count, key) {
            if ((yyjson_is_int(key) || yyjson_is_uint(key)) &&
                yyjson_get_int(key) == code) {
                *result = row_index;
                return true;
            }
        }
    }
    return false;
}

static bool process_matrix(const BongoCatImportCandidate *candidate, yyjson_val *matrix,
    yyjson_val *before, yyjson_val *after, yyjson_val *keyboard_matrix,
    const char *hand_name, const char *key_group, const char *target,
    BongoCatError *error) {
    if (!matrix || yyjson_is_null(matrix)) return true;
    if (!yyjson_is_arr(matrix)) return false;
    if (!yyjson_arr_size(matrix)) return true;
    char resources[BONGO_CAT_PATH_CAP], output_dir[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resources, sizeof(resources), target, "resources") ||
        !bongo_cat_path_join(output_dir, sizeof(output_dir), resources, key_group) ||
        !bongo_cat_path_create_directory(output_dir)) return false;
    size_t modifier_total[3] = {0}, modifier_seen[3] = {0};
    count_modifiers(before, modifier_seen);
    memcpy(modifier_total, modifier_seen, sizeof(modifier_total));
    count_modifiers(matrix, modifier_total);
    count_modifiers(after, modifier_total);
    size_t index, count; yyjson_val *keys;
    yyjson_arr_foreach(matrix, index, count, keys) {
        char hand[BONGO_CAT_PATH_CAP], filename[32];
        snprintf(filename, sizeof(filename), "%zu.png", index);
        if (!asset_file(candidate, hand_name, filename, hand, sizeof(hand)) ||
            !bongo_cat_image_info(hand, NULL, NULL)) {
            size_t missing_index, missing_count; yyjson_val *missing;
            yyjson_arr_foreach(keys, missing_index, missing_count, missing) {
                int modifier = (yyjson_is_int(missing) || yyjson_is_uint(missing))
                    ? bongo_cat_mver_modifier_index((int)yyjson_get_int(missing)) : -1;
                if (modifier >= 0) modifier_seen[modifier]++;
            }
            continue;
        }
        size_t key_index, key_count; yyjson_val *key;
        yyjson_arr_foreach(keys, key_index, key_count, key) {
            if (!yyjson_is_int(key) && !yyjson_is_uint(key)) {
                bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                    "Mver input row %zu contains a non-integer key", index);
                return false;
            }
            int code = (int)yyjson_get_int(key);
            char keyboard[BONGO_CAT_PATH_CAP];
            size_t keyboard_row;
            const char *keyboard_path = NULL;
            if (keyboard_index(keyboard_matrix, code, &keyboard_row)) {
                snprintf(filename, sizeof(filename), "%zu.png", keyboard_row);
                if (asset_file(candidate, "keyboard", filename, keyboard,
                    sizeof(keyboard)) && bongo_cat_image_info(keyboard, NULL, NULL))
                    keyboard_path = keyboard;
            }
            int modifier = bongo_cat_mver_modifier_index(code);
            size_t occurrence = modifier >= 0 ? modifier_seen[modifier]++ : 0;
            KeyNames names = candidate->gamepad_buttons
                ? bongo_cat_mver_gamepad_names(code) : bongo_cat_mver_device_names(code, occurrence,
                    modifier >= 0 ? modifier_total[modifier] : 0);
            if (!names.count) {
                bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                    "Mver input row %zu uses unsupported key code %d", index, code);
                return false;
            }
            if (!bongo_cat_mver_emit_pair(hand, keyboard_path, output_dir, names, error)) return false;
        }
    }
    return true;
}

bool bongo_cat_import_mver_assets(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (candidate->format != BONGO_CAT_IMPORT_MVER &&
        candidate->format != BONGO_CAT_IMPORT_MVER_PATCH) return true;
    FILE *file = bongo_cat_file_open(candidate->config, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL, NULL) : NULL;
    if (file) fclose(file);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *mode = yyjson_is_obj(root)
        ? yyjson_obj_get(root, bongo_cat_mode_name(candidate->mode)) : NULL;
    if (!yyjson_is_obj(mode)) {
        yyjson_doc_free(document);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot parse Mver input configuration: %s", candidate->config);
        return false;
    }
    bool ok = false;
    yyjson_val *keyboard = yyjson_obj_get(mode, "keyboard");
    if (candidate->mode == BONGO_CAT_MODE_STANDARD) {
        yyjson_val *hand = yyjson_obj_get(mode, "hand");
        ok = process_matrix(candidate, hand, NULL, NULL,
            keyboard, "hand", "left-keys", target, error);
        if (ok) ok = copy_standard_pointer_assets(candidate, target);
    } else {
        yyjson_val *left_keys = yyjson_obj_get(mode, "lefthand");
        yyjson_val *right_keys = yyjson_obj_get(mode, "righthand");
        ok = process_matrix(candidate, left_keys, NULL, right_keys, keyboard,
            "lefthand", "left-keys", target, error) &&
            process_matrix(candidate, right_keys, left_keys, NULL, keyboard,
                "righthand", "right-keys", target, error);
    }
    yyjson_doc_free(document);
    if (!ok && error && !error->message[0]) bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Cannot convert Mver input configuration: %s", candidate->config);
    return ok;
}
