#include "preferences_model_cover.h"
#include "preferences_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <string.h>

typedef struct ModelCoverSlot {
    BongoCatModelCover image;
    BongoCatApp *app;
    char path[BONGO_CAT_PATH_CAP];
    int requested_width;
    int requested_height;
    uint64_t generation;
} ModelCoverSlot;

static ModelCoverSlot cover_cache[BONGO_CAT_MODEL_CAP];
static uint64_t cover_generation;
static unsigned cover_load_budget;
static bool cover_load_deferred;

size_t bongo_cat_preferences_model_cover_usage(BongoCatApp *app, size_t *count) {
    size_t bytes = 0, textures = 0;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        const ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || !slot->image.texture) continue;
        ++textures;
        bytes += (size_t)slot->image.width * slot->image.height * 4;
    }
    if (count) *count = textures;
    return bytes;
}

void bongo_cat_preferences_model_cover_cache_clear(BongoCatApp *app) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if ((!app || slot->app == app) && slot->image.texture)
            glDeleteTextures(1, &slot->image.texture);
        if (!app || slot->app == app) memset(slot, 0, sizeof(*slot));
    }
}

bool bongo_cat_preferences_model_cover_reload(BongoCatApp *app,
    const char *path) {
    if (!app || !path || !path[0]) return false;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (!slot->image.texture || slot->app != app ||
            strcmp(slot->path, path) != 0) continue;
        int width = 0, height = 0;
        BongoCatError ignored = {0};
        unsigned int texture = bongo_cat_image_texture_thumbnail(path,
            slot->requested_width, slot->requested_height, &width, &height,
            &ignored);
        if (!texture) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Model cover cache reload failed: %s", path);
            return false;
        }
        unsigned int previous = slot->image.texture;
        slot->image.texture = texture;
        slot->image.width = width;
        slot->image.height = height;
        slot->generation = cover_generation;
        glDeleteTextures(1, &previous);
        if (app->preferences)
            bongo_cat_preferences_invalidate(app->preferences);
        SDL_Log("Model cover cache reloaded: %s (%dx%d)", path,
            width, height);
        return true;
    }
    if (app->preferences)
        bongo_cat_preferences_invalidate(app->preferences);
    return true;
}

void bongo_cat_preferences_model_cache_clear(BongoCatApp *app) {
    bongo_cat_preferences_remove_dialog_clear(app);
    bongo_cat_preferences_model_cover_cache_clear(app);
}

void bongo_cat_preferences_model_cache_abandon(BongoCatApp *app) {
    bongo_cat_preferences_remove_dialog_clear(app);
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i)
        if (!app || cover_cache[i].app == app)
            memset(&cover_cache[i], 0, sizeof(cover_cache[i]));
}

void bongo_cat_preferences_model_covers_begin(BongoCatApp *app) {
    cover_generation++;
    if (!cover_generation) cover_generation++;
    /* Keep existing thumbnails during a model transaction. Decoding another
       cover in a progress frame competes with the atlas currently loading. */
    cover_load_budget = app && app->loading_model[0] ? 0 : 1;
    cover_load_deferred = false;
}

const BongoCatModelCover *bongo_cat_preferences_model_cover(
    BongoCatApp *app, const BongoCatModelEntry *entry,
    int pixel_width, int pixel_height) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), entry->adapter_directory,
        "resources/cover.png")) return NULL;
    if (!bongo_cat_path_is_file(path)) {
        static char last_missing[BONGO_CAT_PATH_CAP];
        if (strcmp(last_missing, path)) {
            snprintf(last_missing, sizeof(last_missing), "%s", path);
            SDL_Log("Model cover unavailable: id=%s path=%s",
                entry->id, path);
        }
        return NULL;
    }
    ModelCoverSlot *empty = NULL;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->image.texture && slot->app == app && !strcmp(slot->path, path)) {
            if (slot->requested_width == pixel_width &&
                slot->requested_height == pixel_height) {
                slot->generation = cover_generation; return &slot->image;
            }
            glDeleteTextures(1, &slot->image.texture);
            memset(slot, 0, sizeof(*slot));
            empty = slot;
            break;
        }
        if (!slot->image.texture && !empty) empty = slot;
    }
    if (!empty) return NULL;
    if (!cover_load_budget) {
        cover_load_deferred = true;
        return NULL;
    }
    cover_load_budget--;
    BongoCatError ignored = {0};
    empty->image.texture = bongo_cat_image_texture_thumbnail(path,
        pixel_width, pixel_height, &empty->image.width,
        &empty->image.height, &ignored);
    if (!empty->image.texture) return NULL;
    empty->app = app; empty->generation = cover_generation;
    empty->requested_width = pixel_width;
    empty->requested_height = pixel_height;
    snprintf(empty->path, sizeof(empty->path), "%s", path);
    return &empty->image;
}

void bongo_cat_preferences_model_covers_prune(BongoCatApp *app) {
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        ModelCoverSlot *slot = &cover_cache[i];
        if (slot->app != app || !slot->image.texture ||
            slot->generation == cover_generation) continue;
        glDeleteTextures(1, &slot->image.texture);
        memset(slot, 0, sizeof(*slot));
    }
    if (cover_load_deferred && app)
        bongo_cat_preferences_invalidate(app->preferences);
}
