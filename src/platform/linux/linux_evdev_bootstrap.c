#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "linux_evdev_bootstrap.h"
#include "linux_evdev_probe.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <linux/major.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#define EVDEV_DEVICE_DIRECTORY "/dev/input"
#define EVDEV_MAX_DEVICES 32
#define EVDEV_REMAP_BASE 64
#define EVDEV_ELEVATED_ARGUMENT "--bongocat-evdev-elevated"

#ifndef BONGO_CAT_SUDO_EXECUTABLE
#define BONGO_CAT_SUDO_EXECUTABLE "/usr/bin/sudo"
#endif

extern char **environ;

static void fail(const char *message) {
    fprintf(stderr, "BongoCat evdev startup: %s%s%s\n", message,
        errno ? ": " : "", errno ? strerror(errno) : "");
    exit(EXIT_FAILURE);
}

static bool enabled(void) {
    const char *option = getenv("BONGOCAT_ENABLE_EVDEV");
    return option && strcmp(option, "1") == 0;
}

static bool wayland_session(void) {
    const char *display = getenv("WAYLAND_DISPLAY");
    const char *session = getenv("XDG_SESSION_TYPE");
    return (display && display[0]) || (session && strcmp(session, "wayland") == 0);
}

static bool parse_id(const char *text, unsigned long *value) {
    if (!text || !text[0] || !value) return false;
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || end == text || *end) return false;
    *value = parsed;
    return true;
}

static size_t open_devices(int descriptors[EVDEV_MAX_DEVICES],
    char nodes[EVDEV_MAX_DEVICES][EVDEV_NODE_CAP], bool *denied) {
    DIR *directory = opendir(EVDEV_DEVICE_DIRECTORY);
    if (!directory) {
        if (errno == EACCES || errno == EPERM) *denied = true;
        return 0;
    }
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) && count < EVDEV_MAX_DEVICES) {
        BongoCatEvdevCapabilities capabilities;
        if (!bongo_cat_evdev_probe(entry->d_name, &capabilities)) continue;
        int fd = openat(dirfd(directory), entry->d_name,
            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if (errno == EACCES || errno == EPERM) *denied = true;
            continue;
        }
        struct stat info;
        if (fstat(fd, &info) || !S_ISCHR(info.st_mode) ||
            major(info.st_rdev) != INPUT_MAJOR) {
            close(fd);
            continue;
        }
        descriptors[count] = fd;
        memcpy(nodes[count], entry->d_name, strlen(entry->d_name) + 1);
        ++count;
    }
    closedir(directory);
    return count;
}

static void close_devices(const int descriptors[EVDEV_MAX_DEVICES], size_t count) {
    for (size_t i = 0; i < count; ++i) close(descriptors[i]);
}

static void descriptor_environment(const int descriptors[EVDEV_MAX_DEVICES],
    size_t count, char nodes[EVDEV_MAX_DEVICES][EVDEV_NODE_CAP], char output[1024]) {
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        int written = snprintf(output + used, 1024 - used, "%s%d:%s",
            i ? "," : "", descriptors[i], nodes[i]);
        if (written <= 0 || (size_t)written >= 1024 - used)
            fail("too many input descriptors");
        used += (size_t)written;
    }
}

static void remap_devices(int descriptors[EVDEV_MAX_DEVICES], size_t count) {
    int temporary[EVDEV_MAX_DEVICES];
    for (size_t i = 0; i < count; ++i) {
        temporary[i] = fcntl(descriptors[i], F_DUPFD_CLOEXEC, EVDEV_REMAP_BASE);
        if (temporary[i] < 0) fail("cannot preserve input descriptor");
    }
    close_devices(descriptors, count);
    for (size_t i = 0; i < count; ++i) {
        int target = 3 + (int)i;
        if (dup2(temporary[i], target) != target) fail("cannot inherit input descriptor");
        close(temporary[i]);
        descriptors[i] = target;
    }
}

static void protect_inherited_devices(const char *descriptors) {
    const char *cursor = descriptors;
    while (cursor && *cursor) {
        errno = 0;
        char *separator = NULL;
        long fd = strtol(cursor, &separator, 10);
        if (errno || separator == cursor || fd < 3 || fd > INT_MAX ||
            *separator != ':') {
            errno = 0;
            fail("invalid inherited input descriptor list");
        }
        int flags = fcntl((int)fd, F_GETFD);
        if (flags < 0 || fcntl((int)fd, F_SETFD, flags | FD_CLOEXEC) < 0)
            fail("cannot protect inherited input descriptor");
        const char *comma = strchr(separator + 1, ',');
        if (!comma) return;
        cursor = comma + 1;
    }
}

static void executable_path(char output[4096]) {
    ssize_t length = readlink("/proc/self/exe", output, 4095);
    if (length <= 0 || length >= 4095) fail("cannot locate BongoCat executable");
    output[length] = '\0';
}

