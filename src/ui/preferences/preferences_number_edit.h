#ifndef BONGO_CAT_PREFERENCES_NUMBER_EDIT_H
#define BONGO_CAT_PREFERENCES_NUMBER_EDIT_H

#include "preferences_controls.h"

bool bongo_cat_pref_number_edit(struct nk_context *context, const char *id,
    struct nk_rect bounds, const char *formatted, bool integer,
    double minimum, double maximum, double *value);
void bongo_cat_pref_number_edit_reset(struct nk_context *context);

#endif
