#include "model_import.h"
#include "model_import_mver_internal.h"
#include "runtime.h"
#include "../../mver/mver_render.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static const char *format_name(BongoCatImportFormat format) {
    if (format == BONGO_CAT_IMPORT_MVER) return "bongo-cat-mver";
    if (format == BONGO_CAT_IMPORT_MVER_PATCH) return "bongo-cat-mver-patch";
    return "tauri-live2d";
}

static double number_or(yyjson_val *value, double fallback) {
    return yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
}

static bool usable_pointer_image(const char *path) {
    int width = 0, height = 0;
    return bongo_cat_path_is_file(path) &&
        bongo_cat_image_info(path, &width, &height) &&
        (width > 1 || height > 1);
}

static bool pointer_asset(const BongoCatImportCandidate *candidate,
    const char *name) {
    char path[BONGO_CAT_PATH_CAP];
    if (candidate->overrides[0] && bongo_cat_path_join(path, sizeof(path),
            candidate->overrides, name) && bongo_cat_path_is_file(path))
        return usable_pointer_image(path);
    return bongo_cat_path_join(path, sizeof(path), candidate->assets, name) &&
        usable_pointer_image(path);
}

static bool add_standard_pointer(yyjson_mut_doc *output, yyjson_mut_val *root,
    yyjson_val *config, const BongoCatImportCandidate *candidate) {
    if (candidate->mode != BONGO_CAT_MODE_STANDARD) return true;
    yyjson_val *decoration = yyjson_obj_get(config, "decoration");
    yyjson_val *standard = yyjson_obj_get(config, "standard");
    yyjson_val *mouse_value = yyjson_obj_get(standard, "mouse");
    bool mouse = yyjson_is_bool(mouse_value) && yyjson_get_bool(mouse_value);
    yyjson_val *live2d_value = yyjson_obj_get(standard, "l2d");
    bool live2d = !yyjson_is_bool(live2d_value) || yyjson_get_bool(live2d_value);
    size_t device_index = mouse ? 0 : 1;
    yyjson_val *offset_x = yyjson_obj_get(decoration, "offsetX");
    yyjson_val *offset_y = yyjson_obj_get(decoration, "offsetY");
    yyjson_val *scale = yyjson_obj_get(decoration, "scalar");
    yyjson_val *hand_offset = yyjson_obj_get(
        live2d ? standard : decoration, "hand_offset");
    yyjson_val *line = yyjson_obj_get(decoration, "armLineColor");
    yyjson_val *left_handed = yyjson_obj_get(decoration, "leftHanded");
    const char *device = mouse ? "resources/mver-pointer/mouse.png" :
        "resources/mver-pointer/tablet.png";
    const char *left = mouse ? "resources/mver-pointer/mouse_left.png" :
        "resources/mver-pointer/tablet_left.png";
    const char *right = mouse ? "resources/mver-pointer/mouse_right.png" :
        "resources/mver-pointer/tablet_right.png";
    const char *side = mouse ? "resources/mver-pointer/mouse_side.png" : "";
    /* Mver 0.1.6's l2d switch replaces the sprite renderer. The older
       standalone mode 98 instead draws a sprite pointer over Live2D. */
    bool sprite_pointer = !yyjson_is_true(live2d_value) ||
        yyjson_get_int(yyjson_obj_get(config, "mode")) == 98;
    bool enabled = sprite_pointer && pointer_asset(candidate, "arm.png") &&
        pointer_asset(candidate, mouse ? "mouse.png" : "tablet.png");
    yyjson_mut_val *pointer = yyjson_mut_obj_add_obj(output, root, "standardPointer");
    return pointer &&
        yyjson_mut_obj_add_bool(output, pointer, "enabled", enabled) &&
        yyjson_mut_obj_add_bool(output, pointer, "mouse", mouse) &&
        yyjson_mut_obj_add_bool(output, pointer, "leftHanded",
            yyjson_is_bool(left_handed) && yyjson_get_bool(left_handed)) &&
        yyjson_mut_obj_add_str(output, pointer, "arm",
            "resources/mver-pointer/arm.png") &&
        yyjson_mut_obj_add_strcpy(output, pointer, "device", device) &&
        yyjson_mut_obj_add_strcpy(output, pointer, "left", left) &&
        yyjson_mut_obj_add_strcpy(output, pointer, "right", right) &&
        yyjson_mut_obj_add_strcpy(output, pointer, "side", side) &&
        yyjson_mut_obj_add_real(output, pointer, "offsetX",
            number_or(yyjson_arr_get(offset_x, device_index), 0.0)) &&
        yyjson_mut_obj_add_real(output, pointer, "offsetY",
            number_or(yyjson_arr_get(offset_y, device_index), 0.0)) &&
        yyjson_mut_obj_add_real(output, pointer, "scale",
            number_or(yyjson_arr_get(scale, device_index), 1.0)) &&
        yyjson_mut_obj_add_real(output, pointer, "handOffsetX",
            number_or(yyjson_arr_get(hand_offset, 0), 0.0)) &&
        yyjson_mut_obj_add_real(output, pointer, "handOffsetY",
            number_or(yyjson_arr_get(hand_offset, 1), 0.0)) &&
        yyjson_mut_obj_add_int(output, pointer, "lineRed",
            (int)number_or(yyjson_arr_get(line, 0), 0.0)) &&
        yyjson_mut_obj_add_int(output, pointer, "lineGreen",
            (int)number_or(yyjson_arr_get(line, 1), 0.0)) &&
        yyjson_mut_obj_add_int(output, pointer, "lineBlue",
            (int)number_or(yyjson_arr_get(line, 2), 0.0));
}

