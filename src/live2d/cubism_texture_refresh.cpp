#include "cubism_model.hpp"
#include "cubism_model_texture.hpp"
#include "cubism_texture_resolution.hpp"
#include "bongo_cat/image_texture_job.h"
#include "bongo_cat/log.h"
#include "bongo_cat/model_memory.h"

#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace bongo_cat {

namespace {
constexpr uint64_t kShrinkSettleNs = 300000000ull;
constexpr uint64_t kEnlargeSettleNs = 200000000ull;
constexpr uint64_t kPauseReleaseNs = 5000000000ull;
}

struct TextureRefresh {
    BongoCatImageTextureJob *job = nullptr;
    std::shared_ptr<ModelTexture> replacement;
    size_t index = 0;
    int texture_limit = 0;
    bool cancelled = false;
    bool finishing = false;
    bool committed = false;
    bool rescan = false;
    double delete_requested_mib = 0.0;
    uint64_t started = 0;
    uint64_t cancelled_ns = 0;
    uint64_t cleanup_ns = 0;
    uint64_t paused_ns = 0;
    ~TextureRefresh() { bongo_cat_image_texture_job_destroy(job); }
};

void NativeModel::cancel_texture_refresh() {
    delete texture_refresh_;
    texture_refresh_ = nullptr;
    texture_refresh_pending_ = false;
    texture_refresh_index_ = 0;
    texture_refresh_memory_ = {};
}

/* Mirror the aspect-preserving integer fit in the row decoder. Rounded bounds
   can exceed one actual dimension; comparing bounds against actual pixels
   would otherwise schedule redundant refreshes indefinitely. */
static std::pair<int, int> fitted_size(const ModelTexture &texture,
    const TextureResolution &bound) {
    auto size = texture_fitted_size(texture.source_width, texture.source_height, bound);
    if (size.first < 1 || size.second < 1)
        return {texture.width, texture.height};
    return size;
}

TextureResolution NativeModel::texture_refresh_bound(const ModelTexture &texture,
    int limit, float quality_percent) const {
    int reference_width = 0, reference_height = 0;
    canvas_size(&reference_width, &reference_height);
    if (render_options_.mver_projection) {
        if (render_options_.reference_width > 0) reference_width = render_options_.reference_width;
        if (render_options_.reference_height > 0) reference_height = render_options_.reference_height;
    }
    if (reference_width <= 0) reference_width = width_;
    if (reference_height <= 0) reference_height = height_;
    return texture_resolution_for(dynamic_texture_resolution_,
        viewport_width_, viewport_height_, reference_width, reference_height,
        texture.source_width, texture.source_height, limit, quality_percent);
}

bool NativeModel::try_reuse_texture_quality(float quality_percent) {
    /* No GL calls: settings can be drawn with a different context current.
       Commit only after every atlas fits. Active workers keep their original
       budget; a real resize still uses the existing reload/rollback path. */
    if (!texture_quality_valid(quality_percent) || !_model || textures_.empty() ||
        texture_refresh_ || texture_limit_ < 1 ||
        !GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()) return false;
    for (const auto &texture : textures_) {
        if (!texture || !texture->id || texture->source_width < 1 ||
            texture->source_height < 1) return false;
        const auto bound = texture_refresh_bound(*texture, texture_limit_, quality_percent);
        if (fitted_size(*texture, bound) !=
            std::make_pair(texture->width, texture->height)) return false;
    }
    render_quality_percent_ = quality_percent;
    return true;
}

double NativeModel::texture_storage_mib() const {
    double result = 0.0;
    for (size_t i = 0; i < textures_.size(); ++i) {
        const auto &texture = textures_[i];
        if (!texture) continue;
        /* Identical slots can share an allocation. Diagnostics count storage
           once, without allocating a set while memory is being reclaimed. */
        bool shared = false;
        for (size_t j = 0; j < i; ++j)
            if (textures_[j] == texture) { shared = true; break; }
        if (!shared) result += bongo_cat_model_texture_mib(texture->width, texture->height, true);
    }
    return result;
}

void NativeModel::schedule_texture_refresh() {
    if (!dynamic_texture_resolution_ || textures_.empty()) return;
    texture_refresh_pending_ = true;
    texture_resize_ns_ = SDL_GetTicksNS();
    texture_refresh_index_ = 0;
    if (!texture_refresh_) return;
    auto &refresh = *texture_refresh_;
    /* A preserved job may belong to a later atlas. Revisit earlier atlases
       afterwards because they may need a new size as well. */
    refresh.rescan = true;
    if (refresh.finishing || refresh.cancelled) return;
    const auto &next = *refresh.replacement;
    const auto bound = texture_refresh_bound(next, refresh.texture_limit,
        render_quality_percent_);
    const TextureResolution previous_bound{next.max_width, next.max_height, true};
    /* Compare actual aspect-fitted pixels, not window pixels or rounded
       bounds. Different notifications can request exactly the same atlas. */
    if (fitted_size(next, bound) == fitted_size(next, previous_bound)) return;
    cancel_texture_refresh_async();
}

