#ifndef BONGO_CAT_MEMORY_H
#define BONGO_CAT_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bongo_cat_platform_trim_memory(void);

typedef struct BongoCatMemoryUsage {
    uint64_t working_set_bytes;
    uint64_t private_bytes;
    uint64_t process_peak_working_set_bytes;
} BongoCatMemoryUsage;

/* Current process only. False means counters are unavailable, not zero. */
bool bongo_cat_platform_memory_usage(BongoCatMemoryUsage *usage);

typedef struct BongoCatProcessResources {
    BongoCatMemoryUsage memory;
    uint64_t cpu_time_ns;
    uint32_t handles, gdi_objects, user_objects;
    bool memory_available, cpu_available, handles_available, gui_available;
} BongoCatProcessResources;

/* Event/low-frequency diagnostics only. Does not enumerate handles or force
   GPU completion. Unsupported counters are explicitly marked unavailable. */
void bongo_cat_platform_process_resources(BongoCatProcessResources *usage);

#ifdef __cplusplus
}
#endif

#endif
