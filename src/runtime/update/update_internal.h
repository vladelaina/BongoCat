#ifndef BONGO_CAT_UPDATE_INTERNAL_H
#define BONGO_CAT_UPDATE_INTERNAL_H

#include "update_service.h"
#include "bongo_cat/app.h"
#include <SDL3/SDL.h>

typedef enum BongoCatUpdateFetchResult {
    BONGO_CAT_UPDATE_FETCH_OK,
    BONGO_CAT_UPDATE_FETCH_CANCELLED,
    BONGO_CAT_UPDATE_FETCH_UNSUPPORTED,
    BONGO_CAT_UPDATE_FETCH_NETWORK,
    BONGO_CAT_UPDATE_FETCH_RESPONSE,
    BONGO_CAT_UPDATE_FETCH_MEMORY
} BongoCatUpdateFetchResult;

struct BongoCatUpdateService {
    BongoCatApp *app;
    SDL_Mutex *mutex;
    SDL_Mutex *http_mutex;
    SDL_Thread *worker;
    Uint32 event_type;
    BongoCatUpdateStatus status;
    BongoCatUpdateRelease release;
    char error[256];
    bool manual;
    bool open_after_check;
    bool installed;
    bool shutting_down;
    bool http_cancelled;
    void *http_session;
    void *http_connection;
    void *http_request;
};

BongoCatUpdateFetchResult bongo_cat_update_http_fetch(
    BongoCatUpdateService *service, char **response, char *error,
    size_t error_capacity);
void bongo_cat_update_http_cancel(BongoCatUpdateService *service);
void bongo_cat_update_show_completion(BongoCatUpdateService *service);
bool bongo_cat_update_platform_supported(void);
bool bongo_cat_update_platform_store(void);
bool bongo_cat_update_platform_installed(void);
const char *bongo_cat_update_platform_asset(void);

#endif
