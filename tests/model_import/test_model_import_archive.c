#include "model_import_archive.h"
#include "model_storage.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "bongo_cat/path.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <stdio.h>
#include <string.h>

static bool save_zip(mz_zip_archive *zip, const char *path) {
    void *data = NULL;
    size_t size = 0;
    bool ok = mz_zip_writer_finalize_heap_archive(zip, &data, &size) != 0;
    mz_zip_writer_end(zip);
    if (ok) ok = SDL_SaveFile(path, data, size);
    mz_free(data);
    return ok;
}

typedef struct ZipTree {
    mz_zip_archive *zip;
    const char *root;
    const char *prefix;
    mz_uint flags;
} ZipTree;

static BongoCatPathVisit zip_item(void *userdata, const char *directory,
    const char *name) {
    ZipTree *tree = userdata;
    char path[BONGO_CAT_PATH_CAP], relative[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), directory, name))
        return BONGO_CAT_PATH_FAILURE;
    if (bongo_cat_path_is_dir(path))
        return bongo_cat_path_enumerate(path, zip_item, tree)
            ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
    int written = snprintf(relative, sizeof(relative), "%s%s", tree->prefix,
        path + strlen(tree->root) + 1);
    if (written < 0 || (size_t)written >= sizeof(relative))
        return BONGO_CAT_PATH_FAILURE;
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    bool ok = data && mz_zip_writer_add_mem(tree->zip, relative, data,
        size, MZ_BEST_SPEED | tree->flags);
    SDL_free(data);
    return ok ? BONGO_CAT_PATH_CONTINUE : BONGO_CAT_PATH_FAILURE;
}

