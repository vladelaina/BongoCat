#include "preferences_about_online.h"
#include "preferences_about_svg.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#include <unistd.h>
#endif

#define RESPONSE_LIMIT (4u * 1024u * 1024u)
#define CACHE_TTL_NS (SDL_NS_PER_SECOND * 86400LL)

/* Capacity excludes the terminator. Network growth is geometric and bounded;
   disk reads reserve their exact known size without touching a 4 MiB buffer. */
static bool reserve_response(BongoCatAboutRequest *job, size_t capacity) {
    if (capacity > job->limit) return false;
    if (job->response && capacity <= job->capacity) return true;
    char *response = realloc(job->response, capacity + 1);
    if (!response) return false;
    job->response = response;
    job->capacity = capacity;
    job->response[job->length] = 0;
    return true;
}

static bool cache_path(BongoCatAboutRequest *job, char *path, size_t capacity) {
    return job->cache_directory[0] && bongo_cat_path_join(path, capacity,
        job->cache_directory, job->kind == BONGO_ABOUT_CONTRIBUTORS
            ? "contributors-v1.svg" : "wechat-v1.svg");
}

/* SVG parsers modify their input. Only network results need a preserved copy
   for cache writes; disk reads can be parsed in place. */
static bool decode_response(BongoCatAboutRequest *job, bool preserve_response) {
    if (SDL_GetAtomicInt(&job->cancel) || !job->length ||
        memchr(job->response, 0, job->length) || !strstr(job->response, "<svg") ||
        strstr(job->response, "<!DOCTYPE") || strstr(job->response, "<!ENTITY"))
        return false;
    char *copy = preserve_response ? malloc(job->length + 1) : job->response;
    if (!copy) return false;
    if (preserve_response) memcpy(copy, job->response, job->length + 1);
    bool valid;
    if (job->kind == BONGO_ABOUT_CONTRIBUTORS) {
        job->feed = bongo_cat_about_feed_parse(copy, &job->cancel);
        valid = job->feed && job->feed->count;
        if (!valid) {
            bongo_cat_about_feed_free(job->feed);
            job->feed = NULL;
        }
    } else {
        job->qr_pixels = bongo_cat_about_qr_pixels(copy);
        valid = job->qr_pixels != NULL;
    }
    if (preserve_response) free(copy);
    return valid;
}

static bool cache_read(BongoCatAboutRequest *job) {
    char path[BONGO_CAT_PATH_CAP];
    if (!cache_path(job, path, sizeof(path))) return false;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE ||
        !info.size || info.size > job->limit) return false;
    if (!reserve_response(job, (size_t)info.size)) return false;
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    size_t size = (size_t)info.size;
    bool ok = fread(job->response, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    job->length = size;
    job->response[size] = 0;
    if (!ok || !decode_response(job, false)) {
        job->length = 0;
        job->response[0] = 0;
        return false;
    }
    SDL_Time now = 0;
    job->refresh_needed = !SDL_GetCurrentTime(&now) || info.modify_time <= 0 ||
        now < info.modify_time || now - info.modify_time >= CACHE_TTL_NS;
    job->status = 200;
    return true;
}

static void cache_write(BongoCatAboutRequest *job) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP + 64];
    if (SDL_GetAtomicInt(&job->cancel) || !cache_path(job, path, sizeof(path)) ||
        !bongo_cat_path_create_directory(job->cache_directory)) return;
    /* A sibling temporary file preserves the previous cache on failed writes. */
    unsigned long long process_id;
#ifdef _WIN32
    process_id = (unsigned long long)GetCurrentProcessId();
#else
    process_id = (unsigned long long)getpid();
#endif
    int length = snprintf(temporary, sizeof(temporary), "%s.%llu.%llu.tmp", path,
        process_id,
        (unsigned long long)SDL_GetCurrentThreadID());
    if (length < 0 || (size_t)length >= sizeof(temporary)) return;
    FILE *file = bongo_cat_file_open(temporary, "wb");
    if (!file) return;
    bool ok = fwrite(job->response, 1, job->length, file) == job->length;
    if (fclose(file) != 0) ok = false;
    if (!ok || SDL_GetAtomicInt(&job->cancel) ||
        !bongo_cat_file_replace(temporary, path, false))
        bongo_cat_file_remove(temporary);
}

static int complete(BongoCatAboutRequest *job) {
    free(job->response);
    job->response = NULL;
    job->length = 0;
    job->capacity = 0;
    SDL_SetAtomicInt(&job->done, 1);
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = job->event_type;
    event.user.windowID = job->window_id;
    SDL_PushEvent(&event);
    return 0;
}