static bool add_render_profile(yyjson_mut_doc *output, yyjson_mut_val *root,
    yyjson_val *config, const BongoCatImportCandidate *candidate) {
    BongoCatLive2DRenderOptions options;
    bongo_cat_mver_render_options(config, &options);
    yyjson_mut_val *render = yyjson_mut_obj_add_obj(output, root, "render");
    return render &&
        yyjson_mut_obj_add_str(output, render, "profile", "mver-0.1.6") &&
        yyjson_mut_obj_add_bool(output, render, "autoFrame",
            options.auto_frame) &&
        yyjson_mut_obj_add_real(output, render, "projectionScale", options.projection_scale) &&
        yyjson_mut_obj_add_real(output, render, "offsetX", options.offset_x) &&
        yyjson_mut_obj_add_real(output, render, "offsetY", options.offset_y) &&
        yyjson_mut_obj_add_int(output, render, "referenceWidth", options.reference_width) &&
        yyjson_mut_obj_add_int(output, render, "referenceHeight", options.reference_height) &&
        yyjson_mut_obj_add_bool(output, render, "mirror", options.source_mirror) &&
        yyjson_mut_obj_add_bool(output, render, "pointerLeftHanded", options.pointer_left_handed) &&
        yyjson_mut_obj_add_bool(output, render, "mouseForceMove", options.mouse_force_move) &&
        yyjson_mut_obj_add_real(output, render, "mouseSpeed", options.mouse_speed) &&
        yyjson_mut_obj_add_bool(output, render, "customPointerBounds", options.custom_pointer_bounds) &&
        yyjson_mut_obj_add_int(output, render, "pointerLeft", options.pointer_left) &&
        yyjson_mut_obj_add_int(output, render, "pointerTop", options.pointer_top) &&
        yyjson_mut_obj_add_int(output, render, "pointerRight", options.pointer_right) &&
        yyjson_mut_obj_add_int(output, render, "pointerBottom", options.pointer_bottom) &&
        add_standard_pointer(output, root, config, candidate);
}

static bool add_native_render(yyjson_mut_doc *output, yyjson_mut_val *root) {
    yyjson_mut_val *render = yyjson_mut_obj_add_obj(output, root, "render");
    return render && yyjson_mut_obj_add_str(output, render, "profile", "native");
}

bool bongo_cat_import_adapter_metadata(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    bool mver = candidate->format != BONGO_CAT_IMPORT_TAURI;
    yyjson_doc *source = mver ? bongo_cat_json_read_file(candidate->config,
        YYJSON_READ_JSON5 | YYJSON_READ_ALLOW_INVALID_UNICODE, NULL) : NULL;
    yyjson_val *config = source ? yyjson_doc_get_root(source) : NULL;
    yyjson_val *mode = yyjson_obj_get(config, bongo_cat_mode_name(candidate->mode));
    BongoCatMverLabels *labels = mver ? calloc(1, sizeof(*labels)) : NULL;
    if (mver && !labels) {
        yyjson_doc_free(source);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate Mver labels");
        return false;
    }
    if (mver) bongo_cat_mver_labels_load(candidate->config,
        bongo_cat_mode_name(candidate->mode), labels);
    yyjson_mut_doc *output = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = output ? yyjson_mut_obj(output) : NULL;
    yyjson_mut_val *items = root ? yyjson_mut_obj_add_arr(output, root, "bindings") : NULL;
    if (output) yyjson_mut_doc_set_root(output, root);
    bool ok = items &&
        yyjson_mut_obj_add_int(output, root, "schemaVersion",
            BONGO_CAT_MODEL_ADAPTER_SCHEMA) &&
        yyjson_mut_obj_add_str(output, root, "kind", "bongo-cat-runtime-adapter") &&
        yyjson_mut_obj_add_strcpy(output, root, "sourceFormat",
            format_name(candidate->format));
    if (ok && mver) ok = yyjson_is_obj(mode) &&
        add_render_profile(output, root, config, candidate) &&
        bongo_cat_mver_add_behaviors(output, items, config, candidate, labels, error) &&
        bongo_cat_mver_add_audio(output, items, config, yyjson_obj_get(mode, "sounds"),
            candidate, labels, target) &&
        bongo_cat_mver_effects(output, items, config, mode, candidate, target);
    else if (ok) ok = add_native_render(output, root);
    if (ok && mver) {
        /* Adapter data describes assets/rendering only. Mver config owns keys. */
        size_t index, count; yyjson_mut_val *item;
        yyjson_mut_arr_foreach(items, index, count, item)
            yyjson_mut_obj_remove_key(item, "shortcut");
    }
    char path[BONGO_CAT_PATH_CAP];
    if (ok) ok = bongo_cat_path_join(path, sizeof(path), target,
        BONGO_CAT_MODEL_ADAPTER_FILE) &&
        bongo_cat_json_write_file(path, output, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(output);
    yyjson_doc_free(source);
    bongo_cat_mver_labels_clear(labels); free(labels);
    if (!ok && error && !error->message[0])
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot create runtime adapter metadata: %s", candidate->config);
    return ok;
}
