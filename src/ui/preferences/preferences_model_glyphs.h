#ifndef BONGO_CAT_PREFERENCES_MODEL_GLYPHS_H
#define BONGO_CAT_PREFERENCES_MODEL_GLYPHS_H

#include <stddef.h>
#include <stdint.h>

typedef struct BongoCatApp BongoCatApp;
typedef struct BongoCatPreferences BongoCatPreferences;

void bongo_cat_preferences_model_glyphs(const BongoCatApp *app,
    uint32_t *ranges, size_t capacity);
void bongo_cat_preferences_model_glyphs_note(BongoCatPreferences *preferences,
    const char *name);
bool bongo_cat_preferences_model_glyphs_ready(
    const BongoCatPreferences *preferences, const char *name);
void bongo_cat_preferences_model_glyphs_clear_pending(
    BongoCatPreferences *preferences);
bool bongo_cat_preferences_behavior_glyphs_ready(
    const BongoCatPreferences *preferences);

#endif
