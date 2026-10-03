#include "preferences_state.h"
#include "preferences_about_internal.h"
#include <string.h>

const struct nk_user_font *bongo_cat_about_font(BongoCatPreferences *value, float size) {
    int index = NK_CLAMP(0, (int)(size * 2.0f), 63);
    struct nk_user_font *font = &value->about.css_fonts[index];
    *font = *(size >= 18.0f ? value->ui.heading_font : value->ui.caption_font);
    font->height = size;
    return font;
}

/* The same wrapping routine measures and draws, keeping translated layouts aligned. */
float bongo_cat_about_paragraph(struct nk_context *context, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color,
    bool center, float leading) {
    float y = bounds.y;
    int remaining = (int)strlen(text);
    while (*text && (!context || y + font->height <= bounds.y + bounds.h + 1)) {
        int length = 0;
        float width = 0;
        while (length < remaining) {
            nk_rune rune;
            int bytes = nk_utf_decode(text + length, &rune, remaining - length);
            if (bytes <= 0) break;
            /* Nuklear's atlas width is the sum of glyph advances (no kerning).
               Measure each UTF-8 glyph once instead of every growing prefix. */
            float next = width + font->width(font->userdata, font->height, text + length, bytes);
            if (length && next > bounds.w) break;
            length += bytes;
            width = next;
            if (rune == '\n') break;
        }
        if (!length) break;
        if (context) nk_draw_text(nk_window_get_canvas(context),
            nk_rect(bounds.x + (center ? (bounds.w - width) * .5f : 0), y, bounds.w, font->height),
            text, length, font, nk_rgba(0, 0, 0, 0), color);
        y += leading;
        text += length;
        remaining -= length;
        while (*text == ' ' || *text == '\n') { text++; remaining--; }
    }
    return y - bounds.y;
}

float bongo_cat_about_text_height(const char *text, const struct nk_user_font *font, float width) {
    return bongo_cat_about_paragraph(NULL, nk_rect(0, 0, width, 0), text, font,
        nk_rgb(0, 0, 0), false, font->height + 6.0f);
}

void bongo_cat_about_text(BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds, const char *key, const char *fallback,
    const struct nk_user_font *font, struct nk_color color, bool center) {
    const char *label = key ? bongo_cat_i18n_get(value->app->i18n, key, fallback) : fallback;
    bongo_cat_about_paragraph(context, bounds, label, font, color, center, font->height + 6.0f);
}
