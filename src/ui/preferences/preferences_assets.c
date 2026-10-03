#include "preferences_state.h"
#include "ui_icons.h"
#include "bongo_cat/image.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>

static unsigned int load(BongoCatPreferences *value, const char *name,
    int size, int *width, int *height) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), value->app->asset_root,
        name)) return 0;
    BongoCatError error = {0};
    bongo_cat_gl_clear_errors();
    unsigned int texture = bongo_cat_image_texture_thumbnail(path, size,
        size, width, height, &error);
    GLenum upload_error = glGetError();
    if (texture && upload_error != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    if (upload_error != GL_NO_ERROR)
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "UI asset upload failed for %s (0x%x)", name,
            (unsigned)upload_error);
    if (!texture && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    return texture;
}

static int raster_size(const BongoCatPreferences *value, int logical) {
    float scale = value->ui.raster_scale > 0.0f ? value->ui.raster_scale : 1.0f;
    return SDL_clamp((int)ceilf(logical * scale), logical, logical * 4);
}

void bongo_cat_preferences_assets_load(BongoCatPreferences *value) {
    if (!value->logo_texture)
        value->logo_texture = load(value, "bongocat.png", raster_size(value, 192),
            &value->logo_width, &value->logo_height);
    int width = 0, height = 0;
    if (!value->icon_texture)
        value->icon_texture = load(value, "ui-symbols.png", 0, &width, &height);
    value->icon_hidpi_attempted = false;
}

void bongo_cat_preferences_icon_draw(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, int icon, struct nk_rect bounds,
    struct nk_color color) {
    if (!value || !value->icon_texture || icon < 0 ||
        icon >= BONGO_CAT_UI_ICON_COUNT) return;
    bool large = bounds.w > 24.0f || bounds.h > 24.0f;
    bool needs_hidpi = large || value->ui.raster_scale > 1.05f;
    if (needs_hidpi && !value->icon_hidpi_attempted) {
        int width = 0, height = 0;
        value->icon_hidpi_attempted = true;
        value->icon_texture_hidpi = load(value, "ui-symbols@4x.png", 0,
            &width, &height);
    }
    bool hidpi = value->icon_texture_hidpi && needs_hidpi;
    int cell = hidpi ? 96 : 24;
    int atlas_width = cell * BONGO_CAT_UI_ICON_COUNT;
    unsigned int texture = hidpi ? value->icon_texture_hidpi : value->icon_texture;
    struct nk_image image = nk_subimage_id((int)texture, (nk_ushort)atlas_width,
        (nk_ushort)cell, nk_recti(icon * cell, 0, cell, cell));
    nk_draw_image(canvas, bounds, &image, color);
}

void bongo_cat_preferences_support_assets_load(BongoCatPreferences *value) {
    if (value->support_assets_loaded) return;
    value->support_assets_loaded = true;
    int image_size = raster_size(value, 192);
    value->catime_texture = load(value, "catime.png", image_size,
        &value->catime_width, &value->catime_height);
    char path[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), value->app->asset_root,
        "vlaina.jpg")) {
        BongoCatError error = {0};
        value->vlaina_texture = bongo_cat_image_texture_resampled(path,
            image_size, image_size, (float)raster_size(value, 48),
            &value->vlaina_width, &value->vlaina_height, &error);
        if (!value->vlaina_texture && error.message[0])
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    }
}

static void clear(unsigned int *texture) {
    if (*texture) glDeleteTextures(1, texture);
    *texture = 0;
}

void bongo_cat_preferences_support_assets_clear(BongoCatPreferences *value) {
    if (!value) return;
    clear(&value->catime_texture);
    clear(&value->vlaina_texture);
    value->support_assets_loaded = false;
}

void bongo_cat_preferences_assets_clear(BongoCatPreferences *value) {
    if (!value) return;
    value->asset_retry_count = 0;
    value->asset_retry_ns = 0;
    bongo_cat_about_assets_clear(value, true);
    clear(&value->logo_texture);
    clear(&value->icon_texture);
    clear(&value->icon_texture_hidpi);
    value->icon_hidpi_attempted = false;
    bongo_cat_preferences_support_assets_clear(value);
}

void bongo_cat_preferences_assets_abandon(BongoCatPreferences *value) {
    if (!value) return;
    value->asset_retry_count = 0;
    value->asset_retry_ns = 0;
    bongo_cat_about_assets_clear(value, false);
    value->logo_texture = 0;
    value->icon_texture = 0;
    value->icon_texture_hidpi = 0;
    value->catime_texture = 0;
    value->vlaina_texture = 0;
    value->icon_hidpi_attempted = false;
    value->support_assets_loaded = false;
}
