#ifndef BONGO_CAT_CUBISM_TEXTURE_REFRESH_MEMORY_HPP
#define BONGO_CAT_CUBISM_TEXTURE_REFRESH_MEMORY_HPP

#include <cstdint>

namespace bongo_cat {

/* Main-thread only. One begin, one line per cleaned-up job and one settled
   sample; no frame/row sampling and no working-set eviction. */
class TextureRefreshMemory {
public:
    void begin(double live_atlas_mib);
    void finish(bool committed, bool cancelled, double live_atlas_mib,
        double delete_requested_mib, double cleanup_ms, double cancel_ms, bool cleanup_ok);
    bool due(uint64_t now) const { return sample_ns_ && now >= sample_ns_; }
    /* Space allocations after cancellation independently of diagnostics.
       Job cleanup checks GPU completion; this delay cannot guarantee that
       driver caches return memory. Successful commits need no extra pause. */
    bool cancellation_cooldown(uint64_t now) const {
        return cancelled_ && cooldown_ns_ && now < cooldown_ns_;
    }
    void poll(uint64_t now, double live_atlas_mib);
private:
    void log(const char *stage, double live_atlas_mib) const;
    uint64_t sample_ns_ = 0;
    uint64_t cooldown_ns_ = 0;
    unsigned started_ = 0, committed_ = 0, cancelled_ = 0, failed_ = 0;
    unsigned cleanup_errors_ = 0;
    double last_cleanup_ms_ = 0.0, last_cancel_ms_ = 0.0;
    double delete_requested_mib_ = 0.0;
};

} // namespace bongo_cat
#endif
