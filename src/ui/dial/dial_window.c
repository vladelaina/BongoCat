#include "dial_internal.h"
#include "ui_backend.h"
#include "bongo_cat/log.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/runtime_diagnostics.h"
#include "bongo_cat/resource_trace.h"
#include <stdlib.h>
#ifdef _WIN32
#include "windows_hdr.h"
#endif

static bool SDLCALL collect_event(void *userdata, SDL_Event *event) {
    Dial *d = userdata;
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_TERMINATING) {
        d->done = true; return true; /* Let the main application handle shutdown. */
    }
    SDL_Window *window = SDL_GetWindowFromEvent(event);
    if (window != d->window) {
        if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->common.timestamp >= d->input_after_ns) ||
            (window == d->owner && event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)) d->done = true;
        return true; /* Never consume another pet's/preferences/input events. */
    }
    bool input = (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST) ||
        event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL ||
        event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP;
    if (!input || d->event_count == 128) return true;
    d->events[d->event_count++] = *event;
    return false;
}

void dial_preview(Dial *d, BongoCatMenuAction action) {
    if (!d->labels->preview) return;
    if (d->previous_window && d->previous_context)
        SDL_GL_MakeCurrent(d->previous_window,d->previous_context);
    d->labels->preview(d->labels->preview_userdata,action);
    if (!SDL_GL_MakeCurrent(d->window,d->context)) d->done = true;
}

static void animate(Dial *d, uint64_t now) {
    float elapsed = fminf(.1f,(float)(now-d->animation_at)/1000);
    d->animation_at = now;
    float t = fminf(1,(float)(now-d->opened_at)/700);
    float opening = .85f+.15f*(1-powf(1-t,4));
    if (opening != d->opening) { d->opening = opening; d->dirty = true; }
    float response = 1-expf(-17*elapsed);
    for (int i = 0; i < d->count; ++i) {
        if (d->child_focus && i != d->active) { d->lift[i] = 0; continue; }
        float target = d->active == i ? 1.0f : 0.0f;
        float next = d->lift[i]+(target-d->lift[i])*response;
        if (fabsf(next-target) < .002f) next = target;
        if (next != d->lift[i]) { d->lift[i] = next; d->dirty = true; }
    }
    if (dial_child_count(d) && now-d->changed_at <=
        (uint64_t)(DIAL_REVEAL_DURATION_MS +
            (dial_child_count(d)-1)*DIAL_REVEAL_DELAY_MS + 16))
        d->dirty = true;
    if (now-d->opened_at <= (uint64_t)(DIAL_REVEAL_DURATION_MS +
        (d->count-1)*DIAL_REVEAL_DELAY_MS + 16)) d->dirty = true;
}

static bool create(Dial *d) {
    float layout = 1;
    bongo_cat_ui_query_window_scale(d->owner,&layout,NULL);
    SDL_Rect bounds;
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(d->owner),&bounds)) return false;
    float side = fminf(608*740.0f/560*layout,.96f*(float)SDL_min(bounds.w,bounds.h));
    int size = (int)ceilf(side);
    if (size < 1) return false;
    float x = 0, y = 0;
    if (SDL_GetMouseFocus() == d->owner) SDL_GetMouseState(&x,&y);
    else {
        int width, height;
        if (!SDL_GetWindowSize(d->owner,&width,&height)) return false;
        x = (float)width/2; y = (float)height/2;
    }
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_TRANSPARENT |
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    const char *driver = SDL_GetCurrentVideoDriver();
    d->popup = driver && SDL_strcmp(driver,"wayland") == 0;
    if (d->popup) {
        /* Wayland requires compositor-managed popup placement and input grabs. */
        d->window = SDL_CreatePopupWindow(d->owner,(int)x-size/2,(int)y-size/2,
            size,size,flags | SDL_WINDOW_POPUP_MENU);
    } else {
        /* A desktop pet may be nonactivating or fully transparent. A native
           child popup inherits its focus/stacking restrictions on Win32.
           An independent SDL utility window owns its focus and GL surface. */
        d->window = SDL_CreateWindow("BongoCat - Menu",size,size,flags |
            SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY | SDL_WINDOW_ALWAYS_ON_TOP);
        if (d->window) {
            SDL_GetGlobalMouseState(&x,&y);
            SDL_Point position = {(int)x,(int)y};
            SDL_DisplayID display = SDL_GetDisplayForPoint(&position);
            SDL_GetDisplayUsableBounds(display,&bounds);
            int left = SDL_clamp((int)x-size/2,bounds.x,SDL_max(bounds.x,bounds.x+bounds.w-size));
            int top = SDL_clamp((int)y-size/2,bounds.y,SDL_max(bounds.y,bounds.y+bounds.h-size));
            SDL_SetWindowPosition(d->window,left,top);
        }
    }
    if (!d->window) return false;
    SDL_SyncWindow(d->window);
