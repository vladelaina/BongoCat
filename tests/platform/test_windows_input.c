#include "test.h"
#include "windows_input_internal.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <string.h>

int bongo_cat_test_failures;
void test_windows_relative_sources(void);
void test_windows_raw_receiver(void);
void test_windows_pointer_detection(void);
void test_windows_mouse_mapping(void);

typedef struct RawFixture {
    WindowsInputState state;
    BongoCatInputState input;
    BongoCatPlatform platform;
} RawFixture;

static void initialize(RawFixture *test) {
    bongo_cat_input_init(&test->input);
    test->platform.input = &test->input;
    test->platform.native = &test->state;
    test->state.platform = &test->platform;
    InitializeSRWLock(&test->state.platform_lock);
    InitializeSRWLock(&test->state.relative_lock);
}

static void key_packet(RawFixture *test, uintptr_t device, USHORT code,
    USHORT scan, USHORT flags) {
    RAWINPUT packet = {0};
    packet.header.dwSize = offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD);
    packet.header.dwType = RIM_TYPEKEYBOARD;
    packet.header.hDevice = (HANDLE)device;
    packet.header.wParam = RIM_INPUTSINK;
    packet.data.keyboard = (RAWKEYBOARD){.VKey = code, .MakeCode = scan,
        .Flags = flags};
    bongo_cat_windows_input_packet(&test->state, &packet, packet.header.dwSize);
}

static void expect(RawFixture *test, BongoCatInputKind kind, const char *name) {
    BongoCatInputEvent event = {0};
    CHECK(bongo_cat_input_pop(&test->input, &event));
    CHECK(event.kind == kind && strcmp(event.name, name) == 0);
    CHECK(event.timestamp_ms <= SDL_GetTicks());
}

static void expect_empty(RawFixture *test) {
    BongoCatInputEvent event;
    CHECK(!bongo_cat_input_pop(&test->input, &event));
}

static void test_key_devices(void) {
    RawFixture test = {0};
    initialize(&test);
    key_packet(&test, 1, 'A', 0x1e, 0);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    key_packet(&test, 1, 'A', 0x1e, 0);
    key_packet(&test, 2, 'A', 0x1e, 0);
    key_packet(&test, 1, 'A', 0x1e, RI_KEY_BREAK);
    expect_empty(&test);
    bongo_cat_windows_input_remove_device(&test.state, (HANDLE)(uintptr_t)2);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "KeyA");
    key_packet(&test, 1, VK_CONTROL, 0x1d, 0);
    key_packet(&test, 1, VK_CONTROL, 0x1d, RI_KEY_E0);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "ControlRight");
    key_packet(&test, 1, VK_CONTROL, 0x1d, RI_KEY_BREAK);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "ControlLeft");
    CHECK(bongo_cat_input_control_down(&test.input));
    bongo_cat_windows_input_clear_devices(&test.state);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "ControlRight");
    CHECK(!bongo_cat_input_control_down(&test.input));
    CHECK(test.state.device_count == 0);
}

static void test_key_sequences(void) {
    RawFixture test = {0};
    initialize(&test);
    key_packet(&test, 0, VK_NUMPAD1, 0x4f, 0);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "Kp1");
    key_packet(&test, 0, VK_END, 0x4f, RI_KEY_BREAK);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "Kp1");
    key_packet(&test, 0, VK_RETURN, 0x1c, 0);
    key_packet(&test, 0, VK_RETURN, 0x1c, RI_KEY_E0);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "Return");
    key_packet(&test, 0, VK_RETURN, 0x1c, RI_KEY_BREAK);
    expect_empty(&test);
    key_packet(&test, 0, VK_RETURN, 0x1c, RI_KEY_E0 | RI_KEY_BREAK);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "Return");
    key_packet(&test, 0, VK_SHIFT, 0x2a, RI_KEY_E0);
    key_packet(&test, 0, 255, 0, 0);
    key_packet(&test, 0, 'A', 0xff, 0);
    expect_empty(&test);
    key_packet(&test, 0, VK_CONTROL, 0x1d, RI_KEY_E1);
    key_packet(&test, 0, VK_NUMLOCK, 0x45, 0);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "Pause");
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "Pause");
    CHECK(!bongo_cat_input_control_down(&test.input));
    bongo_cat_windows_input_clear_devices(&test.state);
    expect_empty(&test);
}

static void test_buttons(void) {
    RawFixture test = {0};
    initialize(&test);
    WindowsRawDevice *a = bongo_cat_windows_input_device(&test.state, NULL);
    WindowsRawDevice *b = bongo_cat_windows_input_device(&test.state,
        (HANDLE)(uintptr_t)1);
    CHECK(a && b);
    if (!a || !b) { bongo_cat_windows_input_clear_devices(&test.state); return; }
    bongo_cat_windows_input_buttons(&test.state, a,
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN);
    expect(&test, BONGO_CAT_INPUT_MOUSE_DOWN, "Left");
    expect(&test, BONGO_CAT_INPUT_MOUSE_DOWN, "Back");
    bongo_cat_windows_input_buttons(&test.state, b, RI_MOUSE_LEFT_BUTTON_DOWN);
    bongo_cat_windows_input_remove_device(&test.state, NULL);
    expect(&test, BONGO_CAT_INPUT_MOUSE_UP, "Back");
    expect_empty(&test);
    bongo_cat_windows_input_buttons(&test.state, b, RI_MOUSE_LEFT_BUTTON_UP);
    expect(&test, BONGO_CAT_INPUT_MOUSE_UP, "Left");
    bongo_cat_windows_input_buttons(&test.state, b,
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP);
    expect(&test, BONGO_CAT_INPUT_MOUSE_DOWN, "Right");
    expect(&test, BONGO_CAT_INPUT_MOUSE_UP, "Right");
    bongo_cat_windows_input_clear_devices(&test.state);
}

