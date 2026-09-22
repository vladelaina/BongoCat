#ifndef BONGO_CAT_UPDATE_H
#define BONGO_CAT_UPDATE_H

#include "bongo_cat/common.h"

#define BONGO_CAT_UPDATE_URL_CAP 512
#define BONGO_CAT_UPDATE_NOTES_CAP 8192

typedef struct BongoCatUpdateRelease {
    char version[BONGO_CAT_UPDATE_VERSION_CAP];
    char release_url[BONGO_CAT_UPDATE_URL_CAP];
    char installer_url[BONGO_CAT_UPDATE_URL_CAP];
    char portable_url[BONGO_CAT_UPDATE_URL_CAP];
    char package_url[BONGO_CAT_UPDATE_URL_CAP];
    char notes[BONGO_CAT_UPDATE_NOTES_CAP];
} BongoCatUpdateRelease;

#ifdef __cplusplus
extern "C" {
#endif

int bongo_cat_update_compare_versions(const char *left, const char *right);
bool bongo_cat_update_version_valid(const char *version);
bool bongo_cat_update_parse_release(const char *json, const char *platform,
    BongoCatUpdateRelease *release, BongoCatError *error);

#ifdef __cplusplus
}
#endif

#endif
