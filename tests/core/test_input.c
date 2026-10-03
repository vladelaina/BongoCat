#include "test.h"
#include "bongo_cat/input.h"
#include "bongo_cat/mouse.h"

#include <string.h>
#ifdef _WIN32
#include "windows_keys.h"

#endif

void test_input(void) {
#ifdef _WIN32
    char key_name[16];
    RAWKEYBOARD key = {.VKey = VK_NUMPAD1};
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "Kp1") == 0);
    key.VKey = VK_END;
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "Kp1") == 0);
    key.Flags = RI_KEY_E0;
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "End") == 0);
    key.VKey = VK_SNAPSHOT;
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "PrintScreen") == 0);
    key.VKey = VK_APPS;
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "Apps") == 0);

    key = (RAWKEYBOARD){.VKey = VK_SHIFT, .MakeCode = 0x36};
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "ShiftRight") == 0);
    key = (RAWKEYBOARD){.VKey = VK_CONTROL, .MakeCode = 0x1d};
    unsigned left_control = bongo_cat_windows_key_index(&key);
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "ControlLeft") == 0);
    key.Flags = RI_KEY_E0;
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "ControlRight") == 0);
    CHECK(left_control != bongo_cat_windows_key_index(&key));
    key = (RAWKEYBOARD){.VKey = VK_MENU, .Flags = RI_KEY_E0};
    CHECK(strcmp(bongo_cat_windows_key_name(&key, key_name), "AltGr") == 0);
    key.VKey = 255;
    CHECK(bongo_cat_windows_key_name(&key, key_name) == NULL);
    key = (RAWKEYBOARD){.VKey = 'A', .MakeCode = 0xff};
    CHECK(bongo_cat_windows_key_name(&key, key_name) == NULL);
