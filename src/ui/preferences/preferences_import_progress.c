#include "preferences_import_internal.h"
#include "preferences_model_glyphs.h"
#include "model_import.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

void bongo_cat_preferences_import_record_failure(BongoCatImportJob *job,
    const char *source) {
    if (!job || job->failed_name_count >=
        BONGO_CAT_IMPORT_FAILURE_NAME_CAP) return;
    const char *name = bongo_cat_path_name(source);
    if (!name || !name[0]) return;
    SDL_utf8strlcpy(job->failed_names[job->failed_name_count++], name,
        BONGO_CAT_ID_CAP);
}

void bongo_cat_preferences_import_merge_failures(BongoCatImportJob *job,
    const BongoCatImportBatchStats *stats) {
    if (!job || !stats) return;
    for (size_t i = 0; i < stats->failure_name_count; ++i)
        bongo_cat_preferences_import_record_failure(job,
            stats->failure_names[i]);
}

static size_t remember_package(BongoCatImportJob *job, const char *id) {
    if (!job || !id || !id[0]) return BONGO_CAT_MODEL_CAP;
    for (size_t i = 0; i < job->package_id_count; ++i)
        if (!strcmp(job->package_ids[i], id)) return i;
    if (job->package_id_count >= BONGO_CAT_MODEL_CAP)
        return BONGO_CAT_MODEL_CAP;
    size_t index = job->package_id_count++;
    snprintf(job->package_ids[index], BONGO_CAT_ID_CAP, "%s", id);
    return index;
}

void bongo_cat_preferences_import_receive(void *userdata,
    const BongoCatImportReceipt *receipt) {
    BongoCatImportProgressContext *progress = userdata;
    BongoCatImportJob *job = progress->job;
    size_t package_index = receipt->count
        ? remember_package(job, receipt->ids[0]) : BONGO_CAT_MODEL_CAP;
    job->resolved_count += receipt->count;
    job->installed_count += receipt->installed_count;
    job->result = BONGO_CAT_OK;
    bongo_cat_preferences_import_report_progress(job, receipt, package_index,
        ++progress->completed);
}

void bongo_cat_preferences_import_report_progress(BongoCatImportJob *job,
    const BongoCatImportReceipt *receipt, size_t package_index,
    size_t completed) {
    BongoCatImportProgress *progress = receipt && receipt->count
        ? SDL_calloc(1, sizeof(*progress)) : NULL;
    if (progress) {
        progress->job = job;
        progress->package_index = package_index;
        progress->installed_count = receipt->installed_count;
        bongo_cat_import_package_base_id(progress->package_id,
            sizeof(progress->package_id), receipt->ids[0]);
    }
    SDL_Event event = {0};
    SDL_LockMutex(job->dialog->mutex);
    bool current = job->dialog->active &&
        job->dialog->worker_job == job;
    if (current) {
        job->dialog->completed = completed;
        if (job->dialog->total < completed)
            job->dialog->total = completed;
    }
    bool pushed = false;
    if (current && progress) {
        event.type = job->dialog->event_type;
        event.user.windowID = job->dialog->window_id;
        event.user.code = BONGO_CAT_IMPORT_PROGRESS_CODE;
        event.user.data1 = progress;
        event.user.data2 = job->dialog;
        pushed = SDL_PushEvent(&event);
    }
    SDL_UnlockMutex(job->dialog->mutex);
    if (!pushed) SDL_free(progress);
}

bool bongo_cat_preferences_import_progress_event(
    BongoCatImportDialog *dialog, BongoCatApp *app, const SDL_Event *event) {
    if (!dialog || !event ||
        event->user.code != BONGO_CAT_IMPORT_PROGRESS_CODE) return false;
    BongoCatImportProgress *progress =
        (BongoCatImportProgress *)event->user.data1;
    SDL_LockMutex(dialog->mutex);
    bool current = progress && dialog->active &&
        dialog->worker_job == progress->job;
    SDL_UnlockMutex(dialog->mutex);
    if (current && app) {
        bool restored = bongo_cat_settings_restore_model_package(
            &app->settings, progress->package_id);
        if (restored) progress->job->restored_count++;
        if (progress->installed_count || restored)
            bongo_cat_preferences_model_glyphs_note(app->preferences,
                progress->package_id);
        if (progress->installed_count || restored) {
            if (progress->package_index < BONGO_CAT_MODEL_CAP)
                progress->job->package_refresh_requested[
                    progress->package_index] = true;
            bongo_cat_app_request_model_package_refresh(app,
                progress->package_id);
        }
    }
    SDL_free(progress);
    return true;
}
