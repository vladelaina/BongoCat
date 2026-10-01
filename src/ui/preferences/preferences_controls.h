#ifndef BONGO_CAT_PREFERENCES_CONTROLS_H
#define BONGO_CAT_PREFERENCES_CONTROLS_H

#include <stdbool.h>
#include "bongo_cat/config.h"
#include "nuklear_config.h"

/* Stable, unique ids retain numeric drafts; valid edits update value immediately.
 * Integer fields accept digits and an optional minus; floats also accept a dot.
 * Values are clamped to the supplied range. Clicking away discards incomplete text.
 */
bool bongo_cat_pref_control_float(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step,
    float default_value);
bool bongo_cat_pref_control_int(struct nk_context *context, const char *id,
    int minimum, int *value, int maximum, int step, int default_value);
bool bongo_cat_pref_control_slider(struct nk_context *context, const char *id,
    float minimum, float *value, float maximum, float step,
    float default_value);
bool bongo_cat_pref_control_toggle(struct nk_context *context,
    const char *id, bool *value);
bool bongo_cat_pref_control_obs_background(struct nk_context *context,
    const char *id, bool *enabled, BongoCatObsBackgroundColor *color);
int bongo_cat_pref_control_combo(struct nk_context *context, const char *id,
    const char *const *items, int count, int selected);
bool bongo_cat_pref_controls_animating(struct nk_context *context);
void bongo_cat_pref_controls_reset(struct nk_context *context);

#endif
