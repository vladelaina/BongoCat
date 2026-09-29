#include "preferences_state.h"
#include "preferences_import_internal.h"
#include "preferences_render_internal.h"
#include "ui_catime.h"
#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "about/preferences_about_internal.h"
#include "ui_paint_cache.h"
#include "ui_paint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <sys/utime.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void busy(BongoCatImportDialog *dialog, bool open, bool importing) {
    SDL_LockMutex(dialog->mutex);
    dialog->open = open;
    dialog->busy = importing;
    SDL_UnlockMutex(dialog->mutex);
}

static void about_disk_cache(BongoCatApp *app) {
    char root[BONGO_CAT_PATH_CAP], directory[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
    CHECK(bongo_cat_path_join(root, sizeof(root), app->cache_root, "about-cache-test"));
    CHECK(bongo_cat_path_join(directory, sizeof(directory), root, "about"));
    CHECK(bongo_cat_path_create_directory(directory));
    CHECK(bongo_cat_path_join(path, sizeof(path), directory, "wechat-v1.svg"));
    /* Larger than the network's initial buffer, but well below the QR limit. */
    char svg[9000];
    snprintf(svg, sizeof(svg),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"240\">"
        "<!--%08000d--><rect width=\"240\" height=\"240\" fill=\"white\"/></svg>", 0);
    size_t svg_size = strlen(svg);
    Uint32 event_type = SDL_RegisterEvents(1);
    for (int stale = 0; stale < 2; stale++) {
        FILE *file = bongo_cat_file_open(path, "wb");
        CHECK(file != NULL);
        if (!file) return;
        CHECK(fwrite(svg, 1, svg_size, file) == svg_size);
        CHECK(fflush(file) == 0);
        if (stale) {
            time_t old = time(NULL) - 2 * 86400;
#ifdef _WIN32
            struct __utimbuf64 times = {old, old};
            CHECK(_futime64(_fileno(file), &times) == 0);
#else
            struct timespec times[2] = {{old, 0}, {old, 0}};
            CHECK(futimens(fileno(file), times) == 0);
#endif
        }
        CHECK(fclose(file) == 0);
        SDL_PathInfo before = {0}, after = {0};
        CHECK(SDL_GetPathInfo(path, &before));
        BongoCatAboutRequest *job = bongo_cat_about_request(BONGO_ABOUT_WECHAT,
            event_type, 0, root, false);
        CHECK(job != NULL);
        if (!job) return;
        SDL_WaitThread(job->thread, NULL);
        job->thread = NULL;
        CHECK(SDL_GetAtomicInt(&job->done));
        CHECK(job->status == 200 && job->qr_pixels);
        CHECK(job->refresh_needed == (stale != 0));
        CHECK(!job->response && !job->capacity);
        bongo_cat_about_request_free(job);
        CHECK(SDL_GetPathInfo(path, &after));
        CHECK(before.modify_time == after.modify_time);
        file = bongo_cat_file_open(path, "rb");
        CHECK(file != NULL);
        if (file) {
            char bytes[sizeof(svg)] = {0};
            CHECK(fread(bytes, 1, svg_size, file) == svg_size);
            CHECK(strcmp(bytes, svg) == 0);
            CHECK(fclose(file) == 0);
        }
        SDL_FlushEvent(event_type);
    }
    CHECK(bongo_cat_file_remove(path));
    CHECK(bongo_cat_path_remove(directory));
    CHECK(bongo_cat_path_remove(root));
}

static void about_refresh_failure(BongoCatApp *app) {
    BongoCatPreferences *value = calloc(1, sizeof(*value));
    CHECK(value != NULL);
    if (!value) return;
    value->app = app;
    value->visible = true;
    value->about.event_type = SDL_RegisterEvents(1);
    value->about.qr_attempted = value->about.contributors_attempted = true;
    value->about.qr_pixels = calloc(240 * 240, 4);
    value->about.contributors = calloc(1, sizeof(*value->about.contributors));
    unsigned char *pixels = value->about.qr_pixels;
    BongoCatAboutFeed *feed = value->about.contributors;
    CHECK(pixels && feed);
    for (int cancelled = 0; cancelled < 2; cancelled++) {
        BongoCatAboutRequest *job = calloc(1, sizeof(*job));
        CHECK(job != NULL);
        if (!job) break;
        job->kind = BONGO_ABOUT_WECHAT;
        /* A failed refresh and a cancelled successful refresh must both
           preserve the old cache without automatically retrying. */
        if (cancelled) {
            job->status = 200;
            job->qr_pixels = calloc(240 * 240, 4);
            job->refresh_needed = true;
            SDL_SetAtomicInt(&job->cancel, 1);
        }
        SDL_SetAtomicInt(&job->done, 1);
        value->about.qr_request = job;
        bongo_cat_about_refresh(value);
        CHECK(value->about.qr_pixels == pixels && value->about.contributors == feed);
        CHECK(!value->about.qr_request && !value->about.contributors_request);
        CHECK(!value->about.qr_texture_dirty);
    }
    bongo_cat_about_clear(value, false);
    free(value);
}

static void about_unchanged_contributors(BongoCatApp *app) {
    BongoCatPreferences *value = calloc(1, sizeof(*value));
    CHECK(value != NULL);
    if (!value) return;
    value->app = app;
    value->visible = true;
    value->about.event_type = SDL_RegisterEvents(1);
    value->about.qr_attempted = value->about.contributors_attempted = true;
    for (int update = 0; update < 4; update++) {
        BongoCatAboutRequest *job = calloc(1, sizeof(*job));
        CHECK(job != NULL);
        if (!job) break;
        job->feed = calloc(1, sizeof(*job->feed));
        CHECK(job->feed != NULL);
        if (!job->feed) { free(job); break; }
        job->feed->count = 1;
        job->feed->people[0].pixels = calloc(BONGO_ABOUT_AVATAR_SIZE * BONGO_ABOUT_AVATAR_SIZE, 4);
        CHECK(job->feed->people[0].pixels != NULL);
        if (!job->feed->people[0].pixels) { bongo_cat_about_request_free(job); break; }
        if (update >= 2) job->feed->people[0].pixels[0] = 255;
        if (update == 3) strcpy(job->feed->people[0].name, "Renamed contributor");
        job->kind = BONGO_ABOUT_CONTRIBUTORS;
        job->status = 200;
        SDL_SetAtomicInt(&job->done, 1);
        BongoCatAboutFeed *previous = value->about.contributors;
        value->about.contributors_request = job;
        value->about.portraits_loaded = true;
        value->about.portraits_next = 1;
        bongo_cat_about_refresh(value);
        CHECK(!value->about.contributors_request);
        if (update == 1) {
            CHECK(value->about.contributors == previous);
            CHECK(value->about.portraits_loaded && value->about.portraits_next == 1);
        } else {
            CHECK(value->about.contributors != previous);
            CHECK(!value->about.portraits_loaded && value->about.portraits_next == 0);
        }
    }
    bongo_cat_about_clear(value, false);
    free(value);
}

static void about_session_cache(BongoCatPreferences *value) {
    BongoCatAboutState *s = &value->about;
    CHECK(s->contributors_attempted || (s->contributors_request &&
        SDL_GetAtomicInt(&s->contributors_request->cancel)));
    CHECK(s->qr_attempted || (s->qr_request && SDL_GetAtomicInt(&s->qr_request->cancel)));
    BongoCatAboutRequest *contributors_request = s->contributors_request;
    BongoCatAboutRequest *qr_request = s->qr_request;
    /* A second show only raises the already open window. */
    bongo_cat_preferences_show(value);
    CHECK(s->contributors_request == contributors_request);
    CHECK(s->qr_request == qr_request);
    if (!s->contributors) s->contributors = calloc(1, sizeof(*s->contributors));
    if (!s->qr_pixels) s->qr_pixels = calloc(240 * 240, 4);
    BongoCatAboutFeed *feed = s->contributors;
    unsigned char *pixels = s->qr_pixels;
    CHECK(feed && pixels);
    s->qr_open = true;
    SDL_GL_MakeCurrent(value->window, value->gl_context);
    bongo_cat_preferences_page_cache_clear(value, 3, 0);
    bongo_cat_preferences_page_cache_clear(value, 0, 3);
    CHECK(!s->qr_open);
    CHECK(s->contributors == feed && s->qr_pixels == pixels);
    CHECK(s->contributors_request == contributors_request && s->qr_request == qr_request);
    /* A DPI/theme asset rebuild must retain downloaded data and attempt flags. */
    bool qr_attempted = s->qr_attempted;
    bongo_cat_about_assets_clear(value, true);
    CHECK(s->qr_attempted == qr_attempted);
    CHECK(s->contributors == feed && s->qr_pixels == pixels);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
}

static float original_paragraph_height(const char *text,
    const struct nk_user_font *font, float width, float leading) {
    float height = 0;
    while (*text) {
        int length = 0, remaining = (int)strlen(text);
        while (length < remaining) {
            nk_rune rune;
            int bytes = nk_utf_decode(text + length, &rune, remaining - length);
            if (bytes <= 0) break;
            float next = font->width(font->userdata, font->height, text, length + bytes);
            if (length && next > width) break;
            length += bytes;
            if (rune == '\n') break;
        }
        if (!length) break;
        height += leading;
        text += length;
        while (*text == ' ' || *text == '\n') text++;
    }
    return height;
}

static void about_render_cost_regressions(BongoCatPreferences *value) {
    const char *texts[] = {"Open source & community contributions",
        "Every step of BongoCat comes from open source. Thank you to all our contributors.",
        "\xe6\x84\x9f\xe8\xb0\xa2\xe6\x89\x80\xe6\x9c\x89\xe8\xb4\xa1\xe7\x8c\xae\xe8\x80\x85 BongoCat",
        "First line\n  Second line", "", "A"};
    const struct nk_user_font *font = value->ui.caption_font;
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++)
        for (int width = 1; width <= 400; width += 3) {
            float actual = bongo_cat_about_paragraph(NULL, nk_rect(0, 0, (float)width, 0),
                texts[i], font, nk_rgb(0, 0, 0), false, 24);
            CHECK(actual == original_paragraph_height(texts[i], font, (float)width, 24));
        }
    SDL_GL_MakeCurrent(value->window, value->gl_context);
    BongoCatUIPaintKey key = {BONGO_CAT_UI_PAINT_SHADOW, 8, 8, 4, 2, 0, 0, 0};
    unsigned char pixels[64] = {0};
    bongo_cat_ui_paint_cache_begin_frame(&value->ui);
    BongoCatUIPaintTexture *item = bongo_cat_ui_paint_cache_get(&value->ui, &key);
    CHECK(item && bongo_cat_ui_paint_cache_upload(item, pixels, true));
    /* One unused frame and idle trimming must not discard a small hover effect. */
    bongo_cat_ui_paint_cache_begin_frame(&value->ui);
    bongo_cat_ui_paint_cache_begin_frame(&value->ui);
    bongo_cat_ui_trim_idle(&value->ui);
    item = bongo_cat_ui_paint_cache_get(&value->ui, &key);
    CHECK(bongo_cat_ui_paint_cache_ready(item));
    CHECK(bongo_cat_ui_paint_cache_usage(&value->ui, NULL) <= 4u * 1024u * 1024u);
    bongo_cat_ui_paint_destroy(&value->ui);
    CHECK(bongo_cat_ui_paint_cache_usage(&value->ui, NULL) == 0);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
}

