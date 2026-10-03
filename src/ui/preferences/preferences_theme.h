#ifndef BONGO_CAT_PREFERENCES_THEME_H
#define BONGO_CAT_PREFERENCES_THEME_H

#include "nuklear_config.h"
#include <stdbool.h>

bool bongo_cat_pref_capsule_button(struct nk_context *context,
    const char *id, const char *label);

int bongo_cat_pref_theme(struct nk_context *context, const char *id,
    const char *title, const char *const *labels, int selected);
int bongo_cat_pref_fps(struct nk_context *context, const char *id,
    const char *title, int fps, int display_fps);
float bongo_cat_pref_render_quality(struct nk_context *context, const char *id,
    const char *title, const char *detail, float quality_percent);

#endif
