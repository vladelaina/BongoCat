#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ui_font_atlas_internal.h"
#include "bongo_cat/file.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif

bool bongo_cat_ui_font_source_load(UIFontSource *source, const char *path) {
    source->file = bongo_cat_file_open(path, "rb");
    if (!source->file || fseek(source->file, 0, SEEK_END) != 0) return false;
    long size = ftell(source->file);
    if (size <= 0 || fseek(source->file, 0, SEEK_SET) != 0) return false;
#ifdef _WIN32
    HANDLE file = (HANDLE)_get_osfhandle(_fileno(source->file));
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    source->data = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    source->mapping = mapping;
    source->size = source->data ? (size_t)size : 0;
#else
    source->data = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE,
        fileno(source->file), 0);
    if (source->data == MAP_FAILED) source->data = NULL;
    source->size = source->data ? (size_t)size : 0;
#endif
    return source->data != NULL;
}

void bongo_cat_ui_font_source_release(UIFontSource *source) {
#ifdef _WIN32
    if (source->data) UnmapViewOfFile(source->data);
    if (source->mapping) CloseHandle((HANDLE)source->mapping);
#else
    if (source->data) munmap(source->data, source->size);
#endif
    if (source->file) fclose(source->file);
    memset(source, 0, sizeof(*source));
}

static struct nk_font_config font_config(float size, const nk_rune *ranges) {
    struct nk_font_config config = nk_font_config(size);
    config.range = ranges;
    /* CJK glyphs dominate the atlas and gain little from subpixel
       oversampling. Keep the sharper 2x bake for Latin and symbols while
       using the native raster for the large fallback ranges. */
    bool fallback_range = ranges && ranges[0] >= 0x1100;
    config.oversample_h = fallback_range ? 1 : 2;
    config.oversample_v = fallback_range ? 1 : 2;
    return config;
}

static struct nk_font *add_font(struct nk_font_atlas *atlas,
    const UIFontSource *source, float size, const nk_rune *ranges, bool merge) {
    struct nk_font_config config = font_config(size, ranges);
    config.merge_mode = merge;
    int before = atlas->font_num;
    struct nk_font *font;
    if (source && source->data) {
        config.ttf_blob = source->data;
        config.ttf_size = source->size;
        config.ttf_data_owned_by_atlas = 1;
        font = nk_font_atlas_add(atlas, &config);
    } else font = nk_font_atlas_add_default(atlas, size, &config);
    return merge && atlas->font_num > before ? atlas->fonts : font;
}

void bongo_cat_ui_font_detach_source(struct nk_font_atlas *atlas,
    const UIFontSource *source) {
    if (!source || !source->data) return;
    for (struct nk_font_config *base = atlas->config; base; base = base->next) {
        struct nk_font_config *config = base;
        do {
            if (config->ttf_blob == source->data) config->ttf_blob = NULL;
            config = config->n;
        } while (config != base);
    }
}

static void font_to_front(struct nk_font_atlas *atlas, struct nk_font *font) {
    if (!atlas || !font || atlas->fonts == font) return;
    struct nk_font *previous = atlas->fonts;
    while (previous && previous->next != font) previous = previous->next;
    if (!previous) return;
    previous->next = font->next;
    font->next = atlas->fonts;
    atlas->fonts = font;
}

struct nk_font *bongo_cat_ui_font_add_family(struct nk_font_atlas *atlas,
    const UIFontSource *primary, const UIFontSource *fallback,
    const UIFontSource *korean_fallback, float size, const nk_rune *all,
    const nk_rune *primary_ranges, const nk_rune *cjk,
    const nk_rune *korean) {
    bool merge = primary && primary->data && fallback && fallback->data &&
        ((cjk && cjk[0]) || (korean && korean[0]));
    const UIFontSource *base = primary && primary->data ? primary : fallback;
    struct nk_font *font = add_font(atlas, base, size,
        merge ? primary_ranges : all, false);
    if (!font || !merge) return font;
    font_to_front(atlas, font);
    if (cjk && cjk[0] && !add_font(atlas, fallback, size, cjk, true))
        return NULL;
    const UIFontSource *korean_source = korean_fallback && korean_fallback->data ?
        korean_fallback : fallback;
    if (korean && korean[0] &&
        !add_font(atlas, korean_source, size, korean, true)) return NULL;
    return font;
}
