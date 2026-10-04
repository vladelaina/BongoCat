#include "model_import_manifest.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static bool safe_reference(const char *value) {
    if (!value || !value[0] || value[0] == '/' || value[0] == '\\' ||
        strchr(value, ':')) return false;
    const char *part = value;
    while (*part) {
        while (*part == '/' || *part == '\\') part++;
        if (part[0] == '.' && part[1] == '.' &&
            (!part[2] || part[2] == '/' || part[2] == '\\')) return false;
        part = strpbrk(part, "/\\");
        if (!part) break;
    }
    return true;
}
static bool referenced_file(const char *root, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    return safe_reference(relative) &&
        bongo_cat_path_join(path, sizeof(path), root, relative) &&
        bongo_cat_path_is_file(path);
}

static bool referenced_texture(const char *root, const char *relative) {
    char path[BONGO_CAT_PATH_CAP];
    if (!safe_reference(relative) ||
        !bongo_cat_path_join(path, sizeof(path), root, relative)) return false;
    return bongo_cat_image_info(path, NULL, NULL);
}

static bool optional_reference(const char *root, yyjson_val *refs,
    const char *name, bool allow_missing) {
    yyjson_val *value = yyjson_obj_get(refs, name);
    if (!value) return true;
    const char *relative = yyjson_get_str(value);
    return relative && (allow_missing ? (!relative[0] || safe_reference(relative))
        : referenced_file(root, relative));
}

static bool behavior_references(const char *root, yyjson_val *refs,
    bool allow_missing) {
    yyjson_val *expressions = yyjson_obj_get(refs, "Expressions");
    if (expressions && !yyjson_is_arr(expressions)) return false;
    size_t index, count; yyjson_val *item;
    yyjson_arr_foreach(expressions, index, count, item) {
        const char *file = yyjson_get_str(yyjson_obj_get(item, "File"));
        if (!(allow_missing ? safe_reference(file) : referenced_file(root, file)))
            return false;
    }
    yyjson_val *motions = yyjson_obj_get(refs, "Motions");
    if (motions && !yyjson_is_obj(motions)) return false;
    size_t group_index, group_count; yyjson_val *key, *group;
    yyjson_obj_foreach(motions, group_index, group_count, key, group) {
        if (!yyjson_is_arr(group)) return false;
        yyjson_arr_foreach(group, index, count, item) {
            const char *file = yyjson_get_str(yyjson_obj_get(item, "File"));
            if (!(allow_missing ? safe_reference(file) : referenced_file(root, file)))
                return false;
            const char *sound = yyjson_get_str(yyjson_obj_get(item, "Sound"));
            if (sound && !(allow_missing && !sound[0]) && !safe_reference(sound))
                return false;
        }
    }
    return true;
}

bool bongo_cat_import_manifest_document_valid(const char *root,
    yyjson_doc *document, bool allow_missing_optional) {
    yyjson_val *manifest = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *refs = yyjson_is_obj(manifest)
        ? yyjson_obj_get(manifest, "FileReferences") : NULL;
    const char *moc = yyjson_get_str(yyjson_obj_get(refs, "Moc"));
    yyjson_val *textures = yyjson_obj_get(refs, "Textures");
    bool valid = yyjson_get_int(yyjson_obj_get(manifest, "Version")) == 3 &&
        yyjson_is_obj(refs) && referenced_file(root, moc) && yyjson_is_arr(textures) &&
        yyjson_arr_size(textures) > 0;
    size_t index, maximum; yyjson_val *texture;
    yyjson_arr_foreach(textures, index, maximum, texture)
        valid = valid && referenced_texture(root, yyjson_get_str(texture));
    valid = valid && optional_reference(root, refs, "Physics", allow_missing_optional) &&
        optional_reference(root, refs, "Pose", allow_missing_optional) &&
        optional_reference(root, refs, "DisplayInfo", allow_missing_optional) &&
        (!allow_missing_optional || optional_reference(root, refs, "UserData", true)) &&
        behavior_references(root, refs, allow_missing_optional);
    return valid;
}

bool bongo_cat_import_manifest_valid(const char *root, const char *setting,
    BongoCatError *error) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), root, setting)) return false;
    FILE *file = bongo_cat_file_open(path, "rb");
    yyjson_doc *document = file ? yyjson_read_fp(file, 0, NULL, NULL) : NULL;
    if (file) fclose(file);
    bool valid = bongo_cat_import_manifest_document_valid(root, document, false);
    yyjson_doc_free(document);
    if (!valid && error) bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
        "Model manifest or referenced assets are invalid: %s", path);
    return valid;
}
