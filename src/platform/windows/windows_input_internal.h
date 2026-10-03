#ifndef BONGO_CAT_WINDOWS_INPUT_INTERNAL_H
#define BONGO_CAT_WINDOWS_INPUT_INTERNAL_H

#include "windows_input.h"
#include "windows_input_detection.h"
#include "windows_keys.h"
#include "bongo_cat/log.h"

#ifdef _WIN32
#define BONGO_CAT_WINDOWS_RAW_DEVICE_LIMIT 64
#define BONGO_CAT_WINDOWS_RAW_HELD_LIMIT 256
#define BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT 5

typedef struct WindowsRawHeld {
    char name[16];
    unsigned references;
    bool emitted;
} WindowsRawHeld;

typedef struct WindowsRawDevice {
    struct WindowsRawDevice *next;
    HANDLE handle;
    unsigned short keys[BONGO_CAT_WINDOWS_RAW_KEY_COUNT];
    bool buttons[BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT];
    bool absolute_known, pending_e1;
    RECT absolute_bounds;
    double absolute_x, absolute_y;
    unsigned long long generation;
} WindowsRawDevice;

typedef struct WindowsInputState {
    BongoCatPlatform *platform;
    SRWLOCK platform_lock;
    SRWLOCK relative_lock;
    HANDLE thread, stop, ready;
    volatile LONG references;
    HWND window;
    ATOM window_class;
    bool registered;
    DWORD startup_error, read_error, registration_error;
    DWORD recovery_error;
    /* Receiver-thread-only cumulative diagnostics; never store typed content. */
    unsigned long long diagnostic_keys, diagnostic_mouse, diagnostic_invalid;
    unsigned long long diagnostic_background_keys, diagnostic_filtered_keys;
    unsigned long long diagnostic_duplicate_keys;
    unsigned long long diagnostic_device_drops, diagnostic_read_failures;
    unsigned long long diagnostic_queued_keys, diagnostic_queue_failures;
    unsigned long long diagnostic_wake_failures, diagnostic_device_changes;
    ULONGLONG diagnostic_ms;
    DWORD mouse_registration_flags, keyboard_registration_flags;
    DWORD test_start_delay_ms;
    unsigned ownership;
    WindowsRawDevice *devices;
    unsigned device_count;
    WindowsRawHeld keys[BONGO_CAT_WINDOWS_RAW_HELD_LIMIT];
    WindowsRawHeld buttons[BONGO_CAT_WINDOWS_MOUSE_BUTTON_COUNT];
    bool desktop_unavailable, wake_pending, retry_events;
    /* Shared motion buffers are protected by relative_lock. Detection and
       model consumption have separate totals; neither drains the other's data. */
    bool relative_active, receiving;
    long long relative_x, relative_y;
    double absolute_x, absolute_y;
    unsigned long long relative_samples, absolute_samples, generation;
    long long observed_x, observed_y;
    double observed_absolute_x, observed_absolute_y;
    unsigned long long observed_motion, observed_generation;
    /* Model-thread-only state. The receiver publishes resets through generation. */
    WindowsPointerDetection pointer_detection;
    ULONGLONG pointer_probe_ms;
    unsigned long long pointer_generation;
    bool pointer_probe_ready;
} WindowsInputState;

bool bongo_cat_windows_input_push_event(WindowsInputState *state,
    BongoCatInputKind kind, const char *name, float value);
void bongo_cat_windows_input_wake(WindowsInputState *state);
void bongo_cat_windows_input_flush(WindowsInputState *state);
void bongo_cat_windows_input_packet(WindowsInputState *state,
    const RAWINPUT *packet, UINT bytes);
WindowsRawDevice *bongo_cat_windows_input_device(WindowsInputState *state,
    HANDLE handle);
void bongo_cat_windows_input_remove_device(WindowsInputState *state, HANDLE handle);
void bongo_cat_windows_input_clear_devices(WindowsInputState *state);
void bongo_cat_windows_input_key(WindowsInputState *state,
    WindowsRawDevice *device, const RAWKEYBOARD *key);
void bongo_cat_windows_input_buttons(WindowsInputState *state,
    WindowsRawDevice *device, USHORT flags);
void bongo_cat_windows_input_motion(WindowsInputState *state,
    WindowsRawDevice *device, const RAWMOUSE *mouse, const RECT *bounds);
void bongo_cat_windows_input_clear_motion(WindowsInputState *state);
unsigned long long bongo_cat_windows_input_take_observation(
    WindowsInputState *state, WindowsPointerObservation *sample);
bool bongo_cat_windows_input_register(WindowsInputState *state);
void bongo_cat_windows_input_unregister(WindowsInputState *state);
unsigned bongo_cat_windows_input_ownership(WindowsInputState *state);
unsigned bongo_cat_windows_input_restore(WindowsInputState *state, unsigned owned);
bool bongo_cat_windows_input_receiver_create(WindowsInputState *state);
void bongo_cat_windows_input_receiver_destroy(WindowsInputState *state);
bool bongo_cat_windows_input_dispatch(WindowsInputState *state);
#endif
#endif
