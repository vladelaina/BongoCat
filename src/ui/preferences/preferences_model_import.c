#include "preferences_state.h"
#include "preferences_notice.h"
#include "preferences_import_internal.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/utf8.h"

#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static const char *import_failure_message(BongoCatApp *app,
    BongoCatResult result) {
    switch (result) {
    case BONGO_CAT_ERROR_UNSUPPORTED_ARCHIVE:
        return tr(app, "pages.preference.model.hints.importUnsupportedArchive",
            "Only ZIP archives are supported. Extract the archive and import "
            "the model folder, or repack it as ZIP before importing");
    case BONGO_CAT_ERROR_ARGUMENT:
        return tr(app, "pages.preference.model.hints.importInvalidSource",
            "The selected source no longer exists or cannot be used");
    case BONGO_CAT_ERROR_FORMAT:
        return tr(app, "pages.preference.model.hints.importInvalidFormat",
            "No supported model was found, or the model package is incomplete");
    case BONGO_CAT_ERROR_IO:
        return tr(app, "pages.preference.model.hints.importFileAccess",
            "The model files could not be read or saved");
    case BONGO_CAT_ERROR_MEMORY:
        return tr(app, "pages.preference.model.hints.importOutOfMemory",
            "There is not enough memory to import this model");
    case BONGO_CAT_ERROR_CUBISM:
        return tr(app, "pages.preference.model.hints.importLoadFailed",
            "The model was imported, but it could not be displayed");
    default:
        return tr(app, "pages.preference.model.hints.importFailed",
            "Model import failed. Check the selected files and try again");
    }
}

void bongo_cat_preferences_import_merge(BongoCatImportSummary *summary,
    const BongoCatImportJob *job) {
    /* Main-thread only, after joining the worker. Keep unique package ids
       across queued jobs; repeated imports must not inflate the total. */
    for (size_t i = 0; i < job->package_id_count; ++i) {
        size_t index = 0;
        while (index < summary->count &&
            strcmp(summary->ids[index], job->package_ids[i])) index++;
        if (index == BONGO_CAT_MODEL_CAP) continue;
        if (index == summary->count) {
            SDL_utf8strlcpy(summary->ids[index], job->package_ids[i],
                BONGO_CAT_ID_CAP);
            summary->count++;
        }
        summary->imported[index] |= job->package_imported[i] ||
            job->package_refresh_requested[i];
    }
    summary->failed_count += job->failed_count;
    for (size_t i = 0; i < job->failed_name_count &&
        summary->failed_name_count < BONGO_CAT_IMPORT_FAILURE_NAME_CAP; ++i)
        SDL_utf8strlcpy(summary->failed_names[summary->failed_name_count++],
            job->failed_names[i], BONGO_CAT_ID_CAP);
    if (summary->error.code == BONGO_CAT_OK && job->error.code != BONGO_CAT_OK)
        summary->error = job->error;
}

static void append_name(char *list, size_t capacity, const char *name) {
    /* Bound each label as well as the list, preserving UTF-8 boundaries. */
    char shortened[52];
    SDL_utf8strlcpy(shortened, name, sizeof(shortened) - 3);
    if (strlen(shortened) < strlen(name))
        SDL_strlcat(shortened, "...", sizeof(shortened));
    for (char *cursor = shortened; *cursor; ++cursor)
        if ((unsigned char)*cursor < 0x20 || *cursor == 0x7f) *cursor = ' ';
    size_t used = strlen(list);
    snprintf(list + used, capacity - used, "%s%s", used ? ", " : "", shortened);
}

static void append_more(BongoCatApp *app, char *list, size_t capacity,
    size_t count, size_t shown) {
    if (count <= shown) return;
    size_t used = strlen(list);
    snprintf(list + used, capacity - used, tr(app,
        "pages.preference.model.hints.importMoreFailures", " and %zu more"),
        count - shown);
}

static void append_group(BongoCatApp *app,
    const BongoCatImportSummary *summary, bool imported,
    char *message, size_t capacity) {
    size_t count = 0, shown = 0;
    char names[256] = "";
    for (size_t i = 0; i < summary->count; ++i) {
        if (summary->imported[i] != imported) continue;
        count++;
        if (shown >= 3) continue;
        const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models,
            summary->ids[i]);
        const char *custom = bongo_cat_settings_model_label(&app->settings,
            summary->ids[i]);
        const char *name = entry ? bongo_cat_model_name(&app->settings, entry)
            : custom && custom[0] ? custom : summary->ids[i];
        append_name(names, sizeof(names), name);
        shown++;
    }
    if (!count) return;
    append_more(app, names, sizeof(names), count, shown);
    size_t used = strlen(message);
    if (used && used + 1 < capacity) message[used++] = '\n';
    snprintf(message + used, capacity - used, imported
        ? tr(app, "pages.preference.model.hints.importSuccess",
            "Imported %zu model(s): %s")
        : tr(app, "pages.preference.model.hints.importExists",
            "%zu model(s) already exist: %s"), count, names);
    size_t length = strlen(message);
    while (length && !bongo_cat_utf8_valid(message)) message[--length] = '\0';
}

void bongo_cat_preferences_import_message(BongoCatApp *app,
    const BongoCatImportSummary *summary, char *message, size_t capacity) {
    if (!message || !capacity) return;
    message[0] = '\0';
    append_group(app, summary, true, message, capacity);
    append_group(app, summary, false, message, capacity);
    if (!summary->failed_count) return;
    char names[256] = "";
    size_t shown = 0;
    for (; shown < summary->failed_name_count && shown < 3; ++shown)
        append_name(names, sizeof(names), summary->failed_names[shown]);
    if (!shown) {
        append_name(names, sizeof(names), tr(app,
            "pages.preference.model.hints.importUnknownSource", "unknown source"));
        shown = 1;
    }
    append_more(app, names, sizeof(names), summary->failed_count, shown);
    size_t used = strlen(message);
    if (used && used + 1 < capacity) message[used++] = '\n';
    snprintf(message + used, capacity - used, tr(app,
        "pages.preference.model.hints.importBatchResult",
        "Failed to import %zu model(s): %s.\nFirst failure: %s"),
        summary->failed_count, names,
        import_failure_message(app, summary->error.code));
    /* snprintf may truncate a translated string mid-codepoint. */
    size_t length = strlen(message);
    while (length && !bongo_cat_utf8_valid(message)) message[--length] = '\0';
}

void bongo_cat_preferences_import_complete(BongoCatApp *app,
    const BongoCatImportSummary *summary) {
    if (!app || !app->preferences) return;
    bool failed = summary->failed_count > 0;
    char message[1024];
    bongo_cat_preferences_import_message(app, summary, message, sizeof(message));
    if (failed && summary->error.message[0])
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Model import failed: %s", summary->error.message);
    if (app->smoke) {
        if (failed) app->exit_code = 1;
    } else bongo_cat_preferences_notice_show(app, message, failed);
    SDL_Log("Model import batch finished: models=%zu failed=%zu",
        summary->count, summary->failed_count);
    bongo_cat_preferences_invalidate(app->preferences);
    bongo_cat_preferences_render(app->preferences);
}
