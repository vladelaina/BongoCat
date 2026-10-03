#include "mver_config.h"
#include "model_import_path.h"
#include "bongo_cat/path.h"
#include "bongo_cat/json.h"
#include <yyjson.h>

#include <stdio.h>
#include <limits.h>

#include <string.h>

int bongo_cat_mver_gamepad_input_mode(const char *model_directory) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_mver_config_find(model_directory, path, sizeof(path))) return -1;
    yyjson_doc *document = bongo_cat_json_read_file(path,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *input = yyjson_obj_get(yyjson_obj_get(root, "gamepad"), "input_mode");
    int mode = yyjson_is_int(input) && yyjson_get_int(input) >= 0 &&
        yyjson_get_int(input) <= INT_MAX ? (int)yyjson_get_int(input) : -1;
    yyjson_doc_free(document);
    return mode;
}

bool bongo_cat_mver_gamepad_keyboard(const char *model_directory) {
    return bongo_cat_mver_gamepad_input_mode(model_directory) == 0;
}

/* Match Mver's Live2D configuration lookup, not the model asset directory.
   Lock-hand bindings always use standard; gamepad also uses standard for
   expressions and ordinary motions. Audio remains local to each mode. */
const char *bongo_cat_mver_binding_mode(const char *mode, const char *field) {
    if (strcmp(field, "l2d_motion_lockhand") == 0 ||
        (strcmp(mode, "gamepad") == 0 &&
         (strcmp(field, "l2d_expression") == 0 ||
          strcmp(field, "l2d_motion") == 0))) return "standard";
    return mode;
}


bool bongo_cat_import_mver_config_path(const char *root,
    char *path, size_t capacity) {
    static const char *names[] = {"bongocat.skin.json", "config.json"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (bongo_cat_path_join(path, capacity, root, names[i]) &&
            bongo_cat_path_is_file(path)) return true;
    return false;
}

bool bongo_cat_mver_config_find(const char *model_directory, char *output, size_t capacity) {
    char directory[BONGO_CAT_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s", model_directory);
    for (unsigned depth = 0; depth < 4; ++depth) {
        if (bongo_cat_import_mver_config_path(directory, output, capacity)) return true;
        char parent[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_import_parent_path(directory, parent, sizeof(parent))) break;
        snprintf(directory, sizeof(directory), "%s", parent);
    }
    return false;
}

