#include "preferences_state.h"
#include "preferences_model_cover.h"
#include "ui_paint_cache.h"
#include "bongo_cat/resource_trace.h"
#include "bongo_cat/model_memory.h"

/* Read application bookkeeping only. Do not bind GL objects or consume GL
   errors merely to measure them, including when context activation failed. */
void bongo_cat_preferences_resource_note(BongoCatPreferences *value, const char *stage) {
    if (!value) return;
    bongo_cat_model_memory_ui_state(value->window != NULL, value->visible);
    size_t covers = 0, paint = 0;
    size_t cover_bytes = bongo_cat_preferences_model_cover_usage(value->app, &covers);
    size_t paint_bytes = bongo_cat_ui_paint_cache_usage(&value->ui, &paint);
    const double mib = 1024.0 * 1024.0;
    bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_SETTINGS, stage,
        "window=%d context=%d initialized=%d font=%dx%d font_r8_est_mib=%.2f "
        "covers=%zu cover_rgba_est_mib=%.2f paint_textures=%zu paint_est_mib=%.2f "
        "resize_cache=%dx%d draw_cpu_capacity_mib=%.2f",
        value->window != NULL, value->gl_context != NULL, value->ui_initialized,
        value->ui.font_atlas_width, value->ui.font_atlas_height,
        (double)value->ui.font_atlas_width * value->ui.font_atlas_height / mib,
        covers, cover_bytes / mib, paint, paint_bytes / mib,
        value->ui.resize_cache_width, value->ui.resize_cache_height,
        (value->ui.vertex_capacity + value->ui.element_capacity + value->ui.commands.allocated) / mib);
}
