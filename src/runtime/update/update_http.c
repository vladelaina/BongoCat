#include "update_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

#define UPDATE_RESPONSE_LIMIT (1024u * 1024u)
#define UPDATE_INITIAL_CAPACITY 8192u

static bool track_handle(BongoCatUpdateService *service, void **slot,
    HINTERNET handle) {
    SDL_LockMutex(service->http_mutex);
    bool accepted = !service->http_cancelled;
    if (accepted) *slot = handle;
    SDL_UnlockMutex(service->http_mutex);
    if (!accepted) WinHttpCloseHandle(handle);
    return accepted;
}

static void release_handle(BongoCatUpdateService *service, void **slot,
    HINTERNET handle) {
    SDL_LockMutex(service->http_mutex);
    bool owned = *slot == handle;
    if (owned) *slot = NULL;
    SDL_UnlockMutex(service->http_mutex);
    if (owned) WinHttpCloseHandle(handle);
}

static bool cancelled(BongoCatUpdateService *service) {
    SDL_LockMutex(service->http_mutex);
    bool value = service->http_cancelled;
    SDL_UnlockMutex(service->http_mutex);
    return value;
}

static bool response_status(HINTERNET request, DWORD *status) {
    DWORD size = sizeof(*status);
    return WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, status, &size,
        WINHTTP_NO_HEADER_INDEX) != FALSE;
}

static const char *winhttp_error_name(DWORD code) {
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return "network timeout";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "DNS lookup failed";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "network connection refused";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "network connection error";
    case ERROR_WINHTTP_SECURE_FAILURE: return "TLS/SSL certificate error";
    default: return "network request failed";
    }
}

static char *read_response(BongoCatUpdateService *service,
    HINTERNET request) {
    size_t capacity = UPDATE_INITIAL_CAPACITY, length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return NULL;
    while (!cancelled(service)) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (!available) {
            buffer[length] = '\0';
            return buffer;
        }
        if (available > UPDATE_RESPONSE_LIMIT - length) break;
        size_t required = length + available + 1;
        if (required > capacity) {
            size_t next = capacity;
            while (next < required && next <= UPDATE_RESPONSE_LIMIT / 2)
                next *= 2;
            if (next < required) next = required;
            char *resized = next <= UPDATE_RESPONSE_LIMIT + 1
                ? realloc(buffer, next) : NULL;
            if (!resized) break;
            buffer = resized;
            capacity = next;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer + length, available, &read) ||
            !read) break;
        length += read;
    }
    free(buffer);
    return NULL;
}

BongoCatUpdateFetchResult bongo_cat_update_http_fetch(
    BongoCatUpdateService *service, char **response, char *error,
    size_t error_capacity) {
    *response = NULL;
    HINTERNET session = WinHttpOpen(L"BongoCat Update Checker/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session || !track_handle(service, &service->http_session, session))
        return cancelled(service) ? BONGO_CAT_UPDATE_FETCH_CANCELLED :
            BONGO_CAT_UPDATE_FETCH_NETWORK;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
    HINTERNET connection = WinHttpConnect(session, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    bool connected = connection && track_handle(service,
        &service->http_connection, connection);
    HINTERNET request = connected ? WinHttpOpenRequest(connection, L"GET",
        L"/repos/vladelaina/BongoCat/releases/latest", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE) : NULL;
    bool requested = request && track_handle(service,
        &service->http_request, request);
    static const wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    bool sent = requested && WinHttpSendRequest(request, headers,
        (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL);
    DWORD request_error = sent ? ERROR_SUCCESS : GetLastError();
    DWORD status = 0;
    bool valid_status = sent && response_status(request, &status) &&
        status == 200;
    if (valid_status) *response = read_response(service, request);
    if (request) release_handle(service, &service->http_request, request);
    if (connection) release_handle(service, &service->http_connection,
        connection);
    release_handle(service, &service->http_session, session);
    if (cancelled(service)) return BONGO_CAT_UPDATE_FETCH_CANCELLED;
    if (!sent) {
        snprintf(error, error_capacity, "Network error: %s (code %lu)",
            winhttp_error_name(request_error), (unsigned long)request_error);
        return BONGO_CAT_UPDATE_FETCH_NETWORK;
    }
    if (!valid_status) {
        snprintf(error, error_capacity,
            status == 404 ? "No published BongoCat release is available" :
            "GitHub returned HTTP status %lu", (unsigned long)status);
        return BONGO_CAT_UPDATE_FETCH_RESPONSE;
    }
    if (!*response) {
        snprintf(error, error_capacity, "Cannot read the GitHub response");
        return BONGO_CAT_UPDATE_FETCH_MEMORY;
    }
    return BONGO_CAT_UPDATE_FETCH_OK;
}

void bongo_cat_update_http_cancel(BongoCatUpdateService *service) {
    if (!service || !service->http_mutex) return;
    SDL_LockMutex(service->http_mutex);
    service->http_cancelled = true;
    HINTERNET request = service->http_request;
    HINTERNET connection = service->http_connection;
    HINTERNET session = service->http_session;
    service->http_request = service->http_connection =
        service->http_session = NULL;
    SDL_UnlockMutex(service->http_mutex);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
}

#endif
