#include "model_import_tauri_internal.h"

#include "bongo_cat/json.h"

#include <yyjson.h>

static bool add_matrix(yyjson_mut_doc *document, yyjson_mut_val *mode,
    const char *name, const TauriKeyFiles *files) {
    yyjson_mut_val *matrix = yyjson_mut_obj_add_arr(document, mode, name);
    if (!matrix) return false;
    for (size_t i = 0; i < files->count; ++i) {
        yyjson_mut_val *row = yyjson_mut_arr_add_arr(document, matrix);
        if (!row || !yyjson_mut_arr_add_int(document, row,
                files->values[i].code)) return false;
    }
    return true;
}

bool bongo_cat_tauri_write_config(const char *path, BongoCatModelMode mode,
    const TauriKeyFiles *left, const TauriKeyFiles *right,
    const TauriMverCalibration *calibration, BongoCatError *error) {
    if (!path || !left || !right || !calibration) return false;
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    yyjson_mut_val *decoration = root ? yyjson_mut_obj_add_obj(document, root,
        "decoration") : NULL;
    yyjson_mut_val *mode_object = root ? yyjson_mut_obj_add_obj(document, root,
        bongo_cat_mode_name(mode)) : NULL;
    yyjson_mut_val *window_size = decoration ? yyjson_mut_obj_add_arr(document,
        decoration, "window_size") : NULL;
    yyjson_mut_val *offset = decoration ? yyjson_mut_obj_add_arr(document,
        decoration, "l2d_offset") : NULL;
    bool ok = root && decoration && mode_object && window_size && offset &&
        yyjson_mut_arr_add_int(document, window_size,
            calibration->window_width) &&
        yyjson_mut_arr_add_int(document, window_size,
            calibration->window_height) &&
        yyjson_mut_obj_add_real(document, decoration, "l2d_correct",
            calibration->l2d_correct) &&
        yyjson_mut_arr_add_real(document, offset, calibration->l2d_offset_x) &&
        yyjson_mut_arr_add_real(document, offset, calibration->l2d_offset_y) &&
        yyjson_mut_obj_add_bool(document, decoration, "l2d_horizontal_flip",
            calibration->mirror) &&
        yyjson_mut_obj_add_bool(document, decoration, "l2d_auto_frame",
            calibration->auto_frame) &&
        yyjson_mut_obj_add_int(document, root, "mode",
            mode == BONGO_CAT_MODE_STANDARD ? 1 :
            mode == BONGO_CAT_MODE_KEYBOARD ? 2 : 3) &&
        yyjson_mut_obj_add_bool(document, mode_object, "l2d", true);
    if (ok && mode == BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "hand", left) &&
            yyjson_mut_obj_add_bool(document, mode_object, "mouse", false) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode != BONGO_CAT_MODE_STANDARD)
        ok = add_matrix(document, mode_object, "lefthand", left) &&
            add_matrix(document, mode_object, "righthand", right) &&
            yyjson_mut_obj_add_arr(document, mode_object, "keyboard");
    if (ok && mode == BONGO_CAT_MODE_GAMEPAD)
        ok = yyjson_mut_obj_add_int(document, mode_object, "input_mode", 1);
    if (ok) ok = yyjson_mut_obj_add_arr(document, mode_object, "face") &&
        yyjson_mut_obj_add_arr(document, mode_object, "sounds") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_expression") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion") &&
        yyjson_mut_obj_add_arr(document, mode_object, "l2d_motion_lockhand");
    if (ok) ok = bongo_cat_json_write_file(path, document,
        YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(document);
    if (!ok && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot write normalized Mver configuration: %s", path);
    return ok;
}
