#include "model_import_nearby_internal.h"
#include "model_storage.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

#define NEARBY_CACHE_KIND "bongocat/nearby-model-cache"

typedef struct NearbyMarker {
    char source[BONGO_CAT_PATH_CAP];
    char signature[65];
    char identity[65];
    bool adapter_ready;
    bool placeholder;
    bool valid;
} NearbyMarker;

static bool digest_valid(const char *value) {
    if (!value || strlen(value) != 64) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') ||
            (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

static NearbyMarker read_marker(const char *target) {
    NearbyMarker marker = {0};
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), target,
        BONGO_CAT_NEARBY_CACHE_MARKER)) return marker;
    yyjson_doc *document = bongo_cat_json_read_file(path, 0, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(document);
        return marker;
    }
    const char *kind = yyjson_get_str(yyjson_obj_get(root, "kind"));
    const char *source = yyjson_get_str(yyjson_obj_get(root, "source"));
    const char *signature = yyjson_get_str(yyjson_obj_get(root, "signature"));
    const char *identity = yyjson_get_str(yyjson_obj_get(root, "identity"));
    yyjson_val *adapter_ready = yyjson_obj_get(root, "adapterReady");
    yyjson_val *placeholder = yyjson_obj_get(root, "placeholder");
    marker.valid = yyjson_is_obj(root) && kind && source && signature &&
        digest_valid(signature) && digest_valid(identity) &&
        yyjson_is_bool(adapter_ready) && yyjson_is_bool(placeholder) &&
        yyjson_get_int(yyjson_obj_get(root, "schemaVersion")) ==
            BONGO_CAT_NEARBY_CACHE_SCHEMA &&
        strcmp(kind, NEARBY_CACHE_KIND) == 0;
    if (marker.valid) {
        snprintf(marker.source, sizeof(marker.source), "%s", source);
        snprintf(marker.signature, sizeof(marker.signature), "%s", signature);
        snprintf(marker.identity, sizeof(marker.identity), "%s", identity);
        marker.adapter_ready = yyjson_get_bool(adapter_ready);
        marker.placeholder = yyjson_get_bool(placeholder);
    }
    yyjson_doc_free(document);
    return marker;
}

static bool adapter_usable(const char *target, const char *source,
    const NearbyMarker *marker) {
    char mode[BONGO_CAT_PATH_CAP], metadata[BONGO_CAT_PATH_CAP];
    char resources[BONGO_CAT_PATH_CAP];
    return marker && marker->valid && marker->adapter_ready &&
        !marker->placeholder &&
        strcmp(marker->source, source) == 0 &&
        bongo_cat_path_is_dir(target) &&
        bongo_cat_path_join(mode, sizeof(mode), target, ".bongo-cat-mode") &&
        bongo_cat_path_join(metadata, sizeof(metadata), target,
            BONGO_CAT_MODEL_ADAPTER_FILE) &&
        bongo_cat_path_join(resources, sizeof(resources), target, "resources") &&
        bongo_cat_path_is_file(mode) && bongo_cat_path_is_file(metadata) &&
        bongo_cat_path_is_dir(resources);
}

bool bongo_cat_nearby_cached_inspection(const char *target,
    const char *source, const char *signature, char identity[65],
    bool *placeholder) {
    NearbyMarker marker = read_marker(target);
    if (!marker.valid || strcmp(marker.source, source) != 0 ||
        strcmp(marker.signature, signature) != 0) return false;
    snprintf(identity, 65, "%s", marker.identity);
    if (placeholder) *placeholder = marker.placeholder;
    return true;
}

