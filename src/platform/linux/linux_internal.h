#ifndef BONGO_CAT_LINUX_INTERNAL_H
#define BONGO_CAT_LINUX_INTERNAL_H

#include "bongo_cat/platform.h"

typedef struct LinuxEvdevState LinuxEvdevState;
typedef struct LinuxPlatformState {
    void *x11;
    LinuxEvdevState *evdev;
    bool wayland;
    bool evdev_selected;
    bool always_on_top;
} LinuxPlatformState;

bool bongo_cat_linux_evdev_requested(const char *option, bool wayland);

bool bongo_cat_linux_x11_start(BongoCatPlatform *platform, BongoCatError *error);
void bongo_cat_linux_x11_stop(BongoCatPlatform *platform);
/* Read-only evdev keyboard listener. Wayland compositors never forward other
   applications' keys to an X11 client, so XInput2 alone cannot show key
   presses on Wayland; this backend observes the kernel key stream instead. */
bool bongo_cat_linux_evdev_start(BongoCatPlatform *platform, BongoCatError *error);
void bongo_cat_linux_evdev_stop(BongoCatPlatform *platform);
bool bongo_cat_linux_evdev_pointer_active(const BongoCatPlatform *platform);
/* Relative pointer delta accumulated by the evdev listener. Wayland never
   exposes the global cursor position to other clients, so the pet follows
   relative motion from the kernel instead of absolute coordinates. */
bool bongo_cat_linux_evdev_relative_pointer(BongoCatPlatform *platform,
    double *dx, double *dy);
void bongo_cat_linux_evdev_relative_pointer_reset(BongoCatPlatform *platform);
bool bongo_cat_linux_x11_supported(const BongoCatPlatform *platform);
bool bongo_cat_linux_x11_xwayland(const BongoCatPlatform *platform);
void bongo_cat_linux_x11_click_through(BongoCatPlatform *platform, bool enabled);
void bongo_cat_linux_x11_set_above(BongoCatPlatform *platform, bool enabled);
void bongo_cat_linux_x11_configure_capture_window(BongoCatPlatform *platform);
void bongo_cat_linux_x11_begin_drag(BongoCatPlatform *platform);

#endif