static void fill_queue(RawFixture *test, bool recovery) {
    BongoCatInputEvent event = {.kind = BONGO_CAT_INPUT_KEY_DOWN};
    for (unsigned i = 1; i < BONGO_CAT_INPUT_QUEUE_CAP; ++i)
        CHECK(bongo_cat_input_push(&test->input, &event));
    if (!recovery) return;
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    for (unsigned i = 1; i < BONGO_CAT_INPUT_RECOVERY_CAP; ++i)
        CHECK(bongo_cat_input_push(&test->input, &event));
}

static void drain(RawFixture *test) {
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&test->input, &event)) {}
}

static void test_backpressure(void) {
    RawFixture test = {0};
    initialize(&test);
    fill_queue(&test, false);
    key_packet(&test, 1, 'A', 0x1e, 0);
    CHECK(test.state.retry_events);
    drain(&test);
    bongo_cat_windows_input_flush(&test.state);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    fill_queue(&test, true);
    bongo_cat_windows_input_clear_devices(&test.state);
    CHECK(test.state.retry_events);
    drain(&test);
    bongo_cat_windows_input_flush(&test.state);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "KeyA");
    expect_empty(&test);
}

static void test_packets(void) {
    RawFixture test = {0};
    initialize(&test);
    RAWINPUT packet = {0};
    packet.header.dwSize = sizeof(packet);
    packet.header.dwType = RIM_TYPEMOUSE;
    packet.header.wParam = RIM_INPUTSINK;
    packet.data.mouse.lLastX = 8;
    bongo_cat_windows_input_reset_relative(&test.platform);
    CHECK(!test.state.receiving && test.state.ownership == 0);
    bongo_cat_windows_input_packet(&test.state, &packet, sizeof(packet));
    double x, y;
    CHECK(bongo_cat_windows_input_take_relative(&test.platform, &x, &y, NULL));
    CHECK(x == 8.0 && y == 0.0);
    CHECK(!bongo_cat_windows_input_take_relative(&test.platform, &x, &y, NULL));
    bongo_cat_windows_input_packet(&test.state, &packet, 1);
    packet.header.dwSize = sizeof(RAWINPUTHEADER);
    bongo_cat_windows_input_packet(&test.state, &packet, packet.header.dwSize);
    CHECK(!bongo_cat_windows_input_take_relative(&test.platform, &x, &y, NULL));
    packet.header.dwType = RIM_TYPEHID;
    bongo_cat_windows_input_packet(&test.state, &packet, packet.header.dwSize);
    expect_empty(&test);
    bongo_cat_windows_input_clear_devices(&test.state);
    test.state.desktop_unavailable = true;
    key_packet(&test, 1, 'A', 0x1e, 0);
    CHECK(test.state.device_count == 1);
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    bongo_cat_windows_input_clear_devices(&test.state);
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "KeyA");
}

static void test_motion_keeps_key_and_button_edges(void) {
    RawFixture test = {0};
    initialize(&test);
    RAWINPUT packet = {0};
    packet.header.dwSize = sizeof(packet);
    packet.header.dwType = RIM_TYPEMOUSE;
    packet.header.wParam = RIM_INPUTSINK;
    packet.data.mouse.lLastX = 1;
    for (unsigned i = 0; i < 8000; ++i) {
        packet.data.mouse.usButtonFlags = i == 100 ? RI_MOUSE_LEFT_BUTTON_DOWN :
            i == 101 ? RI_MOUSE_LEFT_BUTTON_UP : 0;
        bongo_cat_windows_input_packet(&test.state, &packet, sizeof(packet));
        if (i == 100) key_packet(&test, 1, 'A', 0x1e, 0);
        if (i == 101) key_packet(&test, 1, 'A', 0x1e, RI_KEY_BREAK);
        bongo_cat_windows_input_flush(&test.state);
    }
    expect(&test, BONGO_CAT_INPUT_MOUSE_DOWN, "Left");
    expect(&test, BONGO_CAT_INPUT_KEY_DOWN, "KeyA");
    expect(&test, BONGO_CAT_INPUT_MOUSE_UP, "Left");
    expect(&test, BONGO_CAT_INPUT_KEY_UP, "KeyA");
    expect_empty(&test);
    CHECK(test.state.observed_x == 8000);
    CHECK(!test.state.retry_events);
    bongo_cat_windows_input_clear_devices(&test.state);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS));
    test_key_devices();
    test_key_sequences();
    test_buttons();
    test_backpressure();
    test_packets();
    test_motion_keeps_key_and_button_edges();
    test_windows_relative_sources();
    test_windows_pointer_detection();
    test_windows_mouse_mapping();
    test_windows_raw_receiver();
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