#ifdef _WIN32
    bongo_cat_windows_prepare_transparent_ui(d->window);
#endif
    d->window_id = SDL_GetWindowID(d->window);
    int sharing = 0;
    SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,&sharing);
    if (!SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)) return false;
    d->context = SDL_GL_CreateContext(d->window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,sharing);
    if (!d->context || !SDL_GL_MakeCurrent(d->window,d->context)) return false;
    SDL_GL_SetSwapInterval(0); /* The modal tick supplies a 16 ms frame budget. */
    if (!SDL_GetWindowSize(d->window,&d->width,&d->height) ||
        !SDL_GetWindowSizeInPixels(d->window,&d->pixel_width,&d->pixel_height) ||
        d->width < 1 || d->height < 1) return false;
    d->scale = (float)SDL_min(d->width,d->height)/608;
    d->raster_scale = (float)d->pixel_width/(float)d->width;
    if (!dial_paint_init(d)) return false;
    d->opened_at = d->changed_at = d->animation_at = SDL_GetTicks();
    d->opening = .85f;
    d->input_after_ns = SDL_GetTicksNS();
    if (!dial_paint_frame(d) || !SDL_ShowWindow(d->window)) return false;
#ifdef _WIN32
    /* Present the visible HDR proxy before running preview callbacks/events. */
    if (!dial_paint_frame(d)) return false;
#endif
    if (!d->popup) SDL_RaiseWindow(d->window);
    d->dirty = true;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "Radial menu opened: driver=%s window=%u size=%dx%d popup=%d",
        driver ? driver : "unknown",(unsigned)d->window_id,d->width,d->height,d->popup);
    return true;
}

static void trace_menu(Dial *d, const char *stage) {
    unsigned covers = 0;
    double pixels = 0;
    for (size_t i = 0; i < BONGO_CAT_MODEL_CAP; ++i) {
        if (!d->covers[i].texture) continue;
        ++covers;
        pixels += (double)d->covers[i].width * d->covers[i].height;
    }
    const double mib = 1024.0 * 1024.0;
    bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_MENU, stage,
        "window=%d context=%d pixels=%dx%d font_r8_est_mib=%.2f "
        "covers=%u cover_rgba_est_mib=%.2f vertex_capacity_mib=%.2f",
        d->window != NULL, d->context != NULL, d->pixel_width, d->pixel_height,
        (double)d->paint.font_width * d->paint.font_height / mib,
        covers, pixels * 4 / mib, (double)d->paint.capacity * sizeof(DialVertex) / mib);
}

BongoCatMenuAction bongo_cat_platform_context_menu(BongoCatPlatform *platform,
    const BongoCatMenuLabels *labels) {
    if (!platform || !platform->window || !labels) return BONGO_CAT_MENU_NONE;
    bongo_cat_resource_trace_begin(BONGO_CAT_RESOURCE_MENU, "radial-menu");
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,"Radial menu requested: owner=%u",
        (unsigned)SDL_GetWindowID(platform->window));
    Dial *d = calloc(1,sizeof(*d));
    if (!d) {
        bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_MENU, "allocation-failed", NULL);
        return BONGO_CAT_MENU_NONE;
    }
    d->owner = platform->window; d->labels = labels; d->dark = labels->dark_theme;
    d->previous_window = SDL_GL_GetCurrentWindow();
    d->previous_context = SDL_GL_GetCurrentContext();
    d->active = d->child = d->pressed = -1;
    dial_items(d);
    bool ready = create(d);
    trace_menu(d, ready ? "ready" : "create-failed");
    uint64_t render_total_ns = 0, max_render_ns = 0;
    unsigned render_frames = 0;
    if (!ready) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Radial menu initialization failed: %s",SDL_GetError());
    while (ready && !d->done) {
        bongo_cat_resource_trace_poll();
        uint64_t start = SDL_GetTicks();
        SDL_PumpEvents();
        if (!SDL_GetWindowFromID(d->window_id)) { d->done = true; break; }
        d->event_count = 0;
        SDL_FilterEvents(collect_event,d);
        if (d->done) break;
        /* Process the global toggle before menu keys, so a binding using
           Enter or Space closes the menu without activating an item. */
        if (labels->preview_tick) {
            if (d->previous_window && d->previous_context)
                SDL_GL_MakeCurrent(d->previous_window,d->previous_context);
            labels->preview_tick(labels->preview_userdata);
        }
        if (labels->close_requested && *labels->close_requested) {
            d->done = true;
            break;
        }
        if (!SDL_GL_MakeCurrent(d->window,d->context)) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO,"Radial menu context activation failed: %s",SDL_GetError());
            break;
        }
        for (int i = 0; i < d->event_count && !d->done; ++i) {
            dial_event(d,&d->events[i]);
            if (d->done) SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
                "Radial menu dismissed: event=0x%x action=%d elapsed_ms=%llu",
                (unsigned)d->events[i].type,(int)d->result,
                (unsigned long long)(SDL_GetTicks()-d->opened_at));
        }
        if (d->done) break;
        dial_covers_tick(d);
