#include "model_import.h"
#include "runtime.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool metadata_version(yyjson_val *root) {
    if (!yyjson_is_obj(root)) return false;
    yyjson_val *schema = yyjson_obj_get(root, "schemaVersion");
    const char *kind = yyjson_get_str(yyjson_obj_get(root, "kind"));
    return yyjson_is_int(schema) &&
        yyjson_get_int(schema) == BONGO_CAT_MODEL_ADAPTER_SCHEMA && kind &&
        strcmp(kind, "bongo-cat-runtime-adapter") == 0;
}

static bool same_family(const char *left, const char *right) {
    bool left_effect = left && (strcmp(left, "effect") == 0 ||
        strcmp(left, "effect-clear") == 0);
    bool right_effect = right && (strcmp(right, "effect") == 0 ||
        strcmp(right, "effect-clear") == 0);
    bool left_sound = left && (strcmp(left, "sound") == 0 ||
        strcmp(left, "sound-clear") == 0);
    bool right_sound = right && (strcmp(right, "sound") == 0 ||
        strcmp(right, "sound-clear") == 0);
    return (left_effect && right_effect) || (left_sound && right_sound);
}

static int sequential_index(yyjson_val *bindings, size_t index, const char *kind) {
    int result = 0;
    for (size_t before = 0; before < index; ++before) {
        yyjson_val *previous = yyjson_arr_get(bindings, before);
        const char *previous_kind = yyjson_get_str(yyjson_obj_get(previous, "kind"));
        if (same_family(previous_kind, kind)) result++;
    }
    return result;
}

static BongoCatBehaviorShortcut *shortcut_for(BongoCatApp *app, const char *id) {
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i)
        if (strcmp(app->settings.behavior_shortcuts[i].id, id) == 0)
            return &app->settings.behavior_shortcuts[i];
    return NULL;
}

static bool behavior_id(char *id, size_t capacity, const char *model_id,
    yyjson_val *bindings, size_t index, yyjson_val *item) {
    const char *kind = yyjson_get_str(yyjson_obj_get(item, "kind"));
    const char *group = yyjson_get_str(yyjson_obj_get(item, "group"));
    if (!kind) return false;
    if (strcmp(kind, "sound") == 0 || strcmp(kind, "sound-clear") == 0)
        snprintf(id, capacity, "%s:sound:%d", model_id,
            sequential_index(bindings, index, kind));
    else if (strcmp(kind, "effect") == 0 || strcmp(kind, "effect-clear") == 0)
        snprintf(id, capacity, "%s:effect:%d", model_id,
            sequential_index(bindings, index, kind));
    else if (strcmp(kind, "expression") == 0)
        snprintf(id, capacity, "%s:expression:%d", model_id,
            (int)yyjson_get_int(yyjson_obj_get(item, "index")));
    else if (strcmp(kind, "motion") == 0)
        snprintf(id, capacity, "%s:motion:%s:%d", model_id, group ? group : "",
            (int)yyjson_get_int(yyjson_obj_get(item, "index")));
    else return false;
    return true;
}

void bongo_cat_import_apply_metadata(BongoCatApp *app, const char *model_id,
    const char *directory) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_model_adapter_metadata_path(directory, path,
        sizeof(path))) return;
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *bindings = yyjson_obj_get(root, "bindings");
    if (!metadata_version(root) || !yyjson_is_arr(bindings)) {
        yyjson_doc_free(document);
        return;
    }
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(bindings, index, count, item) {
        char id[BONGO_CAT_PATH_CAP];
        const char *shortcut = yyjson_get_str(yyjson_obj_get(item, "shortcut"));
        const char *label = yyjson_get_str(yyjson_obj_get(item, "label"));
        if (!shortcut || !behavior_id(id, sizeof(id), model_id, bindings, index, item))
            continue;
        BongoCatBehaviorShortcut *existing = shortcut_for(app, id);
        if (existing) {
            if (label && !existing->label[0])
                snprintf(existing->label, sizeof(existing->label), "%s", label);
            continue;
        }
        if (app->settings.behavior_shortcut_count >= BONGO_CAT_BEHAVIOR_BINDING_CAP) break;
        BongoCatBehaviorShortcut *value =
            &app->settings.behavior_shortcuts[app->settings.behavior_shortcut_count++];
        snprintf(value->id, sizeof(value->id), "%s", id);
        snprintf(value->shortcut, sizeof(value->shortcut), "%s", shortcut);
        if (label) snprintf(value->label, sizeof(value->label), "%s", label);
    }
    yyjson_doc_free(document);
}

