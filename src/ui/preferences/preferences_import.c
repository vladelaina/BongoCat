#include "preferences_import_internal.h"
#include "preferences_state.h"
#include "model_import.h"
#ifdef _WIN32
#include "windows_dialog.h"
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void bongo_cat_preferences_import_job_free(BongoCatImportJob *job) {
    if (!job) return;
    for (size_t i = 0; i < job->count; ++i) SDL_free(job->paths[i]);
    SDL_free(job->paths);
    SDL_free(job);
}

static void free_import_event(const SDL_Event *event, bool *release_worker) {
    if (!event) return;
    if (event->user.code == BONGO_CAT_IMPORT_PROGRESS_CODE) {
        SDL_free(event->user.data1);
        return;
    }
    BongoCatImportJob *job = (BongoCatImportJob *)event->user.data1;
    if (release_worker && event->user.code == BONGO_CAT_IMPORT_COMPLETE_CODE)
        *release_worker = true;
    bongo_cat_preferences_import_job_free(job);
}
void bongo_cat_preferences_import_dialog_release(
    BongoCatImportDialog *dialog) {
    bool destroy = false;
    SDL_LockMutex(dialog->mutex);
    destroy = --dialog->references == 0;
    SDL_UnlockMutex(dialog->mutex);
    if (destroy) {
        SDL_DestroyMutex(dialog->mutex);
        SDL_free(dialog);
    }
}
void bongo_cat_preferences_import_destroy(BongoCatImportDialog *dialog) {
    if (!dialog) return;
    SDL_LockMutex(dialog->mutex);
    dialog->active = false;
    dialog->open = false;
    SDL_Thread *worker = dialog->worker;
    SDL_UnlockMutex(dialog->mutex);
    if (worker) SDL_WaitThread(worker, NULL);
    bool release_worker = false;
    SDL_LockMutex(dialog->mutex);
    dialog->worker = NULL;
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, dialog->event_type,
        dialog->event_type) > 0) {
        if (event.user.data2 != dialog) continue;
        free_import_event(&event, &release_worker);
    }
    dialog->worker_job = NULL;
    while (dialog->pending_head) {
        BongoCatImportJob *job = dialog->pending_head;
        dialog->pending_head = job->next;
        bongo_cat_preferences_import_job_free(job);
    }
    dialog->pending_tail = NULL;
    dialog->busy = false;
    dialog->started_ns = 0;
    dialog->completed = dialog->total = 0;
    /* Drop the queued worker reference while the owner still holds dialog. */
    if (release_worker) --dialog->references;
    SDL_UnlockMutex(dialog->mutex);
    bongo_cat_preferences_import_dialog_release(dialog);
}
static BongoCatImportJob *copy_job(const char *const *files) {
    if (!files || !files[0]) return NULL;
    size_t count = 0;
    while (files[count]) ++count;
    BongoCatImportJob *job = SDL_calloc(1, sizeof(*job));
    if (!job) return NULL;
    job->paths = SDL_calloc(count, sizeof(*job->paths));
    if (!job->paths) { SDL_free(job); return NULL; }
    job->count = count;
    for (size_t i = 0; i < count; ++i) {
        job->paths[i] = SDL_strdup(files[i]);
        if (!job->paths[i]) {
            bongo_cat_preferences_import_job_free(job);
            return NULL;
        }
    }
    return job;
}
static BongoCatImportJob *copy_path(const char *path) {
    const char *files[] = {path, NULL};
    return path && path[0] ? copy_job(files) : NULL;
}

static bool start_job(BongoCatImportDialog *dialog, BongoCatApp *app,
    SDL_WindowID window_id, BongoCatImportJob *job) {
    if (!dialog || !app || !job || !app->models_root[0]) return false;
    snprintf(job->models_root, sizeof(job->models_root), "%s", app->models_root);
    job->dialog = dialog;
    job->window_id = window_id;
    SDL_LockMutex(dialog->mutex);
    if (!dialog->active) {
        SDL_UnlockMutex(dialog->mutex);
        return false;
    }
    if (dialog->busy) {
        if (dialog->pending_tail) dialog->pending_tail->next = job;
        else dialog->pending_head = job;
        dialog->pending_tail = job;
        SDL_UnlockMutex(dialog->mutex);
        return true;
    }
    dialog->busy = true;
    dialog->worker_job = job;
    dialog->started_ns = SDL_GetTicksNS();
    dialog->completed = 0;
    dialog->total = job->count;
    ++dialog->references;
    dialog->worker = SDL_CreateThread(bongo_cat_preferences_import_worker,
        BONGO_CAT_SLUG "-model-import", job);
    if (!dialog->worker) {
        dialog->worker_job = NULL;
        dialog->busy = false;
        dialog->started_ns = 0;
        dialog->completed = dialog->total = 0;
        --dialog->references;
    }
    SDL_UnlockMutex(dialog->mutex);
    return dialog->worker != NULL;
}
static void SDLCALL import_callback(void *userdata, const char *const *files,
    int filter) {
    (void)filter;
    BongoCatImportDialog *dialog = userdata;
    BongoCatImportJob *job = copy_job(files);
    SDL_Event event = {0};
    bool pushed = false;
    SDL_LockMutex(dialog->mutex);
    event.type = dialog->event_type;
    event.user.windowID = dialog->window_id;
    event.user.code = BONGO_CAT_IMPORT_EVENT_CODE;
    event.user.data1 = job;
    event.user.data2 = dialog;
    if (dialog->active && job) pushed = SDL_PushEvent(&event);
    if (!dialog->active)
        SDL_Log("[runtime] Folder dialog result ignored during shutdown");
    if (!pushed) dialog->open = false;
    SDL_UnlockMutex(dialog->mutex);
    if (!pushed) bongo_cat_preferences_import_job_free(job);
    bongo_cat_preferences_import_dialog_release(dialog);
}