static bool append(BongoCatAboutRequest *job, const void *data, size_t size) {
    if (SDL_GetAtomicInt(&job->cancel) || size > job->limit - job->length)
        return false;
    size_t needed = job->length + size;
    if (!job->response || needed > job->capacity) {
        size_t capacity = job->capacity ? job->capacity : 4096;
        while (capacity < needed)
            capacity = capacity > job->limit / 2 ? job->limit : capacity * 2;
        if (!reserve_response(job, capacity)) return false;
    }
    memcpy(job->response + job->length, data, size);
    job->length += size;
    job->response[job->length] = 0;
    return true;
}

#ifndef _WIN32
static size_t receive(char *data, size_t size, size_t count, void *user) {
    if (size && count > RESPONSE_LIMIT / size)
        return 0;
    return append(user, data, size * count) ? size * count : 0;
}
static int progress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return SDL_GetAtomicInt(&((BongoCatAboutRequest *)user)->cancel);
}
#endif

static int SDLCALL request_worker(void *user) {
    BongoCatAboutRequest *job = user;
    /* Best effort: background assets should yield CPU time to interaction. */
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    /* Publish stale data before starting the separate background refresh. */
    if (!job->network_only && cache_read(job)) return complete(job);
    if (SDL_GetAtomicInt(&job->cancel)) return complete(job);
    bool ok = false;
#ifdef _WIN32
    HINTERNET session = WinHttpOpen(L"BongoCat About/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = NULL, request = NULL;
    if (session) {
        WinHttpSetTimeouts(session, 3000, 3000, 4000, 4000);
        connection =
            WinHttpConnect(session, L"bongocat.pet",
                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    if (connection)
        request = WinHttpOpenRequest(
            connection, L"GET",
            job->kind == BONGO_ABOUT_CONTRIBUTORS ? L"/co" : L"/wechat",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        ok = !SDL_GetAtomicInt(&job->cancel) &&
             WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
             WinHttpReceiveResponse(request, NULL);
        DWORD status = 0, bytes = sizeof(status);
        if (ok)
            ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     NULL, &status, &bytes, NULL) != FALSE;
        job->status = (int)status;
        /* Error bodies cannot produce usable images; do not download them. */
        if (status != 200) ok = false;
        Uint64 deadline = SDL_GetTicks() + 15000;
        while (ok && !SDL_GetAtomicInt(&job->cancel)) {
            char buffer[4096];
            DWORD read = 0;
            ok = SDL_GetTicks() < deadline &&
                 WinHttpReadData(request, buffer, sizeof(buffer), &read);
            if (!ok || !read)
                break;
            ok = append(job, buffer, read);
        }
    }
    if (request)
        WinHttpCloseHandle(request);
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
#else
    bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    CURL *curl = initialized ? curl_easy_init() : NULL;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL,
                         job->kind == BONGO_ABOUT_CONTRIBUTORS ? "https://bongocat.pet/co"
                                       : "https://bongocat.pet/wechat");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "BongoCat About/1.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, job);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, job);
        ok = curl_easy_perform(curl) == CURLE_OK;
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        job->status = (int)status;
        curl_easy_cleanup(curl);
    }
    /* curl is shared with the update service; process lifetime owns its global state. */
#endif
    if (!ok || SDL_GetAtomicInt(&job->cancel))
        job->status = 0;
    if (job->status == 200) {
        if (decode_response(job, true)) cache_write(job);
        else job->status = 0;
    }
    return complete(job);
}

BongoCatAboutRequest *bongo_cat_about_request(int kind, Uint32 event_type,
    Uint32 window_id, const char *cache_root, bool network_only) {
    BongoCatAboutRequest *job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->limit = kind == BONGO_ABOUT_CONTRIBUTORS ? RESPONSE_LIMIT : 64u * 1024u;
    job->kind = kind;
    job->event_type = event_type;
    job->window_id = window_id;
    job->network_only = network_only;
    if (cache_root && cache_root[0] &&
        !bongo_cat_path_join(job->cache_directory, sizeof(job->cache_directory),
            cache_root, "about")) job->cache_directory[0] = 0;
    job->thread = SDL_CreateThread(request_worker, "bongocat-about", job);
    if (!job->thread) {
        free(job->response);
        free(job);
        return NULL;
    }
    return job;
}

void bongo_cat_about_request_free(BongoCatAboutRequest *job) {
    if (!job)
        return;
    SDL_SetAtomicInt(&job->cancel, 1);
    if (job->thread) SDL_WaitThread(job->thread, NULL);
    bongo_cat_about_feed_free(job->feed);
    free(job->qr_pixels);
    free(job->response);
    free(job);
}
