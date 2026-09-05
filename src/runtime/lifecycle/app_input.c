#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <string.h>

static void log_input_consumer(BongoCatApp *app) {
    if (!app) return;
    uint64_t now = SDL_GetTicksNS();
    if (app->input_diagnostic_due_ns && now < app->input_diagnostic_due_ns)
        return;
    app->input_diagnostic_due_ns = (now ? now : 1) + 30000000000ull;
    SDL_Log("[input] Main consumer: events=%llu key_down=%llu key_up=%llu "
        "mouse_down=%llu mouse_up=%llu key_supported=%llu "
        "key_unsupported=%llu mouse_applied=%llu mouse_updates=%llu "
        "shortcuts=%llu "
        "last=%s dropped=%llu active=%zu buttons=%d,%d,%d "
        "pointer_known=%d pointer=%.1f,%.1f",
        (unsigned long long)app->input_events_consumed,
        (unsigned long long)app->input_key_down_events,
        (unsigned long long)app->input_key_up_events,
        (unsigned long long)app->input_mouse_down_events,
        (unsigned long long)app->input_mouse_up_events,
        (unsigned long long)app->input_key_supported,
        (unsigned long long)app->input_key_unsupported,
        (unsigned long long)app->input_mouse_applied,
        (unsigned long long)app->input_mouse_updates,
        (unsigned long long)app->input_shortcuts_triggered,
        app->input_last_name[0] ? app->input_last_name : "none",
        (unsigned long long)atomic_load_explicit(&app->input.dropped,
            memory_order_relaxed), app->active_input_count,
        app->left_mouse_down, app->right_mouse_down, app->side_mouse_down,
        app->pointer_known, app->pointer_x, app->pointer_y);
}

static void record_input_event(BongoCatApp *app,
    const BongoCatInputEvent *event) {
    if (!app || !event) return;
    app->input_events_consumed++;
    switch (event->kind) {
    case BONGO_CAT_INPUT_KEY_DOWN: app->input_key_down_events++; break;
    case BONGO_CAT_INPUT_KEY_UP: app->input_key_up_events++; break;
    case BONGO_CAT_INPUT_MOUSE_DOWN: app->input_mouse_down_events++; break;
    case BONGO_CAT_INPUT_MOUSE_UP: app->input_mouse_up_events++; break;
    default: break;
    }
    snprintf(app->input_last_name, sizeof(app->input_last_name), "%s",
        event->name[0] ? event->name : "none");
}

void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    if (app && app->secondary_pet) allow_shortcuts = false;
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&app->input, &event)) {
        record_input_event(app, &event);
        if (app->smoke_ignore_global_input) continue;
        if (app->smoke_input_audit) {
            char path[BONGO_CAT_PATH_CAP];
            bongo_cat_path_join(path, sizeof(path), app->state_root,
                "input-audit.txt");
            FILE *file = bongo_cat_file_open(path, "ab");
            if (file) {
                fprintf(file, "kind=%d name=%s value=%.3f\n",
                    event.kind, event.name, event.value);
                fclose(file);
            }
        }
        if (strcmp(event.name, "CapsLock") == 0)
            bongo_cat_input_schedule_release(&app->input, &event, 100);
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_scheduled_release(&app->input, now, &event)) {
        record_input_event(app, &event);
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
    log_input_consumer(app);
}
