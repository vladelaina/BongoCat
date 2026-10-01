#include "preferences_state.h"
#include "preferences_notice.h"
#include "model_import.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatApp *app, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(app->i18n, key, fallback);
}

static const char *import_failure_message(BongoCatApp *app,
    BongoCatResult result) {
    switch (result) {
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

void bongo_cat_preferences_import_complete(BongoCatApp *app,
    BongoCatResult result, const BongoCatError *error, size_t resolved_count,
    size_t installed_count, size_t succeeded_count, size_t failed_count,
    const char (*failed_names)[BONGO_CAT_ID_CAP], size_t failed_name_count) {
    if (!app || !app->preferences) return;
    bool failed = failed_count > 0 || result != BONGO_CAT_OK;
    char batch_message[1024];
    char failure_list[BONGO_CAT_IMPORT_FAILURE_NAME_CAP * (BONGO_CAT_ID_CAP + 2)] = "";
    char more_failures[64] = "";
    size_t shown_names = failed_names && failed_name_count >
        BONGO_CAT_IMPORT_FAILURE_NAME_CAP ? BONGO_CAT_IMPORT_FAILURE_NAME_CAP
        : failed_names ? failed_name_count : 0;
    for (size_t i = 0; i < shown_names; ++i) {
        size_t used = strlen(failure_list);
        snprintf(failure_list + used, sizeof(failure_list) - used,
            "%s%s", i ? ", " : "", failed_names[i]);
    }
    size_t listed_count = shown_names;
    if (!failure_list[0]) {
        snprintf(failure_list, sizeof(failure_list), "%s",
            tr(app, "pages.preference.model.hints.importUnknownSource",
                "unknown source"));
        listed_count = failed_count ? 1 : 0;
    }
    if (failed_count > listed_count)
        snprintf(more_failures, sizeof(more_failures), tr(app,
            "pages.preference.model.hints.importMoreFailures",
            " and %zu more"), failed_count - listed_count);
    const char *message = installed_count ? tr(app,
            "pages.preference.model.hints.importSuccess", "Model imported")
        : tr(app, "pages.preference.model.hints.importExists",
            "Model already exists");
    if (failed) {
        BongoCatResult failure = error && error->code != BONGO_CAT_OK
            ? error->code : result;
        snprintf(batch_message, sizeof(batch_message), tr(app,
            "pages.preference.model.hints.importBatchResult",
            "Import succeeded: %zu; failed: %zu.\nFailed models: %s%s.\nFirst failure: %s"),
            succeeded_count, failed_count, failure_list, more_failures,
            import_failure_message(app, failure));
        message = batch_message;
    }
    if (failed && error && error->message[0])
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Model import failed: %s", error->message);
    if (app->smoke) {
        if (failed) app->exit_code = 1;
    } else bongo_cat_preferences_notice_show(app, message, failed);
    if (result == BONGO_CAT_OK)
        SDL_Log("Resolved %zu model package(s); installed %zu new package(s)",
            resolved_count, installed_count);
    bongo_cat_preferences_invalidate(app->preferences);
    bongo_cat_preferences_render(app->preferences);
}
