#include "model_cover.h"

#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum {
    COVER_RETRY_INITIAL_MS = 250,
    COVER_RETRY_MAX_MS = 5000
};

static size_t find_task(const BongoCatApp *app, const char *model_id) {
    if (!app || !model_id || !model_id[0]) return SIZE_MAX;
    for (size_t i = 0; i < app->pending_model_cover_count; ++i)
        if (strcmp(app->pending_model_cover_ids[i], model_id) == 0)
            return i;
    return SIZE_MAX;
}

static void remove_task_at(BongoCatApp *app, size_t index) {
    if (!app || index >= app->pending_model_cover_count) return;
    size_t remaining = app->pending_model_cover_count - index - 1;
    if (remaining) {
        memmove(app->pending_model_cover_ids + index,
            app->pending_model_cover_ids + index + 1,
            remaining * sizeof(app->pending_model_cover_ids[0]));
        memmove(app->pending_model_cover_paths + index,
            app->pending_model_cover_paths + index + 1,
            remaining * sizeof(app->pending_model_cover_paths[0]));
        memmove(app->pending_model_cover_retry_ns + index,
            app->pending_model_cover_retry_ns + index + 1,
            remaining * sizeof(app->pending_model_cover_retry_ns[0]));
        memmove(app->pending_model_cover_attempts + index,
            app->pending_model_cover_attempts + index + 1,
            remaining * sizeof(app->pending_model_cover_attempts[0]));
    }
    app->pending_model_cover_count--;
    app->pending_model_cover_ids[app->pending_model_cover_count][0] = '\0';
    app->pending_model_cover_paths[app->pending_model_cover_count][0] = '\0';
    app->pending_model_cover_retry_ns[app->pending_model_cover_count] = 0;
    app->pending_model_cover_attempts[app->pending_model_cover_count] = 0;
}

static void discard_removed_tasks(BongoCatApp *app) {
    if (!app || !app->models.count) return;
    for (size_t i = 0; i < app->pending_model_cover_count;) {
        if (bongo_cat_models_find(&app->models,
                app->pending_model_cover_ids[i])) {
            ++i;
            continue;
        }
        SDL_Log("Discarding model cover task for removed model: id=%s",
            app->pending_model_cover_ids[i]);
        remove_task_at(app, i);
    }
}

static bool discard_oldest_task(BongoCatApp *app,
    const char *keep_model_id) {
    if (!app) return false;
    size_t candidate = SIZE_MAX;
    for (size_t i = 0; i < app->pending_model_cover_count; ++i) {
        if (strcmp(app->pending_model_cover_ids[i], keep_model_id) == 0)
            continue;
        if (app->pending_model_cover_attempts[i]) {
            candidate = i;
            break;
        }
        if (candidate == SIZE_MAX) candidate = i;
    }
    if (candidate == SIZE_MAX) return false;
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Discarding superseded model cover task: id=%s attempts=%u",
        app->pending_model_cover_ids[candidate],
        app->pending_model_cover_attempts[candidate]);
    remove_task_at(app, candidate);
    return true;
}

void bongo_cat_model_cover_schedule(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    if (!app || !entry || !entry->adapter_directory[0]) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), entry->adapter_directory,
        BONGO_CAT_MODEL_COVER_FILE)) return;
    discard_removed_tasks(app);
    for (size_t i = 0; i < app->pending_model_cover_count; ++i) {
        if (strcmp(app->pending_model_cover_ids[i], entry->id) != 0 &&
            strcmp(app->pending_model_cover_paths[i], path) != 0) continue;
        /* A model use is a fresh request, so clear any previous backoff. */
        snprintf(app->pending_model_cover_ids[i],
            sizeof(app->pending_model_cover_ids[i]), "%s", entry->id);
        snprintf(app->pending_model_cover_paths[i],
            sizeof(app->pending_model_cover_paths[i]), "%s", path);
        app->pending_model_cover_retry_ns[i] = 0;
        app->pending_model_cover_attempts[i] = 0;
        app->dirty = true;
        SDL_Log("Rescheduling model cover refresh: id=%s path=%s",
            entry->id, path);
        return;
    }
    if (app->pending_model_cover_count >= BONGO_CAT_MODEL_COVER_PENDING_CAP &&
        !discard_oldest_task(app, entry->id)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Model cover queue is full; dropping refresh: id=%s", entry->id);
        return;
    }
    size_t index = app->pending_model_cover_count++;
    snprintf(app->pending_model_cover_ids[index],
        sizeof(app->pending_model_cover_ids[index]), "%s", entry->id);
    snprintf(app->pending_model_cover_paths[index],
        sizeof(app->pending_model_cover_paths[index]), "%s", path);
    app->pending_model_cover_retry_ns[index] = 0;
    app->pending_model_cover_attempts[index] = 0;
    SDL_Log("Scheduling model cover refresh: id=%s source=runtime path=%s",
        entry->id, path);
    app->dirty = true;
}

bool bongo_cat_model_cover_pending(const BongoCatApp *app) {
    return find_task(app, app ? app->loaded_model : NULL) != SIZE_MAX;
}

bool bongo_cat_model_cover_capture_due(const BongoCatApp *app, uint64_t now) {
    if (!app) return false;
    size_t index = find_task(app, app->loaded_model);
    return index != SIZE_MAX &&
        (!app->pending_model_cover_retry_ns[index] ||
        now >= app->pending_model_cover_retry_ns[index]);
}

const char *bongo_cat_model_cover_pending_path(const BongoCatApp *app) {
    size_t index = find_task(app, app ? app->loaded_model : NULL);
    return index == SIZE_MAX ? NULL : app->pending_model_cover_paths[index];
}

void bongo_cat_model_cover_defer(BongoCatApp *app, const char *reason) {
    if (!app) return;
    size_t index = find_task(app, app->loaded_model);
    if (index == SIZE_MAX) return;
    unsigned attempts = ++app->pending_model_cover_attempts[index];
    unsigned shift = attempts > 5 ? 5 : attempts - 1;
    uint64_t delay_ms = (uint64_t)COVER_RETRY_INITIAL_MS << shift;
    if (delay_ms > COVER_RETRY_MAX_MS) delay_ms = COVER_RETRY_MAX_MS;
    app->pending_model_cover_retry_ns[index] =
        SDL_GetTicksNS() + delay_ms * 1000000ull;
    app->dirty = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Model cover refresh deferred: model=%s attempt=%u retry_ms=%llu "
        "reason=%s path=%s", app->loaded_model, attempts,
        (unsigned long long)delay_ms, reason && reason[0] ? reason : "unknown",
        app->pending_model_cover_paths[index]);
}

void bongo_cat_model_cover_finish(BongoCatApp *app) {
    size_t index = find_task(app, app ? app->loaded_model : NULL);
    if (index != SIZE_MAX) remove_task_at(app, index);
}
