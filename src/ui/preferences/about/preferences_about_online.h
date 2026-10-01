#ifndef BONGO_CAT_ABOUT_ONLINE_H
#define BONGO_CAT_ABOUT_ONLINE_H

#include "preferences_about_feed.h"
#include "bongo_cat/common.h"
#include <stdbool.h>
#include <stddef.h>

/* One bounded cache read or network request per job. Worker results are published
   by done. A stale cache succeeds with refresh_needed, then the UI launches one
   network_only job while keeping the cached content visible. */
enum { BONGO_ABOUT_WECHAT, BONGO_ABOUT_CONTRIBUTORS };
typedef struct BongoCatAboutRequest {
    SDL_Thread *thread;
    SDL_AtomicInt done;
    SDL_AtomicInt cancel;
    int kind;
    size_t limit;
    BongoCatAboutFeed *feed;
    unsigned char *qr_pixels;
    char *response;
    size_t length, capacity;
    int status;
    char cache_directory[BONGO_CAT_PATH_CAP];
    bool network_only, refresh_needed;
    Uint32 event_type;
    Uint32 window_id;
} BongoCatAboutRequest;

BongoCatAboutRequest *bongo_cat_about_request(int kind, Uint32 event_type,
    Uint32 window_id, const char *cache_root, bool network_only);
void bongo_cat_about_request_free(BongoCatAboutRequest *request);

#endif
