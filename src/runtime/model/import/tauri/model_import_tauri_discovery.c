#include "model_import_tauri_internal.h"
#include "../model_import_manifest.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static bool mver_input_assets(const char *directory) {
    char path[BONGO_CAT_PATH_CAP], group[BONGO_CAT_PATH_CAP];
    static const char *const groups[] = {"hand", "lefthand", "righthand"};
    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i)
        if (bongo_cat_path_join(group, sizeof(group), directory, groups[i]) &&
            bongo_cat_path_join(path, sizeof(path), group, "0.png") &&
            bongo_cat_path_is_file(path)) return true;
    return false;
}

static bool mver_mode_directory(const char *directory) {
    const char *name = bongo_cat_path_name(directory);
    if (!name || (SDL_strcasecmp(name, "standard") != 0 &&
            SDL_strcasecmp(name, "keyboard") != 0 &&
            SDL_strcasecmp(name, "gamepad") != 0)) return false;
    char image_root[BONGO_CAT_PATH_CAP];
    return bongo_cat_import_parent_path(directory, image_root,
            sizeof(image_root)) &&
        SDL_strcasecmp(bongo_cat_path_name(image_root), "img") == 0 &&
        mver_input_assets(directory);
}

static bool inside_mver_model(const char *directory) {
    char current[BONGO_CAT_PATH_CAP];
    int written = snprintf(current, sizeof(current), "%s",
        directory ? directory : "");
    if (written < 0 || (size_t)written >= sizeof(current)) return false;
    for (int depth = 0; depth < 4; ++depth) {
        if (mver_mode_directory(current)) return true;
        char parent[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_import_parent_path(current, parent, sizeof(parent)))
            break;
        snprintf(current, sizeof(current), "%s", parent);
    }
    return false;
}

static BongoCatPathVisit discover_model(void *userdata,
    const char *dirname, const char *name) {
    BongoCatImportDiscovery *discovery = userdata;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_file(path) ||
        !bongo_cat_import_has_suffix(name, ".model3.json"))
        return BONGO_CAT_PATH_CONTINUE;
    if (inside_mver_model(dirname)) return BONGO_CAT_PATH_CONTINUE;
    if (bongo_cat_import_manifest_valid(dirname, name, NULL) &&
        !bongo_cat_import_tauri_add_candidate(discovery, dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    return BONGO_CAT_PATH_CONTINUE;
}

static BongoCatPathVisit discover_recursive_item(void *userdata,
    const char *dirname, const char *name) {
    BongoCatImportDiscovery *discovery = userdata;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_file(path))
        return discover_model(userdata, dirname, name);
    if (!bongo_cat_path_is_dir(path) || discovery->depth >= 8 || name[0] == '.')
        return BONGO_CAT_PATH_CONTINUE;
    discovery->depth++;
    bool ok = bongo_cat_path_enumerate(path, discover_recursive_item,
        discovery);
    discovery->depth--;
    return ok ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

int bongo_cat_import_tauri_discover_exact(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    if (!source || !discovery || !bongo_cat_path_is_dir(source)) return 0;
    if (!discovery->source_name[0]) snprintf(discovery->source_name,
        sizeof(discovery->source_name), "%s", bongo_cat_path_name(source));
    size_t before = discovery->count;
    if (!bongo_cat_path_enumerate(source, discover_model, discovery)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            discovery->ambiguous
                ? "A model directory contains multiple model3 manifests"
                : "Cannot inspect nearby model directory");
        return -1;
    }
    for (size_t i = before; i < discovery->count; ++i)
        snprintf(discovery->candidates[i].package_root,
            sizeof(discovery->candidates[i].package_root), "%s", source);
    return discovery->count > before ? 1 : 0;
}

int bongo_cat_import_tauri_discover_recursive(const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    if (!source || !discovery || !bongo_cat_path_is_dir(source)) return 0;
    if (!discovery->source_name[0]) snprintf(discovery->source_name,
        sizeof(discovery->source_name), "%s", bongo_cat_path_name(source));
    size_t before = discovery->count;
    if (!bongo_cat_path_enumerate(source, discover_recursive_item, discovery)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            discovery->ambiguous
                ? "A model directory contains multiple model3 manifests"
                : "Cannot scan model directory or it contains too many models");
        return -1;
    }
    return discovery->count > before ? 1 : 0;
}
