#include "runtime.h"
#include "bongo_cat/path.h"

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

static uint64_t preferences_hash(const BongoCatConfig *config) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(config->model);
    HASH_FIELD(config->window.pass_through);
    HASH_FIELD(config->window.always_on_top);
    HASH_FIELD(config->window.hide_on_hover);
    HASH_FIELD(config->window.keep_in_screen);
    HASH_FIELD(config->window.edge_snap);
    HASH_FIELD(config->window.hide_delay_seconds);
    HASH_FIELD(config->app);
    HASH_FIELD(config->shortcuts);
    HASH_FIELD(config->behavior_shortcut_count);
    hash = hash_bytes(hash, config->behavior_shortcuts,
        config->behavior_shortcut_count * sizeof(config->behavior_shortcuts[0]));
#undef HASH_FIELD
    return hash;
}

static uint64_t session_hash(const BongoCatConfig *config) {
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(config->window.visible);
    HASH_FIELD(config->window.scale_percent);
    HASH_FIELD(config->window.opacity_percent);
    HASH_FIELD(config->window.x);
    HASH_FIELD(config->window.y);
    HASH_FIELD(config->window.width);
    HASH_FIELD(config->window.height);
    HASH_FIELD(config->current_model);
#undef HASH_FIELD
    return hash;
}

void bongo_cat_config_store_initialize(BongoCatApp *app) {
    uint64_t preferences = preferences_hash(&app->config);
    uint64_t session = session_hash(&app->config);
    app->preferences_observed_hash = preferences;
    app->session_observed_hash = session;
    app->preferences_saved_hash = app->preferences_store_valid ? preferences : 0;
    app->session_saved_hash = app->session_store_valid ? session : 0;
}

static bool save_preferences(BongoCatApp *app, uint64_t hash) {
    BongoCatError error = {0};
    if (bongo_cat_preferences_save(app->preferences_path,
        &app->config, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->preferences_saved_hash = hash;
    return true;
}

static bool save_session(BongoCatApp *app, uint64_t hash) {
    BongoCatError error = {0};
    if (bongo_cat_session_save(app->session_path,
        &app->config, &error) != BONGO_CAT_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
        return false;
    }
    app->session_saved_hash = hash;
    return true;
}

void bongo_cat_config_store_update(BongoCatApp *app, uint64_t now) {
    if (!app || app->smoke || !app->preferences_path[0] || !app->session_path[0]) return;
    uint64_t preferences = preferences_hash(&app->config);
    uint64_t session = session_hash(&app->config);
    if (preferences != app->preferences_observed_hash) {
        app->preferences_observed_hash = preferences;
        app->preferences_save_due_ns = now + SAVE_DELAY_NS;
    } else if (preferences != app->preferences_saved_hash &&
        !app->preferences_save_due_ns) app->preferences_save_due_ns = now + SAVE_DELAY_NS;
    if (session != app->session_observed_hash) {
        app->session_observed_hash = session;
        app->session_save_due_ns = now + SAVE_DELAY_NS;
    } else if (session != app->session_saved_hash &&
        !app->session_save_due_ns) app->session_save_due_ns = now + SAVE_DELAY_NS;
    if (app->preferences_save_due_ns && now >= app->preferences_save_due_ns)
        app->preferences_save_due_ns = save_preferences(app, preferences)
            ? 0 : now + RETRY_DELAY_NS;
    if (app->session_save_due_ns && now >= app->session_save_due_ns)
        app->session_save_due_ns = save_session(app, session)
            ? 0 : now + RETRY_DELAY_NS;
}

void bongo_cat_config_store_flush(BongoCatApp *app) {
    if (!app || app->smoke || !app->preferences_path[0] || !app->session_path[0]) return;
    uint64_t preferences = preferences_hash(&app->config);
    uint64_t session = session_hash(&app->config);
    if (preferences != app->preferences_saved_hash) save_preferences(app, preferences);
    if (session != app->session_saved_hash) save_session(app, session);
    app->preferences_observed_hash = preferences;
    app->session_observed_hash = session;
    app->preferences_save_due_ns = app->session_save_due_ns = 0;
}
