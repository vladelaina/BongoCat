#include "cubism_texture_refresh_memory.hpp"
#include "bongo_cat/log.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/resource_trace.h"

#include <SDL3/SDL_timer.h>

namespace bongo_cat {

void TextureRefreshMemory::log(const char *stage, double live_atlas_mib) const {
    BongoCatMemoryUsage usage{};
    const bool available = bongo_cat_platform_memory_usage(&usage);
    constexpr double mib = 1024.0 * 1024.0;
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[texture-refresh-memory] stage=%s started=%u committed=%u cancelled=%u "
        "failed=%u live_atlas_est_mib=%.1f old_atlas_delete_requested_est_mib=%.1f "
        "cleanup_errors=%u last_cleanup_ms=%.1f last_cancel_ms=%.1f "
        "memory_available=%d ws_mib=%.1f private_mib=%.1f",
        stage, started_, committed_, cancelled_, failed_, live_atlas_mib,
        delete_requested_mib_, cleanup_errors_, last_cleanup_ms_, last_cancel_ms_,
        available ? 1 : 0,
        available ? (double)usage.working_set_bytes / mib : -1.0,
        available ? (double)usage.private_bytes / mib : -1.0);
}

void TextureRefreshMemory::begin(double live_atlas_mib) {
    bongo_cat_resource_trace_atlas(live_atlas_mib);
    ++started_;
    if (started_ == 1) log("begin", live_atlas_mib);
    sample_ns_ = 0;
}

void TextureRefreshMemory::finish(bool committed, bool cancelled,
    double live_atlas_mib, double delete_requested_mib,
    double cleanup_ms, double cancel_ms, bool cleanup_ok) {
    bongo_cat_resource_trace_atlas(live_atlas_mib);
    if (committed) ++committed_;
    else if (cancelled) ++cancelled_;
    else ++failed_;
    delete_requested_mib_ += delete_requested_mib;
    if (!cleanup_ok) ++cleanup_errors_;
    last_cleanup_ms_ = cleanup_ms;
    last_cancel_ms_ = cancel_ms;
    log(cleanup_ok ? "cleanup" : "cleanup-failed", live_atlas_mib);
    sample_ns_ = SDL_GetTicksNS() + 3000000000ull;
}

void TextureRefreshMemory::poll(uint64_t now, double live_atlas_mib) {
    if (!due(now)) return;
    /* The decoder/job buffers and old owners have already been released.
       Coalesce heap reclamation across a resize burst, rather than scanning
       the heap on every strip, cancelled job or atlas replacement. */
    bongo_cat_platform_trim_memory();
    log("settled", live_atlas_mib);
    *this = {};
}

} // namespace bongo_cat