#ifdef _WIN32
        /* The native monitor query is cached briefly. Keep checking while
           idle so a color-mode transition cannot leave a stale SDR frame. */
        bool hdr = bongo_cat_windows_hdr_enabled(d->window);
        if (hdr != d->hdr_presenting) {
            d->hdr_presenting = hdr;
            d->dirty = true;
        }
#endif
        animate(d,SDL_GetTicks());
        if (d->dirty && SDL_GetTicks() >= d->render_retry_at) {
            d->dirty = false;
            const char *previous = bongo_cat_diagnostics_phase("radial-menu-render");
            uint64_t render_started_ns = SDL_GetTicksNS();
            bool painted = dial_paint_frame(d);
            uint64_t render_ns = SDL_GetTicksNS() - render_started_ns;
            render_total_ns += render_ns;
            max_render_ns = SDL_max(max_render_ns, render_ns);
            ++render_frames;
            bongo_cat_diagnostics_phase(previous);
            if (!painted) {
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO,"Radial menu rendering failed: %s",SDL_GetError());
                /* Keep processing input during transient display failures,
                   but close after persistent rendering failures. */
                if (++d->render_failures >= 3) break;
                d->dirty = true;
                d->render_retry_at = SDL_GetTicks() + 250;
            } else {
                d->render_failures = 0;
                d->render_retry_at = 0;
            }
        }
        uint64_t elapsed = SDL_GetTicks()-start;
        if (elapsed < 16) SDL_Delay((Uint32)(16-elapsed));
    }
    trace_menu(d, "before-release");
    uint64_t cleanup_started_ns = SDL_GetTicksNS();
    bool cleanup_gl = !d->context || (SDL_GetWindowFromID(d->window_id) &&
        SDL_GL_MakeCurrent(d->window,d->context));
    if (cleanup_gl) dial_paint_free(d);
    else {
        /* A destroyed window cannot safely own GL deletion calls. Its context
           releases GPU allocations; release only CPU allocations here. */
        if (d->paint.atlas.permanent.alloc) nk_font_atlas_clear(&d->paint.atlas);
        for (int i = 0; i < 4; ++i) free(d->paint.ranges[i]);
        free(d->paint.vertices);
    }
    bool context_destroyed = !d->context || SDL_GL_DestroyContext(d->context);
    if (d->window_id && SDL_GetWindowFromID(d->window_id)) SDL_DestroyWindow(d->window);
    if (d->previous_window && d->previous_context)
        SDL_GL_MakeCurrent(d->previous_window,d->previous_context);
    if (labels->restore) labels->restore(labels->preview_userdata,d->result);
    BongoCatMenuAction result = d->result;
    free(d);
    /* Trim only after GL/CPU teardown and preview restoration have finished. */
    bongo_cat_platform_trim_memory();
    bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_MENU, "released",
        "created=%d gl_cleanup=%d context_destroyed=%d action=%d "
        "cleanup_ms=%.1f frames=%u render_ms=%.1f max_frame_ms=%.1f",
        ready, cleanup_gl, context_destroyed, (int)result,
        (double)(SDL_GetTicksNS() - cleanup_started_ns) / 1000000.0,
        render_frames, (double)render_total_ns / 1000000.0, (double)max_render_ns / 1000000.0);
    return result;
}
