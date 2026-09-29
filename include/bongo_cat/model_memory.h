#ifndef BONGO_CAT_MODEL_MEMORY_H
#define BONGO_CAT_MODEL_MEMORY_H

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Main-thread diagnostics for the app's synchronous model switch. Secondary
   pets have independent processes. Stage names must be string literals.
   Sampling never writes logs; stage output has a per-switch upper bound. */
void bongo_cat_model_memory_begin(const char *previous, const char *next,
    bool dynamic, int width, int height);
void bongo_cat_model_memory_sample(void);
void bongo_cat_model_memory_log(const char *stage, const char *format, ...)
    SDL_PRINTF_VARARG_FUNC(2);
void bongo_cat_model_memory_complete(bool success, int width, int height);
void bongo_cat_model_memory_after_trim(void);
/* Update settings ownership flags shared with the resource trace. */
void bongo_cat_model_memory_ui_state(bool retained, bool visible);
/* One settled model sample; resource_trace owns the wider lifecycle trace. */
void bongo_cat_model_memory_poll(void);

/* RGBA8 storage estimate only, excluding driver copies and other resources. */
double bongo_cat_model_texture_mib(int width, int height, bool mipmaps);

#ifdef __cplusplus
}
#endif
#endif
