#ifndef BONGO_CAT_WINDOWS_INPUT_H
#define BONGO_CAT_WINDOWS_INPUT_H

#include "bongo_cat/platform.h"

#ifdef _WIN32
bool bongo_cat_windows_input_start(BongoCatPlatform *platform);
void bongo_cat_windows_input_stop(BongoCatPlatform *platform);
bool bongo_cat_windows_input_take_relative(BongoCatPlatform *platform,
    double *x, double *y);
void bongo_cat_windows_input_reset_relative(BongoCatPlatform *platform);
#endif

#endif
