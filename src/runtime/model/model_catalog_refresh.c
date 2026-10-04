#include "runtime.h"
#include "model_import.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BongoCatModelRefreshJob {
    struct BongoCatModelRefresh *owner;
    BongoCatModelCatalog models;
    char asset_root[BONGO_CAT_PATH_CAP];
    char settings_path[BONGO_CAT_PATH_CAP];
    char session_path[BONGO_CAT_PATH_CAP];
    char models_root[BONGO_CAT_PATH_CAP];
    char cache_root[BONGO_CAT_PATH_CAP];
    char nearby_root[BONGO_CAT_PATH_CAP];
    char active_model_id[BONGO_CAT_ID_CAP];
    char package_id[BONGO_CAT_ID_CAP];
    BongoCatRemovedModel removed_models[BONGO_CAT_MODEL_CAP];
    size_t removed_model_count;
    uint64_t started_ns;
    uint64_t revision;
    BongoCatError error;
    bool success;
} BongoCatModelRefreshJob;

struct BongoCatModelRefresh {
    SDL_Mutex *mutex;
    SDL_Thread *worker;
    BongoCatModelRefreshJob *completed;
    Uint32 event_type;
    uint64_t revision;
    bool busy;
    bool active_nearby;
    bool rerun_full;
    bool rerun_nearby;
    size_t pending_package_count;
    char pending_packages[BONGO_CAT_MODEL_CAP][BONGO_CAT_ID_CAP];
};

static BongoCatModelRefresh *create_refresh(void) {
    BongoCatModelRefresh *refresh = calloc(1, sizeof(*refresh));
    if (!refresh) return NULL;
    refresh->mutex = SDL_CreateMutex();
    refresh->event_type = SDL_RegisterEvents(1);
    if (!refresh->mutex || refresh->event_type == (Uint32)-1) {
        if (refresh->mutex) SDL_DestroyMutex(refresh->mutex);
        free(refresh);
        return NULL;
    }
    return refresh;
}

static int SDLCALL refresh_worker(void *userdata) {
    BongoCatModelRefreshJob *job = userdata;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot lower model catalog thread priority: %s", SDL_GetError());
        SDL_ClearError();
    }
    BongoCatApp *scan = calloc(1, sizeof(*scan));
    if (scan) {
        snprintf(scan->asset_root, sizeof(scan->asset_root), "%s",
            job->asset_root);
        snprintf(scan->settings_path, sizeof(scan->settings_path), "%s",
            job->settings_path);
        snprintf(scan->session_path, sizeof(scan->session_path), "%s",
            job->session_path);
        snprintf(scan->models_root, sizeof(scan->models_root), "%s",
            job->models_root);
        snprintf(scan->cache_root, sizeof(scan->cache_root), "%s",
            job->cache_root);
        snprintf(scan->session.active_model_id,
            sizeof(scan->session.active_model_id), "%s",
            job->active_model_id);
        scan->settings.removed_model_count = job->removed_model_count;
        memcpy(scan->settings.removed_models, job->removed_models,
            job->removed_model_count * sizeof(job->removed_models[0]));
        scan->models = job->models;
        BongoCatResult result = BONGO_CAT_OK;
        if (job->package_id[0])
            result = bongo_cat_import_installed_package(scan,
                job->models_root, job->package_id, &job->error);
        else result = bongo_cat_model_catalog_scan(scan, false,
            job->nearby_root);
        job->models = scan->models;
        job->success = result == BONGO_CAT_OK;
        free(scan);
    }

    BongoCatModelRefresh *refresh = job->owner;
    SDL_LockMutex(refresh->mutex);
    refresh->completed = job;
    SDL_UnlockMutex(refresh->mutex);

    SDL_Event event = {0};
    event.type = refresh->event_type;
    event.user.data1 = refresh;
    SDL_PushEvent(&event);
    return 0;
}

static bool start_refresh(BongoCatApp *app, bool include_nearby,
    const char *package_id) {
    BongoCatModelRefresh *refresh = app->model_refresh;
    BongoCatModelRefreshJob *job = calloc(1, sizeof(*job));
    if (!job) return false;
    job->owner = refresh;
    job->started_ns = SDL_GetTicksNS();
    job->revision = refresh->revision;
    if (package_id) job->models = app->models;
    snprintf(job->asset_root, sizeof(job->asset_root), "%s", app->asset_root);
    snprintf(job->settings_path, sizeof(job->settings_path), "%s", app->settings_path);
    snprintf(job->session_path, sizeof(job->session_path), "%s", app->session_path);
    snprintf(job->models_root, sizeof(job->models_root), "%s", app->models_root);
    snprintf(job->cache_root, sizeof(job->cache_root), "%s", app->cache_root);
    if (include_nearby)
        snprintf(job->nearby_root, sizeof(job->nearby_root), "%s",
            app->nearby_root);
    if (package_id)
        snprintf(job->package_id, sizeof(job->package_id), "%s", package_id);
    snprintf(job->active_model_id, sizeof(job->active_model_id), "%s",
        app->session.active_model_id);
    job->removed_model_count = app->settings.removed_model_count;
    if (job->removed_model_count > BONGO_CAT_MODEL_CAP)
        job->removed_model_count = BONGO_CAT_MODEL_CAP;
    memcpy(job->removed_models, app->settings.removed_models,
        job->removed_model_count * sizeof(job->removed_models[0]));
    refresh->busy = true;
    refresh->active_nearby = include_nearby;
    refresh->worker = SDL_CreateThread(refresh_worker,
        BONGO_CAT_SLUG "-model-catalog", job);
    if (refresh->worker) return true;
    refresh->busy = false;
    free(job);
    return false;
}

