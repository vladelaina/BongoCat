#ifndef BONGO_CAT_PREFERENCES_STATE_H
#define BONGO_CAT_PREFERENCES_STATE_H

#include "preferences_internal.h"
#include "preferences_scrollbar.h"
#include "preferences_text_session.h"
#include "about/preferences_about.h"
#include "ui_backend.h"

#define BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS 5000000000ull
#define BONGO_CAT_MODEL_LOAD_VISUAL_RAMP_NS 3000000000ull
#define BONGO_CAT_MODEL_LOAD_VISUAL_COMPLETE_NS 160000000ull
#define BONGO_CAT_MODEL_LOAD_VISUAL_WAIT_CAP 0.95f
#include "bongo_cat/i18n.h"
#include "bongo_cat/preferences.h"
#include "modal_frame.h"

typedef struct BongoCatPreferenceNotice {
    char message[1024];
    uint64_t started_ns;
    uint64_t until_ns;
    uint64_t timer_updated_ns;
    struct nk_rect bounds;
    bool hovered;
    bool error;
} BongoCatPreferenceNotice;

struct BongoCatPreferences {
    BongoCatApp *app;
    SDL_Window *window;
    SDL_GLContext gl_context;
    bool owns_gl_context;
    bool transparent_window;
    bool visible;
    bool ui_initialized;
    BongoCatUIBackend ui;
    unsigned int logo_texture;
    unsigned int icon_texture;
    unsigned int icon_texture_hidpi;
    bool icon_hidpi_attempted;
    int logo_width;
    int logo_height;
    unsigned int catime_texture;
    unsigned int vlaina_texture;
    int catime_width, catime_height;
    int vlaina_width, vlaina_height;
    bool support_assets_loaded;
    BongoCatAboutState about;
    struct nk_user_font support_logs_font;
    int page;
    int style_theme;
    BongoCatLanguage font_language;
    nk_rune glyph_ranges[2048];
    bool input_active;
    bool import_requested;
    bool import_drop_active;
    bool import_render_active;
    BongoCatImportDialog *import_dialog;
    bool frame_checked;
    bool render_dirty;
    bool font_reload_pending;
    bool font_reload_defer_once;
    size_t pending_import_name_count;
    char pending_import_names[BONGO_CAT_MODEL_CAP][BONGO_CAT_ID_CAP];
    bool model_directory_watch_active;
    bool model_directory_watch_known;
    bool model_directory_watch_refresh;
    bool model_directory_watch_available;
    size_t model_directory_watch_count;
    uint64_t model_directory_watch_sum;
    uint64_t model_directory_watch_xor;
    uint64_t model_directory_watch_due_ns;
    bool smoke_behavior_open_pending;
    bool model_selection_pending;
    bool pending_model_multiple;
    bool pending_model_active;
    bool model_loading;
    bool model_load_visual_active;
    float model_load_progress;
    float model_load_render_progress;
    uint64_t model_load_render_ns;
    uint64_t model_load_visual_started_ns;
    uint64_t model_load_visual_completion_ns;
    char pending_model_id[BONGO_CAT_ID_CAP];
    char loading_model_id[BONGO_CAT_ID_CAP];
    char model_load_visual_id[BONGO_CAT_ID_CAP];
    uint64_t last_render_ns;
    uint64_t shown_ns;
    float pending_raster_scale;
    uint64_t raster_retry_ns;
    uint64_t render_retry_ns;
    float scroll_current[4];
    float scroll_target[4];
    bool scroll_ready[4];
    bool page_seen;
    int last_page;
    uint64_t page_transition_ns;
    BongoCatPreferenceNotice notices[4];
    bool behavior_dialog;
    bool behavior_dialog_input_armed;
    int behavior_tab;
    uint64_t behavior_dialog_opened_ns;
    uint64_t behavior_dialog_closing_ns;
    uint64_t behavior_tab_transition_ns;
    float behavior_scroll[3];
    bool behavior_audio_playing[BONGO_CAT_BEHAVIOR_CAP];
    BongoCatPreferencesScrollbar behavior_scrollbar;
    BongoCatPreferencesTextSession behavior_rename;
    BongoCatPreferencesTextSession model_rename;
    bool native_drag;
    bool chrome_dragging;
    bool live_resize_active;
    bool live_resize_rendering;
    bool live_resize_pending;
    bool live_resize_timer;
    bool live_resize_modal_ready;
    BongoCatModalFrame live_resize_modal_frame;
    unsigned live_resize_layout_frames;
    int drag_window_x;
    int drag_window_y;
    float drag_pointer_x;
    float drag_pointer_y;
    char shortcut_id[BONGO_CAT_ID_CAP + 16];
    char *shortcut_target;
    int shortcut_capacity;
    char shortcut_original[BONGO_CAT_SHORTCUT_CAP];
    SDL_Keycode shortcut_key;
    bool shortcut_recording;
    uint64_t shortcut_suppress_until_ns;
#ifdef __APPLE__
    /* The permission changes while the user is in System Settings, so the page
       reads it again when it becomes visible and when the window regains focus
       instead of querying macOS per frame. */
    bool input_monitoring_authorized;
    bool input_monitoring_valid;
#endif
};

#ifdef __APPLE__
void bongo_cat_preferences_input_monitoring_refresh(BongoCatPreferences *value);
#endif

int bongo_cat_preferences_resolved_theme(const BongoCatPreferences *value);
void bongo_cat_preferences_apply_theme(BongoCatPreferences *value);
bool bongo_cat_preferences_open_window(BongoCatPreferences *value);
void bongo_cat_preferences_release_idle_window(BongoCatPreferences *value);
bool bongo_cat_preferences_scale_event(BongoCatPreferences *value,
    const SDL_Event *event);
bool bongo_cat_preferences_refresh_raster(BongoCatPreferences *value);
bool bongo_cat_preferences_reload_fonts(BongoCatPreferences *value);
bool bongo_cat_preferences_reload_language(BongoCatPreferences *value);
void bongo_cat_preferences_drag_tick(BongoCatPreferences *value);
void bongo_cat_preferences_live_resize_install(BongoCatPreferences *value);
void bongo_cat_preferences_live_resize_uninstall(BongoCatPreferences *value);
void bongo_cat_preferences_record_frame(BongoCatPreferences *value);
void bongo_cat_preferences_assets_load(BongoCatPreferences *value);
void bongo_cat_preferences_support_assets_load(BongoCatPreferences *value);
void bongo_cat_preferences_support_assets_clear(BongoCatPreferences *value);
void bongo_cat_preferences_process_model_selection(BongoCatPreferences *value);
void bongo_cat_preferences_model_visual_begin(BongoCatPreferences *value,
    const char *model_id);
void bongo_cat_preferences_model_load_progress(BongoCatPreferences *value,
    float progress);
float bongo_cat_preferences_model_visual_progress(BongoCatPreferences *value,
    const char *model_id);
void bongo_cat_preferences_assets_clear(BongoCatPreferences *value);
void bongo_cat_preferences_assets_abandon(BongoCatPreferences *value);
void bongo_cat_preferences_model_cover_cache_clear(BongoCatApp *app);
void bongo_cat_preferences_page_cache_clear(BongoCatPreferences *value,
    int previous_page, int next_page);
void bongo_cat_preferences_smoke_frame(BongoCatPreferences *value);
void bongo_cat_preferences_icon_draw(BongoCatPreferences *value,
    struct nk_command_buffer *canvas, int icon, struct nk_rect bounds,
    struct nk_color color);

#endif
