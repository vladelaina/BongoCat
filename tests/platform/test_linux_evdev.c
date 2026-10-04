#include "linux_evdev_internal.h"
#include "test.h"
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

int bongo_cat_test_failures;
static BongoCatInputState input;
static LinuxPlatformState native;
static BongoCatPlatform platform = {.input = &input, .native = &native};
static LinuxEvdevState state;

static void send_event(EvdevDevice *device, unsigned type, unsigned code, int value) {
    struct input_event event = {.type = type, .code = code, .value = value};
    bongo_cat_evdev_event(&state, device, &event);
}

static void expect(BongoCatInputKind kind, const char *name) {
    BongoCatInputEvent event = {0};
    CHECK(bongo_cat_input_pop(&input, &event));
    CHECK(event.kind == kind);
    CHECK(strcmp(event.name, name) == 0);
}

static void expect_empty(void) {
    BongoCatInputEvent event;
    CHECK(!bongo_cat_input_pop(&input, &event));
}

static void test_policy_and_masks(void) {
    CHECK(!bongo_cat_linux_evdev_requested(NULL, true));
    CHECK(!bongo_cat_linux_evdev_requested("", true));
    CHECK(!bongo_cat_linux_evdev_requested("0", true));
    CHECK(!bongo_cat_linux_evdev_requested("true", true));
    CHECK(!bongo_cat_linux_evdev_requested("1", false));
    CHECK(bongo_cat_linux_evdev_requested("1", true));
    BongoCatError error = {0};
    CHECK(!bongo_cat_linux_evdev_start(&platform, &error));
    CHECK(native.evdev == NULL);
    CHECK(!bongo_cat_linux_evdev_pointer_active(&platform));
    CHECK(bongo_cat_evdev_node_valid("event0"));
    CHECK(bongo_cat_evdev_node_valid("event123"));
    CHECK(!bongo_cat_evdev_node_valid(NULL));
    CHECK(!bongo_cat_evdev_node_valid("event"));
    CHECK(!bongo_cat_evdev_node_valid("event1/../event2"));
    CHECK(!bongo_cat_evdev_node_valid("event1x"));
    CHECK(!bongo_cat_evdev_node_valid("event12345678901234567890123456789"));
    CHECK(bongo_cat_evdev_mask_bit("1 0", 64, 64));
    CHECK(!bongo_cat_evdev_mask_bit("1 0", 64, 0));
    CHECK(bongo_cat_evdev_mask_bit("1 0 0", 32, 64));
    CHECK(bongo_cat_evdev_mask_bit("8000000000000000\n", 64, 63));
    CHECK(bongo_cat_evdev_mask_bit("80000000", 32, 31));
    CHECK(!bongo_cat_evdev_mask_bit("100000000", 32, 32));
    CHECK(!bongo_cat_evdev_mask_bit("-1", 64, 0));
    CHECK(!bongo_cat_evdev_mask_bit("1x", 64, 0));
    CHECK(!bongo_cat_evdev_mask_bit("10000000000000000", 64, 0));
    CHECK(!bongo_cat_evdev_mask_bit("", 64, 0));
    CHECK(!bongo_cat_evdev_mask_bit("1", 64, KEY_CNT));
}