static void close_over_scrolled_models(BongoCatPreferences *value) {
    if (bongo_cat_ui_native_chrome()) return;
    struct nk_context *context = &value->ui.context;
    value->page = 1;
    value->page_seen = true;
    value->last_page = 1;
    value->page_transition_ns = 0;
    for (int scroll = 0; scroll <= 800; scroll += 40) {
        nk_input_begin(context);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_false);
        nk_input_end(context);
        bongo_cat_preferences_draw_frame(value, 800, 500, false);
        nk_clear(context);
        value->scroll_ready[1] = true;
        value->scroll_current[1] = (float)scroll;
        value->scroll_target[1] = (float)scroll;
        nk_input_begin(context);
        nk_input_motion(context, 746, 36);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_true);
        nk_input_button(context, NK_BUTTON_LEFT, 746, 36, nk_false);
        nk_input_end(context);
        CHECK(bongo_cat_preferences_draw_frame(value, 800, 500, false));
        CHECK(!value->model_selection_pending);
        value->model_selection_pending = false;
        nk_clear(context);
    }
}

static void inactive_model_behaviors(BongoCatPreferences *value) {
    BongoCatApp *app = value->app;
    char active[BONGO_CAT_ID_CAP], loaded[BONGO_CAT_ID_CAP];
    snprintf(active, sizeof(active), "%s", app->session.active_model_id);
    snprintf(loaded, sizeof(loaded), "%s", app->loaded_model);
    BongoCatLive2D *live2d = app->live2d;
    const BongoCatModelEntry *model = NULL;
    for (size_t i = 0; i < app->models.count; ++i)
        if (strcmp(app->models.entries[i].id, loaded)) {
            model = &app->models.entries[i];
            break;
        }
    CHECK(model != NULL);
    if (!model) return;
    bongo_cat_preferences_behavior_dialog_open_model(value, model);
    CHECK(value->behavior_dialog);
    CHECK(!strcmp(value->behavior_model_id, model->id));
    CHECK(!bongo_cat_preferences_behavior_model_loaded(value));
    CHECK(!value->model_selection_pending);
    CHECK(!strcmp(active, app->session.active_model_id));
    CHECK(!strcmp(loaded, app->loaded_model));
    CHECK(live2d == app->live2d);
    const BongoCatBehaviorCatalog *catalog =
        bongo_cat_preferences_behavior_catalog(value);
    CHECK(catalog->count > 0);
    for (size_t i = 0; i < catalog->count; ++i)
        CHECK(!strncmp(catalog->entries[i].id, model->id, strlen(model->id)));
    bongo_cat_preferences_behavior_dialog_close(value);
}

