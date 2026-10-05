#ifndef BONGO_CAT_CONFIG_STORE_TEST_H
#define BONGO_CAT_CONFIG_STORE_TEST_H

#include "runtime.h"
#include "test.h"

typedef struct ConfigSave {
    bool settings;
    uint64_t time;
    BongoCatResult result;
    size_t length;
    char *json;
} ConfigSave;

typedef struct ConfigTrace {
    ConfigSave saves[2];
    size_t count;
    unsigned failures;
    uint64_t time;
} ConfigTrace;

void config_reference_load(BongoCatApp *app);
void config_reference_update(BongoCatApp *app, uint64_t now);
void config_reference_flush(BongoCatApp *app);
BongoCatApp *config_test_app(bool saturated, bool clean);
void config_test_trace(ConfigTrace *trace);
void config_test_clear(ConfigTrace *trace);
void config_test_compare(const BongoCatApp *left, const BongoCatApp *right);
void config_test_compare_trace(const ConfigTrace *left,
    const ConfigTrace *right);
BongoCatResult config_test_settings_save(const char *path,
    const BongoCatSettings *settings, BongoCatError *error);
BongoCatResult config_test_session_save(const char *path,
    const BongoCatSessionState *session, BongoCatError *error);

#endif
