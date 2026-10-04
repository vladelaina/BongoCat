#ifndef BONGO_CAT_IMAGE_UPLOAD_SYNC_H
#define BONGO_CAT_IMAGE_UPLOAD_SYNC_H

#include "bongo_cat/image.h"
#include <SDL3/SDL_opengl.h>

/* Owned by the texture loader/job and used only on its GL context. At most one
   transfer is in flight, so the staging strip is never renamed while busy. */
typedef struct BongoCatImageUploadSync {
    GLsync fence;
    PFNGLFENCESYNCPROC fence_sync;
    PFNGLCLIENTWAITSYNCPROC client_wait;
    PFNGLDELETESYNCPROC delete_sync;
    bool initialized;
    bool blocking_only;
    uint64_t submitted_ns;
    unsigned deferred, blocking;
} BongoCatImageUploadSync;

/* NULL requests a bounded, synchronous transfer. Drivers without
   sync objects also finish synchronously before the staging strip is reused. */
GLenum bongo_cat_image_upload_sync_submit(BongoCatImageUploadSync *sync);
/* Zero-timeout query: 0 = still busy, 1 = ready, -1 = driver failure.
   A fence stuck for a second recovers via glFinish and disables further
   fences for this upload owner. Never reuse the strip without completion. */
int bongo_cat_image_upload_sync_poll(BongoCatImageUploadSync *sync,
    BongoCatError *error);
/* Initial loading waits only before staging reuse/finalization. Keep native
   event pumping alive via progress and yield CPU time between queries. */
bool bongo_cat_image_upload_sync_wait(BongoCatImageUploadSync *sync,
    BongoCatImageProgress progress, void *userdata, float progress_value,
    BongoCatError *error);
void bongo_cat_image_upload_sync_discard(BongoCatImageUploadSync *sync);
/* Retire a cancelled transfer before its destination/staging objects are
   destroyed. This waits only when a deferred fence exists. */
void bongo_cat_image_upload_sync_retire(BongoCatImageUploadSync *sync);

#endif
