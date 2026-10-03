#include "runtime.h"

#include <stdlib.h>
#ifdef _WIN32
#include "windows_autostart.h"
#include "windows_game_compatibility.h"
#include "storage_paths.h"
#endif

int bongo_cat_app_run(int argc, char **argv) {
#ifdef _WIN32
    int autostart_exit = 0;
    if (bongo_cat_windows_autostart_command(argc, argv, &autostart_exit))
        return autostart_exit;
    if (!bongo_cat_windows_game_compatibility_command()) return 1;
#endif
    if (bongo_cat_platform_update_shutdown_argument(argc, argv)) return 0;
    bool secondary = bongo_cat_multi_pet_secondary_argument(argc, argv);
    if (!secondary && !bongo_cat_platform_single_instance_begin()) return 0;
    BongoCatApp *app = calloc(1, sizeof(*app));
    if (!app) {
        BongoCatError memory = {0};
        bongo_cat_error_set(&memory, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate application state");
        bongo_cat_startup_failure(NULL, &memory);
        if (!secondary) bongo_cat_platform_single_instance_end();
        return 1;
    }
    BongoCatError error = {0};
#ifdef _WIN32
    /* Check the saved opt-in before opening logs, windows or model resources. */
    bongo_cat_settings_defaults(&app->settings);
    if (bongo_cat_startup_arguments(app, argc, argv, &error) &&
        bongo_cat_storage_paths_prepare(app, &error) &&
        bongo_cat_settings_load(app->settings_path, &app->settings, &error) == BONGO_CAT_OK) {
        bool restarting = false;
        bool success = bongo_cat_windows_game_compatibility_startup(app, &restarting, &error);
        if (!success || restarting) {
            if (!success) bongo_cat_startup_failure(NULL, &error);
            free(app);
            if (!secondary) bongo_cat_platform_single_instance_end();
            bongo_cat_windows_game_compatibility_finish();
            return success ? 0 : 1;
        }
    }
    error = (BongoCatError){0};
#endif
    if (!bongo_cat_app_initialize(app, argc, argv, &error)) {
        bongo_cat_startup_failure(app, &error);
        if (app->smoke) bongo_cat_startup_ci_failure(app, &error);
        bongo_cat_app_shutdown(app, "shutdown:startup-failure", 1);
        free(app);
        if (!secondary) bongo_cat_platform_single_instance_end();
        return 1;
    }
    bongo_cat_app_loop(app);
    int exit_code = app->exit_code;
    bongo_cat_app_shutdown(app, "shutdown:normal", exit_code);
    free(app);
    if (!secondary) bongo_cat_platform_single_instance_end();
#ifdef _WIN32
    bongo_cat_windows_game_compatibility_finish();
#endif
    return exit_code;
}