static void elevate(int argc, char **argv) {
    char executable[4096];
    executable_path(executable);
    char **sudo_argv = calloc((size_t)argc + 5, sizeof(*sudo_argv));
    if (!sudo_argv) fail("cannot allocate sudo argument list");
    sudo_argv[0] = BONGO_CAT_SUDO_EXECUTABLE;
    sudo_argv[1] = "--preserve-env=DISPLAY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,"
        "XDG_SESSION_TYPE,DBUS_SESSION_BUS_ADDRESS";
    sudo_argv[2] = "--";
    sudo_argv[3] = executable;
    sudo_argv[4] = EVDEV_ELEVATED_ARGUMENT;
    for (int i = 1; i < argc; ++i) sudo_argv[i + 4] = argv[i];
    sudo_argv[argc + 4] = NULL;
    execv(BONGO_CAT_SUDO_EXECUTABLE, sudo_argv);
    fail("cannot execute sudo");
}

static void drop_privileges(void) {
    unsigned long original_uid, original_gid;
    if (!parse_id(getenv("SUDO_UID"), &original_uid) || !original_uid ||
        !parse_id(getenv("SUDO_GID"), &original_gid)) {
        errno = 0;
        fail("sudo did not provide a non-root caller identity");
    }
    if ((unsigned long)(uid_t)original_uid != original_uid ||
        (unsigned long)(gid_t)original_gid != original_gid) {
        errno = 0;
        fail("sudo caller identity is out of range");
    }
    struct passwd *account = getpwuid((uid_t)original_uid);
    if (!account || !account->pw_name || !account->pw_dir ||
        setenv("HOME", account->pw_dir, 1) || setenv("USER", account->pw_name, 1) ||
        setenv("LOGNAME", account->pw_name, 1))
        fail("cannot restore the invoking user's environment");
    if (setgroups(0, NULL) || setgid((gid_t)original_gid) ||
        setuid((uid_t)original_uid))
        fail("cannot drop elevated privileges");
    if (getuid() != (uid_t)original_uid || geteuid() != (uid_t)original_uid ||
        getgid() != (gid_t)original_gid || getegid() != (gid_t)original_gid) {
        errno = 0;
        fail("privilege drop verification failed");
    }
}

static void restart_unprivileged(int argc, char **argv) {
    char **child_argv = calloc((size_t)argc, sizeof(*child_argv));
    if (!child_argv) fail("cannot allocate argument list");
    child_argv[0] = argv[0];
    for (int i = 2; i < argc; ++i) child_argv[i - 1] = argv[i];
    child_argv[argc - 1] = NULL;
    execve("/proc/self/exe", child_argv, environ);
    fail("cannot restart BongoCat after dropping privileges");
}

void bongo_cat_linux_evdev_bootstrap(int argc, char **argv) {
    bool elevated = argc > 1 && strcmp(argv[1], EVDEV_ELEVATED_ARGUMENT) == 0;
    bool requested = enabled() && wayland_session();
    if (!elevated && !requested) return;
#if defined(BONGO_CAT_HAS_CUBISM)
    /* The bundled Linux GLEW loader initializes GLX, not an EGL-backed native
       Wayland context. Select XWayland before SDL is initialized; evdev still
       supplies global input for the surrounding Wayland session. */
    if (setenv("SDL_VIDEODRIVER", "x11", 1))
        fail("cannot select the X11 video driver for Cubism");
#endif
    if (elevated && geteuid() != 0) {
        errno = 0;
        fail("internal elevated mode requires root");
    }
    if (!elevated && geteuid() == 0) {
        errno = 0;
        fail("do not run BongoCat directly as root");
    }
    const char *inherited_environment = getenv("BONGOCAT_EVDEV_FDS");
    if (!elevated && inherited_environment) {
        protect_inherited_devices(inherited_environment);
        return;
    }

    int descriptors[EVDEV_MAX_DEVICES];
    char nodes[EVDEV_MAX_DEVICES][EVDEV_NODE_CAP];
    bool denied = false;
    size_t count = open_devices(descriptors, nodes, &denied);
    if (!elevated && denied) {
        close_devices(descriptors, count);
        elevate(argc, argv);
    }
    if (elevated && denied) {
        close_devices(descriptors, count);
        errno = EACCES;
        fail("root could not open every input device");
    }
    if (!count) {
        errno = 0;
        fail("no readable keyboard or pointer event devices were found");
    }
    if (elevated) remap_devices(descriptors, count);
    char inherited[1024] = {0};
    descriptor_environment(descriptors, count, nodes, inherited);
    if (setenv("BONGOCAT_EVDEV_FDS", inherited, 1) ||
        setenv("BONGOCAT_ENABLE_EVDEV", "1", 1))
        fail("cannot prepare evdev environment");
    if (!elevated) return;
    drop_privileges();
    restart_unprivileged(argc, argv);
}