void test_model_import_archive(void) {
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char archive[BONGO_CAT_PATH_CAP], models[BONGO_CAT_PATH_CAP];
    char directory[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    char *cwd = SDL_GetCurrentDirectory();
    CHECK(cwd != NULL);
    if (!cwd) return;
    snprintf(root, sizeof(root), "%s/archive-test-%llu", cwd,
        (unsigned long long)SDL_GetTicksNS());
    SDL_free(cwd);
    CHECK(child(package, sizeof(package), root, "fixture", true));
    CHECK(mver_fixture(package));
    CHECK(child(models, sizeof(models), root, "installed", true));
    CHECK(child(archive, sizeof(archive), root, "example.ZIP", false));
    CHECK(bongo_cat_import_is_archive(archive));
    CHECK(!bongo_cat_import_is_archive(package));
    BongoCatError error = {0};
    for (int nested = 0; nested < 2; ++nested) {
        mz_zip_archive zip = {0};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        ZipTree tree = {&zip, package, nested ? "outer/model/" : "", 0};
        if (nested) {
            CHECK(mz_zip_writer_add_mem(&zip, "outer/", NULL, 0, 0));
            CHECK(mz_zip_writer_add_mem(&zip, "outer/model/", NULL, 0, 0));
        }
        CHECK(bongo_cat_path_enumerate(package, zip_item, &tree));
        CHECK(save_zip(&zip, archive));
        CHECK(bongo_cat_import_archive_extract(archive, directory, temporary,
            &error) == BONGO_CAT_OK);
        CHECK(strcmp(bongo_cat_path_name(directory), "example") == 0);
        bongo_cat_import_archive_cleanup(temporary);
        CHECK(!bongo_cat_path_is_dir(temporary));
        BongoCatImportSession *session = bongo_cat_import_session_create(models, &error);
        CHECK(session != NULL);
        BongoCatImportBatchStats stats;
        CHECK(bongo_cat_import_session_install_progressive(session, archive,
            NULL, NULL, &stats, &error) == BONGO_CAT_OK);
        CHECK(stats.succeeded_count > 0 && stats.failed_count == 0);
        BongoCatImportReceipt receipt;
        CHECK(bongo_cat_import_session_install(session, archive, &receipt,
            &error) == BONGO_CAT_OK);
        CHECK(receipt.count > 0 && receipt.installed_count == 0);
        bongo_cat_import_session_destroy(session);
    }
    /* Miniz writes the UTF-8 flag by default. Legacy bytes must explicitly
       omit it, just like ZIPs produced by older Chinese archivers. */
    const struct {
        const char *raw;
        const char *decoded;
        mz_uint flags;
        bool legacy;
    } names[] = {
        {"\xE9\xBD\x90\xE5\x8F\xB8\xE7\xA4\xBC/file.txt",
         "\xE9\xBD\x90\xE5\x8F\xB8\xE7\xA4\xBC/file.txt", 0, false},
        {"\xE9\xBD\x90\xE5\x8F\xB8\xE7\xA4\xBC/file.txt",
         "\xE9\xBD\x90\xE5\x8F\xB8\xE7\xA4\xBC/file.txt",
         MZ_ZIP_FLAG_ASCII_FILENAME, false},
        {"\xC6\xEB\xCB\xBE\xC0\xF1/file.txt",
         "\xE9\xBD\x90\xE5\x8F\xB8\xE7\xA4\xBC/file.txt",
         MZ_ZIP_FLAG_ASCII_FILENAME, true},
        /* 0x5C is part of this GBK character, not a path separator. */
        {"\x81\x5C/file.txt", "\xE4\xB9\x97/file.txt",
         MZ_ZIP_FLAG_ASCII_FILENAME, true},
        {"\xC6\xEB/file.txt", NULL, 0, false},
        {"\x81/file.txt", NULL, MZ_ZIP_FLAG_ASCII_FILENAME, false},
        {"\xC6\xEB/../escape", NULL, MZ_ZIP_FLAG_ASCII_FILENAME, true}
    };
    for (size_t i = 0; i < SDL_arraysize(names); ++i) {
        const char *decoded = names[i].decoded;
#ifndef _WIN32
        if (names[i].legacy) decoded = NULL;
#endif
        mz_zip_archive zip = {0};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        CHECK(mz_zip_writer_add_mem(&zip, names[i].raw, "payload", 7,
            names[i].flags));
        CHECK(save_zip(&zip, archive));
        BongoCatResult result = bongo_cat_import_archive_extract(archive,
            directory, temporary, &error);
        CHECK(result == (decoded ? BONGO_CAT_OK : BONGO_CAT_ERROR_FORMAT));
        if (decoded && result == BONGO_CAT_OK) {
            char path[BONGO_CAT_PATH_CAP];
            CHECK(bongo_cat_path_join(path, sizeof(path), directory, decoded));
            size_t size = 0;
            void *data = SDL_LoadFile(path, &size);
            CHECK(data && size == 7 && memcmp(data, "payload", 7) == 0);
            SDL_free(data);
        } else if (!decoded) {
            CHECK(!directory[0] && !temporary[0]);
        }
        bongo_cat_import_archive_cleanup(temporary);
    }
#ifdef _WIN32
    {
        mz_zip_archive zip = {0};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        ZipTree tree = {&zip, package,
            "bongo_cat_mver_\xC6\xEB\xCB\xBE\xC0\xF1/",
            MZ_ZIP_FLAG_ASCII_FILENAME};
        CHECK(bongo_cat_path_enumerate(package, zip_item, &tree));
        CHECK(save_zip(&zip, archive));
        BongoCatImportSession *session = bongo_cat_import_session_create(models,
            &error);
        CHECK(session != NULL);
        BongoCatImportBatchStats stats = {0};
        CHECK(bongo_cat_import_session_install_progressive(session, archive,
            NULL, NULL, &stats, &error) == BONGO_CAT_OK);
        CHECK(stats.succeeded_count > 0 && stats.failed_count == 0);
        BongoCatImportReceipt receipt;
        CHECK(bongo_cat_import_session_install(session, archive, &receipt,
            &error) == BONGO_CAT_OK);
        CHECK(receipt.count > 0 && receipt.installed_count == 0);
        bongo_cat_import_session_destroy(session);
    }
#endif
    const char *unsafe[] = {"../escape", "safe/../../escape", "\\absolute",
        "C:/escape", "safe\\..\\escape", "file:stream", "NUL.txt", "dir./file"};
    for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); ++i) {
        mz_zip_archive zip = {0};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        CHECK(mz_zip_writer_add_mem(&zip, unsafe[i], "x", 1, 0));
        CHECK(save_zip(&zip, archive));
        CHECK(bongo_cat_import_archive_extract(archive, directory, temporary,
            &error) == BONGO_CAT_ERROR_FORMAT);
        CHECK(!directory[0] && !temporary[0]);
    }
    CHECK(write_text(archive, "not a zip"));
    CHECK(bongo_cat_import_archive_extract(archive, directory, temporary,
        &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(!directory[0] && !temporary[0]);
    mz_zip_archive empty_model = {0};
    CHECK(mz_zip_writer_init_heap(&empty_model, 0, 0));
    CHECK(mz_zip_writer_add_mem(&empty_model, "readme.txt", "text", 4, 0));
    CHECK(save_zip(&empty_model, archive));
    BongoCatImportSession *session = bongo_cat_import_session_create(models, &error);
    CHECK(session != NULL);
    BongoCatImportBatchStats stats;
    CHECK(bongo_cat_import_session_install_progressive(session, archive,
        NULL, NULL, &stats, &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(stats.succeeded_count == 0 && stats.failed_count == 1);
    bongo_cat_import_session_destroy(session);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
}