static void test_multiple_devices(void) {
    EvdevDevice a = {.keyboard = true}, b = {.keyboard = true};
    send_event(&a, EV_KEY, KEY_A, 1);
    send_event(&a, EV_KEY, KEY_A, 1);
    send_event(&a, EV_KEY, KEY_A, 2);
    send_event(&b, EV_KEY, KEY_A, 1);
    expect(BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    bongo_cat_evdev_release(&state, &a);
    expect_empty();
    bongo_cat_evdev_release(&state, &b);
    expect(BONGO_CAT_INPUT_KEY_UP, "KeyA");
    expect_empty();
    send_event(&a, EV_KEY, KEY_LEFTMETA, 1);
    send_event(&a, EV_KEY, KEY_RIGHTMETA, 1);
    send_event(&a, EV_KEY, KEY_LEFTMETA, 0);
    expect(BONGO_CAT_INPUT_KEY_DOWN, "Meta");
    expect_empty();
    send_event(&a, EV_KEY, KEY_RIGHTMETA, 0);
    expect(BONGO_CAT_INPUT_KEY_UP, "Meta");
    send_event(&a, EV_KEY, KEY_ENTER, 1);
    send_event(&b, EV_KEY, KEY_KPENTER, 1);
    expect(BONGO_CAT_INPUT_KEY_DOWN, "Return");
    bongo_cat_evdev_release(&state, &a);
    expect_empty();
    bongo_cat_evdev_release(&state, &b);
    expect(BONGO_CAT_INPUT_KEY_UP, "Return");
    send_event(&a, EV_KEY, KEY_CNT, 1);
    send_event(&a, EV_KEY, KEY_A, -1);
    expect_empty();
}

static void test_overflow_and_motion(void) {
    EvdevDevice device = {.keyboard = true, .pointer = true, .relative = true};
    send_event(&device, EV_KEY, KEY_LEFTCTRL, 1);
    expect(BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    CHECK(bongo_cat_input_control_down(&input));
    send_event(&device, EV_SYN, SYN_DROPPED, 0);
    expect(BONGO_CAT_INPUT_KEY_UP, "ControlLeft");
    CHECK(!bongo_cat_input_control_down(&input));
    send_event(&device, EV_KEY, KEY_B, 1);
    send_event(&device, EV_REL, REL_X, 10);
    expect_empty();
    CHECK(state.relative_x == 0);
    send_event(&device, EV_SYN, SYN_REPORT, 0);
    send_event(&device, EV_KEY, KEY_B, 1);
    expect(BONGO_CAT_INPUT_KEY_DOWN, "KeyB");
    bongo_cat_evdev_release(&state, &device);
    expect(BONGO_CAT_INPUT_KEY_UP, "KeyB");
    send_event(&device, EV_KEY, BTN_SIDE, 1);
    send_event(&device, EV_KEY, BTN_SIDE, 0);
    expect(BONGO_CAT_INPUT_MOUSE_DOWN, "Back");
    expect(BONGO_CAT_INPUT_MOUSE_UP, "Back");
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    send_event(&device, EV_REL, REL_X, 12);
    send_event(&device, EV_REL, REL_Y, -7);
    CHECK(SDL_HasEvent(platform.wake_event_type));
    native.evdev = &state;
    double x = 0, y = 0;
    CHECK(bongo_cat_linux_evdev_relative_pointer(&platform, &x, &y));
    CHECK(x == 12 && y == -7);
    CHECK(!bongo_cat_linux_evdev_relative_pointer(&platform, &x, &y));
    send_event(&device, EV_REL, REL_X, INT_MAX);
    send_event(&device, EV_REL, REL_X, INT_MAX);
    CHECK(bongo_cat_linux_evdev_relative_pointer(&platform, &x, &y));
    CHECK(x == 32768 && y == 0);
    device.relative = false;
    send_event(&device, EV_REL, REL_X, 10);
    CHECK(!bongo_cat_linux_evdev_relative_pointer(&platform, &x, &y));
    native.evdev = NULL;
}

static void test_read_and_disconnect(void) {
    int pipes[2];
    CHECK(pipe(pipes) == 0);
    CHECK(fcntl(pipes[0], F_SETFL, O_NONBLOCK) == 0);
    state.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    CHECK(state.epoll_fd >= 0);
    state.device_count = 1;
    EvdevDevice *device = &state.devices[0];
    *device = (EvdevDevice){.fd = pipes[0], .keyboard = true, .relative = true};
    CHECK(bongo_cat_evdev_read(&state, device));
    struct input_event event = {.type = EV_KEY, .code = KEY_A, .value = 1};
    CHECK(write(pipes[1], &event, sizeof(event)) == sizeof(event));
    CHECK(bongo_cat_evdev_read(&state, device));
    expect(BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    close(pipes[1]);
    CHECK(!bongo_cat_evdev_read(&state, device));
    bongo_cat_evdev_remove(&state, 0);
    expect(BONGO_CAT_INPUT_KEY_UP, "KeyA");
    CHECK(state.device_count == 0);
    CHECK(!atomic_load(&state.pointer_active));
    close(state.epoll_fd);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS));
    bongo_cat_input_init(&input);
    platform.wake_event_type = SDL_RegisterEvents(1);
    state.platform = &platform;
    state.motion_lock = SDL_CreateMutex();
    CHECK(state.motion_lock != NULL);
    atomic_init(&state.pointer_active, false);
    test_policy_and_masks();
    test_multiple_devices();
    test_overflow_and_motion();
    test_read_and_disconnect();
    expect_empty();
    SDL_DestroyMutex(state.motion_lock);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