static void shortcut_chord_capture(BongoCatPreferences *value) {
    static const struct { SDL_Keycode key; const char *binding; } punctuation[] = {
        {SDLK_EQUALS, "Alt+="}, {SDLK_MINUS, "Alt+-"},
        {SDLK_LEFTBRACKET, "Alt+BracketLeft"}, {SDLK_RIGHTBRACKET, "Alt+BracketRight"},
        {SDLK_BACKSLASH, "Alt+Backslash"}, {SDLK_SEMICOLON, "Alt+Semicolon"},
        {SDLK_APOSTROPHE, "Alt+Quote"}, {SDLK_COMMA, "Alt+Comma"},
        {SDLK_PERIOD, "Alt+Period"}, {SDLK_SLASH, "Alt+Slash"},
        {SDLK_GRAVE, "Alt+BackQuote"}, {SDLK_KP_PLUS, "Alt+KpPlus"},
        {SDLK_KP_MINUS, "Alt+KpMinus"}, {SDLK_KP_MULTIPLY, "Alt+KpMultiply"},
        {SDLK_KP_DIVIDE, "Alt+KpDivide"}, {SDLK_KP_DECIMAL, "Alt+KpDecimal"}
    };
    for (size_t i = 0; i < sizeof(punctuation) / sizeof(punctuation[0]); ++i) {
        char recorded[BONGO_CAT_SHORTCUT_CAP] = "";
        bongo_cat_preferences_shortcut_begin(value, "test-punctuation", recorded, sizeof(recorded));
        SDL_Event input = {0};
        input.type = SDL_EVENT_KEY_DOWN;
        input.key.down = true;
        input.key.key = punctuation[i].key;
        input.key.mod = SDL_KMOD_ALT;
        CHECK(bongo_cat_preferences_shortcut_event(value, &input));
        CHECK(!strcmp(recorded, punctuation[i].binding));
        bongo_cat_preferences_shortcut_cancel(value);
        CHECK(!recorded[0]);
    }
    char target[BONGO_CAT_SHORTCUT_CAP] = "";
    bongo_cat_preferences_shortcut_begin(value, "test-chord", target, sizeof(target));
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.key = SDLK_F23;
    event.key.mod = SDL_KMOD_CTRL;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!strcmp(target, "Control+F23"));
    event.key.key = SDLK_F24;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!strcmp(target, "Control+F23+F24"));
    event.key.repeat = true;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!strcmp(target, "Control+F23+F24"));
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    event.key.repeat = false;
    event.key.key = SDLK_F23;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!value->shortcut_recording);
    CHECK(!strcmp(target, "Control+F23+F24"));
    bongo_cat_preferences_shortcut_begin(value, "test-chord", target, sizeof(target));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.mod = 0;
    event.key.key = SDLK_F23;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    event.key.key = SDLK_LCTRL;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!strcmp(target, "F23+Control"));
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!value->shortcut_recording);
    CHECK(!strcmp(target, "F23+Control"));
    char small[5] = "F1";
    bongo_cat_preferences_shortcut_begin(value, "test-small", small, sizeof(small));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.mod = 0;
    event.key.key = SDLK_F23;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    event.key.key = SDLK_F24;
    CHECK(bongo_cat_preferences_shortcut_event(value, &event));
    CHECK(!value->shortcut_recording);
    CHECK(!strcmp(small, "F1"));
}

