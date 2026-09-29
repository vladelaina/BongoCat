#include "image_texture_cache.h"
#include "bongo_cat/file.h"

#include <SDL3/SDL_filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char cache_root[BONGO_CAT_PATH_CAP];

void bongo_cat_image_set_texture_cache_root(const char *root) {
    /* Configure once, before starting model workers. An unset root also keeps
       tools and embedders from unexpectedly writing into the user's profile. */
    cache_root[0] = '\0';
    if (root && root[0] && !bongo_cat_path_join(cache_root, sizeof(cache_root),
        root, "textures-rgba-v1")) cache_root[0] = '\0';
}

static bool digest_valid(const char *digest) {
    if (!digest || strlen(digest) != 64) return false;
    for (int i = 0; i < 64; ++i)
        if (!((digest[i] >= '0' && digest[i] <= '9') ||
              (digest[i] >= 'a' && digest[i] <= 'f'))) return false;
    return true;
}

bool bongo_cat_texture_cache_path(const char *digest, int width, int height,
    char *path, size_t capacity) {
    if (!cache_root[0] || !digest_valid(digest) || width < 1 || height < 1)
        return false;
    char name[112];
    snprintf(name, sizeof(name), "%s-%dx%d.rgba", digest, width, height);
    return bongo_cat_path_join(path, capacity, cache_root, name);
}

void bongo_cat_image_forget_texture_cache(const char *digest,
    int max_width, int max_height) {
    char path[BONGO_CAT_PATH_CAP];
    if (bongo_cat_texture_cache_path(digest, max_width, max_height,
        path, sizeof(path))) bongo_cat_file_remove(path);
}

typedef struct CacheEntry {
    char name[112];
    uint64_t size;
    SDL_Time modified;
} CacheEntry;

typedef struct CacheScan {
    CacheEntry entries[128];
    size_t count;
    uint64_t total;
    bool overflow;
} CacheScan;

static BongoCatPathVisit collect(void *userdata, const char *directory,
    const char *name) {
    CacheScan *scan = userdata;
    size_t length = strlen(name);
    if (length < 70) return BONGO_CAT_PATH_CONTINUE;
    char digest[65];
    memcpy(digest, name, 64); digest[64] = '\0';
    if (!digest_valid(digest) || name[64] != '-') return BONGO_CAT_PATH_CONTINUE;
    char path[BONGO_CAT_PATH_CAP];
    SDL_PathInfo info;
    if (!bongo_cat_path_join(path, sizeof(path), directory, name) ||
        !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE)
        return BONGO_CAT_PATH_CONTINUE;
    if (strcmp(name + length - 4, ".tmp") == 0) {
        /* Never remove another pet's active writer. Only abandoned files older
           than a day are eligible; all removal is local and non-recursive. */
        uint64_t now = (uint64_t)time(NULL) * 1000000000ull;
        if (info.modify_time > 0 && now > (uint64_t)info.modify_time &&
            now - (uint64_t)info.modify_time > 86400000000000ull)
            bongo_cat_file_remove(path);
        return BONGO_CAT_PATH_CONTINUE;
    }
    if (strcmp(name + length - 5, ".rgba") != 0 ||
        length >= sizeof(scan->entries[0].name)) return BONGO_CAT_PATH_CONTINUE;
    if (scan->count == 128) {
        scan->overflow = true;
        return BONGO_CAT_PATH_SUCCESS;
    }
    CacheEntry *entry = &scan->entries[scan->count++];
    memcpy(entry->name, name, length + 1);
    entry->size = info.size;
    entry->modified = info.modify_time;
    if (UINT64_MAX - scan->total < info.size) scan->overflow = true;
    else scan->total += info.size;
    return BONGO_CAT_PATH_CONTINUE;
}

static int oldest_first(const void *left, const void *right) {
    const CacheEntry *a = left, *b = right;
    return a->modified < b->modified ? -1 : a->modified > b->modified;
}

bool bongo_cat_texture_cache_reserve(uint64_t bytes) {
    const uint64_t budget = 1024ull * 1024 * 1024;
    if (!cache_root[0] || bytes > BONGO_CAT_TEXTURE_CACHE_ENTRY_LIMIT ||
        !bongo_cat_path_create_directory(cache_root)) return false;
    CacheScan scan = {0};
    if (!bongo_cat_path_enumerate(cache_root, collect, &scan) || scan.overflow)
        return false;
    qsort(scan.entries, scan.count, sizeof(scan.entries[0]), oldest_first);
    size_t remaining = scan.count;
    for (size_t i = 0; i < scan.count &&
        (scan.total > budget - bytes || remaining >= 64); ++i) {
        char path[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_join(path, sizeof(path), cache_root,
            scan.entries[i].name) && bongo_cat_file_remove(path)) {
            scan.total -= scan.entries[i].size;
            --remaining;
        }
    }
    return scan.total <= budget - bytes && remaining < 64;
}
