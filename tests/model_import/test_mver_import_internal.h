#ifndef BONGO_CAT_TEST_MVER_IMPORT_INTERNAL_H
#define BONGO_CAT_TEST_MVER_IMPORT_INTERNAL_H

#include <stdio.h>

extern int failures;

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", \
        __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

void test_mver_container_discovery(void);
void test_mver_audio(void);
void test_mver_config(void);
void test_mver_pointer_modes(void);
void test_model_import_source(void);
void test_model_import_archive(void);
void test_mver_manifest(void);
void test_tauri_portable(void);

#endif
