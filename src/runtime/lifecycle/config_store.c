#include "runtime.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

#define SAVE_DELAY_NS 300000000ull
#define RETRY_DELAY_NS 1000000000ull

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t settings_hash(const BongoCatSettings *settings) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(settings->model);
    HASH_FIELD(settings->window);
    HASH_FIELD(settings->app);
    HASH_FIELD(settings->shortcuts);
    HASH_FIELD(settings->behavior_shortcut_count);
    size_t behavior_count = settings->behavior_shortcut_count;
    if (behavior_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    hash = hash_bytes(hash, settings->behavior_shortcuts,
        behavior_count * sizeof(settings->behavior_shortcuts[0]));
    HASH_FIELD(settings->model_label_count);
    size_t model_count = settings->model_label_count;
    if (model_count > BONGO_CAT_MODEL_CAP) model_count = BONGO_CAT_MODEL_CAP;
    hash = hash_bytes(hash, settings->model_labels,
        model_count * sizeof(settings->model_labels[0]));
    HASH_FIELD(settings->removed_model_count);
    size_t removed_count = settings->removed_model_count;
    if (removed_count > BONGO_CAT_MODEL_CAP)
        removed_count = BONGO_CAT_MODEL_CAP;
    hash = hash_bytes(hash, settings->removed_models,
        removed_count * sizeof(settings->removed_models[0]));
    HASH_FIELD(settings->extensions_json);
#undef HASH_FIELD
    return hash;
}

static uint64_t session_hash(const BongoCatSessionState *session) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(session->window.visible);
    HASH_FIELD(session->window.position_known);
    HASH_FIELD(session->window.scale_percent);
    HASH_FIELD(session->window.opacity_percent);
    HASH_FIELD(session->window.x);
    HASH_FIELD(session->window.y);
    HASH_FIELD(session->window.width);
    HASH_FIELD(session->window.height);
    HASH_FIELD(session->window.content_width);
    HASH_FIELD(session->window.content_height);
    HASH_FIELD(session->active_model_id);
    HASH_FIELD(session->last_update_check_day);
    HASH_FIELD(session->last_update_check_version);
    HASH_FIELD(session->available_update_version);
    HASH_FIELD(session->additional_model_count);
    size_t model_count = session->additional_model_count;
    if (model_count > BONGO_CAT_ADDITIONAL_MODEL_CAP)
        model_count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    hash = hash_bytes(hash, session->additional_model_ids,
        model_count * sizeof(session->additional_model_ids[0]));
    HASH_FIELD(session->active_behavior_count);
    size_t behavior_count = session->active_behavior_count;
    if (behavior_count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    hash = hash_bytes(hash, session->active_behaviors,
        behavior_count * sizeof(session->active_behaviors[0]));
#undef HASH_FIELD
    return hash;
}

static bool reject_configuration(const char *path, char *rejected,
    size_t capacity) {
    for (unsigned index = 0; index < 1000; ++index) {
        int length = index ? snprintf(rejected, capacity, "%s.rejected.%u",
            path, index) : snprintf(rejected, capacity, "%s.rejected", path);
        if (length < 0 || (size_t)length >= capacity) return false;
        if (bongo_cat_path_is_file(rejected) ||
            bongo_cat_path_is_dir(rejected)) continue;
        return bongo_cat_path_rename(path, rejected);
    }
    return false;
}

static void handle_rejected_configuration(const char *description,
    const char *path, BongoCatResult result, bool *blocked) {
    if (result != BONGO_CAT_ERROR_FORMAT || !bongo_cat_path_is_file(path)) {
        *blocked = bongo_cat_path_is_file(path);
        return;
    }
    char rejected[BONGO_CAT_PATH_CAP];
    if (reject_configuration(path, rejected, sizeof(rejected))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "%s preserved as rejected file: %s", description, rejected);
        return;
    }
    *blocked = true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "%s will not be overwritten because it could not be preserved: %s",
        description, path);
}

