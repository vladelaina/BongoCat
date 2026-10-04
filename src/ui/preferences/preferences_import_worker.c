#include "preferences_import_internal.h"

#include <SDL3/SDL.h>

int SDLCALL bongo_cat_preferences_import_worker(void *userdata) {
    BongoCatImportJob *job = userdata;
    if (!SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot lower model import thread priority: %s", SDL_GetError());
        SDL_ClearError();
    }
    job->result = BONGO_CAT_ERROR_FORMAT;
    SDL_Log("[runtime] Model import started: sources=%llu",
        (unsigned long long)job->count);
    BongoCatImportSession *session = bongo_cat_import_session_create(
        job->models_root, &job->error);
    if (!session) {
        job->result = job->error.code ? job->error.code : BONGO_CAT_ERROR_IO;
        if (job->error.code == BONGO_CAT_OK) job->error.code = job->result;
        job->failed_count = job->count;
        for (size_t i = 0; i < job->count; ++i)
            bongo_cat_preferences_import_record_failure(job, job->paths[i]);
    }
    BongoCatImportProgressContext progress = {job, 0};
    for (size_t i = 0; session && i < job->count; ++i) {
        size_t before = progress.completed;
        BongoCatImportBatchStats stats = {0};
        BongoCatError error = {0};
        SDL_Log("[runtime] Model import source started: index=%llu/%llu path=%s",
            (unsigned long long)i + 1ULL, (unsigned long long)job->count,
            job->paths[i]);
        BongoCatResult result = bongo_cat_import_session_install_progressive(
            session, job->paths[i], bongo_cat_preferences_import_receive,
            &progress, &stats, &error);
        SDL_Log("[runtime] Model import source completed: index=%llu/%llu "
            "result=%d succeeded=%llu failed=%llu",
            (unsigned long long)i + 1ULL, (unsigned long long)job->count,
            (int)result, (unsigned long long)stats.succeeded_count,
            (unsigned long long)stats.failed_count);
        job->succeeded_count += stats.succeeded_count;
        job->failed_count += stats.failed_count;
        bongo_cat_preferences_import_merge_failures(job, &stats);
        if (result != BONGO_CAT_OK && job->error.code == BONGO_CAT_OK) {
            job->result = result;
            job->error = error;
            if (job->error.code == BONGO_CAT_OK) job->error.code = result;
        }
        if (stats.failed_count) {
            progress.completed += stats.failed_count;
            bongo_cat_preferences_import_report_progress(job, NULL,
                BONGO_CAT_MODEL_CAP, progress.completed);
        } else if (progress.completed == before) {
            bongo_cat_preferences_import_report_progress(job, NULL,
                BONGO_CAT_MODEL_CAP, ++progress.completed);
        }
    }
    bongo_cat_import_session_destroy(session);
    if (job->succeeded_count) job->result = BONGO_CAT_OK;
    if (!job->succeeded_count && job->error.code == BONGO_CAT_OK)
        bongo_cat_error_set(&job->error, BONGO_CAT_ERROR_FORMAT,
            "No model could be imported");
    SDL_Log("[runtime] Model import finished: resolved=%llu installed=%llu "
        "succeeded=%llu failed=%llu result=%d",
        (unsigned long long)job->resolved_count,
        (unsigned long long)job->installed_count,
        (unsigned long long)job->succeeded_count,
        (unsigned long long)job->failed_count, (int)job->result);
    SDL_Event event = {0};
    SDL_LockMutex(job->dialog->mutex);
    bool notify = job->dialog->active;
    if (notify) {
        event.type = job->dialog->event_type;
        event.user.windowID = job->window_id;
        event.user.code = BONGO_CAT_IMPORT_COMPLETE_CODE;
        event.user.data1 = job;
        event.user.data2 = job->dialog;
        notify = SDL_PushEvent(&event);
    }
    if (!notify) {
        job->dialog->worker_job = NULL;
        job->dialog->busy = false;
        job->dialog->started_ns = 0;
        job->dialog->completed = job->dialog->total = 0;
    }
    SDL_UnlockMutex(job->dialog->mutex);
    if (!notify) {
        BongoCatImportDialog *dialog = job->dialog;
        bongo_cat_preferences_import_job_free(job);
        bongo_cat_preferences_import_dialog_release(dialog);
    }
    return 0;
}