int main(int argc, char **argv) {
    BongoCatApp *app = calloc(1, sizeof(*app));
    BongoCatError error = {0};
    if (!app) return 1;
    if (!bongo_cat_app_initialize(app, argc, argv, &error)) {
        fprintf(stderr, "Initialization failed: %s\n", error.message);
        bongo_cat_app_shutdown(app, "test:failed", 1);
        free(app);
        return 1;
    }
    BongoCatPreferences *value = app->preferences;
    about_disk_cache(app);
    about_refresh_failure(app);
    about_unchanged_contributors(app);
    CHECK(value != NULL);
    if (value) {
        shortcut_chord_capture(value);
        for (int cycle = 0; cycle < 3; ++cycle) {
            bongo_cat_preferences_show(value);
            CHECK(value->window && value->gl_context && value->ui_initialized);
            if (!cycle) about_render_cost_regressions(value);
            about_session_cache(value);
            if (!cycle) inactive_model_behaviors(value);
            bongo_cat_preferences_close(value);
            CHECK(!value->behavior_catalog);
            CHECK(!value->about.contributors && !value->about.qr_pixels);
            CHECK(!value->about.contributors_attempted && !value->about.qr_attempted);
            CHECK(!value->about.contributors_request ||
                SDL_GetAtomicInt(&value->about.contributors_request->cancel));
            CHECK(!value->about.qr_request ||
                SDL_GetAtomicInt(&value->about.qr_request->cancel));
            CHECK(!value->window && !value->gl_context && !value->ui_initialized);
            CHECK(SDL_GL_GetCurrentContext() == app->gl_context);
        }
        bongo_cat_preferences_show(value);
        SDL_Window *window = value->window;
        busy(value->import_dialog, true, false);
        bongo_cat_preferences_close(value);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window && !value->visible);
        CHECK(!value->about.contributors_attempted && !value->about.qr_attempted);
        busy(value->import_dialog, false, true);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window);
        bongo_cat_preferences_show(value);
        CHECK(value->window == window && value->visible);
        about_session_cache(value);
        busy(value->import_dialog, false, false);
        bongo_cat_preferences_render(value);
        CHECK(value->window == window && value->visible);
        busy(value->import_dialog, false, true);
        bongo_cat_preferences_close(value);
        CHECK(value->window == window && !value->visible);
        busy(value->import_dialog, false, false);
        bongo_cat_preferences_render(value);
        CHECK(!value->window && !value->gl_context && !value->ui_initialized);
        bongo_cat_preferences_show(value);
        CHECK(value->visible && value->ui_initialized);
        close_over_scrolled_models(value);
        bongo_cat_preferences_close(value);
    }
    bongo_cat_app_shutdown(app, "test:complete", failures != 0);
    free(app);
    printf("Preferences lifecycle: %d failures\n", failures);
    return failures != 0;
}
