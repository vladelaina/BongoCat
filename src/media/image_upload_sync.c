#include "image_upload_sync.h"
#include "bongo_cat/log.h"
#include <SDL3/SDL.h>

/* A fence that makes no progress must not strand a load or background job.
   This is a fallback threshold, not a deadline for a blocking driver call. */
#define FENCE_FALLBACK_NS 1000000000ull

static void initialize(BongoCatImageUploadSync *sync) {
    if (sync->initialized) return;
    sync->initialized = true;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    /* This driver advertises ARB_sync but, in observed startup and switching
       loads, repeatedly returns TIMEOUT for zero-timeout queries despite a
       flush. Use the previously working bounded completion path upfront. */
    if (renderer && SDL_strstr(renderer, "SVGA3D")) {
        sync->blocking_only = true;
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[texture-sync] mode=blocking reason=svga3d-fence-compatibility renderer=%s",
            renderer);
        return;
    }
    int major = 0, minor = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
    if (!(major > 3 || (major == 3 && minor >= 2)) &&
        !SDL_GL_ExtensionSupported("GL_ARB_sync")) return;
    sync->fence_sync = (PFNGLFENCESYNCPROC)SDL_GL_GetProcAddress("glFenceSync");
    sync->client_wait = (PFNGLCLIENTWAITSYNCPROC)SDL_GL_GetProcAddress("glClientWaitSync");
    sync->delete_sync = (PFNGLDELETESYNCPROC)SDL_GL_GetProcAddress("glDeleteSync");
}

GLenum bongo_cat_image_upload_sync_submit(BongoCatImageUploadSync *sync) {
    if (sync) {
        initialize(sync);
        if (sync->fence) return GL_INVALID_OPERATION;
        if (!sync->blocking_only && sync->fence_sync &&
            sync->client_wait && sync->delete_sync) {
            sync->fence = sync->fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            GLenum status = glGetError();
            if (status != GL_NO_ERROR) return status;
            if (!sync->fence) return GL_INVALID_OPERATION;
            /* Also submit while the main window is static or another window
               owns the next swap. Poll never waits for command completion. */
            glFlush();
            sync->submitted_ns = SDL_GetTicksNS();
            ++sync->deferred;
            return glGetError();
        }
        ++sync->blocking;
    }
    glFinish();
    return glGetError();
}

static int recover_stalled_fence(BongoCatImageUploadSync *sync,
    BongoCatError *error) {
    /* Complete the outstanding copy before releasing its fence or reusing
       the staging strip. Deleting a fence alone would not bound GPU memory. */
    glFinish();
    GLenum status = glGetError();
    bongo_cat_image_upload_sync_discard(sync);
    sync->blocking_only = true;
    ++sync->blocking;
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
        "[texture-sync] mode=blocking reason=fence-stalled recovered=%d gl_error=0x%x",
        status == GL_NO_ERROR, (unsigned)status);
    if (status == GL_NO_ERROR) return 1;
    bongo_cat_error_set(error, status == GL_OUT_OF_MEMORY ?
        BONGO_CAT_ERROR_MEMORY : BONGO_CAT_ERROR_PLATFORM,
        "Texture transfer synchronous recovery failed (GL 0x%x)", (unsigned)status);
    return -1;
}

int bongo_cat_image_upload_sync_poll(BongoCatImageUploadSync *sync,
    BongoCatError *error) {
    if (!sync || !sync->fence) return 1;
    GLenum result = sync->client_wait(sync->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (result == GL_TIMEOUT_EXPIRED) {
        if (SDL_GetTicksNS() - sync->submitted_ns >= FENCE_FALLBACK_NS)
            return recover_stalled_fence(sync, error);
        return 0;
    }
    if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
        bongo_cat_image_upload_sync_discard(sync);
        return 1;
    }
    GLenum status = glGetError();
    /* Query failure is not completion. Finish before dropping the fence so
       even error/cancel cleanup cannot outrun an outstanding transfer. */
    bongo_cat_image_upload_sync_retire(sync);
    sync->blocking_only = true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Texture transfer completion query failed (0x%x, GL 0x%x)",
        (unsigned)result, (unsigned)status);
    return -1;
}

bool bongo_cat_image_upload_sync_wait(BongoCatImageUploadSync *sync,
    BongoCatImageProgress progress, void *userdata, float progress_value,
    BongoCatError *error) {
    uint64_t last_progress = SDL_GetTicksNS();
    for (;;) {
        int ready = bongo_cat_image_upload_sync_poll(sync, error);
        if (ready) return ready > 0;
        uint64_t now = SDL_GetTicksNS();
        if (progress && now - last_progress >= 16000000ull) {
            progress(userdata, progress_value);
            last_progress = SDL_GetTicksNS();
        }
        SDL_Delay(1);
    }
}

void bongo_cat_image_upload_sync_discard(BongoCatImageUploadSync *sync) {
    if (!sync || !sync->fence) return;
    /* Deletes only the query object; this does not complete GPU work.
       Callers must poll/retire before reusing or retiring its resources. */
    sync->delete_sync(sync->fence);
    sync->fence = NULL;
    sync->submitted_ns = 0;
}

void bongo_cat_image_upload_sync_retire(BongoCatImageUploadSync *sync) {
    if (!sync || !sync->fence) return;
    /* A cancelled job is about to delete its partial destination and staging
       strip. Complete the outstanding copy first so the driver cannot keep a
       renamed surface alive after the job has gone away. */
    glFinish();
    bongo_cat_image_upload_sync_discard(sync);
}
