#ifndef BONGO_CAT_MODEL_IMPORT_TAURI_INTERNAL_H
#define BONGO_CAT_MODEL_IMPORT_TAURI_INTERNAL_H

#include "model_import_tauri.h"
#include "../model_import_path.h"
#include "bongo_cat/path.h"

#define TAURI_KEY_CAP 256
#define BONGO_CAT_TAURI_SOURCE_FILE "bongocat.mver.json"

typedef struct TauriMverCalibration {
    double l2d_correct;
    double l2d_offset_x;
    double l2d_offset_y;
    int window_width;
    int window_height;
    bool mirror;
    bool auto_frame;
} TauriMverCalibration;

typedef struct TauriKeyFile {
    char name[BONGO_CAT_ID_CAP];
    char path[BONGO_CAT_PATH_CAP];
    int code;
} TauriKeyFile;

typedef struct TauriKeyFiles {
    TauriKeyFile values[TAURI_KEY_CAP];
    size_t count;
} TauriKeyFiles;

/* Candidate construction is private to Tauri discovery. */
bool bongo_cat_import_tauri_add_candidate(BongoCatImportDiscovery *discovery,
    const char *directory, const char *setting);

/* Source package lookup. */
bool bongo_cat_tauri_find_resource_file(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity);
bool bongo_cat_tauri_find_resource_directory(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity);
bool bongo_cat_tauri_find_package_file(
    const BongoCatImportCandidate *candidate, const char *name,
    char *path, size_t capacity);

/* Mver normalization steps. */
int bongo_cat_tauri_key_code(const char *filename);
void bongo_cat_tauri_calibration_defaults(TauriMverCalibration *calibration);
void bongo_cat_tauri_legacy_calibration(
    const BongoCatImportCandidate *candidate, TauriMverCalibration *calibration);
bool bongo_cat_tauri_copy_image_or_placeholder(const char *source,
    const char *target, BongoCatError *error);
bool bongo_cat_tauri_copy_preview(const BongoCatImportCandidate *candidate,
    const char *mode_root, BongoCatError *error);
bool bongo_cat_tauri_copy_runtime_images(
    const BongoCatImportCandidate *candidate, const char *mode_root,
    BongoCatError *error);
bool bongo_cat_tauri_copy_input_images(
    const BongoCatImportCandidate *candidate,
    const char *mode_root, TauriKeyFiles *left, TauriKeyFiles *right,
    BongoCatError *error);
bool bongo_cat_tauri_write_config(const char *path, BongoCatModelMode mode,
    const TauriKeyFiles *left, const TauriKeyFiles *right,
    const TauriMverCalibration *calibration, BongoCatError *error);
bool bongo_cat_tauri_copy_model_tree(const char *source, const char *target,
    unsigned depth, BongoCatError *error);
bool bongo_cat_tauri_read_calibration(
    const BongoCatImportCandidate *candidate,
    TauriMverCalibration *calibration);

#endif
