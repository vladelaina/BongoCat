#include "overlay_internal.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

void bongo_cat_overlay_clear_textures(BongoCatOverlay *value) {
    if (value->background) glDeleteTextures(1, &value->background);
    if (value->composite) glDeleteTextures(1, &value->composite);
    value->background = 0;
    value->composite = 0;
    value->clean_paws = false;
    value->composed_cover = false;
    value->composite_dirty = false;
    value->model_pointer_preferred = false;
    value->reference_width = value->reference_height = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (value->cache[i].texture) glDeleteTextures(1, &value->cache[i].texture);
        memset(&value->cache[i], 0, sizeof(value->cache[i]));
    }
    value->left = value->right = 0;
    value->effect = 0;
    value->left_name[0] = value->right_name[0] = '\0';
    value->left_path[0] = value->right_path[0] = '\0';
    value->effect_path[0] = '\0';
    value->background_path[0] = '\0';
    value->last_input_path[0] = '\0';
    value->input_texture_failures = 0;
}

BongoCatResult bongo_cat_overlay_load(BongoCatOverlay *value,
    const char *directory, bool model_pointer_preferred,
    const BongoCatLive2DRenderOptions *render_options, BongoCatError *error) {
    if (!value || !directory) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Missing model overlay state or directory");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    BongoCatError local = {0};
    BongoCatError *failure = error ? error : &local;
    *failure = (BongoCatError){0};
    char path[BONGO_CAT_PATH_CAP];
    /* Without the licensed Cubism runtime there is no model renderer.  Use the
       model's composed preview so the desktop pet remains visually complete;
       Cubism builds keep the background-only layer behind the animated model. */
#ifdef BONGO_CAT_HAS_CUBISM
    if (!bongo_cat_path_join(path, sizeof(path), directory,
        "resources/background.png")) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_IO, "Model overlay path is too long");
        return BONGO_CAT_ERROR_IO;
    }
#else
    if (!bongo_cat_path_join(path, sizeof(path), directory,
        "resources/cover.png")) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_IO, "Model overlay path is too long");
        return BONGO_CAT_ERROR_IO;
    }
#endif
    bongo_cat_overlay_clear_textures(value);
    bongo_cat_mver_pointer_overlay_clear(value->mver_pointer);
    GLuint background = 0;
    int background_width = 0, background_height = 0;
    BongoCatError background_error = {0};
    if (bongo_cat_path_is_file(path)) background = bongo_cat_image_texture(path,
        &background_width, &background_height, &background_error);
    BongoCatError pointer_error = {0};
    if (!model_pointer_preferred &&
        !bongo_cat_mver_pointer_overlay_load(value->mver_pointer, directory,
            &pointer_error)) {
        if (background) glDeleteTextures(1, &background);
        *failure = pointer_error;
        if (!failure->message[0] && background_error.message[0])
            *failure = background_error;
        if (!failure->message[0]) bongo_cat_error_set(failure, BONGO_CAT_ERROR_IO,
            "Unable to load model overlay");
        return BONGO_CAT_ERROR_IO;
    }
    if (!background && background_error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", background_error.message);
    value->background = background;
    /* SFML sprites use original pixels at the top-left of the configured view.
       Their image size is not necessarily the size of that view. */
#ifdef BONGO_CAT_HAS_CUBISM
    if (render_options && render_options->mver_projection) {
        value->reference_width = render_options->reference_width;
        value->reference_height = render_options->reference_height;
        /* Some Mver packages enlarge the window while retaining the original
           full-canvas sprites. Keep their shared sprite coordinates in memory.
           Do not infer a canvas from arbitrary partial images or pointer art. */
        if (background_width == 612 &&
            (background_height == 352 || background_height == 354) &&
            !bongo_cat_overlay_mver_pointer_enabled(value) &&
            value->reference_width > background_width &&
            value->reference_width % background_width == 0 &&
            value->reference_height % background_height == 0 &&
            value->reference_width / background_width ==
                value->reference_height / background_height) {
            value->reference_width = background_width;
            value->reference_height = background_height;
        }
    }
#else
    (void)render_options;
#endif
    value->model_pointer_preferred = model_pointer_preferred;
    value->composed_cover = false;
    value->clean_paws = false;
#ifndef BONGO_CAT_HAS_CUBISM
    value->composed_cover = true;
    value->clean_paws = true;
#endif
    snprintf(value->directory, sizeof(value->directory), "%s", directory);
    snprintf(value->background_path, sizeof(value->background_path), "%s", path);
    return BONGO_CAT_OK;
}

#ifdef BONGO_CAT_HAS_CUBISM
GLuint bongo_cat_overlay_cached_texture(BongoCatOverlay *value, const char *path) {
    value->clock++;
    TextureSlot *oldest = NULL;
    for (size_t i = 0; i < 4; ++i) {
        TextureSlot *slot = &value->cache[i];
        if (slot->texture && strcmp(slot->path, path) == 0) {
            slot->used = value->clock;
            return slot->texture;
        }
        if (slot->texture &&
            (slot->texture == value->left || slot->texture == value->right ||
            slot->texture == value->effect)) continue;
        if (!oldest || !slot->texture || slot->used < oldest->used) oldest = slot;
    }
    if (!oldest) return 0;
    if (oldest->texture) glDeleteTextures(1, &oldest->texture);
    BongoCatError ignored = {0};
    oldest->texture = bongo_cat_image_texture(path, NULL, NULL, &ignored);
    snprintf(oldest->path, sizeof(oldest->path), "%s", path);
    oldest->used = value->clock;
    return oldest->texture;
}
#endif

