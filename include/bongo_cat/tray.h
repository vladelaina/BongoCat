#ifndef BONGO_CAT_TRAY_H
#define BONGO_CAT_TRAY_H

#include "bongo_cat/common.h"

typedef struct BongoCatApp BongoCatApp;
typedef struct BongoCatTray BongoCatTray;

BongoCatTray *bongo_cat_tray_create(BongoCatApp *app, BongoCatError *error);
void bongo_cat_tray_destroy(BongoCatTray *tray);
void bongo_cat_tray_sync(BongoCatTray *tray);
void bongo_cat_tray_prepare_menu(BongoCatTray *tray, void *native_handle);
bool bongo_cat_tray_self_test(BongoCatTray *tray);

#endif