#endif
    BongoCatInputState state;
    bongo_cat_input_init(&state);
    bool edges[BONGO_CAT_INPUT_KEY_STATE_CAP] = {0};
    CHECK(bongo_cat_input_edge(edges, 42, true));
    CHECK(!bongo_cat_input_edge(edges, 42, true));
    CHECK(bongo_cat_input_edge(edges, 42, false));
    CHECK(!bongo_cat_input_edge(edges, 42, false));
    CHECK(bongo_cat_input_edge(edges, BONGO_CAT_INPUT_KEY_STATE_CAP, true));
    BongoCatInputEvent event = {.kind = BONGO_CAT_INPUT_KEY_DOWN, .value = 1.0f};
    memcpy(event.name, "KeyA", sizeof("KeyA"));
    CHECK(bongo_cat_input_push(&state, &event));
    BongoCatInputEvent output;
    CHECK(bongo_cat_input_pop(&state, &output));
    CHECK(output.kind == BONGO_CAT_INPUT_KEY_DOWN);

    memcpy(event.name, "ControlLeft", sizeof("ControlLeft"));
    event.kind = BONGO_CAT_INPUT_KEY_DOWN;
    CHECK(bongo_cat_input_push(&state, &event));
    CHECK(bongo_cat_input_control_down(&state));
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    CHECK(bongo_cat_input_push(&state, &event));
    CHECK(!bongo_cat_input_control_down(&state));
    CHECK(strcmp(output.name, "KeyA") == 0);
    CHECK(bongo_cat_input_pop(&state, &output));
    CHECK(output.kind == BONGO_CAT_INPUT_KEY_DOWN);
    CHECK(bongo_cat_input_pop(&state, &output));
    CHECK(output.kind == BONGO_CAT_INPUT_KEY_UP);
    CHECK(!bongo_cat_input_pop(&state, &output));

    memcpy(event.name, "ShiftRight", sizeof("ShiftRight"));
    event.kind = BONGO_CAT_INPUT_KEY_DOWN;
    CHECK(bongo_cat_input_push(&state, &event));
    CHECK(bongo_cat_input_shift_down(&state));
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    CHECK(bongo_cat_input_push(&state, &event));
    CHECK(!bongo_cat_input_shift_down(&state));
    CHECK(bongo_cat_input_pop(&state, &output));
    CHECK(bongo_cat_input_pop(&state, &output));

    CHECK(bongo_cat_input_mouse(&state, 1.0, 2.0));
    CHECK(!bongo_cat_input_mouse(&state, 8.0, 9.0));
    double x, y;
    CHECK(bongo_cat_input_take_mouse(&state, &x, &y));
    CHECK(x == 8.0 && y == 9.0);
    CHECK(!bongo_cat_input_take_mouse(&state, &x, &y));
    CHECK(bongo_cat_input_mouse(&state, 10.0, 11.0));
    CHECK(bongo_cat_input_take_mouse(&state, &x, &y));
    CHECK(x == 10.0 && y == 11.0);

    CHECK(!bongo_cat_mouse_sample_fresh(1000000000ull, 0));
    CHECK(bongo_cat_mouse_sample_fresh(1000000000ull, 1000000000ull));
    CHECK(bongo_cat_mouse_sample_fresh(1099999999ull, 1000000000ull));
    CHECK(!bongo_cat_mouse_sample_fresh(1100000000ull, 1000000000ull));
    CHECK(!bongo_cat_mouse_sample_fresh(31000000000ull, 1000000000ull));
    CHECK(!bongo_cat_mouse_sample_fresh(999999999ull, 1000000000ull));

    BongoCatMouseTracking tracking = {0};
    bongo_cat_mouse_target(&tracking, 0.0, 0.0);
    CHECK(bongo_cat_mouse_step(&tracking, 1.0f / 60.0f, &x, &y));
    CHECK(x == 0.0 && y == 0.0);
    bongo_cat_mouse_target(&tracking, 100.0, 40.0);
    CHECK(bongo_cat_mouse_step(&tracking, 1.0f / 60.0f, &x, &y));
    CHECK(x > 24.99 && x < 25.01 && y > 9.99 && y < 10.01);
    for (int i = 0; i < 64 && !tracking.settled; ++i)
        CHECK(bongo_cat_mouse_step(&tracking, 1.0f / 60.0f, &x, &y));
    CHECK(tracking.settled && x == 100.0 && y == 40.0);
    CHECK(!bongo_cat_mouse_step(&tracking, 1.0f / 60.0f, &x, &y));
    float drag_x = 0.0f, drag_y = 0.0f;
    bongo_cat_mouse_drag_coordinates(0.0f, 0.0f, false, &drag_x, &drag_y);
    CHECK(drag_x == -1.0f && drag_y == 1.0f);
    bongo_cat_mouse_drag_coordinates(1.0f, 1.0f, false, &drag_x, &drag_y);
    CHECK(drag_x == 1.0f && drag_y == -1.0f);
    bongo_cat_mouse_drag_coordinates(1.0f, 0.0f, true, &drag_x, &drag_y);
    CHECK(drag_x == -1.0f && drag_y == 1.0f);
    CHECK(bongo_cat_mouse_parameter_value(-30.0f, 30.0f,
        0.0f, 0.5f, 'X', false) == -30.0f);
    CHECK(bongo_cat_mouse_parameter_value(-30.0f, 30.0f,
        1.0f, 0.5f, 'X', false) == 30.0f);
    CHECK(bongo_cat_mouse_parameter_value(-30.0f, 30.0f,
        0.0f, 0.5f, 'X', true) == 30.0f);
    CHECK(bongo_cat_mouse_parameter_value(-30.0f, 30.0f,
        0.5f, 0.0f, 'Y', true) == 30.0f);
    CHECK(bongo_cat_mouse_parameter_value(-30.0f, 30.0f,
        0.0f, 0.0f, 'Z', false) == 30.0f);
    CHECK(bongo_cat_mouse_parameter_value(-10.0f, 30.0f,
        0.5f, 0.5f, 'X', false) == 0.0f);
    CHECK(bongo_cat_mouse_centered_ratio(80.0, 80.0, 0.0, 100.0) == 0.5f);
    CHECK(bongo_cat_mouse_centered_ratio(0.0, 80.0, 0.0, 100.0) == 0.0f);
    CHECK(bongo_cat_mouse_centered_ratio(100.0, 80.0, 0.0, 100.0) == 1.0f);
    CHECK(bongo_cat_mouse_centered_ratio(40.0, 80.0, 0.0, 100.0) == 0.25f);
    CHECK(bongo_cat_mouse_centered_ratio(90.0, 80.0, 0.0, 100.0) == 0.75f);

    bongo_cat_input_init(&state);
    event.kind = BONGO_CAT_INPUT_KEY_DOWN;
    memcpy(event.name, "Fill", sizeof("Fill"));
    for (int i = 0; i < 255; ++i) CHECK(bongo_cat_input_push(&state, &event));
    BongoCatInputEvent release = {.kind = BONGO_CAT_INPUT_KEY_UP, .timestamp_ms = 7};
    snprintf(release.name, sizeof(release.name), "KeyZ");
    CHECK(bongo_cat_input_push(&state, &release));
    CHECK(bongo_cat_input_pop(&state, &output));
    event.timestamp_ms = 7;
    memcpy(event.name, "KeyZ", sizeof("KeyZ"));
    CHECK(bongo_cat_input_push(&state, &event));
    BongoCatInputKind key_z_order[2] = {0}; size_t key_z_count = 0;
    while (bongo_cat_input_pop(&state, &output))
        if (strcmp(output.name, "KeyZ") == 0 && key_z_count < 2)
            key_z_order[key_z_count++] = output.kind;
    CHECK(key_z_count == 2 && key_z_order[0] == BONGO_CAT_INPUT_KEY_UP &&
        key_z_order[1] == BONGO_CAT_INPUT_KEY_DOWN);
    CHECK(atomic_load(&state.dropped) == 0);

    memcpy(event.name, "ControlLeft", sizeof("ControlLeft"));
    for (int i = 0; i < 255; ++i) CHECK(bongo_cat_input_push(&state, &event));
    CHECK(!bongo_cat_input_push(&state, &event));
    CHECK(bongo_cat_input_control_down(&state));
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    CHECK(bongo_cat_input_push(&state, &event));
    CHECK(!bongo_cat_input_control_down(&state));
    for (unsigned i = 1; i < BONGO_CAT_INPUT_RECOVERY_CAP - 1; ++i) {
        snprintf(release.name, sizeof(release.name), "Release%u", i);
        CHECK(bongo_cat_input_push(&state, &release));
    }
    snprintf(release.name, sizeof(release.name), "RecoveryFull");
    CHECK(!bongo_cat_input_push(&state, &release));
    CHECK(atomic_load(&state.dropped) == 2);

    bongo_cat_input_init(&state);
    event.kind = BONGO_CAT_INPUT_KEY_DOWN;
    event.timestamp_ms = 100;
    memcpy(event.name, "CapsLock", sizeof("CapsLock"));
    bongo_cat_input_schedule_release(&state, &event, 100);
    CHECK(!bongo_cat_input_take_scheduled_release(&state, 199, &output));
    CHECK(bongo_cat_input_take_scheduled_release(&state, 200, &output));
    CHECK(output.kind == BONGO_CAT_INPUT_KEY_UP);
    CHECK(strcmp(output.name, "CapsLock") == 0);
    bongo_cat_input_schedule_release(&state, &event, 100);
    event.kind = BONGO_CAT_INPUT_KEY_UP;
    bongo_cat_input_schedule_release(&state, &event, 100);
    CHECK(!bongo_cat_input_take_scheduled_release(&state, 9999, &output));
}
