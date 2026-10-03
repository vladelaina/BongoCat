#ifndef BONGO_CAT_WINDOWS_GAME_COMPATIBILITY_H
#define BONGO_CAT_WINDOWS_GAME_COMPATIBILITY_H

#include "bongo_cat/common.h"
typedef struct BongoCatApp BongoCatApp;
#ifdef __cplusplus
extern "C" {
#endif
bool bongo_cat_windows_game_compatibility_command(void);
bool bongo_cat_windows_game_compatibility_startup(BongoCatApp *app,
    bool *restarting, BongoCatError *error);
bool bongo_cat_windows_game_compatibility_set(BongoCatApp *app, bool enabled,
    BongoCatError *error);
void bongo_cat_windows_game_compatibility_finish(void);
bool bongo_cat_windows_game_compatibility_elevated(bool *value, BongoCatError *error);
bool bongo_cat_windows_game_compatibility_launch(bool administrator, BongoCatError *error);
void bongo_cat_windows_game_compatibility_cancel(void);
#ifdef __cplusplus
}
#endif
#endif