static bool write_marker(const char *target, const char *source,
    const char *signature, const char *identity, BongoCatModelMode mode,
    bool placeholder, bool adapter_ready) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document ? yyjson_mut_obj(document) : NULL;
    if (document) yyjson_mut_doc_set_root(document, root);
    char path[BONGO_CAT_PATH_CAP];
    bool ok = root && yyjson_mut_obj_add_int(document, root, "schemaVersion",
            BONGO_CAT_NEARBY_CACHE_SCHEMA) &&
        yyjson_mut_obj_add_str(document, root, "kind", NEARBY_CACHE_KIND) &&
        yyjson_mut_obj_add_strcpy(document, root, "source", source) &&
        yyjson_mut_obj_add_strcpy(document, root, "signature", signature) &&
        yyjson_mut_obj_add_strcpy(document, root, "identity", identity) &&
        yyjson_mut_obj_add_bool(document, root, "placeholder", placeholder) &&
        yyjson_mut_obj_add_bool(document, root, "adapterReady",
            adapter_ready) &&
        yyjson_mut_obj_add_strcpy(document, root, "mode",
            bongo_cat_mode_name(mode)) &&
        bongo_cat_path_join(path, sizeof(path), target,
            BONGO_CAT_NEARBY_CACHE_MARKER) &&
        bongo_cat_json_write_file(path, document, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(document);
    return ok;
}

void bongo_cat_nearby_remember_inspection(const char *target,
    const char *source, const char *signature, const char *identity,
    bool placeholder, BongoCatModelMode mode) {
    if ((bongo_cat_path_is_dir(target) ||
            bongo_cat_path_create_directory(target)) &&
        write_marker(target, source, signature, identity, mode,
            placeholder, false)) return;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot cache nearby model identity: %s", source);
}

static bool use_previous_cache(const char *target, const char *source,
    char identity[65], BongoCatError *error) {
    NearbyMarker marker = read_marker(target);
    if (!adapter_usable(target, source, &marker)) return false;
    snprintf(identity, 65, "%s", marker.identity);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot refresh nearby model cache; using the previous cache: %s",
        error && error->message[0] ? error->message : source);
    if (error) *error = (BongoCatError){0};
    return true;
}

bool bongo_cat_nearby_refresh_cache(
    const BongoCatImportCandidate *candidate, const char *cache_root,
    const char *id, const char *source, const char *signature,
    char identity[65], bool placeholder,
    char adapter[BONGO_CAT_PATH_CAP], bool *created, BongoCatError *error) {
    char temporary[BONGO_CAT_PATH_CAP], backup[BONGO_CAT_PATH_CAP];
    char name[BONGO_CAT_ID_CAP + 8];
    snprintf(name, sizeof(name), ".%s.tmp", id);
    if (!bongo_cat_path_join(adapter, BONGO_CAT_PATH_CAP, cache_root, id) ||
        !bongo_cat_path_join(temporary, sizeof(temporary), cache_root, name))
        return false;
    snprintf(name, sizeof(name), ".%s.old", id);
    if (!bongo_cat_path_join(backup, sizeof(backup), cache_root, name))
        return false;
    bongo_cat_model_remove_tree(temporary, NULL);
    if (!bongo_cat_path_is_dir(adapter) && bongo_cat_path_is_dir(backup))
        bongo_cat_path_rename(backup, adapter);
    if (bongo_cat_path_is_dir(adapter))
        bongo_cat_model_remove_tree(backup, NULL);
    *created = !bongo_cat_path_is_dir(adapter);
    NearbyMarker marker = read_marker(adapter);
    if (adapter_usable(adapter, source, &marker) &&
        strcmp(marker.signature, signature) == 0 &&
        strcmp(marker.identity, identity) == 0) return true;
    if (!bongo_cat_path_create_directory(temporary) ||
        !bongo_cat_import_prepare_adapter(candidate, temporary, error) ||
        !write_marker(temporary, source, signature, identity,
            candidate->mode, placeholder, true)) {
        bongo_cat_model_remove_tree(temporary, NULL);
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot build nearby model adapter: %s", source);
        return use_previous_cache(adapter, source, identity, error);
    }
    bool had_adapter = bongo_cat_path_is_dir(adapter);
    if (had_adapter && !bongo_cat_path_rename(adapter, backup)) {
        bongo_cat_model_remove_tree(temporary, NULL);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot update nearby model adapter: %s", SDL_GetError());
        return use_previous_cache(adapter, source, identity, error);
    }
    if (!bongo_cat_path_rename(temporary, adapter)) {
        if (had_adapter) bongo_cat_path_rename(backup, adapter);
        bongo_cat_model_remove_tree(temporary, NULL);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot activate nearby model adapter: %s", SDL_GetError());
        return use_previous_cache(adapter, source, identity, error);
    }
    bongo_cat_model_remove_tree(backup, NULL);
    return true;
}
