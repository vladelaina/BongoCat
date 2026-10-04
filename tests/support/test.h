#ifndef BONGO_CAT_TEST_H
#define BONGO_CAT_TEST_H

#include <stdio.h>

extern int bongo_cat_test_failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    bongo_cat_test_failures++; \
} } while (0)

void test_config(void);
void test_language(void);
void test_input(void);
void test_models(void);
void test_mver_pointer(void);
void test_shortcut(void);
void test_sound_shortcut(void);
void test_update(void);

#endif
