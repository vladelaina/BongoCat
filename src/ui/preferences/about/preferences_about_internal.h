#ifndef BONGO_CAT_PREFERENCES_ABOUT_INTERNAL_H
#define BONGO_CAT_PREFERENCES_ABOUT_INTERNAL_H

#include "preferences_about.h"

/* Shared text layout and rendering. */
const struct nk_user_font *bongo_cat_about_font(BongoCatPreferences *value, float size);
float bongo_cat_about_text_height(const char *text, const struct nk_user_font *font, float width);
float bongo_cat_about_paragraph(struct nk_context *context, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color,
    bool center, float leading);
void bongo_cat_about_text(BongoCatPreferences *value, struct nk_context *context,
                          struct nk_rect bounds, const char *key, const char *fallback,
                          const struct nk_user_font *font, struct nk_color color, bool center);

/* Page sections and their UI helpers. */
void bongo_cat_preferences_about_hero(BongoCatPreferences *value, struct nk_context *context);
void bongo_cat_preferences_about_projects(BongoCatPreferences *value, struct nk_context *context);
void bongo_cat_about_contributors(BongoCatPreferences *value, struct nk_context *context);
void bongo_cat_about_wechat(BongoCatPreferences *value, struct nk_rect anchor);
void bongo_cat_about_wechat_icon(BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds);
void bongo_cat_about_ensure_event(BongoCatPreferences *value);

#endif
