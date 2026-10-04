#ifndef BONGO_CAT_PREFERENCES_IMPORT_INTERNAL_H
#define BONGO_CAT_PREFERENCES_IMPORT_INTERNAL_H

#include "preferences_internal.h"
#include "model_import.h"

typedef struct BongoCatImportSummary {
    char ids[BONGO_CAT_MODEL_CAP][BONGO_CAT_ID_CAP];
    bool imported[BONGO_CAT_MODEL_CAP];
    size_t count;
    size_t failed_count;
    size_t failed_name_count;
    char failed_names[BONGO_CAT_IMPORT_FAILURE_NAME_CAP][BONGO_CAT_ID_CAP];
    BongoCatError error;
} BongoCatImportSummary;

typedef struct BongoCatImportJob {
    struct BongoCatImportJob *next;
    SDL_WindowID window_id;
    size_t count;
    char **paths;
    char models_root[BONGO_CAT_PATH_CAP];
    char package_ids[BONGO_CAT_MODEL_CAP][BONGO_CAT_ID_CAP];
    bool package_refresh_requested[BONGO_CAT_MODEL_CAP];
    bool package_imported[BONGO_CAT_MODEL_CAP];
    size_t package_id_count;
    size_t resolved_count;
    size_t installed_count;
    size_t restored_count;
    size_t succeeded_count;
    size_t failed_count;
    size_t failed_name_count;
    char failed_names[BONGO_CAT_IMPORT_FAILURE_NAME_CAP][BONGO_CAT_ID_CAP];
    BongoCatResult result;
    BongoCatError error;
    BongoCatImportDialog *dialog;
} BongoCatImportJob;

typedef struct BongoCatImportProgressContext {
    BongoCatImportJob *job;
    size_t completed;
} BongoCatImportProgressContext;

typedef struct BongoCatImportProgress {
    BongoCatImportJob *job;
    char package_id[BONGO_CAT_ID_CAP];
    size_t package_index;
    size_t installed_count;
} BongoCatImportProgress;

enum {
    BONGO_CAT_IMPORT_EVENT_CODE = 0x42434e49,
    BONGO_CAT_IMPORT_PROGRESS_CODE = 0x42434e4a,
    BONGO_CAT_IMPORT_COMPLETE_CODE = 0x42434e4b
};

struct BongoCatImportDialog {
    SDL_Mutex *mutex;
    Uint32 event_type;
    SDL_WindowID window_id;
    SDL_Thread *worker;
    BongoCatImportJob *worker_job;
    BongoCatImportJob *pending_head;
    BongoCatImportJob *pending_tail;
    BongoCatImportSummary summary;
    uint64_t started_ns;
    size_t completed;
    size_t total;
    int references;
    bool active;
    bool open;
    bool busy;
};

void bongo_cat_preferences_import_report_progress(BongoCatImportJob *job,
    const BongoCatImportReceipt *receipt, size_t package_index,
    size_t completed);
void bongo_cat_preferences_import_receive(void *userdata,
    const BongoCatImportReceipt *receipt);
void bongo_cat_preferences_import_record_failure(BongoCatImportJob *job,
    const char *source);
void bongo_cat_preferences_import_merge_failures(BongoCatImportJob *job,
    const BongoCatImportBatchStats *stats);
bool bongo_cat_preferences_import_progress_event(
    BongoCatImportDialog *dialog, BongoCatApp *app, const SDL_Event *event);
void bongo_cat_preferences_import_job_free(BongoCatImportJob *job);
void bongo_cat_preferences_import_dialog_release(
    BongoCatImportDialog *dialog);
int SDLCALL bongo_cat_preferences_import_worker(void *userdata);
void bongo_cat_preferences_import_merge(BongoCatImportSummary *summary,
    const BongoCatImportJob *job);
void bongo_cat_preferences_import_message(BongoCatApp *app,
    const BongoCatImportSummary *summary, char *message, size_t capacity);
void bongo_cat_preferences_import_complete(BongoCatApp *app,
    const BongoCatImportSummary *summary);

#endif
