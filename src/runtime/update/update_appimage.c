#include "update_internal.h"

#ifdef __linux__

#include "bongo_cat/file.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The bundle is mounted from the running process's own file descriptor, so
 * writing a sibling temporary file and renaming it over the original keeps the
 * live mount valid while the next launch picks up the replacement. */
#define APPIMAGE_SUFFIX_CAP 64
#define APPIMAGE_TIMEOUT_MS 900000L

typedef struct AppImageDownload {
    BongoCatUpdateService *service;
    int descriptor;
    uint64_t length;
    bool failed;
} AppImageDownload;

static bool cancelled(BongoCatUpdateService *service) {
    SDL_LockMutex(service->http_mutex);
    bool value = service->http_cancelled;
    SDL_UnlockMutex(service->http_mutex);
    return value;
}

static size_t write_download(const void *data, size_t size, size_t count,
    void *userdata) {
    AppImageDownload *download = userdata;
    if (size == 0 || count > SIZE_MAX / size) return 0;
    size_t amount = size * count, remaining = amount;
    const unsigned char *cursor = data;
    while (remaining) {
        ssize_t written = write(download->descriptor, cursor, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            download->failed = true;
            return 0;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    download->length += amount;
    return count;
}

static int download_progress(void *userdata, curl_off_t download_total,
    curl_off_t download_now, curl_off_t upload_total, curl_off_t upload_now) {
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return cancelled(userdata) ? 1 : 0;
}

/* A truncated download or a stray error page must never replace a working
 * bundle, so require the ELF and AppImage type 2 signatures. */
static bool valid_appimage(const char *path) {
    unsigned char header[12] = {0};
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    size_t size = fread(header, 1, sizeof(header), file);
    if (fclose(file) != 0) return false;
    return size == sizeof(header) &&
        memcmp(header, "\x7f" "ELF", 4) == 0 &&
        memcmp(header + 8, "AI\x02", 3) == 0;
}

bool bongo_cat_update_appimage_install(BongoCatUpdateService *service,
    const char *url, const char *target, char *error, size_t error_capacity) {
    if (!service || !url || !url[0] || !target || !target[0] ||
        !error || !error_capacity) return false;
    char temporary[BONGO_CAT_PATH_CAP + APPIMAGE_SUFFIX_CAP];
    int length = snprintf(temporary, sizeof(temporary), "%s.update-%llu",
        target, (unsigned long long)getpid());
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        snprintf(error, error_capacity, "The AppImage path is too long");
        return false;
    }
    /* The owner execute bit always survives the umask, so the replacement
     * stays runnable without a separate chmod. */
    int descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (descriptor < 0) {
        snprintf(error, error_capacity,
            "Cannot write the update next to the running AppImage");
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        close(descriptor);
        bongo_cat_file_remove(temporary);
        snprintf(error, error_capacity, "Cannot initialize the network client");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        close(descriptor);
        bongo_cat_file_remove(temporary);
        snprintf(error, error_capacity, "Cannot initialize the network client");
        return false;
    }
    AppImageDownload download = {service, descriptor, 0, false};
    struct curl_slist *headers = curl_slist_append(NULL,
        "Accept: application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BongoCat Update Checker/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, APPIMAGE_TIMEOUT_MS);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_download);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, service);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    if (result == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    /* Flush the bytes before the rename so a crash cannot leave a truncated
     * bundle in place of a working one. */
    bool synced = !download.failed && fsync(descriptor) == 0;
    bool closed = close(descriptor) == 0;
    bool aborted = cancelled(service) || result == CURLE_ABORTED_BY_CALLBACK;
    if (result != CURLE_OK || !synced || !closed || download.failed) {
        bongo_cat_file_remove(temporary);
        if (aborted) {
            snprintf(error, error_capacity, "The update was cancelled");
        } else if (download.failed || !synced || !closed) {
            snprintf(error, error_capacity,
                "Cannot write the downloaded AppImage");
        } else {
            snprintf(error, error_capacity, "Network error: %s",
                curl_easy_strerror(result));
        }
        return false;
    }
    if (status != 200) {
        bongo_cat_file_remove(temporary);
        snprintf(error, error_capacity, "GitHub returned HTTP status %ld",
            status);
        return false;
    }
    if (!download.length || !valid_appimage(temporary)) {
        bongo_cat_file_remove(temporary);
        snprintf(error, error_capacity,
            "The downloaded file is not a valid AppImage");
        return false;
    }
    if (!bongo_cat_file_replace(temporary, target, true)) {
        bongo_cat_file_remove(temporary);
        snprintf(error, error_capacity,
            "Cannot replace the running AppImage");
        return false;
    }
    return true;
}

#else

bool bongo_cat_update_appimage_install(BongoCatUpdateService *service,
    const char *url, const char *target, char *error, size_t error_capacity) {
    (void)service;
    (void)url;
    (void)target;
    (void)error;
    (void)error_capacity;
    return false;
}

#endif
