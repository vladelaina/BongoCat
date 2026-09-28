#include "bongo_cat/app.h"
#include <SDL3/SDL_main.h>
#if defined(__linux__)
#include "platform/linux/linux_evdev_bootstrap.h"
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
    bongo_cat_linux_evdev_bootstrap(argc, argv);
#endif
    return bongo_cat_app_run(argc, argv);
}
