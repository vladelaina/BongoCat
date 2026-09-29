#ifndef BONGO_CAT_PREFERENCES_WIDGETS_INTERNAL_H
#define BONGO_CAT_PREFERENCES_WIDGETS_INTERNAL_H

#include <stdbool.h>
#include "nuklear_config.h"

typedef struct FormStyle {
    struct nk_style_item background;
    struct nk_color window_color;
    struct nk_color border_color;
    struct nk_vec2 padding;
    struct nk_vec2 spacing;
    float border;
} FormStyle;

bool bongo_cat_pref_form_begin(struct nk_context *context, const char *id,
    int lines, FormStyle *saved);
void bongo_cat_pref_form_end(struct nk_context *context,
    const FormStyle *saved);
void bongo_cat_pref_form_label(struct nk_context *context, const char *title);
void bongo_cat_pref_row_icon_clear(struct nk_context *context);
int bongo_cat_pref_detail_lines(const struct nk_context *context,
    const char *text);
void bongo_cat_pref_form_title_sized(struct nk_context *context,
    const char *title, float control_width);
/* Square brackets mark text in #f77daa; measurement omits the markers. */
int bongo_cat_pref_detail_text(struct nk_context *context, const char *text,
    struct nk_rect bounds, bool draw);
void bongo_cat_pref_description(struct nk_context *context,
    const char *text, int lines);

#endif
