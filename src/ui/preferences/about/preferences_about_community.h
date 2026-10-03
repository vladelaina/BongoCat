#ifndef BONGO_CAT_PREFERENCES_ABOUT_COMMUNITY_H
#define BONGO_CAT_PREFERENCES_ABOUT_COMMUNITY_H

#include "preferences_internal.h"

void bongo_cat_preferences_about_community(
    BongoCatPreferences *value, struct nk_context *context);
void bongo_cat_preferences_about_projects_heading(
    BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds);
void bongo_cat_preferences_about_localized_link(
    BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *key, const char *fallback, const char *url,
    const char *animation_id, BongoCatUIPalette palette);

#endif
