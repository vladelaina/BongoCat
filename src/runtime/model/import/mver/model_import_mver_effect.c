#include "model_import_mver_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <yyjson.h>

static bool effect_source(const BongoCatImportCandidate *candidate, const char *name,
    char *output, size_t capacity) {
    char directory[BONGO_CAT_PATH_CAP];
    if (candidate->overrides[0] &&
        bongo_cat_path_join(directory, sizeof(directory), candidate->overrides, "face") &&
        bongo_cat_path_join(output, capacity, directory, name) &&
        bongo_cat_path_is_file(output)) return true;
    return bongo_cat_path_join(directory, sizeof(directory), candidate->assets, "face") &&
        bongo_cat_path_join(output, capacity, directory, name) &&
        bongo_cat_path_is_file(output);
}

static bool clear_binding(yyjson_mut_doc *output, yyjson_mut_val *items,
    yyjson_val *root, const BongoCatImportCandidate *candidate) {
    yyjson_val *decoration = yyjson_obj_get(root, "decoration");
    yyjson_val *row = yyjson_obj_get(decoration, "emoticonClear");
    if (!row || yyjson_is_null(row) ||
        (yyjson_is_arr(row) && !yyjson_arr_size(row))) return true;
    char shortcut[BONGO_CAT_SHORTCUT_CAP];
    if (!bongo_cat_mver_chord(candidate, row, shortcut, sizeof(shortcut))) return false;
    yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
    return item && yyjson_mut_obj_add_str(output, item, "kind", "effect-clear") &&
        yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut);
}

bool bongo_cat_mver_effects(void *raw_output, void *raw_items, void *raw_root,
    void *raw_mode,
    const BongoCatImportCandidate *candidate, const char *target) {
    yyjson_mut_doc *output = raw_output;
    yyjson_mut_val *items = raw_items;
    yyjson_val *root = raw_root;
    yyjson_val *mode = raw_mode;
    yyjson_val *rows = yyjson_obj_get(mode, "face");
    if (!yyjson_is_arr(rows) || !yyjson_arr_size(rows)) return true;
    yyjson_val *decoration = yyjson_obj_get(root, "decoration");
    bool momentary = !yyjson_get_bool(yyjson_obj_get(decoration, "emoticonKeep"));
    size_t emitted = 0;
    char resources[BONGO_CAT_PATH_CAP], target_effects[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(resources, sizeof(resources), target, "resources") ||
        !bongo_cat_path_join(target_effects, sizeof(target_effects), resources, "effects") ||
        !bongo_cat_path_create_directory(target_effects)) return false;
    size_t index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        char shortcut[BONGO_CAT_SHORTCUT_CAP], name[32];
        char source[BONGO_CAT_PATH_CAP], destination[BONGO_CAT_PATH_CAP];
        if (yyjson_is_null(row) || (yyjson_is_arr(row) && !yyjson_arr_size(row)))
            continue;
        snprintf(name, sizeof(name), "%zu.png", index);
        if (!effect_source(candidate, name, source, sizeof(source)) ||
            !bongo_cat_image_info(source, NULL, NULL)) continue;
        if (!bongo_cat_mver_chord(candidate, row, shortcut, sizeof(shortcut))) return false;
        if (!bongo_cat_path_join(destination, sizeof(destination), target_effects, name) ||
            !bongo_cat_path_copy_file(source, destination)) return false;
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
        char relative[BONGO_CAT_PATH_CAP];
        snprintf(relative, sizeof(relative), "resources/effects/%s", name);
        if (!item || !yyjson_mut_obj_add_str(output, item, "kind", "effect") ||
            !yyjson_mut_obj_add_int(output, item, "index", (int)index) ||
            !yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut) ||
            !yyjson_mut_obj_add_strcpy(output, item, "effect", relative) ||
            (momentary && !yyjson_mut_obj_add_bool(output, item, "momentary", true))) return false;
        emitted++;
    }
    return !emitted || momentary || clear_binding(output, items, root, candidate);
}