bool bongo_cat_preferences_import_open(BongoCatImportDialog *dialog,
    SDL_Window *window) {
    if (!dialog || !window) return false;
    SDL_LockMutex(dialog->mutex);
    if (!dialog->active || dialog->open) {
        SDL_UnlockMutex(dialog->mutex);
        return false;
    }
    dialog->open = true;
    dialog->window_id = SDL_GetWindowID(window);
    ++dialog->references;
    SDL_UnlockMutex(dialog->mutex);
#ifdef _WIN32
    bongo_cat_windows_show_open_folder_dialog(import_callback, dialog, window,
        NULL, true);
#else
    SDL_ShowOpenFolderDialog(import_callback, dialog, window, NULL, true);
#endif
    return true;
}

static void start_pending_jobs(BongoCatImportDialog *dialog, BongoCatApp *app) {
    for (;;) {
        SDL_LockMutex(dialog->mutex);
        BongoCatImportJob *job = dialog->active && !dialog->busy
            ? dialog->pending_head : NULL;
        if (job) {
            dialog->pending_head = job->next;
            if (!dialog->pending_head) dialog->pending_tail = NULL;
            job->next = NULL;
        }
        SDL_UnlockMutex(dialog->mutex);
        if (!job) return;
        if (start_job(dialog, app, job->window_id, job)) return;
        BongoCatError error = {0};
        bongo_cat_error_set(&error, BONGO_CAT_ERROR_IO,
            "Cannot start model import thread: %s", SDL_GetError());
        for (size_t i = 0; i < job->count; ++i)
            bongo_cat_preferences_import_record_failure(job, job->paths[i]);
        bongo_cat_preferences_import_complete(app, error.code, &error,
            0, 0, 0, job->count, job->failed_names, job->failed_name_count);
        bongo_cat_preferences_import_job_free(job);
    }
}

static void complete_job(BongoCatImportDialog *dialog, BongoCatApp *app,
    BongoCatImportJob *job) {
    SDL_Thread *worker = NULL;
    SDL_LockMutex(dialog->mutex);
    if (dialog->worker_job == job) {
        worker = dialog->worker;
        dialog->worker = NULL;
        dialog->worker_job = NULL;
        dialog->busy = false;
        dialog->started_ns = 0;
        dialog->completed = dialog->total = 0;
    }
    SDL_UnlockMutex(dialog->mutex);
    if (worker) SDL_WaitThread(worker, NULL);
    size_t restored_count = job->restored_count;
    if (job->result == BONGO_CAT_OK)
        for (size_t i = 0; i < job->package_id_count; ++i)
            if (bongo_cat_settings_restore_model_package(&app->settings,
                    job->package_ids[i])) restored_count++;
    bool catalog_changed = job->installed_count > 0 || restored_count > 0;
    if (job->result == BONGO_CAT_OK && catalog_changed)
        for (size_t i = 0; i < job->package_id_count; ++i)
            if (!job->package_refresh_requested[i])
                bongo_cat_app_request_model_package_refresh(app,
                    job->package_ids[i]);
    start_pending_jobs(dialog, app);
    bongo_cat_preferences_import_complete(app, job->result, &job->error,
        job->resolved_count, job->installed_count + restored_count,
        job->succeeded_count, job->failed_count, job->failed_names,
        job->failed_name_count);
    bongo_cat_preferences_import_job_free(job);
    bongo_cat_preferences_import_dialog_release(dialog);
}

bool bongo_cat_preferences_import_event(BongoCatImportDialog *dialog,
    BongoCatApp *app, const SDL_Event *event) {
    if (!dialog || !event || event->type != dialog->event_type ||
        event->user.data2 != dialog) return false;
    BongoCatImportJob *job = (BongoCatImportJob *)event->user.data1;
    if (event->user.code == BONGO_CAT_IMPORT_EVENT_CODE) {
        SDL_Window *owner = SDL_GetWindowFromID(event->user.windowID);
        SDL_LockMutex(dialog->mutex);
        dialog->open = false;
        bool accept = dialog->active && owner != NULL;
        SDL_UnlockMutex(dialog->mutex);
        if (!accept || !app || !job || !start_job(dialog, app,
            event->user.windowID, job))
            bongo_cat_preferences_import_job_free(job);
        return true;
    }
    if (bongo_cat_preferences_import_progress_event(dialog, app, event))
        return true;
    if (event->user.code == BONGO_CAT_IMPORT_COMPLETE_CODE && app && job) {
        complete_job(dialog, app, job);
        return true;
    }
    return false;
}

void bongo_cat_preferences_import_path(BongoCatApp *app,
    SDL_Window *window, const char *path) {
    if (!app || !app->preferences || !window || !path || !path[0]) return;
    BongoCatImportJob *job = copy_path(path);
    if (!job || !start_job(app->preferences->import_dialog, app,
        SDL_GetWindowID(window), job))
        bongo_cat_preferences_import_job_free(job);
    else app->preferences->render_dirty = true;
}
