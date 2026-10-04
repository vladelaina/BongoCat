#include "model_import_tauri_internal.h"

#include "bongo_cat/path.h"


bool bongo_cat_tauri_find_resource_file(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity) {
    if (!candidate || !name || !path || !capacity) return false;
    const char *roots[] = {candidate->assets, candidate->directory,
        candidate->package_root};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i][0]) continue;
        char resources[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(resources, sizeof(resources), roots[i],
                "resources") && bongo_cat_path_join(path, capacity,
                resources, name) && bongo_cat_path_is_file(path)) return true;
        if (bongo_cat_path_join(path, capacity, roots[i], name) &&
            bongo_cat_path_is_file(path)) return true;
    }
    return false;
}

bool bongo_cat_tauri_find_resource_directory(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity) {
    if (!candidate || !name || !path || !capacity) return false;
    const char *roots[] = {candidate->assets, candidate->directory,
        candidate->package_root};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (!roots[i][0]) continue;
        char resources[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(resources, sizeof(resources), roots[i],
                "resources") && bongo_cat_path_join(path, capacity,
                resources, name) && bongo_cat_path_is_dir(path)) return true;
        if (bongo_cat_path_join(path, capacity, roots[i], name) &&
            bongo_cat_path_is_dir(path)) return true;
    }
    return false;
}

bool bongo_cat_tauri_find_package_file(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity) {
    if (!candidate || !name || !path || !capacity) return false;
    const char *roots[] = {candidate->directory, candidate->assets,
        candidate->package_root};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
        if (roots[i][0] && bongo_cat_path_join(path, capacity, roots[i], name) &&
            bongo_cat_path_is_file(path)) return true;
    return false;
}