void NativeModel::cancel_texture_refresh_async() {
    if (!texture_refresh_ || texture_refresh_->finishing || texture_refresh_->cancelled) return;
    texture_refresh_->cancelled = true;
    texture_refresh_->cancelled_ns = SDL_GetTicksNS();
    texture_refresh_index_ = 0;
    bongo_cat_image_texture_job_cancel(texture_refresh_->job);
}

bool NativeModel::texture_refresh_busy() const {
    return texture_refresh_ && (texture_refresh_->finishing || texture_refresh_->cancelled ||
        (!texture_refresh_->paused_ns &&
            SDL_GetTicksNS() - texture_resize_ns_ >= kEnlargeSettleNs));
}

bool NativeModel::texture_refresh_pending(bool active) const {
    return texture_refresh_ || (active && texture_refresh_pending_) ||
        texture_refresh_memory_.due(SDL_GetTicksNS());
}

bool NativeModel::texture_refresh_due(bool active, bool allow_start) const {
    const uint64_t now = SDL_GetTicksNS();
    if (texture_refresh_) {
        const auto &refresh = *texture_refresh_;
        if (refresh.finishing || refresh.cancelled) return true;
        if (active)
            return refresh.paused_ns || (now - texture_resize_ns_ >= kEnlargeSettleNs &&
                bongo_cat_image_texture_job_needs_poll(refresh.job));
        /* Observe the pause transition once, then service it again only when
           its bounded retention period expires. Modal loops must also ask
           this query: busy() intentionally excludes paused work. */
        return !refresh.paused_ns || now - refresh.paused_ns >= kPauseReleaseNs;
    }
    return texture_refresh_memory_.due(now) ||
        (allow_start && active && texture_refresh_pending_ &&
            dynamic_texture_resolution_ &&
            now - texture_resize_ns_ >= kEnlargeSettleNs &&
            !texture_refresh_memory_.cancellation_cooldown(now));
}

