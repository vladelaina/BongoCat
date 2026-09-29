#include "preferences_import_internal.h"

BongoCatImportDialog *bongo_cat_preferences_import_create(void) {
    BongoCatImportDialog *dialog = SDL_calloc(1, sizeof(*dialog));
    if (!dialog) return NULL;
    dialog->event_type = SDL_RegisterEvents(1);
    dialog->mutex = SDL_CreateMutex();
    if (dialog->event_type == (Uint32)-1 || !dialog->mutex) {
        if (dialog->mutex) SDL_DestroyMutex(dialog->mutex);
        SDL_free(dialog);
        return NULL;
    }
    dialog->references = 1;
    dialog->active = true;
    return dialog;
}

bool bongo_cat_preferences_import_is_open(const BongoCatImportDialog *dialog) {
    if (!dialog) return false;
    SDL_LockMutex(dialog->mutex);
    bool open = dialog->open;
    SDL_UnlockMutex(dialog->mutex);
    return open;
}

bool bongo_cat_preferences_import_status(const BongoCatImportDialog *dialog,
    uint64_t *started_ns, size_t *completed, size_t *total) {
    if (!dialog) return false;
    SDL_LockMutex(dialog->mutex);
    bool busy = dialog->busy || dialog->pending_head != NULL;
    if (started_ns) *started_ns = dialog->started_ns;
    if (completed) *completed = dialog->completed;
    if (total) *total = dialog->total;
    SDL_UnlockMutex(dialog->mutex);
    return busy;
}