static void request_refresh(BongoCatApp *app, bool include_nearby,
    const char *package_id) {
    if (!app) return;
    if (!app->model_refresh) app->model_refresh = create_refresh();
    BongoCatModelRefresh *refresh = app->model_refresh;
    if (!refresh) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Background model refresh is unavailable: %s", SDL_GetError());
        return;
    }
    if (refresh->busy) {
        if (!package_id || include_nearby) {
            refresh->rerun_full = true;
            refresh->rerun_nearby = refresh->rerun_nearby || include_nearby;
            refresh->pending_package_count = 0;
            return;
        }
        if (refresh->rerun_full) return;
        for (size_t i = 0; i < refresh->pending_package_count; ++i)
            if (!strcmp(refresh->pending_packages[i], package_id)) return;
        if (refresh->pending_package_count < BONGO_CAT_MODEL_CAP)
            snprintf(refresh->pending_packages[
                refresh->pending_package_count++], BONGO_CAT_ID_CAP,
                "%s", package_id);
        else {
            refresh->rerun_full = true;
            refresh->pending_package_count = 0;
        }
        return;
    }
    if (!start_refresh(app, include_nearby, package_id))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot start background model refresh: %s", SDL_GetError());
}

void bongo_cat_app_request_model_refresh(BongoCatApp *app) {
    request_refresh(app, false, NULL);
}

bool bongo_cat_app_model_refresh_busy(const BongoCatApp *app) {
    return app && app->model_refresh && app->model_refresh->busy;
}

void bongo_cat_app_request_model_package_refresh(BongoCatApp *app,
    const char *package_id) {
    if (!package_id || !package_id[0]) return;
    request_refresh(app, false, package_id);
}

void bongo_cat_app_request_nearby_model_refresh(BongoCatApp *app) {
    if (SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN")) return;
    request_refresh(app, true, NULL);
}

void bongo_cat_model_refresh_invalidate(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh) return;
    refresh->revision++;
    if (refresh->busy) {
        refresh->rerun_full = true;
        refresh->rerun_nearby = refresh->rerun_nearby ||
            refresh->active_nearby;
        refresh->pending_package_count = 0;
    }
}

void bongo_cat_model_refresh_update(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh || !refresh->busy) return;
    SDL_LockMutex(refresh->mutex);
    BongoCatModelRefreshJob *job = refresh->completed;
    refresh->completed = NULL;
    SDL_UnlockMutex(refresh->mutex);
    if (!job) return;

    if (refresh->worker) SDL_WaitThread(refresh->worker, NULL);
    refresh->worker = NULL;
    refresh->busy = false;
    if (job->success && job->revision == refresh->revision) {
        bool changed = app->models.count != job->models.count ||
            memcmp(app->models.entries, job->models.entries,
                job->models.count * sizeof(job->models.entries[0])) != 0;
        if (changed) {
            app->models = job->models;
            if (job->package_id[0])
                bongo_cat_model_catalog_finish_package(app, job->package_id);
            else bongo_cat_model_catalog_finish(app);
            bongo_cat_preferences_models_changed(app->preferences);
        }
        SDL_Log("Background model refresh completed: models=%llu changed=%d "
            "elapsed_ms=%.1f", (unsigned long long)job->models.count,
            changed, (SDL_GetTicksNS() - job->started_ns) / 1000000.0);
    } else if (!job->success)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Background model refresh failed: %s",
            job->error.message[0] ? job->error.message : "not enough memory");
    free(job);
    bool rerun_full = refresh->rerun_full;
    bool include_nearby = refresh->rerun_nearby;
    char package_id[BONGO_CAT_ID_CAP];
    package_id[0] = '\0';
    if (rerun_full) {
        refresh->rerun_full = false;
        refresh->rerun_nearby = false;
        refresh->pending_package_count = 0;
    } else if (refresh->pending_package_count) {
        snprintf(package_id, sizeof(package_id), "%s",
            refresh->pending_packages[0]);
        memmove(refresh->pending_packages, refresh->pending_packages + 1,
            (refresh->pending_package_count - 1) *
                sizeof(refresh->pending_packages[0]));
        --refresh->pending_package_count;
    }
    if ((rerun_full || package_id[0]) && !start_refresh(app, include_nearby,
            package_id[0] ? package_id : NULL))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot restart background model refresh: %s", SDL_GetError());
    bongo_cat_preferences_invalidate(app->preferences);
}

bool bongo_cat_model_refresh_event(BongoCatApp *app,
    const SDL_Event *event) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh || !event || event->type != refresh->event_type ||
        event->user.data1 != refresh) return false;
    bongo_cat_model_refresh_update(app);
    return true;
}

void bongo_cat_model_refresh_shutdown(BongoCatApp *app) {
    BongoCatModelRefresh *refresh = app ? app->model_refresh : NULL;
    if (!refresh) return;
    if (refresh->worker) SDL_WaitThread(refresh->worker, NULL);
    SDL_LockMutex(refresh->mutex);
    BongoCatModelRefreshJob *job = refresh->completed;
    refresh->completed = NULL;
    SDL_UnlockMutex(refresh->mutex);
    free(job);
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, refresh->event_type,
        refresh->event_type) > 0) {}
    SDL_DestroyMutex(refresh->mutex);
    free(refresh);
    app->model_refresh = NULL;
}
