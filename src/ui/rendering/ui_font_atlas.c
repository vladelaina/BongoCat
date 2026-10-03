#include "ui_font_atlas_internal.h"

#include <SDL3/SDL_opengl.h>
#include <stdlib.h>
#include <string.h>

/* Reuse mappings only within this bake. Owners remain in sources[] and are
   detached/released once, including on failure. */
static const UIFontSource *load_source(UIFontSource *sources,
    const char *const *paths, int index) {
    if (!paths[index]) return NULL;
    for (int i = 0; i < index; ++i)
        if (sources[i].data && paths[i] && strcmp(paths[i], paths[index]) == 0)
            return &sources[i];
    return bongo_cat_ui_font_source_load(&sources[index], paths[index]) ?
        &sources[index] : NULL;
}

bool bongo_cat_ui_font_atlas_create(BongoCatUIBackend *ui,
    const char *body_path, const char *body_fallback_path,
    const char *body_korean_fallback_path, const char *heading_path,
    const char *heading_fallback_path,
    const char *heading_korean_fallback_path,
    const nk_rune *glyph_ranges, float raster_scale) {
    UIFontSource sources[6] = {0};
    const char *paths[] = {body_path, body_fallback_path,
        body_korean_fallback_path, heading_path, heading_fallback_path,
        heading_korean_fallback_path};
    const UIFontSource *body = load_source(sources, paths, 0);
    const UIFontSource *body_fallback = body_fallback_path &&
        (!body_path || strcmp(body_path, body_fallback_path) != 0) ?
        load_source(sources, paths, 1) : NULL;
    const UIFontSource *body_korean_fallback = body_korean_fallback_path &&
        (!body_path || strcmp(body_path, body_korean_fallback_path) != 0) &&
        (!body_fallback_path || strcmp(body_fallback_path,
            body_korean_fallback_path) != 0) ?
        load_source(sources, paths, 2) : NULL;
    const UIFontSource *heading = load_source(sources, paths, 3);
    const UIFontSource *heading_fallback = heading_fallback_path &&
        (!heading_path || strcmp(heading_path, heading_fallback_path) != 0) ?
        load_source(sources, paths, 4) : NULL;
    const UIFontSource *heading_korean_fallback = heading_korean_fallback_path &&
        (!heading_path || strcmp(heading_path, heading_korean_fallback_path) != 0) &&
        (!heading_fallback_path || strcmp(heading_fallback_path,
            heading_korean_fallback_path) != 0) ?
        load_source(sources, paths, 5) : NULL;
    const UIFontSource *heading_source = heading ? heading : body;
    const UIFontSource *heading_fallback_source = heading_fallback ?
        heading_fallback : body_fallback;
    const UIFontSource *heading_korean_fallback_source =
        heading_korean_fallback ? heading_korean_fallback :
        (body_korean_fallback ? body_korean_fallback : heading_fallback_source);
    nk_rune *primary_ranges = NULL, *cjk_ranges = NULL;
    nk_rune *korean_ranges = NULL;
    if (!bongo_cat_ui_font_split_ranges(glyph_ranges, &primary_ranges,
        &cjk_ranges, &korean_ranges)) {
        for (int i = 0; i < 6; ++i)
            bongo_cat_ui_font_source_release(&sources[i]);
        return false;
    }
    nk_font_atlas_init_default(&ui->atlas);
    nk_font_atlas_begin(&ui->atlas);
    GLint maximum_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_texture);
    /* Keep the atlas bounded on high-DPI displays. The UI is laid out in
       logical pixels, so a 1.5x raster is enough for crisp text while a 2x
       atlas can quadruple both the texture and bake working memory. */
    float scale_limit = maximum_texture >= 8192 ? 1.5f : 1.0f;
    float font_scale = NK_CLAMP(1.0f, raster_scale, scale_limit);
    if (font_scale + .01f < raster_scale) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Preferences font raster scale limited to %.2fx by the UI memory "
        "budget (GPU texture limit %d)",
        font_scale, maximum_texture);
    const UIFontSource *body_source = body;
    const UIFontSource *fallback_source = body_fallback;
    const UIFontSource *body_korean_fallback_source =
        body_korean_fallback ? body_korean_fallback : fallback_source;
    struct nk_font *caption_font = bongo_cat_ui_font_add_family(&ui->atlas,
        body_source, fallback_source, body_korean_fallback_source,
        18.0f * font_scale, glyph_ranges, primary_ranges, cjk_ranges,
        korean_ranges);
    struct nk_font *body_font = bongo_cat_ui_font_add_family(&ui->atlas,
        body_source, fallback_source, body_korean_fallback_source,
        20.0f * font_scale, glyph_ranges, primary_ranges, cjk_ranges,
        korean_ranges);
    /* Identical families at 20 px can share glyphs and the font handle. */
    bool shared_label = body_source == heading_source &&
        fallback_source == heading_fallback_source &&
        body_korean_fallback_source == heading_korean_fallback_source;
    struct nk_font *label_font = shared_label ? body_font :
        bongo_cat_ui_font_add_family(&ui->atlas,
        heading_source, heading_fallback_source,
        heading_korean_fallback_source, 20.0f * font_scale, glyph_ranges,
        primary_ranges, cjk_ranges, korean_ranges);
    struct nk_font *heading_font = bongo_cat_ui_font_add_family(&ui->atlas,
        heading_source, heading_fallback_source,
        heading_korean_fallback_source, 28.0f * font_scale, glyph_ranges,
        primary_ranges, cjk_ranges, korean_ranges);
    struct nk_font *hero_font = bongo_cat_ui_font_add_family(&ui->atlas,
        heading_source, heading_fallback_source,
        heading_korean_fallback_source, 36.0f * font_scale, glyph_ranges,
        primary_ranges, cjk_ranges, korean_ranges);
    bool uploaded = caption_font && body_font && label_font && heading_font &&
        hero_font && bongo_cat_ui_font_upload_atlas(ui);
    if (uploaded) {
        ui->caption_font = &caption_font->handle;
        ui->body_font = &body_font->handle;
        ui->label_font = &label_font->handle;
        ui->heading_font = &heading_font->handle;
        ui->hero_font = &hero_font->handle;
        caption_font->handle.height = 18.0f;
        body_font->handle.height = 20.0f;
        label_font->handle.height = 20.0f;
        heading_font->handle.height = 28.0f;
        hero_font->handle.height = 36.0f;
        ui->latin_glyph_ranges = primary_ranges;
        ui->cjk_glyph_ranges = cjk_ranges;
        ui->korean_glyph_ranges = korean_ranges;
        ui->font_probe_loaded = bongo_cat_ui_font_has_ranges(body_font,
            glyph_ranges);
        nk_style_set_font(&ui->context, ui->body_font);
    } else {
        free(korean_ranges); free(cjk_ranges); free(primary_ranges);
    }
    ui->font_path_found = body_path != NULL || body_fallback_path != NULL ||
        body_korean_fallback_path != NULL;
    ui->font_file_loaded = body || body_fallback || body_korean_fallback;
    ui->custom_font_loaded = ui->font_file_loaded && body_font != NULL;
    for (int i = 0; i < 6; ++i)
        bongo_cat_ui_font_detach_source(&ui->atlas, &sources[i]);
    nk_font_atlas_cleanup(&ui->atlas);
    for (int i = 0; i < 6; ++i)
        bongo_cat_ui_font_source_release(&sources[i]);
    return uploaded;
}

void bongo_cat_ui_font_atlas_destroy(BongoCatUIBackend *ui) {
    if (!ui) return;
    nk_font_atlas_clear(&ui->atlas);
    free(ui->korean_glyph_ranges); free(ui->cjk_glyph_ranges);
    free(ui->latin_glyph_ranges);
    ui->korean_glyph_ranges = NULL; ui->cjk_glyph_ranges = NULL;
    ui->latin_glyph_ranges = NULL;
    ui->caption_font = NULL;
    ui->body_font = NULL;
    ui->label_font = NULL;
    ui->heading_font = NULL;
    ui->hero_font = NULL;
    ui->font_atlas_width = 0;
    ui->font_atlas_height = 0;
}
