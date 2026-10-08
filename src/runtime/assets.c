#include "runtime.h"
#include "bongo_cat/path.h"
#include "bongo_cat/platform.h"

#include <stdio.h>

static bool required_assets(const char *root) {
    if (!root || !root[0]) return false;
    const char *files[] = {"locales/en-US.json", "models/standard/cat.model3.json",
        "models/standard/demomodel.moc3",
        "models/standard/demomodel.1024/texture_00.png"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_path_join(path, sizeof(path), root, files[i]) ||
            !bongo_cat_path_is_file(path)) return false;
    }
    return true;
}

static bool asset_directory(char *output, size_t capacity, const char *root) {
    return bongo_cat_path_join(output, capacity, root, "assets") &&
        bongo_cat_path_is_dir(output) && required_assets(output);
}

BongoCatResult bongo_cat_app_locate_assets(BongoCatApp *app,
    BongoCatError *error) {
    app->asset_root[0] = '\0'; app->locale_root[0] = '\0';
    const char *base = SDL_GetBasePath();
    if (!asset_directory(app->asset_root, sizeof(app->asset_root), base ? base : "")) {
        char cache[BONGO_CAT_PATH_CAP], name[64];
        snprintf(name, sizeof(name), "embedded-assets-%s", BONGO_CAT_VERSION);
        if (bongo_cat_path_join(cache, sizeof(cache), app->data_root, name)) {
            BongoCatError embedded = {0};
            if (bongo_cat_platform_embedded_assets(cache, &embedded) == BONGO_CAT_OK) {
                asset_directory(app->asset_root, sizeof(app->asset_root), cache);
            } else if (embedded.message[0]) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", embedded.message);
            }
        }
    }
#ifndef NDEBUG
    if (!required_assets(app->asset_root) && required_assets(BONGO_CAT_DEV_ASSET_ROOT))
        snprintf(app->asset_root, sizeof(app->asset_root), "%s", BONGO_CAT_DEV_ASSET_ROOT);
#endif
    if (!required_assets(app->asset_root)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Required application assets are missing or incomplete");
        return BONGO_CAT_ERROR_IO;
    }
    if (!bongo_cat_path_join(app->locale_root, sizeof(app->locale_root),
        app->asset_root, "locales") || !bongo_cat_path_is_dir(app->locale_root)) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Application locale assets are missing");
        return BONGO_CAT_ERROR_IO;
    }
    bongo_cat_window_set_icon(app, app->window);
    return BONGO_CAT_OK;
}
