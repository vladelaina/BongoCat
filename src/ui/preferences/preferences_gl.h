#ifndef BONGO_CAT_PREFERENCES_GL_H
#define BONGO_CAT_PREFERENCES_GL_H

#include <stdbool.h>

typedef struct BongoCatPreferences BongoCatPreferences;

bool bongo_cat_preferences_gl_create(BongoCatPreferences *value);
bool bongo_cat_preferences_gl_destroy(BongoCatPreferences *value);

#endif
