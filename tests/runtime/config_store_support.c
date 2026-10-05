#include "config_store_test.h"

#include <stdlib.h>
#include <string.h>

static ConfigTrace *active_trace;

/* Locale selection is unrelated to update eligibility; keep loads stable. */
bool bongo_cat_system_language(BongoCatLanguage *language) {
    (void)language;
    return false;
}

void config_test_trace(ConfigTrace *trace) {
    active_trace = trace;
}

void config_test_clear(ConfigTrace *trace) {
    for (size_t i = 0; i < trace->count; ++i) free(trace->saves[i].json);
    trace->count = 0;
}

static ConfigSave *attempt(bool settings) {
    if (!active_trace || active_trace->count >= 2) abort();
    ConfigSave *save = &active_trace->saves[active_trace->count++];
    *save = (ConfigSave){.settings = settings, .time = active_trace->time};
    return save;
}

static BongoCatResult finish(ConfigSave *save, const char *path,
    BongoCatResult result) {
    save->result = result;
    if (result != BONGO_CAT_OK) return result;
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) abort();
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) abort();
    save->length = (size_t)length;
    save->json = malloc(save->length + 1);
    if (!save->json || fread(save->json, 1, save->length, file) !=
        save->length || fclose(file)) abort();
    save->json[save->length] = '\0';
    if (remove(path)) abort();
    return result;
}

BongoCatResult config_test_settings_save(const char *path,
    const BongoCatSettings *settings, BongoCatError *error) {
    ConfigSave *save = attempt(true);
    if (active_trace->failures & 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "injected settings failure");
        return finish(save, path, BONGO_CAT_ERROR_IO);
    }
    return finish(save, path, bongo_cat_settings_save(path, settings, error));
}

BongoCatResult config_test_session_save(const char *path,
    const BongoCatSessionState *session, BongoCatError *error) {
    ConfigSave *save = attempt(false);
    if (active_trace->failures & 2) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "injected session failure");
        return finish(save, path, BONGO_CAT_ERROR_IO);
    }
    return finish(save, path, bongo_cat_session_save(path, session, error));
}

static void fill_saturated(BongoCatApp *app) {
    BongoCatSettings *settings = &app->settings;
    BongoCatSessionState *session = &app->session;
    settings->behavior_shortcut_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    session->active_behavior_count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < BONGO_CAT_BEHAVIOR_BINDING_CAP; ++i) {
        BongoCatBehaviorShortcut *entry = &settings->behavior_shortcuts[i];
        snprintf(entry->id, sizeof(entry->id), "standard:expression:%zu", i);
        snprintf(entry->label, sizeof(entry->label), "Expression %zu", i);
        snprintf(entry->shortcut, sizeof(entry->shortcut), "Ctrl+Alt+%zu", i);
        BongoCatActiveBehavior *active = &session->active_behaviors[i];
        snprintf(active->model_id, sizeof(active->model_id), "standard");
        snprintf(active->behavior_id, sizeof(active->behavior_id), "%s", entry->id);
    }
    settings->model_label_count = settings->removed_model_count = BONGO_CAT_MODEL_CAP;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        snprintf(settings->model_labels[i].id, BONGO_CAT_ID_CAP, "model-%zu", i);
        snprintf(settings->model_labels[i].label, BONGO_CAT_ID_CAP, "Model %zu", i);
        snprintf(settings->removed_models[i].id, BONGO_CAT_ID_CAP, "removed-%zu", i);
    }
    session->additional_model_count = BONGO_CAT_ADDITIONAL_MODEL_CAP;
    for (size_t i = 0; i < BONGO_CAT_ADDITIONAL_MODEL_CAP; ++i)
        snprintf(session->additional_model_ids[i], BONGO_CAT_ID_CAP, "pet-%zu", i);
    bongo_cat_settings_validate(settings);
    bongo_cat_session_validate(session);
}

BongoCatApp *config_test_app(bool saturated, bool clean) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) abort();
    bongo_cat_settings_defaults(&app->settings);
    bongo_cat_session_defaults(&app->session);
    if (saturated) fill_saturated(app);
    snprintf(app->settings_path, sizeof(app->settings_path),
        "config-store-test-settings.json");
    snprintf(app->session_path, sizeof(app->session_path),
        "config-store-test-session.json");
    bongo_cat_config_store_load(app);
    if (clean) {
        app->settings_saved_hash = app->settings_observed_hash;
        app->session_saved_hash = app->session_observed_hash;
        app->settings_store_valid = app->session_store_valid = true;
    }
    return app;
}

void config_test_compare(const BongoCatApp *left, const BongoCatApp *right) {
#define FIELD(name) CHECK(!memcmp(&left->name, &right->name, sizeof(left->name)))
    FIELD(settings); FIELD(session);
    FIELD(settings_observed_hash); FIELD(session_observed_hash);
    FIELD(settings_saved_hash); FIELD(session_saved_hash);
    FIELD(settings_save_due_ns); FIELD(session_save_due_ns);
    FIELD(settings_store_valid); FIELD(session_store_valid);
    FIELD(settings_store_blocked); FIELD(session_store_blocked);
#undef FIELD
}

void config_test_compare_trace(const ConfigTrace *left,
    const ConfigTrace *right) {
    CHECK(left->count == right->count);
    for (size_t i = 0; i < left->count && i < right->count; ++i) {
        const ConfigSave *a = &left->saves[i], *b = &right->saves[i];
        CHECK(a->settings == b->settings);
        CHECK(a->time == b->time);
        CHECK(a->result == b->result);
        CHECK(a->length == b->length);
        if (a->length == b->length && a->length)
            CHECK(!memcmp(a->json, b->json, a->length));
    }
}
