#include "model_import.h"
#include "model_storage.h"
#include "test_mver_import_internal.h"
#include "test_mver_support.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

void test_model_import_source(void) {
    char *temporary = SDL_GetCurrentDirectory();
    CHECK(temporary != NULL);
    if (!temporary) return;
    char root[BONGO_CAT_PATH_CAP], package[BONGO_CAT_PATH_CAP];
    char moc[BONGO_CAT_PATH_CAP], config[BONGO_CAT_PATH_CAP];
    char directory[BONGO_CAT_PATH_CAP], normalized[BONGO_CAT_PATH_CAP];
    BongoCatError error = {0};
    snprintf(root, sizeof(root), "%s/bongocat-source-%llu", temporary,
        (unsigned long long)SDL_GetTicksNS());
    CHECK(SDL_CreateDirectory(root));
    CHECK(child(package, sizeof(package), root, "mver", true));
    CHECK(mver_fixture(package));
    CHECK(child(moc, sizeof(moc), package,
        "img/standard/cat_model/cat.moc3", false));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, package) == 0);
    CHECK(bongo_cat_import_source_directory(moc, normalized, 2,
        &error) == BONGO_CAT_ERROR_ARGUMENT);
    CHECK(bongo_cat_import_source_directory(NULL, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_ARGUMENT);
    CHECK(bongo_cat_import_source_directory(moc, NULL, 0,
        &error) == BONGO_CAT_ERROR_ARGUMENT);
    CHECK(child(config, sizeof(config), package, "config.json", false));
    CHECK(child(directory, sizeof(directory), package,
        BONGO_CAT_SKIN_CONFIG_FILE, false));
    CHECK(SDL_RenamePath(config, directory));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, package) == 0);
    CHECK(write_text(directory, "invalid json"));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(strstr(error.message, "Mver configuration") != NULL);

    CHECK(child(package, sizeof(package), root, "tauri", true));
    CHECK(child(directory, sizeof(directory), package, "nested", true));
    CHECK(child(moc, sizeof(moc), directory, "CAT.MOC3", false));
    CHECK(write_text(moc, "MOC3"));
    CHECK(child(config, sizeof(config), package, "cat.model3.json", false));
    CHECK(write_text(config, "{\"Version\":3,\"FileReferences\":{"
        "\"Moc\":\"nested/CAT.MOC3\",\"Textures\":[\"texture.png\"]}}"));
    CHECK(child(directory, sizeof(directory), package, "texture.png", false));
    char texture[BONGO_CAT_PATH_CAP];
    snprintf(texture, sizeof(texture), "%s/resources/assets/bongocat.png",
        BONGO_CAT_NATIVE_SOURCE_DIR);
    CHECK(SDL_CopyFile(texture, directory));
    error = (BongoCatError){0};
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, package) == 0 && error.code == BONGO_CAT_OK);
    CHECK(bongo_cat_import_source_directory(config, normalized,
        sizeof(normalized), &error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, package) == 0);
    CHECK(bongo_cat_path_remove(directory));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(bongo_cat_path_remove(moc));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_ARGUMENT);
    /* An orphan must not discover models in sibling directories. */
    CHECK(child(moc, sizeof(moc), root, "orphan.moc3", false));
    CHECK(write_text(moc, "MOC3"));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(child(moc, sizeof(moc), root, "unsupported.txt", false));
    CHECK(write_text(moc, "text"));
    CHECK(bongo_cat_import_source_directory(moc, normalized,
        sizeof(normalized), &error) == BONGO_CAT_ERROR_FORMAT);
    CHECK(bongo_cat_import_source_directory(root, normalized,
        sizeof(normalized), &error) == BONGO_CAT_OK);
    CHECK(strcmp(normalized, root) == 0);
    CHECK(bongo_cat_model_remove_tree(root, NULL));
    SDL_free(temporary);
}
