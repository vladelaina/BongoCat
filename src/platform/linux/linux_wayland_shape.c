#include "linux_shape.h"
#include <SDL3/SDL.h>

#ifdef BONGO_CAT_HAS_WAYLAND_SHAPE
#include <wayland-client.h>
#include <stdlib.h>
#include <string.h>

typedef struct WaylandInputShape {
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
} WaylandInputShape;

static void registry_global(void *userdata, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    WaylandInputShape *shape = userdata;
    if (!shape->compositor && strcmp(interface, "wl_compositor") == 0)
        shape->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
}

static void registry_removed(void *userdata, struct wl_registry *registry, uint32_t name) {
    (void)userdata; (void)registry; (void)name;
}

void bongo_cat_linux_wayland_shape_destroy(void *state) {
    WaylandInputShape *shape = state;
    if (!shape) return;
    if (shape->compositor) wl_compositor_destroy(shape->compositor);
    if (shape->registry) wl_registry_destroy(shape->registry);
    if (shape->queue) wl_event_queue_destroy(shape->queue);
    free(shape);
}

void *bongo_cat_linux_wayland_shape_create(SDL_Window *window) {
    struct wl_display *display = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    if (!display) return NULL;
    WaylandInputShape *shape = calloc(1, sizeof(*shape));
    if (!shape) return NULL;
    shape->display = display;
    shape->queue = wl_display_create_queue(display);
    if (!shape->queue) goto failed;
    /* Discover the compositor on our own queue without dispatching SDL's
       pending events. All access stays on the application's main thread. */
    struct wl_display *wrapper = wl_proxy_create_wrapper(display);
    if (!wrapper) goto failed;
    wl_proxy_set_queue((struct wl_proxy *)wrapper, shape->queue);
    shape->registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    if (!shape->registry) goto failed;
    static const struct wl_registry_listener listener = {registry_global, registry_removed};
    if (wl_registry_add_listener(shape->registry, &listener, shape) < 0 ||
        wl_display_roundtrip_queue(display, shape->queue) < 0 || !shape->compositor)
        goto failed;
    /* No ongoing registry listener is needed. Retaining an undispatched queue
       would accumulate global/hotplug notifications for the life of the pet. */
    wl_proxy_set_queue((struct wl_proxy *)shape->compositor, NULL);
    wl_registry_destroy(shape->registry);
    shape->registry = NULL;
    wl_event_queue_destroy(shape->queue);
    shape->queue = NULL;
    return shape;
failed:
    bongo_cat_linux_wayland_shape_destroy(shape);
    return NULL;
}

bool bongo_cat_linux_wayland_shape(void *state, SDL_Window *window,
    const unsigned char *mask, int width, int height, bool empty, bool commit) {
    WaylandInputShape *shape = state;
    struct wl_surface *surface = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!shape || !surface || wl_display_get_error(shape->display)) return false;
    struct wl_region *region = NULL;
    if (empty || mask) {
        region = wl_compositor_create_region(shape->compositor);
        if (!region) return false;
    }
    if (!empty && mask) {
        size_t stride = ((size_t)width + 7) / 8;
        for (int y = 0; y < height;) {
            const unsigned char *row = mask + (size_t)y * stride;
            int next_y = y + 1;
            while (next_y < height &&
                memcmp(row, mask + (size_t)next_y * stride, stride) == 0) ++next_y;
            for (int x = 0; x < width;) {
                if (!(row[x / 8] & (1u << (x % 8)))) { ++x; continue; }
                int start = x++;
                while (x < width && (row[x / 8] & (1u << (x % 8)))) ++x;
                wl_region_add(region, start, y, x - start, next_y - y);
            }
            y = next_y;
        }
    }
    wl_surface_set_input_region(surface, region);
    if (region) wl_region_destroy(region);
    /* Render updates are committed with SDL's next buffer swap. A user toggle
       must apply immediately even when rendering is paused or the pet is hidden. */
    if (commit) wl_surface_commit(surface);
    wl_display_flush(shape->display);
    return true;
}
#else
void *bongo_cat_linux_wayland_shape_create(SDL_Window *window) {
    (void)window; return NULL;
}
void bongo_cat_linux_wayland_shape_destroy(void *state) { (void)state; }
bool bongo_cat_linux_wayland_shape(void *state, SDL_Window *window,
    const unsigned char *mask, int width, int height, bool empty, bool commit) {
    (void)state; (void)window; (void)mask; (void)width; (void)height;
    (void)empty; (void)commit;
    return false;
}
#endif
