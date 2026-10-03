#include "preferences_state.h"
#include "preferences_about_internal.h"
#include <SDL3/SDL_opengl.h>
#include <string.h>
#include <stdlib.h>

void bongo_cat_about_ensure_event(BongoCatPreferences *value) {
    if (!value->about.event_type)
        value->about.event_type = SDL_RegisterEvents(1);
}

static bool same_contributors(const BongoCatAboutFeed *a, const BongoCatAboutFeed *b) {
    if (!a || !b || a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        const BongoCatAboutContributor *left = &a->people[i], *right = &b->people[i];
        if (strcmp(left->name, right->name) || strcmp(left->profile, right->profile) ||
            (!left->pixels != !right->pixels) ||
            (left->pixels && memcmp(left->pixels, right->pixels,
                BONGO_ABOUT_AVATAR_SIZE * BONGO_ABOUT_AVATAR_SIZE * 4))) return false;
    }
    return true;
}

/* Collect CPU results even on other settings pages; GL uploads remain lazy.
   Cancelled jobs keep their slot until completion, bounding rapid reopen work. */
void bongo_cat_about_refresh(BongoCatPreferences *value) {
    BongoCatAboutState *s = &value->about;
    BongoCatAboutRequest **slots[] = {&s->qr_request, &s->contributors_request};
    for (int i = 0; i < 2; i++) {
        BongoCatAboutRequest *job = *slots[i];
        if (!job || !SDL_GetAtomicInt(&job->done)) continue;
        bool refresh = value->visible && job->refresh_needed &&
            !SDL_GetAtomicInt(&job->cancel);
        int kind = job->kind;
        if (value->visible && job->status == 200 && !SDL_GetAtomicInt(&job->cancel)) {
            if (value->page == 3) value->render_dirty = true;
            if (job->kind == BONGO_ABOUT_WECHAT) {
                if (!s->qr_pixels || !job->qr_pixels ||
                    memcmp(s->qr_pixels, job->qr_pixels, 240 * 240 * 4)) {
                    free(s->qr_pixels);
                    s->qr_pixels = job->qr_pixels;
                    job->qr_pixels = NULL;
                    s->qr_texture_dirty = true;
                }
            } else if (!same_contributors(s->contributors, job->feed)) {
                bongo_cat_about_feed_free(s->contributors);
                s->contributors = job->feed;
                job->feed = NULL;
                s->portraits_loaded = false;
                s->portraits_next = 0;
            }
        }
        bongo_cat_about_request_free(job);
        *slots[i] = NULL;
        if (refresh)
            *slots[i] = bongo_cat_about_request(kind, s->event_type,
                SDL_GetWindowID(value->window), value->app->cache_root, true);
    }
    if (!value->visible) return;
    bongo_cat_about_ensure_event(value);
    if (!s->contributors_attempted && !s->contributors_request) {
        s->contributors_attempted = true;
        s->contributors_request = bongo_cat_about_request(BONGO_ABOUT_CONTRIBUTORS,
            s->event_type, SDL_GetWindowID(value->window), value->app->cache_root, false);
    }
    if (!s->qr_attempted && !s->qr_request) {
        s->qr_attempted = true;
        s->qr_request = bongo_cat_about_request(BONGO_ABOUT_WECHAT,
            s->event_type, SDL_GetWindowID(value->window), value->app->cache_root, false);
    }
}

bool bongo_cat_about_event(BongoCatPreferences *value, const SDL_Event *event) {
    if (value->about.event_type && event->type == value->about.event_type) {
        bongo_cat_about_refresh(value);
        if (value->visible && value->page == 3) value->render_dirty = true;
        return true;
    }
    return false;
}

void bongo_cat_about_assets_clear(BongoCatPreferences *value, bool gl_ready) {
    BongoCatAboutState *s = &value->about;
    if (gl_ready) {
        if (s->wechat_icon_texture)
            glDeleteTextures(1, &s->wechat_icon_texture);
        if (s->qr_texture)
            glDeleteTextures(1, &s->qr_texture);
        glDeleteTextures(BONGO_ABOUT_CONTRIBUTOR_CAP, s->portraits);
    }
    s->qr_texture = 0;
    s->qr_texture_dirty = false;
    s->wechat_icon_texture = 0;
    s->wechat_icon_attempted = false;
    memset(s->portraits, 0, sizeof(s->portraits));
    s->portraits_loaded = false;
    s->portraits_next = 0;
}

void bongo_cat_about_clear(BongoCatPreferences *value, bool gl_ready) {
    BongoCatAboutState *s = &value->about;
    /* Closing settings never waits for DNS/TLS. Shutdown joins remaining jobs. */
    if (s->qr_request)
        SDL_SetAtomicInt(&s->qr_request->cancel, 1);
    if (s->contributors_request)
        SDL_SetAtomicInt(&s->contributors_request->cancel, 1);
    bongo_cat_about_feed_free(s->contributors);
    s->contributors = NULL;
    free(s->qr_pixels);
    s->qr_pixels = NULL;
    s->contributors_attempted = false;
    bongo_cat_about_assets_clear(value, gl_ready);
    s->portraits_loaded = s->qr_attempted = s->qr_open = false;
}

void bongo_cat_about_shutdown(BongoCatPreferences *value) {
    bongo_cat_about_request_free(value->about.contributors_request);
    value->about.contributors_request = NULL;
    bongo_cat_about_request_free(value->about.qr_request);
    value->about.qr_request = NULL;
}
