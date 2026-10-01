#include "update_internal.h"

#ifndef _WIN32

#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATE_RESPONSE_LIMIT (1024u * 1024u)

typedef struct UpdateResponseBuffer {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} UpdateResponseBuffer;

static bool cancelled(BongoCatUpdateService *service) {
    SDL_LockMutex(service->http_mutex);
    bool value = service->http_cancelled;
    SDL_UnlockMutex(service->http_mutex);
    return value;
}

static size_t write_response(const void *data, size_t size, size_t count,
    void *userdata) {
    UpdateResponseBuffer *buffer = userdata;
    if (!buffer || size == 0 || count > SIZE_MAX / size) return 0;
    size_t amount = size * count;
    if (amount > UPDATE_RESPONSE_LIMIT - buffer->length) {
        buffer->failed = true;
        return 0;
    }
    size_t required = buffer->length + amount + 1;
    if (required > buffer->capacity) {
        size_t next = buffer->capacity;
        while (next < required && next <= UPDATE_RESPONSE_LIMIT / 2)
            next *= 2;
        if (next < required) next = required;
        char *resized = realloc(buffer->data, next);
        if (!resized) {
            buffer->failed = true;
            return 0;
        }
        buffer->data = resized;
        buffer->capacity = next;
    }
    memcpy(buffer->data + buffer->length, data, amount);
    buffer->length += amount;
    buffer->data[buffer->length] = '\0';
    return count;
}

static int transfer_progress(void *userdata, curl_off_t download_total,
    curl_off_t download_now, curl_off_t upload_total, curl_off_t upload_now) {
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return cancelled(userdata) ? 1 : 0;
}

static const char *curl_error_name(CURLcode code) {
    switch (code) {
    case CURLE_OPERATION_TIMEDOUT: return "network timeout";
    case CURLE_COULDNT_RESOLVE_HOST: return "DNS lookup failed";
    case CURLE_COULDNT_CONNECT: return "network connection refused";
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION: return "TLS/SSL certificate error";
    case CURLE_ABORTED_BY_CALLBACK: return "request cancelled";
    default: return "network request failed";
    }
}

BongoCatUpdateFetchResult bongo_cat_update_http_fetch(
    BongoCatUpdateService *service, char **response, char *error,
    size_t error_capacity) {
    *response = NULL;
    if (!service || !error || !error_capacity)
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        snprintf(error, error_capacity, "Cannot initialize the network client");
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(error, error_capacity, "Cannot initialize the network client");
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    }
    SDL_LockMutex(service->http_mutex);
    bool accepted = !service->http_cancelled;
    if (accepted) service->http_request = curl;
    SDL_UnlockMutex(service->http_mutex);
    if (!accepted) {
        curl_easy_cleanup(curl);
        return BONGO_CAT_UPDATE_FETCH_CANCELLED;
    }

    UpdateResponseBuffer buffer = {
        .data = malloc(1), .length = 0, .capacity = 1, .failed = false
    };
    if (!buffer.data) {
        curl_easy_cleanup(curl);
        SDL_LockMutex(service->http_mutex);
        service->http_request = NULL;
        SDL_UnlockMutex(service->http_mutex);
        snprintf(error, error_capacity,
            "Cannot allocate the GitHub response buffer");
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    }
    buffer.data[0] = '\0';
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl, CURLOPT_URL,
        "https://api.github.com/repos/vladelaina/BongoCat/releases/latest");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BongoCat Update Checker/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, service);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    if (result == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    SDL_LockMutex(service->http_mutex);
    service->http_request = NULL;
    bool was_cancelled = service->http_cancelled;
    SDL_UnlockMutex(service->http_mutex);
    curl_easy_cleanup(curl);

    if (was_cancelled || result == CURLE_ABORTED_BY_CALLBACK) {
        free(buffer.data);
        return BONGO_CAT_UPDATE_FETCH_CANCELLED;
    }
    if (buffer.failed) {
        snprintf(error, error_capacity, "Cannot read the GitHub response");
        free(buffer.data);
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    }
    if (result != CURLE_OK) {
        snprintf(error, error_capacity, "Network error: %s (code %d)",
            curl_error_name(result), (int)result);
        free(buffer.data);
        return BONGO_CAT_UPDATE_FETCH_NETWORK;
    }
    if (status != 200) {
        snprintf(error, error_capacity,
            status == 404 ? "No published BongoCat release is available" :
            "GitHub returned HTTP status %ld", status);
        free(buffer.data);
        return BONGO_CAT_UPDATE_FETCH_RESPONSE;
    }
    *response = buffer.data;
    return BONGO_CAT_UPDATE_FETCH_OK;
}

void bongo_cat_update_http_cancel(BongoCatUpdateService *service) {
    if (!service || !service->http_mutex) return;
    SDL_LockMutex(service->http_mutex);
    service->http_cancelled = true;
    SDL_UnlockMutex(service->http_mutex);
}

#endif
