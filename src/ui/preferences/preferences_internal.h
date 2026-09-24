#ifndef BONGO_CAT_PREFERENCES_INTERNAL_H
#define BONGO_CAT_PREFERENCES_INTERNAL_H

#include "bongo_cat/app.h"
#include "nuklear_config.h"
#include "ui_catime.h"
#include <SDL3/SDL.h>

void bongo_cat_preferences_page_settings(BongoCatPreferences *value,
    struct nk_context *context);
void bongo_cat_preferences_page_model(BongoCatPreferences *value,
    struct nk_context *context);
bool bongo_cat_preferences_behavior_dialog_active(
    const BongoCatPreferences *value);
void bongo_cat_preferences_behavior_dialog_open(
    BongoCatPreferences *value);
void bongo_cat_preferences_behavior_dialog_open_model(
    BongoCatPreferences *value, const BongoCatModelEntry *model);
const BongoCatBehaviorCatalog *bongo_cat_preferences_behavior_catalog(
    const BongoCatPreferences *value);
bool bongo_cat_preferences_behavior_model_loaded(
    const BongoCatPreferences *value);
void bongo_cat_preferences_behavior_dialog_draw(
    BongoCatPreferences *value, struct nk_context *context);
void bongo_cat_preferences_behavior_dialog_close(
    BongoCatPreferences *value);
bool bongo_cat_preferences_random_dialog_active(
    const BongoCatPreferences *value);
void bongo_cat_preferences_random_dialog_open(BongoCatPreferences *value,
    BongoCatBehaviorKind kind);
void bongo_cat_preferences_random_dialog_close(BongoCatPreferences *value);
void bongo_cat_preferences_random_dialog_draw(
    BongoCatPreferences *value, struct nk_context *context);
bool bongo_cat_preferences_behavior_rename_event(
    BongoCatPreferences *value, const SDL_Event *event);
void bongo_cat_preferences_behavior_rename_begin(BongoCatPreferences *value,
    const BongoCatBehaviorEntry *entry, BongoCatBehaviorShortcut *binding,
    struct nk_rect bounds);
void bongo_cat_preferences_behavior_rename_finish(
    BongoCatPreferences *value, bool save);
bool bongo_cat_preferences_model_rename_event(
    BongoCatPreferences *value, const SDL_Event *event);
void bongo_cat_preferences_model_rename_begin(BongoCatPreferences *value,
    const BongoCatModelEntry *entry, struct nk_rect bounds);
void bongo_cat_preferences_model_rename_finish(
    BongoCatPreferences *value, bool save);
bool bongo_cat_preferences_model_name_draw(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    const BongoCatModelEntry *entry, struct nk_rect bounds,
    BongoCatUIPalette palette);
void bongo_cat_preferences_behavior_row_draw(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect row, const BongoCatBehaviorEntry *entry, BongoCatUIPalette palette,
    float opacity, bool enabled);
void bongo_cat_preferences_page_shortcuts(BongoCatPreferences *value,
    struct nk_context *context);
void bongo_cat_preferences_page_about(BongoCatPreferences *value,
    struct nk_context *context);
bool bongo_cat_preferences_shortcut_active(const BongoCatPreferences *value,
    const char *id);
void bongo_cat_preferences_shortcut_begin(BongoCatPreferences *value,
    const char *id, char *target, int capacity);
bool bongo_cat_preferences_shortcut_event(BongoCatPreferences *value,
    const SDL_Event *event);
void bongo_cat_preferences_shortcut_cancel(BongoCatPreferences *value);
void bongo_cat_preferences_shortcut_smoke(BongoCatPreferences *value);
void bongo_cat_preferences_import_path(BongoCatApp *app, SDL_Window *window,
    const char *path);
void bongo_cat_preferences_model_cache_clear(BongoCatApp *app);
bool bongo_cat_preferences_remove_dialog_active(const BongoCatApp *app);
void bongo_cat_preferences_remove_dialog_open(BongoCatApp *app, const char *id);
void bongo_cat_preferences_remove_dialog_draw(BongoCatApp *app,
    struct nk_context *context);
void bongo_cat_preferences_remove_dialog_clear(const BongoCatApp *app);
void bongo_cat_preferences_remove_dialog_close(BongoCatApp *app);
bool bongo_cat_preferences_chrome_drag_allowed(
    const BongoCatPreferences *value);

typedef struct BongoCatImportDialog BongoCatImportDialog;
BongoCatImportDialog *bongo_cat_preferences_import_create(void);
void bongo_cat_preferences_import_destroy(BongoCatImportDialog *dialog);
bool bongo_cat_preferences_import_open(BongoCatImportDialog *dialog,
    SDL_Window *window);
bool bongo_cat_preferences_import_is_open(const BongoCatImportDialog *dialog);
bool bongo_cat_preferences_import_status(const BongoCatImportDialog *dialog,
    uint64_t *started_ns, size_t *completed, size_t *total);
bool bongo_cat_preferences_import_event(BongoCatImportDialog *dialog,
    BongoCatApp *app, const SDL_Event *event);

#endif
