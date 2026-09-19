#include "update_internal.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "preferences_notice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "windows_package.h"
#include <windows.h>

bool bongo_cat_update_platform_supported(void) {
    return true;
}

bool bongo_cat_update_platform_store(void) {
    return bongo_cat_windows_is_packaged();
}

static bool current_executable(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    return length > 0 && length < capacity;
}

bool bongo_cat_update_platform_installed(void) {
    wchar_t running[2048] = {0};
    if (!current_executable(running, _countof(running))) return false;
    const DWORD views[] = {
        RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY
    };
    const wchar_t *keys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BongoCat_is1",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BongoCat"
    };
    for (size_t key = 0; key < _countof(keys); ++key) {
        for (size_t index = 0; index < _countof(views); ++index) {
            wchar_t install_root[2048] = {0};
            DWORD size = sizeof(install_root);
            LSTATUS result = RegGetValueW(HKEY_CURRENT_USER,
                keys[key],
                L"InstallLocation", RRF_RT_REG_SZ | views[index], NULL,
                install_root, &size);
            if (result != ERROR_SUCCESS || !install_root[0]) continue;
            size_t length = wcslen(install_root);
            while (length && (install_root[length - 1] == L'\\' ||
                install_root[length - 1] == L'/')) install_root[--length] = L'\0';
            static const wchar_t executable[] = L"\\BongoCat.exe";
            if (length + _countof(executable) > _countof(install_root)) continue;
            memcpy(install_root + length, executable, sizeof(executable));
            if (_wcsicmp(running, install_root) == 0) return true;
        }
    }
    return false;
}

const char *bongo_cat_update_platform_asset(void) {
#ifdef _WIN64
    return "windows-x64";
#else
    return "windows-x86";
#endif
}

bool bongo_cat_update_platform_appimage(char *path, size_t capacity) {
    (void)path;
    (void)capacity;
    return false;
}

#else

/* Unix packages are published as a single archive for each architecture.
 * They do not have an installer registration to inspect, but they can still
 * use the release API and open the matching archive when an update exists. */
bool bongo_cat_update_platform_supported(void) {
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
    return true;
#else
    return false;
#endif
#elif defined(__linux__) && (defined(__x86_64__) || defined(__amd64__))
    return true;
#else
    return false;
#endif
}
bool bongo_cat_update_platform_store(void) { return false; }
bool bongo_cat_update_platform_installed(void) { return false; }
const char *bongo_cat_update_platform_asset(void) {
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return "macos-arm64";
#else
    return "macos-x64";
#endif
#elif defined(__x86_64__) || defined(__amd64__)
    return "linux-x64";
#else
    return "unsupported";
#endif
}

/* The AppImage runtime exports the absolute path of the bundle it mounted, so
 * its presence is the only reliable signal that this build can replace
 * itself. */
bool bongo_cat_update_platform_appimage(char *path, size_t capacity) {
#if defined(__linux__)
    if (!path || !capacity) return false;
    const char *bundle = SDL_getenv("APPIMAGE");
    if (!bundle || !bundle[0] || strlen(bundle) >= capacity) return false;
    if (!bongo_cat_path_is_file(bundle)) return false;
    snprintf(path, capacity, "%s", bundle);
    return true;
#else
    (void)path;
    (void)capacity;
    return false;
#endif
}

#endif

static const char *tr(BongoCatUpdateService *service, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(service->app->i18n, key, fallback);
}

void bongo_cat_update_show_completion(BongoCatUpdateService *service) {
    BongoCatUpdateSnapshot snapshot;
    bongo_cat_update_snapshot(service, &snapshot);
    char message[192];
    if (snapshot.status == BONGO_CAT_UPDATE_CURRENT) {
        snprintf(message, sizeof(message), "%s v%s", tr(service,
            "native.support.latest", "Already up to date"), BONGO_CAT_VERSION);
        bongo_cat_preferences_notice_show(service->app, message, false);
    } else if (snapshot.status == BONGO_CAT_UPDATE_AVAILABLE) {
        snprintf(message, sizeof(message), "%s v%s", tr(service,
            "native.support.updateAvailable", "New version available:"),
            snapshot.release.version);
        bongo_cat_preferences_notice_show(service->app, message, false);
    } else if (snapshot.status == BONGO_CAT_UPDATE_INSTALLED) {
        snprintf(message, sizeof(message), tr(service,
            "native.support.updateInstalled", "Restart to apply v%s"),
            snapshot.release.version);
        bongo_cat_preferences_notice_show(service->app, message, false);
    } else if (snapshot.status == BONGO_CAT_UPDATE_ERROR) {
        char detail[384], status_text[16];
        static const char prefix[] = "GitHub returned HTTP status ";
        const char *value = snapshot.error + sizeof(prefix) - 1;
        char *end = NULL;
        unsigned long status = strncmp(snapshot.error, prefix,
            sizeof(prefix) - 1) == 0 ? strtoul(value, &end, 10) : 0;
        if (status && end && !*end) {
            snprintf(status_text, sizeof(status_text), "%lu", status);
            snprintf(detail, sizeof(detail), tr(service,
                "native.support.updateHttpFailed",
                "GitHub returned HTTP status %s"), status_text);
        } else snprintf(detail, sizeof(detail), "%s", snapshot.error[0] ?
            snapshot.error : tr(service, "native.support.updateFailed",
                "Unable to check for updates"));
        bongo_cat_preferences_notice_show(service->app, detail, true);
    }
}
