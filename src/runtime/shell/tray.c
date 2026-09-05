#include "bongo_cat/tray.h"
#include "bongo_cat/app.h"
#include "bongo_cat/image.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"
#include "modal_frame.h"
#include "runtime.h"
#include "ui_native_theme.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

struct BongoCatTray {
    BongoCatApp *app;
    SDL_Tray *handle;
    SDL_TrayEntry *visible;
    SDL_TrayEntry *pass_through;
    SDL_TrayEntry *always_on_top;
    SDL_TrayEntry *preferences;
    SDL_TrayEntry *exit;
    BongoCatImage icon;
    BongoCatModalFrame modal_frame;
    bool state_valid, restore_requested;
    uint64_t restore_after_ns;
    unsigned restore_count;
    bool last_visible, last_pass_through, last_always_on_top;
    BongoCatLanguage last_language;
};

static void on_visible(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatTray *tray = userdata;
    BongoCatApp *app = tray->app;
    bongo_cat_window_set_visible(app, !app->session.window.visible);
    bongo_cat_tray_sync(tray);
    bongo_cat_preferences_invalidate(app->preferences);
}

static void on_pass_through(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatTray *tray = userdata;
    tray->app->settings.window.pass_through = !tray->app->settings.window.pass_through;
    bongo_cat_window_mark_hit_dirty(tray->app);
    bongo_cat_window_sync_click_through(tray->app);
    bongo_cat_tray_sync(tray);
    bongo_cat_preferences_invalidate(tray->app->preferences);
}

static void on_always_on_top(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatTray *tray = userdata;
    tray->app->settings.window.always_on_top = !tray->app->settings.window.always_on_top;
    bongo_cat_platform_set_always_on_top(&tray->app->platform,
        tray->app->settings.window.always_on_top);
    bongo_cat_window_mark_hit_dirty(tray->app);
    bongo_cat_window_sync_click_through(tray->app);
    bongo_cat_tray_sync(tray);
    bongo_cat_preferences_invalidate(tray->app->preferences);
}

static void on_preferences(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    BongoCatTray *tray = userdata;
    bongo_cat_preferences_show(tray->app->preferences);
}

static void on_tray_left_click(void *userdata) {
    BongoCatTray *tray = userdata;
    if (tray && tray->app && tray->app->preferences)
        bongo_cat_preferences_show(tray->app->preferences);
}

static void on_tray_modal_frame(void *userdata) {
    BongoCatTray *tray = userdata;
    if (tray) bongo_cat_modal_frame_tick(&tray->modal_frame);
}

static void on_exit(void *userdata, SDL_TrayEntry *entry) {
    (void)entry;
    ((BongoCatTray *)userdata)->app->running = false;
}

static void on_tray_restore(void *userdata) {
    BongoCatTray *tray = userdata;
    if (!tray) return;
    if (!tray->restore_requested)
        SDL_Log("Windows shell restarted; scheduling tray restoration");
    tray->restore_requested = true;
    tray->restore_after_ns = 0;
}

void bongo_cat_tray_prepare_menu(BongoCatTray *tray, void *native_handle) {
    if (!tray || !tray->app) return;
    bool dark = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    bongo_cat_ui_native_menu_prepare_native(native_handle, dark);
}

static SDL_TrayEntry *add(SDL_TrayMenu *menu, const char *label,
    SDL_TrayEntryFlags flags, SDL_TrayCallback callback, BongoCatTray *tray) {
    SDL_TrayEntry *entry = SDL_InsertTrayEntryAt(menu, -1, label, flags);
    if (entry && callback) SDL_SetTrayEntryCallback(entry, callback, tray);
    return entry;
}

static void destroy_native_tray(BongoCatTray *tray) {
    if (!tray || !tray->handle) return;
    bongo_cat_platform_set_tray_callbacks(
        tray->handle, NULL, NULL, NULL, NULL);
    SDL_DestroyTray(tray->handle);
    tray->handle = NULL;
    tray->visible = tray->pass_through = tray->always_on_top = NULL;
    tray->preferences = tray->exit = NULL;
    tray->state_valid = false;
}

