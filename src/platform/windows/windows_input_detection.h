#ifndef BONGO_CAT_WINDOWS_INPUT_DETECTION_H
#define BONGO_CAT_WINDOWS_INPUT_DETECTION_H

#ifdef _WIN32
#include <stdbool.h>
#include <windows.h>

typedef enum WindowsPointerReason {
    WINDOWS_POINTER_DESKTOP,
    WINDOWS_POINTER_HIDDEN,
    WINDOWS_POINTER_CLIPPED,
    WINDOWS_POINTER_STALLED,
    WINDOWS_POINTER_RECENTERED
} WindowsPointerReason;

typedef struct WindowsPointerObservation {
    HWND foreground;
    DWORD pid;
    bool foreign, position_known, monitor_known, clip_known, cursor_known;
    POINT position;
    RECT monitor, clip;
    DWORD cursor_flags;
    double raw_x, raw_y;
    double absolute_x, absolute_y;
    unsigned long long motion_packets;
} WindowsPointerObservation;

/* Owned by the model thread; device statistics are transferred under its lock. */
typedef struct WindowsPointerDetection {
    HWND foreground;
    DWORD pid;
    bool initialized, relative, hint_pending, away, edge_seen;
    ULONGLONG last_ms, window_ms, hint_ms, evidence_ms;
    POINT anchor;
    double raw_x, raw_y;
    double absolute_x, absolute_y;
    unsigned moving_samples, returns;
    long long excursion;
    WindowsPointerReason reason;
} WindowsPointerDetection;

bool bongo_cat_windows_pointer_clip_locked(const RECT *clip);
bool bongo_cat_windows_pointer_detect(WindowsPointerDetection *state,
    const WindowsPointerObservation *sample, ULONGLONG now_ms);
#endif
#endif
