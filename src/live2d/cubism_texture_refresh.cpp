#include "cubism_model.hpp"
#include "cubism_model_texture.hpp"
#include "cubism_texture_resolution.hpp"
#include "bongo_cat/image_texture_job.h"
#include "bongo_cat/log.h"
#include "bongo_cat/model_memory.h"

#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstdint>
#include <new>
#include <utility>

namespace bongo_cat {

namespace {
constexpr uint64_t kResizeSettleNs = 600000000ull;
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
    int width = texture.source_width, height = texture.source_height;
    if (width < 1 || height < 1 || bound.max_width < 1 || bound.max_height < 1)
        return {texture.width, texture.height};
    if (width > bound.max_width || height > bound.max_height) {
        if ((int64_t)bound.max_width * height <= (int64_t)bound.max_height * width) {
            height = std::max(1, (int)((int64_t)height * bound.max_width / width));
            width = bound.max_width;
        } else {
            width = std::max(1, (int)((int64_t)width * bound.max_height / height));
            height = bound.max_height;
        }
    }
    return {width, height};
}

TextureResolution NativeModel::texture_refresh_bound(const ModelTexture &texture,
    int limit) const {
    int reference_width = 0, reference_height = 0;
    canvas_size(&reference_width, &reference_height);
    if (render_options_.mver_projection) {
        if (render_options_.reference_width > 0) reference_width = render_options_.reference_width;
        if (render_options_.reference_height > 0) reference_height = render_options_.reference_height;
    }
    if (reference_width <= 0) reference_width = width_;
    if (reference_height <= 0) reference_height = height_;
    return texture_resolution_for(true, viewport_width_, viewport_height_,
        reference_width, reference_height, texture.source_width, texture.source_height, limit);
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
    const auto bound = texture_refresh_bound(next, refresh.texture_limit);
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
            SDL_GetTicksNS() - texture_resize_ns_ >= kResizeSettleNs));
}

bool NativeModel::texture_refresh_pending(bool active) const {
    return texture_refresh_ || (active && texture_refresh_pending_) ||
        texture_refresh_memory_.due(SDL_GetTicksNS());
}

bool NativeModel::refresh_texture_resolution(bool active) {
    const uint64_t now = SDL_GetTicksNS();
    if (!texture_refresh_ && texture_refresh_memory_.due(now))
        texture_refresh_memory_.poll(now, texture_storage_mib());
    if (!texture_refresh_ && texture_refresh_memory_.cancellation_cooldown(now))
        return false;
    if (!texture_refresh_pending_ || !dynamic_texture_resolution_ ||
        textures_.empty() || !setting_) return false;
    bool changed = false;
    if (texture_refresh_) {
        auto &refresh = *texture_refresh_;
        if (!active) {
            /* Brief gestures/hides pause the same bounded job. A long pause
               releases its unfinished atlas; obsolete jobs drain immediately,
               even while hidden, without uploading another batch. */
            texture_resize_ns_ = now;
            if (!refresh.paused_ns) refresh.paused_ns = now;
            if (!refresh.finishing && !refresh.cancelled &&
                now - refresh.paused_ns >= kPauseReleaseNs) {
                cancel_texture_refresh_async();
            }
        } else refresh.paused_ns = 0;
        BongoCatError error{};
        if (!refresh.finishing) {
            if (!refresh.cancelled && (!active ||
                now - texture_resize_ns_ < kResizeSettleNs)) return false;
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
                bind_textures();
                triangle_alpha_.clear();
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
    if (!active || now - texture_resize_ns_ < kResizeSettleNs) return false;
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (limit < 1) { texture_refresh_pending_ = false; return false; }
    for (; texture_refresh_index_ < textures_.size(); ++texture_refresh_index_) {
        const auto &current = textures_[texture_refresh_index_];
        if (!current) continue;
        auto bound = texture_refresh_bound(*current, limit);
        if (fitted_size(*current, bound) == std::make_pair(current->width, current->height))
            continue;
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
        next.dynamic_resolution = true;
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