void bongo_cat_config_store_load(BongoCatApp *app) {
    app->settings_store_valid = false;
    app->session_store_valid = false;
    app->settings_store_blocked = false;
    app->session_store_blocked = false;
    BongoCatError error = {0};
    BongoCatResult loaded = bongo_cat_settings_load(
        app->settings_path, &app->settings, &error);
    app->settings_store_valid = loaded == BONGO_CAT_OK &&
        bongo_cat_path_is_file(app->settings_path);
    bool language_detected = false;
    if (!app->settings_store_valid) {
        /* System locale is only a first-run/recovery fallback; a valid
           settings file always wins so an explicit user choice persists. */
        BongoCatLanguage detected_language = app->settings.app.language;
        language_detected = bongo_cat_system_language(&detected_language);
        if (language_detected) app->settings.app.language = detected_language;
    }
    if (language_detected)
        SDL_Log("[runtime] Initial language selected from system locale: %s",
            bongo_cat_language_name(app->settings.app.language));
    if (loaded != BONGO_CAT_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Settings ignored: %s",
            error.message);
        handle_rejected_configuration("Settings", app->settings_path,
            loaded, &app->settings_store_blocked);
    }
    error = (BongoCatError){0};
    loaded = bongo_cat_session_load(
        app->session_path, &app->session, &error);
    app->session_store_valid = loaded == BONGO_CAT_OK &&
        bongo_cat_path_is_file(app->session_path);
    if (loaded != BONGO_CAT_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Session ignored: %s",
            error.message);
        handle_rejected_configuration("Session", app->session_path,
            loaded, &app->session_store_blocked);
    }
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    app->settings_observed_hash = settings;
    app->session_observed_hash = session;
    app->settings_saved_hash = app->settings_store_valid ||
        app->settings_store_blocked ? settings : ~settings;
    app->session_saved_hash = app->session_store_valid ||
        app->session_store_blocked ? session : ~session;
}

static bool save_settings(BongoCatApp *app) {
    bongo_cat_settings_validate(&app->settings);
    uint64_t hash = settings_hash(&app->settings);
    BongoCatError error = {0};
    if (bongo_cat_settings_save(app->settings_path,
        &app->settings, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->settings_saved_hash = hash;
    app->settings_observed_hash = hash;
    app->settings_store_valid = true;
    return true;
}

static bool save_session(BongoCatApp *app) {
    bongo_cat_session_validate(&app->session);
    uint64_t hash = session_hash(&app->session);
    BongoCatError error = {0};
    if (bongo_cat_session_save(app->session_path,
        &app->session, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->session_saved_hash = hash;
    app->session_observed_hash = hash;
    app->session_store_valid = true;
    return true;
}

void bongo_cat_config_store_update(BongoCatApp *app, uint64_t now) {
    if (!app || app->smoke || !app->settings_path[0] || !app->session_path[0]) return;
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    if (!app->secondary_pet && !app->settings_store_blocked) {
        if (settings != app->settings_observed_hash) {
            app->settings_observed_hash = settings;
            app->settings_save_due_ns = now + SAVE_DELAY_NS;
        } else if (settings != app->settings_saved_hash &&
            !app->settings_save_due_ns)
            app->settings_save_due_ns = now + SAVE_DELAY_NS;
    }
    if (!app->session_store_blocked) {
        if (session != app->session_observed_hash) {
            app->session_observed_hash = session;
            app->session_save_due_ns = now + SAVE_DELAY_NS;
        } else if (session != app->session_saved_hash &&
            !app->session_save_due_ns)
            app->session_save_due_ns = now + SAVE_DELAY_NS;
    }
    if (!app->secondary_pet && !app->settings_store_blocked &&
        app->settings_save_due_ns &&
        now >= app->settings_save_due_ns)
        app->settings_save_due_ns = save_settings(app)
            ? 0 : now + RETRY_DELAY_NS;
    if (!app->session_store_blocked && app->session_save_due_ns &&
        now >= app->session_save_due_ns)
        app->session_save_due_ns = save_session(app)
            ? 0 : now + RETRY_DELAY_NS;
}

void bongo_cat_config_store_flush(BongoCatApp *app) {
    if (!app || app->smoke || !app->settings_path[0] || !app->session_path[0]) return;
    uint64_t settings = settings_hash(&app->settings);
    uint64_t session = session_hash(&app->session);
    if (!app->secondary_pet && !app->settings_store_blocked &&
        settings != app->settings_saved_hash) save_settings(app);
    if (!app->session_store_blocked &&
        session != app->session_saved_hash) save_session(app);
    app->settings_observed_hash = settings_hash(&app->settings);
    app->session_observed_hash = session_hash(&app->session);
    app->settings_save_due_ns = app->session_save_due_ns = 0;
}
