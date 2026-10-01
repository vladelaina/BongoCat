#include "macos_internal.h"
#include "macos_keys.h"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

typedef struct MacInputState {
    BongoCatPlatform *platform;
    SDL_Thread *thread;
    SDL_Semaphore *ready;
    CFMachPortRef tap;
    CFRunLoopSourceRef source;
    CFRunLoopRef loop;
    bool key_down[BONGO_CAT_INPUT_KEY_STATE_CAP];
    atomic_bool supported;
    atomic_bool stop_requested;
    atomic_uint references;
} MacInputState;

static atomic_bool global_supported = ATOMIC_VAR_INIT(false);

static void release_state(MacInputState *state) {
    if (atomic_fetch_sub(&state->references, 1) != 1) return;
    SDL_DestroySemaphore(state->ready);
    free(state);
}

static void push(MacInputState *state, BongoCatInputKind kind,
    const char *name, float value) {
    if (!state || !name) return;
    BongoCatInputEvent input = {.kind = kind, .timestamp_ms = SDL_GetTicks(), .value = value};
    snprintf(input.name, sizeof(input.name), "%s", name);
    if (bongo_cat_input_push(state->platform->input, &input)) {
        SDL_Event wake = {0};
        wake.type = state->platform->wake_event_type;
        SDL_PushEvent(&wake);
    }
}

static CGEventFlags modifier_flag(CGKeyCode code) {
    if (code == 56 || code == 60) return kCGEventFlagMaskShift;
    if (code == 59 || code == 62) return kCGEventFlagMaskControl;
    if (code == 58 || code == 61) return kCGEventFlagMaskAlternate;
    if (code == 54 || code == 55) return kCGEventFlagMaskCommand;
    if (code == 57) return kCGEventFlagMaskAlphaShift;
    return 0;
}

static CGEventRef event_tap(CGEventTapProxy proxy, CGEventType type,
    CGEventRef event, void *userdata) {
    (void)proxy;
    MacInputState *state = userdata;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        memset(state->key_down, 0, sizeof(state->key_down));
        if (state->tap) CGEventTapEnable(state->tap, true);
        return event;
    }
    if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
        type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged) {
        CGPoint point = CGEventGetLocation(event);
        if (bongo_cat_input_mouse(state->platform->input, point.x, point.y)) {
            SDL_Event wake = {0};
            wake.type = state->platform->wake_event_type;
            SDL_PushEvent(&wake);
        }
        return event;
    }
    if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
        CGKeyCode code = (CGKeyCode)CGEventGetIntegerValueField(event,
            kCGKeyboardEventKeycode);
        char buffer[16]; const char *name = bongo_cat_macos_key_name(code, buffer);
        bool down = type == kCGEventKeyDown;
        if (type == kCGEventFlagsChanged) {
            CGEventFlags flag = modifier_flag(code);
            down = flag && (CGEventGetFlags(event) & flag) != 0;
        }
        if (name && bongo_cat_input_edge(state->key_down, code, down))
            push(state, down ? BONGO_CAT_INPUT_KEY_DOWN :
                BONGO_CAT_INPUT_KEY_UP, name, down ? 1.0f : 0.0f);
        return event;
    }
    const char *name = type == kCGEventLeftMouseDown || type == kCGEventLeftMouseUp
        ? "Left" : type == kCGEventRightMouseDown || type == kCGEventRightMouseUp
        ? "Right" : "Middle";
    bool down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
        type == kCGEventOtherMouseDown;
    push(state, down ? BONGO_CAT_INPUT_MOUSE_DOWN : BONGO_CAT_INPUT_MOUSE_UP,
        name, down ? 1.0f : 0.0f);
    return event;
}

static int SDLCALL input_thread(void *userdata) {
    MacInputState *state = userdata;
    @autoreleasepool {
        CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
            CGEventMaskBit(kCGEventKeyUp) | CGEventMaskBit(kCGEventFlagsChanged) |
            CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
            CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
            CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp) |
            CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDragged) |
            CGEventMaskBit(kCGEventRightMouseDragged) | CGEventMaskBit(kCGEventOtherMouseDragged);
        /* The permission this listener is built with, kept only to word the
           warning below; the page reads the current permission itself. */
        bool permitted = false;
        if (@available(macOS 10.15, *)) permitted = CGPreflightListenEventAccess();
        state->tap = CGEventTapCreate(kCGSessionEventTap, kCGTailAppendEventTap,
            kCGEventTapOptionListenOnly, mask, event_tap, state);
        if (state->tap) {
            state->source = CFMachPortCreateRunLoopSource(NULL, state->tap, 0);
        }
        if (state->source && !atomic_load(&state->stop_requested)) {
            state->loop = CFRunLoopGetCurrent(); CFRetain(state->loop);
            CFRunLoopAddSource(state->loop, state->source, kCFRunLoopCommonModes);
            CGEventTapEnable(state->tap, true);
            atomic_store(&state->supported, true);
            atomic_store(&global_supported, true);
        }
        SDL_SignalSemaphore(state->ready);
        if (!atomic_load(&state->supported)) {
            /* Not a permission verdict: a listener can also fail to build with
               the permission already granted. */
            if (permitted)
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "macOS input listener failed to initialize");
            else
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "macOS input "
                    "monitoring permission is required for global keyboard input");
        }
        while (atomic_load(&state->supported) &&
            !atomic_load(&state->stop_requested))
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        if (state->loop && state->source)
            CFRunLoopRemoveSource(state->loop, state->source, kCFRunLoopCommonModes);
        if (state->tap) CFMachPortInvalidate(state->tap);
        if (state->source) CFRelease(state->source);
        if (state->tap) CFRelease(state->tap);
        if (state->loop) CFRelease(state->loop);
        atomic_store(&global_supported, false);
    }
    release_state(state);
    return 0;
}

bool bongo_cat_macos_input_start(BongoCatPlatform *platform, BongoCatError *error) {
    MacInputState *state = calloc(1, sizeof(*state));
    if (!state) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate macOS input state");
        return false;
    }
    state->platform = platform;
    atomic_init(&state->supported, false);
    atomic_init(&state->stop_requested, false);
    /* Caller and worker each release one reference, including after timeout. */
    atomic_init(&state->references, 2);
    state->ready = SDL_CreateSemaphore(0);
    state->thread = state->ready ? SDL_CreateThread(input_thread,
        BONGO_CAT_SLUG "-macos-input", state) : NULL;
    if (!state->ready || !state->thread) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot start the macOS input listener");
        if (state->thread) SDL_WaitThread(state->thread, NULL);
        if (state->ready) SDL_DestroySemaphore(state->ready);
        free(state);
        return false;
    }
    platform->native = state;
    return true;
}

void bongo_cat_macos_input_stop(BongoCatPlatform *platform) {
    MacInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    atomic_store(&state->stop_requested, true);
    if (state->thread &&
        !SDL_WaitSemaphoreTimeout(state->ready, 3000)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "macOS input thread did not initialize before shutdown");
        SDL_DetachThread(state->thread);
        platform->native = NULL;
        release_state(state);
        return;
    }
    if (state->thread) SDL_WaitThread(state->thread, NULL);
    platform->native = NULL;
    release_state(state);
}

bool bongo_cat_macos_input_supported(void) { return atomic_load(&global_supported); }

bool bongo_cat_macos_input_monitoring_authorized(void) {
    if (@available(macOS 10.15, *)) return CGPreflightListenEventAccess();
    return false;
}

bool bongo_cat_macos_input_monitoring_request(void) {
    if (@available(macOS 10.15, *)) return CGRequestListenEventAccess();
    return false;
}
#endif
