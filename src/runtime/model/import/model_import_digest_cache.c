#include "model_import.h"
#include "bongo_cat/sha256.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define DIGEST_CACHE_BUCKET_CAP 4093
#define DIGEST_CACHE_ENTRY_CAP 16384

typedef struct DigestCacheEntry {
    struct DigestCacheEntry *next;
    char *path;
    char digest[65];
    uint64_t size;
    uint64_t modified;
} DigestCacheEntry;

struct BongoCatImportDigestCache {
    DigestCacheEntry **buckets;
    size_t count;
};

static uint64_t hash_path(const char *value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *cursor = (const unsigned char *)value;
        cursor && *cursor; ++cursor) {
        unsigned char byte = *cursor == '\\' ? '/' : *cursor;
#ifdef _WIN32
        if (byte >= 'A' && byte <= 'Z') byte = (unsigned char)(byte + 32);
#endif
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool path_equal(const char *left, const char *right) {
#ifdef _WIN32
    return SDL_strcasecmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

static DigestCacheEntry *cache_entry(BongoCatImportDigestCache *cache,
    const char *path, size_t *bucket) {
    if (!cache || !cache->buckets || !path) return NULL;
    size_t index = (size_t)(hash_path(path) % DIGEST_CACHE_BUCKET_CAP);
    if (bucket) *bucket = index;
    for (DigestCacheEntry *entry = cache->buckets[index]; entry;
        entry = entry->next)
        if (path_equal(entry->path, path)) return entry;
    return NULL;
}

static void cache_store(BongoCatImportDigestCache *cache, const char *path,
    uint64_t size, uint64_t modified, const char digest[65]) {
    if (!cache || !cache->buckets || cache->count >= DIGEST_CACHE_ENTRY_CAP)
        return;
    size_t bucket = 0;
    DigestCacheEntry *entry = cache_entry(cache, path, &bucket);
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        size_t length = strlen(path) + 1;
        if (entry) entry->path = malloc(length);
        if (!entry || !entry->path) {
            if (entry) free(entry->path);
            free(entry);
            return;
        }
        memcpy(entry->path, path, length);
        entry->next = cache->buckets[bucket];
        cache->buckets[bucket] = entry;
        cache->count++;
    }
    entry->size = size;
    entry->modified = modified;
    memcpy(entry->digest, digest, 65);
}

BongoCatImportDigestCache *bongo_cat_import_digest_cache_create(void) {
    BongoCatImportDigestCache *cache = calloc(1, sizeof(*cache));
    if (cache) cache->buckets = calloc(DIGEST_CACHE_BUCKET_CAP,
        /* Each bucket holds an entry pointer, not an entry. */
        // NOLINTNEXTLINE(bugprone-sizeof-expression)
        sizeof(*cache->buckets));
    if (cache && cache->buckets) return cache;
    if (cache) free(cache->buckets);
    free(cache);
    return NULL;
}

void bongo_cat_import_digest_cache_destroy(BongoCatImportDigestCache *cache) {
    if (!cache) return;
    if (cache->buckets) for (size_t i = 0;
        i < DIGEST_CACHE_BUCKET_CAP; ++i) {
        DigestCacheEntry *entry = cache->buckets[i];
        while (entry) {
            DigestCacheEntry *next = entry->next;
            free(entry->path);
            free(entry);
            entry = next;
        }
    }
    free(cache->buckets);
    free(cache);
}

bool bongo_cat_import_digest_file_cached(BongoCatImportDigestCache *cache,
    const char *path, uint64_t size, uint64_t modified, char output[65]) {
    DigestCacheEntry *entry = cache_entry(cache, path, NULL);
    if (entry && entry->size == size && entry->modified == modified) {
        memcpy(output, entry->digest, 65);
        return true;
    }
    if (bongo_cat_sha256_file(path, output, NULL) != BONGO_CAT_OK)
        return false;
    cache_store(cache, path, size, modified, output);
    return true;
}
