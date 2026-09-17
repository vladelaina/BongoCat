#include "model_import_mver_copy.h"
#include "model_import_path.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct MverDirectoryCopy {
    const char *target_root;
    BongoCatError *error;
    unsigned depth;
} MverDirectoryCopy;

static bool rebase(const char *source_root, const char *installed_root,
    const char *source, char *target, size_t capacity) {
    char relative[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_import_relative_path(source_root, source, relative,
            sizeof(relative)))
        return false;
    if (!relative[0]) {
        int written = snprintf(target, capacity, "%s", installed_root);
        return written >= 0 && (size_t)written < capacity;
    }
    return bongo_cat_path_join(target, capacity, installed_root, relative);
}

static bool copy_relative_file(const char *source_root, const char *source,
    const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_import_relative_path(source_root, source, relative,
            sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    if (bongo_cat_path_is_file(target)) return true;
    if (bongo_cat_path_copy_file(source, target)) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot copy imported model file: %s", source);
    return false;
}

static bool distribution_file(const char *name) {
    if (!name || name[0] == '.') return true;
    size_t length = strlen(name);
    if (length >= 4 && SDL_strcasecmp(name + length - 4, "_bak") == 0)
        return true;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    static const char *const extensions[] = {
        ".exe", ".dll", ".pdf", ".bak", ".lock", ".pdb", ".zip"
    };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (SDL_strcasecmp(dot, extensions[i]) == 0) return true;
    return false;
}

static bool runtime_resources(const char *directory, const char *name) {
    if (!name || SDL_strcasecmp(name, "Resources") != 0 ||
        !bongo_cat_path_is_dir(directory)) return false;
    char font[BONGO_CAT_PATH_CAP], logo[BONGO_CAT_PATH_CAP];
    return bongo_cat_path_join(font, sizeof(font), directory, "cat.ttf") &&
        bongo_cat_path_join(logo, sizeof(logo), directory, "l2dlogo.png") &&
        bongo_cat_path_is_file(font) && bongo_cat_path_is_file(logo);
}

static bool copy_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);

static BongoCatPathVisit copy_child(void *userdata, const char *dirname,
    const char *name) {
    MverDirectoryCopy *context = userdata;
    char source[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(source, sizeof(source), dirname, name))
        return BONGO_CAT_PATH_FAILURE;
    if (runtime_resources(source, name) || distribution_file(name))
        return BONGO_CAT_PATH_CONTINUE;
    char target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(target, sizeof(target), context->target_root,
            name)) return BONGO_CAT_PATH_FAILURE;
    if (!bongo_cat_path_is_dir(source)) {
        if (context->depth == 0) return BONGO_CAT_PATH_CONTINUE;
        return bongo_cat_path_copy_file(source, target)
            ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    }
    return copy_tree(source, target, context->depth + 1, context->error)
        ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

static bool copy_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error) {
    if (depth > 32 || !bongo_cat_path_create_directory(target)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Cannot create imported model directory: %s", target);
        return false;
    }
    MverDirectoryCopy context = {target, error, depth};
    return bongo_cat_path_enumerate(source, copy_child, &context);
}

static bool copy_relative_directory(const char *source_root,
    const char *source, const char *target_root, BongoCatError *error) {
    char relative[BONGO_CAT_PATH_CAP], target[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_import_relative_path(source_root, source, relative,
            sizeof(relative)) ||
        !relative[0] || !bongo_cat_path_join(target, sizeof(target),
            target_root, relative)) return false;
    return copy_tree(source, target, 1, error);
}

static bool copy_package(const BongoCatImportCandidate *candidate,
    const char *target, BongoCatError *error) {
    if (!bongo_cat_path_create_directory(target) ||
        !copy_tree(candidate->package_root, target, 0, error)) return false;
    return copy_relative_file(candidate->package_root, candidate->config,
        target, error);
}

bool bongo_cat_import_mver_copy_package(
    const BongoCatImportCandidate *candidate, const char *target,
    BongoCatImportCandidate *installed, BongoCatError *error) {
    if (!candidate || !target || !installed) return false;
    char base[BONGO_CAT_PATH_CAP];
    bool patch = candidate->format == BONGO_CAT_IMPORT_MVER_PATCH;
    if (patch) {
        char patch_relative[BONGO_CAT_PATH_CAP];
        bool patch_inside_package = bongo_cat_import_relative_path(
            candidate->package_root, candidate->patch_root, patch_relative,
            sizeof(patch_relative));
        const char *patch_source_root = patch_inside_package
            ? candidate->package_root : candidate->patch_root;
        snprintf(base, sizeof(base), "%s", target);
        if (!copy_package(candidate, base, error) ||
            (!patch_inside_package && candidate->overrides[0] &&
                !copy_relative_directory(patch_source_root,
                    candidate->overrides, target, error)) ||
            (candidate->overrides[0] && !rebase(patch_source_root, target,
                candidate->overrides, installed->overrides,
                sizeof(installed->overrides))) ||
            !rebase(patch_source_root, target, candidate->patch_root,
                installed->patch_root, sizeof(installed->patch_root)))
            return false;
    } else if (candidate->format == BONGO_CAT_IMPORT_MVER) {
        snprintf(base, sizeof(base), "%s", target);
        if (!copy_package(candidate, base, error)) return false;
    } else return false;
    if (!rebase(candidate->package_root, base, candidate->directory,
            installed->directory, sizeof(installed->directory)) ||
        !rebase(candidate->package_root, base, candidate->assets,
            installed->assets, sizeof(installed->assets)) ||
        !rebase(candidate->package_root, base, candidate->config,
            installed->config, sizeof(installed->config))) return false;
    installed->format = candidate->format;
    return true;
}
