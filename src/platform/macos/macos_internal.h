#ifndef BONGO_CAT_MACOS_INTERNAL_H
#define BONGO_CAT_MACOS_INTERNAL_H

#include "bongo_cat/platform.h"

bool bongo_cat_macos_input_start(BongoCatPlatform *platform, BongoCatError *error);
void bongo_cat_macos_input_stop(BongoCatPlatform *platform);
bool bongo_cat_macos_input_supported(void);
bool bongo_cat_macos_input_monitoring_authorized(void);
bool bongo_cat_macos_input_monitoring_request(void);

#endif