bool bongo_cat_import_render_options(const char *directory,
    BongoCatLive2DRenderOptions *options) {
    if (!options) return false;
    *options = (BongoCatLive2DRenderOptions){0};
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_model_adapter_metadata_path(directory, path,
        sizeof(path))) return false;
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *render = yyjson_obj_get(root, "render");
    const char *profile = yyjson_get_str(yyjson_obj_get(render, "profile"));
    yyjson_val *scale = yyjson_obj_get(render, "projectionScale");
    int width = (int)yyjson_get_sint(yyjson_obj_get(render, "referenceWidth"));
    int height = (int)yyjson_get_sint(yyjson_obj_get(render, "referenceHeight"));
    bool valid = metadata_version(root) &&
        yyjson_is_obj(render) && profile && strcmp(profile, "mver-0.1.6") == 0 &&
        yyjson_is_num(scale) && yyjson_get_num(scale) > 0.0 &&
        yyjson_get_num(scale) <= 100.0 && width > 0 && height > 0;
    if (valid) {
        options->mver_projection = true;
        options->auto_frame = yyjson_get_bool(yyjson_obj_get(render, "autoFrame"));
        options->projection_scale = (float)yyjson_get_num(scale);
        options->offset_x = (float)yyjson_get_num(yyjson_obj_get(render, "offsetX"));
        options->offset_y = (float)yyjson_get_num(yyjson_obj_get(render, "offsetY"));
        options->reference_width = width;
        options->reference_height = height;
        yyjson_val *mirror = yyjson_obj_get(render, "mirror");
        options->source_mirror = yyjson_is_bool(mirror) && yyjson_get_bool(mirror);
        yyjson_val *left_handed = yyjson_obj_get(render, "pointerLeftHanded");
        options->pointer_left_handed = yyjson_is_bool(left_handed) &&
            yyjson_get_bool(left_handed);
        yyjson_val *force = yyjson_obj_get(render, "mouseForceMove");
        options->mouse_force_move = yyjson_is_bool(force) && yyjson_get_bool(force);
        yyjson_val *mouse_speed = yyjson_obj_get(render, "mouseSpeed");
        options->mouse_speed = yyjson_is_num(mouse_speed)
            ? (float)yyjson_get_num(mouse_speed) : 1.0f;
        yyjson_val *custom = yyjson_obj_get(render, "customPointerBounds");
        options->custom_pointer_bounds = yyjson_is_bool(custom) &&
            yyjson_get_bool(custom);
        options->pointer_left = (int)yyjson_get_sint(
            yyjson_obj_get(render, "pointerLeft"));
        options->pointer_top = (int)yyjson_get_sint(
            yyjson_obj_get(render, "pointerTop"));
        options->pointer_right = (int)yyjson_get_sint(
            yyjson_obj_get(render, "pointerRight"));
        options->pointer_bottom = (int)yyjson_get_sint(
            yyjson_obj_get(render, "pointerBottom"));
        if (options->custom_pointer_bounds &&
            (options->pointer_right <= options->pointer_left ||
             options->pointer_bottom <= options->pointer_top))
            options->custom_pointer_bounds = false;
    }
    yyjson_doc_free(document);
    return valid;
}
