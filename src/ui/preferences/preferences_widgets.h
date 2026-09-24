#ifndef BONGO_CAT_PREFERENCES_WIDGETS_H
#define BONGO_CAT_PREFERENCES_WIDGETS_H

#include <stdbool.h>
#include "bongo_cat/config.h"
#include "nuklear_config.h"
#include "preferences_icons.h"

void bongo_cat_pref_section(struct nk_context *context, const char *title);
void bongo_cat_pref_section_icon(struct nk_context *context,
    const char *title, BongoCatPrefIcon icon);
void bongo_cat_pref_row_icon(struct nk_context *context,
    BongoCatPrefIcon icon);
bool bongo_cat_pref_toggle(struct nk_context *context, const char *id,
    const char *title, const char *description, bool *value);
bool bongo_cat_pref_toggle_help(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *help, bool *value);
bool bongo_cat_pref_toggle_float(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value);
/* Square brackets in detail mark highlighted option names. */
bool bongo_cat_pref_toggle_float_detail(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value, const char *detail,
    bool available);
/* Adds a config button left of the numeric field; returns whether it was
   clicked. The button shares the numeric field's visibility. */
bool bongo_cat_pref_toggle_float_config(struct nk_context *context, const char *id,
    const char *title, const char *unit, bool *enabled, float minimum, float *value,
    float maximum, float step, float default_value);
bool bongo_cat_pref_obs_background(struct nk_context *context, const char *id,
    const char *title, const char *question, const char *reply, bool *enabled,
    BongoCatObsBackgroundColor *color);
bool bongo_cat_pref_float(struct nk_context *context, const char *id,
    const char *title, const char *description, float minimum, float *value,
    float maximum, float step, float default_value);
/* Returns whether the trailing action button was clicked. */
bool bongo_cat_pref_float_action(struct nk_context *context, const char *id,
    const char *title, const char *detail, float minimum, float *value,
    float maximum, float step, float default_value, const char *button);
bool bongo_cat_pref_int(struct nk_context *context, const char *id,
    const char *title, const char *description, int minimum, int *value,
    int maximum, int step, int default_value);
bool bongo_cat_pref_slider(struct nk_context *context, const char *id,
    const char *title, const char *description, float minimum, float *value,
    float maximum, float step, float default_value);
int bongo_cat_pref_combo(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *const *items,
    int count, int selected);
int bongo_cat_pref_edit(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *value,
    bool recording, const char *idle_hint, const char *record_hint);
bool bongo_cat_pref_button(struct nk_context *context, const char *id,
    const char *title, const char *description, const char *button);
void bongo_cat_pref_status(struct nk_context *context, const char *id,
    const char *title, const char *description);

#endif