static bool create_native_tray(BongoCatTray *tray, BongoCatError *error) {
    tray->handle = SDL_CreateTray(tray->icon.surface, BONGO_CAT_TRAY_TITLE);
    if (!tray->handle) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Tray creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_TrayMenu *menu = SDL_CreateTrayMenu(tray->handle);
    SDL_TrayEntry *separator1 = NULL, *separator2 = NULL;
    if (menu) {
        tray->preferences = add(menu, bongo_cat_i18n_get(tray->app->i18n,
            "composables.useAppMenu.labels.preference", "Preferences"),
            SDL_TRAYENTRY_BUTTON, on_preferences, tray);
        separator1 = add(menu, NULL, 0, NULL, tray);
        tray->visible = add(menu, bongo_cat_i18n_get(tray->app->i18n,
            "composables.useAppMenu.labels.showCat", "Show BongoCat"),
            SDL_TRAYENTRY_CHECKBOX, on_visible, tray);
        tray->pass_through = add(menu, bongo_cat_i18n_get(tray->app->i18n,
            "composables.useAppMenu.labels.passThrough", "Mouse pass-through"),
            SDL_TRAYENTRY_CHECKBOX, on_pass_through, tray);
        tray->always_on_top = add(menu, bongo_cat_i18n_get(tray->app->i18n,
            "composables.useAppMenu.labels.alwaysOnTop", "Always on top"),
            SDL_TRAYENTRY_CHECKBOX, on_always_on_top, tray);
        separator2 = add(menu, NULL, 0, NULL, tray);
        tray->exit = add(menu, bongo_cat_i18n_get(tray->app->i18n,
            "composables.useAppMenu.labels.quitApp", "Exit"),
            SDL_TRAYENTRY_BUTTON, on_exit, tray);
    }
    if (!menu || !tray->preferences || !separator1 || !tray->visible ||
        !tray->pass_through || !tray->always_on_top || !separator2 ||
        !tray->exit) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Tray menu creation failed: %s", SDL_GetError());
        destroy_native_tray(tray);
        return false;
    }
    bongo_cat_platform_set_tray_callbacks(tray->handle,
        on_tray_left_click, on_tray_modal_frame, on_tray_restore, tray);
    tray->state_valid = false;
    return true;
}

BongoCatTray *bongo_cat_tray_create(BongoCatApp *app, BongoCatError *error) {
    if (!app || !app->settings.app.tray_visible) return NULL;
    BongoCatTray *tray = calloc(1, sizeof(*tray));
    if (!tray) return NULL;
    tray->app = app;
    bongo_cat_modal_frame_init(&tray->modal_frame, app);
    char path[BONGO_CAT_PATH_CAP];
    bongo_cat_path_join(path, sizeof(path), app->asset_root, "bongocat.png");
    if (bongo_cat_image_load(path, &tray->icon, error) != BONGO_CAT_OK) {
        bongo_cat_tray_destroy(tray);
        return NULL;
    }
    if (!create_native_tray(tray, error)) {
        bongo_cat_tray_destroy(tray);
        return NULL;
    }
    SDL_Log("Tray created");
    bongo_cat_tray_sync(tray);
    return tray;
}

static bool restore_native_tray(BongoCatTray *tray) {
    if (!tray->restore_requested) return tray->handle != NULL;
    uint64_t now = SDL_GetTicksNS();
    if (now < tray->restore_after_ns) return tray->handle != NULL;
    destroy_native_tray(tray);
    BongoCatError error = {0};
    if (!create_native_tray(tray, &error)) {
        tray->restore_requested = true;
        tray->restore_after_ns = now + 1000000000ull;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Tray restoration failed; retrying: %s", error.message);
        return false;
    }
    tray->restore_requested = false;
    tray->restore_count++;
    SDL_Log("Tray restored after Windows shell restart (count=%u)",
        tray->restore_count);
    return true;
}