bool NativeModel::refresh_texture_resolution(bool active, bool allow_start) {
    if (!texture_refresh_due(active, allow_start)) return false;
    const uint64_t now = SDL_GetTicksNS();
    if (!texture_refresh_ && texture_refresh_memory_.due(now))
        texture_refresh_memory_.poll(now, texture_storage_mib());
    if (!texture_refresh_ && texture_refresh_memory_.cancellation_cooldown(now))
        return false;
    bool changed = false;
    if (texture_refresh_) {
        auto &refresh = *texture_refresh_;
        if (!active) {
            /* Brief gestures/hides pause the same bounded job. A long pause
               releases its unfinished atlas; obsolete jobs drain immediately,
               even while hidden, without uploading another batch. */
            if (!refresh.paused_ns) refresh.paused_ns = now;
            if (!refresh.finishing && !refresh.cancelled &&
                now - refresh.paused_ns >= kPauseReleaseNs) {
                cancel_texture_refresh_async();
            }
        } else if (refresh.paused_ns) {
            refresh.paused_ns = 0;
            texture_resize_ns_ = now;
        }
        BongoCatError error{};
        if (!refresh.finishing) {
            if (!refresh.cancelled && (!active ||
                now - texture_resize_ns_ < kEnlargeSettleNs)) return false;
            auto &next = *refresh.replacement;
            int result = bongo_cat_image_texture_job_poll(refresh.job,
                &next.id, &next.width, &next.height, &next.alpha, &error);
            if (!result) return false;
            if (result > 0 && !refresh.cancelled && refresh.index < textures_.size()) {
                auto previous = std::move(textures_[refresh.index]);
                SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
                    "[texture-refresh] result=ready texture=%u from=%dx%d to=%dx%d elapsed_ms=%.1f",
                    (unsigned)refresh.index, previous ? previous->width : 0,
                    previous ? previous->height : 0, next.width, next.height,
                    (double)(now - refresh.started) / 1000000.0);
                textures_[refresh.index] = std::move(refresh.replacement);
                /* Repeated slots of the same file already share the old
                   allocation. Keep them shared after resizing instead of
                   decoding/uploading one identical replacement per slot.
                   Different files may have diverged since the initial hash. */
                const char *file = setting_->GetTextureFileName((int)refresh.index);
                for (size_t i = 0; file && i < textures_.size(); ++i) {
                    if (textures_[i] != previous) continue;
                    const char *other = setting_->GetTextureFileName((int)i);
                    if (other && std::strcmp(file, other) == 0)
                        textures_[i] = textures_[refresh.index];
                }
                bind_textures();
                triangle_alpha_.clear();
                /* A higher-resolution atlas can reveal thin details that the
                   previous alpha mask could not represent. */
                try {
                    prepare_frame_bounds();
                } catch (const std::bad_alloc &) {
                    /* Raw vertices remain a safe, allocation-free fallback. */
                    frame_drawables_.clear();
                }
                visual_state_cached_ = false;
                if (previous && previous.use_count() == 1)
                    refresh.delete_requested_mib = bongo_cat_model_texture_mib(
                        previous->width, previous->height, true);
                previous.reset();
                refresh.committed = changed = true;
            } else if (!refresh.cancelled) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                    "[texture-refresh] Keeping current texture after refresh failed: %s",
                    error.message[0] ? error.message : "decode or upload unavailable");
            }
            refresh.finishing = true;
            refresh.cleanup_ns = SDL_GetTicksNS();
        }
        /* Release job storage and fence the preceding work/deletions. Do not
           start another atlas until this retires. Supported drivers poll with
           zero timeout; compatibility/error recovery stays in the sync module. */
        const int cleaned = bongo_cat_image_texture_job_cleanup_poll(refresh.job, &error);
        if (!cleaned) return changed;
        if (cleaned < 0) SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
            "[texture-refresh] Cleanup completion failed: %s", error.message);
        if (!refresh.cancelled)
            texture_refresh_index_ = refresh.rescan ? 0 : refresh.index + 1;
        texture_refresh_memory_.finish(refresh.committed, refresh.cancelled,
            texture_storage_mib(), refresh.delete_requested_mib,
            (double)(SDL_GetTicksNS() - refresh.cleanup_ns) / 1000000.0,
            refresh.cancelled_ns ?
                (double)(SDL_GetTicksNS() - refresh.cancelled_ns) / 1000000.0 : 0.0,
            cleaned > 0);
        delete texture_refresh_;
        texture_refresh_ = nullptr;
        return changed;
    }
    /* Always drain an existing job above, even if future work is disabled.
       Native modal callbacks may service resources but never start a job. */
    if (!allow_start || !active || !texture_refresh_pending_ ||
        !dynamic_texture_resolution_ || textures_.empty() || !setting_ ||
        now - texture_resize_ns_ < kEnlargeSettleNs) return false;
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (limit < 1) { texture_refresh_pending_ = false; return false; }
    for (; texture_refresh_index_ < textures_.size(); ++texture_refresh_index_) {
        const auto &current = textures_[texture_refresh_index_];
        if (!current) continue;
        auto bound = texture_refresh_bound(*current, limit, render_quality_percent_);
        const auto fitted = fitted_size(*current, bound);
        if (fitted == std::make_pair(current->width, current->height))
            continue;
        // Restore enlarged detail promptly; coalesce brief shrink bursts before
        // preparing a smaller atlas so oversized storage can be released sooner.
        const bool enlarging = fitted.first > current->width ||
            fitted.second > current->height;
        if (!enlarging && now - texture_resize_ns_ < kShrinkSettleNs)
            return false;
        std::unique_ptr<TextureRefresh> refresh(new(std::nothrow) TextureRefresh);
        BongoCatError error{};
        std::string texture_path;
        try {
            if (refresh) refresh->replacement = std::make_shared<ModelTexture>();
            texture_path = path(setting_->GetTextureFileName((int)texture_refresh_index_));
        } catch (const std::bad_alloc &) { refresh.reset(); }
        if (!refresh) {
            texture_refresh_pending_ = false;
            return false;
        }
        auto &next = *refresh->replacement;
        next.context = current->context;
        next.direct = current->direct;
        next.dynamic_resolution = dynamic_texture_resolution_;
        next.max_width = bound.max_width; next.max_height = bound.max_height;
        next.source_width = current->source_width; next.source_height = current->source_height;
        refresh->index = texture_refresh_index_;
        refresh->texture_limit = limit;
        refresh->started = SDL_GetTicksNS();
        refresh->job = bongo_cat_image_texture_job_start(
            texture_path.c_str(),
            bound.max_width, bound.max_height, &error);
        if (!refresh->job) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "[texture-refresh] Cannot start background resize: %s", error.message);
            continue;
        }
        texture_refresh_memory_.begin(texture_storage_mib());
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[texture-refresh] result=started texture=%u display=%dx%d from=%dx%d bound=%dx%d "
            "rgba8_est_mib=%.1f",
            (unsigned)refresh->index, viewport_width_, viewport_height_,
            current->width, current->height, bound.max_width, bound.max_height,
            bongo_cat_model_texture_mib(bound.max_width, bound.max_height, true));
        texture_refresh_ = refresh.release();
        return false;
    }
    texture_refresh_pending_ = false;
    return false;
}

} // namespace bongo_cat
