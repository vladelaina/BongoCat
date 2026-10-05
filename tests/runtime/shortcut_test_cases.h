#ifndef BONGO_CAT_SHORTCUT_TEST_CASES_H
#define BONGO_CAT_SHORTCUT_TEST_CASES_H

#include "shortcut_test_support.h"
#include "test.h"

extern BongoCatApp *shortcut_app;
void shortcut_reset(size_t behaviors, size_t bindings);
void shortcut_check(BongoCatInputKind kind, const char *name, float value,
    const char *expected);
void shortcut_random_cases(void);
void shortcut_live_edit_cases(void);

#endif