void bongo_cat_tray_sync(BongoCatTray *tray) {
    if (!tray || !restore_native_tray(tray)) return;
    BongoCatApp *app = tray->app;
    if (tray->state_valid && tray->last_visible == app->session.window.visible &&
        tray->last_pass_through == app->settings.window.pass_through &&
        tray->last_always_on_top == app->settings.window.always_on_top &&
        tray->last_language == app->settings.app.language) return;
    tray->state_valid = true;
    tray->last_visible = app->session.window.visible;
    tray->last_pass_through = app->settings.window.pass_through;
    tray->last_always_on_top = app->settings.window.always_on_top;
    tray->last_language = app->settings.app.language;
    SDL_SetTrayEntryChecked(tray->visible, tray->app->session.window.visible);
    SDL_SetTrayEntryChecked(tray->pass_through, tray->app->settings.window.pass_through);
    SDL_SetTrayEntryChecked(tray->always_on_top, tray->app->settings.window.always_on_top);
    // The checkbox already communicates visibility; keep the action name
    // stable so users do not have to reinterpret the menu after each click.
    SDL_SetTrayEntryLabel(tray->visible, bongo_cat_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.showCat", "Show BongoCat"));
    SDL_SetTrayEntryLabel(tray->pass_through, bongo_cat_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.passThrough", "Mouse pass-through"));
    SDL_SetTrayEntryLabel(tray->always_on_top, bongo_cat_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.alwaysOnTop", "Always on top"));
    SDL_SetTrayEntryLabel(tray->preferences, bongo_cat_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.preference", "Preferences"));
    SDL_SetTrayEntryLabel(tray->exit, bongo_cat_i18n_get(tray->app->i18n,
        "composables.useAppMenu.labels.quitApp", "Exit"));
    SDL_UpdateTrays();
}

bool bongo_cat_tray_self_test(BongoCatTray *tray) {
    if (!tray || !tray->handle || !tray->visible || !tray->pass_through ||
        !tray->always_on_top || !tray->preferences || !tray->exit) return false;
    SDL_Log("Tray self-test: entries ready");
    int count = 0;
    const SDL_TrayEntry **entries = SDL_GetTrayEntries(
        SDL_GetTrayEntryParent(tray->preferences), &count);
    if (!entries || count < 1 || entries[0] != tray->preferences) return false;
#ifdef _WIN32
    on_tray_restore(tray);
    bongo_cat_tray_sync(tray);
    if (tray->restore_requested || !tray->handle || !tray->preferences ||
        !tray->visible || !tray->pass_through || !tray->always_on_top ||
        !tray->exit) return false;
    SDL_Log("Tray self-test: shell restart restored");
#endif
    BongoCatApp *app = tray->app;
    bool visible = app->session.window.visible;
    bool pass_through = app->settings.window.pass_through;
    bool always_on_top = app->settings.window.always_on_top;
    on_visible(tray, tray->visible); on_visible(tray, tray->visible);
    on_pass_through(tray, tray->pass_through);
    on_pass_through(tray, tray->pass_through);
    on_always_on_top(tray, tray->always_on_top);
    on_always_on_top(tray, tray->always_on_top);
    SDL_Log("Tray self-test: state actions restored");
    on_preferences(tray, tray->preferences);
    bool preferences = bongo_cat_preferences_visible(app->preferences);
    SDL_Log("Tray self-test: preferences visible=%d", preferences);
    bongo_cat_preferences_close(app->preferences);
    SDL_Log("Tray self-test: preferences closed");
    bool result = preferences && bongo_cat_modal_frame_self_test() &&
        app->session.window.visible == visible &&
        app->settings.window.pass_through == pass_through &&
        app->settings.window.always_on_top == always_on_top;
    SDL_Log("Tray self-test: result=%d", result);
    return result;
}

void bongo_cat_tray_destroy(BongoCatTray *tray) {
    if (!tray) return;
    destroy_native_tray(tray);
    bongo_cat_image_free(&tray->icon);
    free(tray);
}
