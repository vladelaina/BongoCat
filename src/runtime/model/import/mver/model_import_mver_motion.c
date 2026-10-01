#include "model_import_mver_internal.h"
#include "model_import_mver_manifest.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <yyjson.h>

static bool add_rows(yyjson_mut_doc *output, yyjson_mut_val *items,
    yyjson_val *rows, const BongoCatImportCandidate *candidate,
    const char *kind, const char *field, const char *group, yyjson_val *available,
    const BongoCatMverLabels *labels,
    BongoCatError *error) {
    if (!rows || yyjson_is_null(rows)) return true;
    if (!yyjson_is_arr(rows)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Mver %s bindings exceed the matching Live2D manifest entries", kind);
        return false;
    }
    size_t limit = yyjson_arr_size(rows);
    if (limit > yyjson_arr_size(available)) limit = yyjson_arr_size(available);
    size_t index, count; yyjson_val *row;
    yyjson_arr_foreach(rows, index, count, row) {
        if (index >= limit) break;
        if (yyjson_is_null(row) || (yyjson_is_arr(row) && !yyjson_arr_size(row)))
            continue;
        const char *file = yyjson_get_str(yyjson_obj_get(
            yyjson_arr_get(available, index), "File"));
        char path[BONGO_CAT_PATH_CAP];
        if (!file || !bongo_cat_path_join(path, sizeof(path), candidate->directory,
                file) || !bongo_cat_path_is_file(path)) continue;
        char shortcut[BONGO_CAT_SHORTCUT_CAP];
        if (!bongo_cat_mver_chord(candidate, row, shortcut, sizeof(shortcut))) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                "Mver %s binding %zu is not a supported input chord", kind, index);
            return false;
        }
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(output, items);
        const char *label = bongo_cat_mver_label(labels, field, index);
        if (!item || !yyjson_mut_obj_add_strcpy(output, item, "kind", kind) ||
            !yyjson_mut_obj_add_int(output, item, "index", (int)index) ||
            !yyjson_mut_obj_add_strcpy(output, item, "shortcut", shortcut) ||
            (label && !yyjson_mut_obj_add_strcpy(output, item, "label", label)) ||
            (group && !yyjson_mut_obj_add_strcpy(output, item, "group", group))) return false;
    }
    return true;
}

static yyjson_doc *manifest(const BongoCatImportCandidate *candidate,
    yyjson_val **references) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), candidate->directory,
        candidate->setting)) return NULL;
    yyjson_doc *document = bongo_cat_import_mver_manifest_read(path, NULL);
    *references = document ? yyjson_obj_get(yyjson_doc_get_root(document),
        "FileReferences") : NULL;
    return document;
}

static bool add_motion_group(yyjson_mut_doc *output, yyjson_mut_val *items,
    yyjson_val *configured, yyjson_val *motions, const char *field,
    const char *group, const BongoCatImportCandidate *candidate,
    const BongoCatMverLabels *labels, BongoCatError *error) {
    yyjson_val *available = yyjson_obj_get(motions, group);
    if (yyjson_is_arr(configured) && !yyjson_is_arr(available)) return true;
    return add_rows(output, items, configured, candidate, "motion", field,
        group, available, labels, error);
}

bool bongo_cat_mver_add_behaviors(void *raw_output, void *raw_items,
    void *raw_mode, const BongoCatImportCandidate *candidate,
    const BongoCatMverLabels *labels, BongoCatError *error) {
    yyjson_mut_doc *output = raw_output;
    yyjson_mut_val *items = raw_items;
    yyjson_val *mode = raw_mode, *references = NULL;
    yyjson_doc *document = manifest(candidate, &references);
    yyjson_val *expressions = yyjson_obj_get(references, "Expressions");
    yyjson_val *motions = yyjson_obj_get(references, "Motions");
    bool ok = document && add_rows(output, items,
        yyjson_obj_get(mode, "l2d_expression"), candidate, "expression",
        "l2d_expression", NULL,
        expressions, labels, error) &&
        add_motion_group(output, items, yyjson_obj_get(mode, "l2d_motion"),
            motions, "l2d_motion", "CAT_motion", candidate, labels, error) &&
        add_motion_group(output, items,
            yyjson_obj_get(mode, "l2d_motion_lockhand"), motions,
            "l2d_motion_lockhand", "CAT_motion_lock", candidate, labels, error);
    yyjson_doc_free(document);
    if (!ok && error && !error->message[0])
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Cannot map Mver behaviors to the Live2D manifest");
    return ok;
}
