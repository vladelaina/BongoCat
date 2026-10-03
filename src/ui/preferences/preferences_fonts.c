#include "preferences_fonts.h"

#include "preferences_model_cover.h"
#include "preferences_model_glyphs.h"
#include "ui_font.h"
#include "ui_font_atlas.h"
#include "ui_paint.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/memory_policy.h"

#include <SDL3/SDL.h>
#include <string.h>

void bongo_cat_preferences_fonts_resolve(BongoCatPreferences *value,
    BongoCatPreferenceFonts *fonts) {
    memset(fonts, 0, sizeof(*fonts));
    fonts->body = bongo_cat_ui_system_font(fonts->body_path,
        sizeof(fonts->body_path), false);
    fonts->body_fallback = bongo_cat_ui_system_font(fonts->body_fallback_path,
        sizeof(fonts->body_fallback_path), true);
    fonts->body_korean_fallback = bongo_cat_ui_system_korean_font(
        fonts->body_korean_fallback_path,
        sizeof(fonts->body_korean_fallback_path));
    fonts->heading = bongo_cat_ui_system_heading_font(fonts->heading_path,
        sizeof(fonts->heading_path), false);
    fonts->heading_fallback = bongo_cat_ui_system_heading_font(
        fonts->heading_fallback_path,
        sizeof(fonts->heading_fallback_path), true);
    fonts->heading_korean_fallback = bongo_cat_ui_system_korean_heading_font(
        fonts->heading_korean_fallback_path,
        sizeof(fonts->heading_korean_fallback_path));
    if (!fonts->heading) fonts->heading = fonts->body;
    if (!fonts->heading_fallback) fonts->heading_fallback =
        fonts->body_fallback;
    if (!value->app->i18n) return;
    size_t range_capacity = sizeof(value->glyph_ranges) / sizeof(value->glyph_ranges[0]);
    size_t range_count = bongo_cat_i18n_glyph_ranges(value->app->i18n,
        value->glyph_ranges, range_capacity - 2);
    /* Shortcut arrows must be available even before a binding is recorded. */
    if (range_count && range_count + 2 <= range_capacity) {
        value->glyph_ranges[range_count - 1] = 0x2190;
        value->glyph_ranges[range_count] = 0x2193;
        value->glyph_ranges[range_count + 1] = 0;
    }
    /* Include label glyphs in the initial atlas so page switches need no bake. */
    bongo_cat_preferences_model_glyphs(value->app, value->glyph_ranges,
        sizeof(value->glyph_ranges) / sizeof(value->glyph_ranges[0]));
    fonts->ranges = value->glyph_ranges;
}

static bool reload(BongoCatPreferences *value,
    const BongoCatPreferenceFonts *fonts, float raster_scale) {
    bool reloaded = bongo_cat_ui_font_atlas_reload(&value->ui, fonts->body,
        fonts->body_fallback, fonts->body_korean_fallback, fonts->heading,
        fonts->heading_fallback, fonts->heading_korean_fallback, fonts->ranges,
        raster_scale);
    if (reloaded) bongo_cat_memory_policy_ui_loaded();
    return reloaded;
}

bool bongo_cat_preferences_reload_fonts(BongoCatPreferences *value) {
    if (!value || !value->ui_initialized) return false;
    if (value->ui.frame_building) {
        value->font_reload_pending = true;
        value->render_dirty = true;
        return true;
    }
    BongoCatPreferenceFonts fonts;
    bongo_cat_preferences_fonts_resolve(value, &fonts);
    bool reloaded = reload(value, &fonts, value->ui.raster_scale);
    if (reloaded) {
        value->font_reload_pending = false;
        bongo_cat_preferences_model_glyphs_clear_pending(value);
    }
    return reloaded;
}

bool bongo_cat_preferences_refresh_raster(BongoCatPreferences *value) {
    if (!value || value->pending_raster_scale <= 0.0f) return true;
    uint64_t now = SDL_GetTicksNS();
    if (value->raster_retry_ns > now) return false;
    float raster_scale = value->pending_raster_scale;
    BongoCatPreferenceFonts fonts;
    bongo_cat_preferences_fonts_resolve(value, &fonts);
    if (!reload(value, &fonts, raster_scale)) {
        value->raster_retry_ns = now + 1000000000ull;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Preferences font atlas could not be rebuilt for %.2fx",
            raster_scale);
        return false;
    }
    value->pending_raster_scale = 0.0f;
    value->raster_retry_ns = 0;
    bongo_cat_ui_paint_destroy(&value->ui);
    bongo_cat_preferences_model_cover_cache_clear(value->app);
    bongo_cat_preferences_assets_clear(value);
    bongo_cat_preferences_assets_load(value);
    return true;
}
