#ifndef BONGO_CAT_UPDATE_SERVICE_H
#define BONGO_CAT_UPDATE_SERVICE_H

#include "bongo_cat/update.h"

typedef struct BongoCatApp BongoCatApp;
typedef union SDL_Event SDL_Event;
typedef struct BongoCatUpdateService BongoCatUpdateService;

typedef enum BongoCatUpdateStatus {
    BONGO_CAT_UPDATE_IDLE,
    BONGO_CAT_UPDATE_CHECKING,
    BONGO_CAT_UPDATE_CURRENT,
    BONGO_CAT_UPDATE_AVAILABLE,
    BONGO_CAT_UPDATE_ERROR,
    BONGO_CAT_UPDATE_STORE,
    BONGO_CAT_UPDATE_UNSUPPORTED
} BongoCatUpdateStatus;

typedef struct BongoCatUpdateSnapshot {
    BongoCatUpdateStatus status;
    BongoCatUpdateRelease release;
    char error[256];
    bool installed;
} BongoCatUpdateSnapshot;

BongoCatUpdateService *bongo_cat_update_create(BongoCatApp *app);
void bongo_cat_update_start_automatic(BongoCatUpdateService *service);
bool bongo_cat_update_check(BongoCatUpdateService *service, bool manual);
bool bongo_cat_update_event(BongoCatUpdateService *service,
    const SDL_Event *event);
void bongo_cat_update_snapshot(BongoCatUpdateService *service,
    BongoCatUpdateSnapshot *snapshot);
bool bongo_cat_update_open(BongoCatUpdateService *service);
bool bongo_cat_update_refresh_and_open(BongoCatUpdateService *service);
void bongo_cat_update_destroy(BongoCatUpdateService *service);

#endif
