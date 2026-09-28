#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "update_internal.h"
#include "bongo_cat/i18n.h"
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

bool bongo_cat_update_start_package(const char *url) {
    (void)url;
    return false;
}

const char *bongo_cat_update_platform_asset(void) {
#ifdef _WIN64
    return "windows-x64";
#else
    return "windows-x86";
#endif
}

#else

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Unix packages are published as portable archives by default. The RPM
 * package uses a private runtime directory and an updater helper so its
 * package-managed installation can be upgraded in place. */
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
bool bongo_cat_update_platform_installed(void) {
#if defined(__linux__)
    static const char runtime_prefix[] = "/usr/libexec/bongocat/";
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable,
        sizeof(executable) - 1);
    if (length <= 0 || (size_t)length >= sizeof(executable)) return false;
    executable[length] = '\0';
    if (strncmp(executable, runtime_prefix,
            sizeof(runtime_prefix) - 1) != 0) return false;
    return access("/usr/bin/dnf", X_OK) == 0 &&
        access("/usr/bin/pkexec", X_OK) == 0;
#else
    return false;
#endif
}
bool bongo_cat_update_start_package(const char *url) {
#if defined(__linux__)
    static const char helper[] =
        "/usr/libexec/bongocat/bongocat-update-rpm.sh";
    if (!url || access(helper, X_OK) != 0) return false;
    char parent_pid[32];
    int length = snprintf(parent_pid, sizeof(parent_pid), "%ld",
        (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(parent_pid)) return false;
    pid_t first = fork();
    if (first < 0) return false;
    if (first == 0) {
        if (setsid() < 0) _exit(127);
        pid_t second = fork();
        if (second < 0) _exit(127);
        if (second == 0) {
            execl(helper, helper, "--url", url, "--pid", parent_pid,
                (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(first, &status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == first && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0;
#else
    (void)url;
    return false;
#endif
}
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
