#include "ui_font_atlas_internal.h"

#include <stdlib.h>

typedef enum GlyphRangeKind {
    GLYPH_RANGE_PRIMARY,
    GLYPH_RANGE_CJK,
    GLYPH_RANGE_KOREAN
} GlyphRangeKind;

// Nuklear does not skip missing glyphs while merging fonts, so each script
// must be assigned to a font that actually contains it.
static GlyphRangeKind glyph_range_kind(nk_rune point) {
    bool korean = (point >= 0x1100 && point <= 0x11ff) ||
        (point >= 0x3130 && point <= 0x318f) ||
        (point >= 0xa960 && point <= 0xa97f) ||
        (point >= 0xac00 && point <= 0xd7ff);
    if (korean) return GLYPH_RANGE_KOREAN;
    return point >= 0x2e80 ? GLYPH_RANGE_CJK : GLYPH_RANGE_PRIMARY;
}

static void append_range(nk_rune *ranges, size_t *count,
    nk_rune first, nk_rune last) {
    ranges[(*count)++] = first;
    ranges[(*count)++] = last;
}

static int compare_ranges(const void *left, const void *right) {
    const nk_rune *a = left, *b = right;
    return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 :
        (a[1] < b[1] ? -1 : a[1] > b[1]);
}

static void normalize_ranges(nk_rune *ranges, size_t *count) {
    qsort(ranges, *count / 2, sizeof(*ranges) * 2, compare_ranges);
    size_t written = 0;
    for (size_t read = 0; read < *count; read += 2) {
        nk_rune first = ranges[read], last = ranges[read + 1];
        if (written >= 2 && first <= ranges[written - 1] + 1) {
            ranges[written - 1] = NK_MAX(ranges[written - 1], last);
            continue;
        }
        ranges[written++] = first;
        ranges[written++] = last;
    }
    ranges[written] = 0;
    *count = written;
}

bool bongo_cat_ui_font_split_ranges(const nk_rune *ranges,
    nk_rune **primary, nk_rune **cjk, nk_rune **korean) {
    *primary = NULL; *cjk = NULL; *korean = NULL;
    if (!ranges) return true;
    size_t entries = 0;
    while (ranges[entries]) entries++;
    size_t capacity = entries + 32;
    *primary = calloc(capacity, sizeof(**primary));
    *cjk = calloc(capacity, sizeof(**cjk));
    *korean = calloc(capacity, sizeof(**korean));
    if (!*primary || !*cjk || !*korean) {
        free(*korean); free(*cjk); free(*primary);
        return false;
    }
    static const nk_rune boundaries[] = {0x1100, 0x1200, 0x2e80,
        0x3130, 0x3190, 0xa960, 0xa980, 0xac00, 0xd800};
    size_t counts[3] = {0};
    for (size_t pair = 0; ranges[pair] && ranges[pair + 1]; pair += 2) {
        nk_rune cursor = ranges[pair], last = ranges[pair + 1];
        while (cursor <= last) {
            nk_rune segment_last = last;
            for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
                if (boundaries[i] > cursor && boundaries[i] - 1 < segment_last)
                    segment_last = boundaries[i] - 1;
            GlyphRangeKind kind = glyph_range_kind(cursor);
            nk_rune *target = kind == GLYPH_RANGE_PRIMARY ? *primary :
                (kind == GLYPH_RANGE_CJK ? *cjk : *korean);
            append_range(target, &counts[kind], cursor, segment_last);
            if (segment_last == last) break;
            cursor = segment_last + 1;
        }
    }
    normalize_ranges(*primary, &counts[GLYPH_RANGE_PRIMARY]);
    normalize_ranges(*cjk, &counts[GLYPH_RANGE_CJK]);
    normalize_ranges(*korean, &counts[GLYPH_RANGE_KOREAN]);
    return true;
}

bool bongo_cat_ui_font_has_ranges(const struct nk_font *font,
    const nk_rune *ranges) {
    if (!font) return false;
    if (!ranges) {
        const struct nk_font_glyph *glyph = nk_font_find_glyph(font, 'A');
        return glyph && glyph->codepoint == 'A';
    }
    for (size_t pair = 2; ranges[pair] && ranges[pair + 1]; pair += 2)
        for (nk_rune point = ranges[pair]; point <= ranges[pair + 1]; ++point) {
            const struct nk_font_glyph *glyph = nk_font_find_glyph(font, point);
            if (!glyph || glyph->codepoint != point) return false;
        }
    return true;
}
