#ifndef BONGO_CAT_RESOURCE_TRACE_H
#define BONGO_CAT_RESOURCE_TRACE_H

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BongoCatResourceScope {
    BONGO_CAT_RESOURCE_MODEL,
    BONGO_CAT_RESOURCE_RESIZE,
    BONGO_CAT_RESOURCE_SETTINGS,
    BONGO_CAT_RESOURCE_MENU,
    BONGO_CAT_RESOURCE_SNAPSHOT,
    BONGO_CAT_RESOURCE_SCOPE_COUNT
} BongoCatResourceScope;

/* Main thread only. Fixed storage, no GL queries, no logging from workers.
   One operation per scope; repeated resize events join the same operation.
   Notes are bounded per operation and all output is rate limited together. */
void bongo_cat_resource_trace_begin(BongoCatResourceScope scope, const char *label);
void bongo_cat_resource_trace_note(BongoCatResourceScope scope,
    const char *stage, const char *format, ...) SDL_PRINTF_VARARG_FUNC(3);
void bongo_cat_resource_trace_end(BongoCatResourceScope scope,
    const char *stage, const char *format, ...) SDL_PRINTF_VARARG_FUNC(3);
void bongo_cat_resource_trace_poll(void);
/* Emit one final process snapshot during orderly shutdown. The snapshot is
   intentionally independent of an operation scope so an early exit still
   leaves a useful end-of-run memory/handle reading. */
void bongo_cat_resource_trace_shutdown(void);
/* Called before applying geometry and from the main loop. Dimensions here are
   logical window pixels; atlas/viewport logs report physical pixels separately. */
void bongo_cat_resource_trace_resize_request(int from_w, int from_h, int to_w, int to_h);
void bongo_cat_resource_trace_resize_observe(int width, int height,
    bool gesture, bool texture_pending, bool snapshot);
/* Records application-owned estimates, not measured VRAM or returned RAM. */
void bongo_cat_resource_trace_atlas(double mib);
void bongo_cat_resource_trace_render(double mask_mib, unsigned pool_targets,
    int width, int height);
void bongo_cat_resource_trace_settings(bool retained, bool visible);

#ifdef __cplusplus
}
#endif
#endif
