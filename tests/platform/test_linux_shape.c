#include "test.h"
#include "linux_shape.h"
#include <SDL3/SDL.h>
#include <string.h>

int bongo_cat_test_failures;
static int logical_width = 4, logical_height = 2;
static int frame_width = 8, frame_height = 4;
static unsigned char frame[8 * 4 * 4], applied_mask[8];
static bool read_failed, apply_failed, applied_empty, applied_rectangle;
static unsigned updates, reads;

static bool window_size(SDL_Window *window, int *width, int *height) {
    (void)window; *width = logical_width; *height = logical_height; return true;
}
static SDL_WindowFlags window_flags(SDL_Window *window) {
    (void)window; return SDL_WINDOW_TRANSPARENT;
}
bool bongo_cat_gl_read_window(int x, int y, int width, int height,
    bool back, void *pixels) {
    CHECK(x == 0 && y == 0 && width == frame_width && height == frame_height && back);
    ++reads;
    if (read_failed) return false;
    memcpy(pixels, frame, (size_t)width * height * 4);
    return true;
}
bool bongo_cat_linux_x11_shape_supported(BongoCatPlatform *platform) {
    (void)platform; return true;
}
bool bongo_cat_linux_x11_shape(BongoCatPlatform *platform,
    const unsigned char *mask, int width, int height, bool empty) {
    (void)platform;
    ++updates;
    applied_empty = empty;
    applied_rectangle = !empty && !mask;
    if (mask) {
        size_t size = ((size_t)width + 7) / 8 * height;
        CHECK(size <= sizeof(applied_mask));
        if (size <= sizeof(applied_mask)) memcpy(applied_mask, mask, size);
    }
    return !apply_failed;
}
void *bongo_cat_linux_wayland_shape_create(SDL_Window *window) { (void)window; return NULL; }
void bongo_cat_linux_wayland_shape_destroy(void *state) { (void)state; }
bool bongo_cat_linux_wayland_shape(void *state, SDL_Window *window,
    const unsigned char *mask, int width, int height, bool empty, bool commit) {
    (void)state; (void)window; (void)mask; (void)width; (void)height;
    (void)empty; (void)commit; return false;
}

#define SDL_GetWindowSize window_size
#define SDL_GetWindowFlags window_flags
#include "../../src/platform/linux/linux_shape.c"

static void alpha_at(int x, int y, unsigned char alpha) {
    frame[((frame_height - 1 - y) * frame_width + x) * 4 + 3] = alpha;
}

int main(void) {
    BongoCatPlatform platform = {0};
    bongo_cat_linux_shape_init(&platform);
    LinuxInputShape *shape = platform.presenter;
    CHECK(shape && shape->available);
    if (!shape) return 1;
    alpha_at(1, 1, 9);  /* A single visible physical pixel keeps its logical pixel. */
    alpha_at(3, 1, 8);  /* Transparent threshold, inside a different logical pixel. */
    alpha_at(7, 3, 255);
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(shape->valid && shape->applied && updates == 1);
    CHECK(applied_mask[0] == 0x01 && applied_mask[1] == 0x08);
    uint8_t alpha = 0;
    CHECK(bongo_cat_linux_shape_alpha(&platform, 8, 4, 1, 2, &alpha) && alpha == 9);
    CHECK(!bongo_cat_linux_shape_alpha(&platform, 8, 4, 8, 2, &alpha));
    CHECK(!bongo_cat_linux_shape_alpha(&platform, 4, 2, 1, 1, &alpha));
    frame[0] = 255; /* RGB animation alone must not recreate the input region. */
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(updates == 1 && reads == 2);
    bongo_cat_linux_shape_force(&platform, true);
    CHECK(applied_empty && updates == 2);
    memset(frame, 0, sizeof(frame));
    alpha_at(5, 0, 255);
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(updates == 2); /* Still globally transparent while the model animates. */
    bongo_cat_linux_shape_force(&platform, false);
    CHECK(!applied_empty && !applied_rectangle && updates == 3);
    CHECK(applied_mask[0] == 0x04 && applied_mask[1] == 0);
    size_t previous_capacity = shape->rgba_capacity;
    frame_width = 2; frame_height = 1;
    memset(frame, 0, sizeof(frame));
    alpha_at(0, 0, 255);
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(shape->rgba_capacity < previous_capacity);
    CHECK(applied_mask[0] == 0x03 && applied_mask[1] == 0x03);
    read_failed = true;
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(!shape->valid && !shape->applied && applied_rectangle);
    read_failed = false;
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(shape->valid && shape->applied && !applied_rectangle);
    apply_failed = true;
    memset(frame, 0, sizeof(frame));
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(!shape->valid && !shape->applied);
    apply_failed = false;
    bongo_cat_linux_shape_capture(&platform, frame_width, frame_height);
    CHECK(shape->valid && shape->applied);
    CHECK(applied_mask[0] == 0 && applied_mask[1] == 0);
    bongo_cat_linux_shape_reset(&platform);
    CHECK(!shape->valid && !shape->applied && applied_rectangle);
    bongo_cat_linux_shape_destroy(&platform);
    CHECK(platform.presenter == NULL);
    return bongo_cat_test_failures ? 1 : 0;
}
