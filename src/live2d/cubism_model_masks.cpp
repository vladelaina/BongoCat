#include "cubism_model.hpp"
#include "cubism_target_bindings.hpp"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <new>
#include <utility>

namespace bongo_cat {

static size_t combination_count(Csm::csmInt32 count,
    const Csm::csmInt32 *mask_counts, const Csm::csmInt32 *const *masks) {
    std::set<std::vector<Csm::csmInt32>> combinations;
    for (Csm::csmInt32 i = 0; i < count; ++i) {
        if (mask_counts[i] <= 0) continue;
        std::vector<Csm::csmInt32> ids(masks[i], masks[i] + mask_counts[i]);
        std::sort(ids.begin(), ids.end());
        combinations.insert(std::move(ids));
    }
    // Include hidden expressions so they cannot exhaust mask capacity later.
    return combinations.size();
}

int NativeModel::prepare_mask_layout() {
    size_t drawables = combination_count(_model->GetDrawableCount(),
        _model->GetDrawableMaskCounts(), _model->GetDrawableMasks());
    size_t offscreens = combination_count(_model->GetOffscreenCount(),
        _model->GetOffscreenMaskCounts(), _model->GetOffscreenMasks());
    int count = std::max(mask_buffer_count(drawables), mask_buffer_count(offscreens));
    drawable_masks_.layout = mask_layout(drawables, count);
    offscreen_masks_.layout = mask_layout(offscreens, count);
    return count;
}

bool NativeModel::update_mask_buffers() {
    if (width_ == mask_last_width_ && height_ == mask_last_height_)
        return !mask_update_failed_;
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer || !_model || mask_texture_limit_ <= 0) return false;
    mask_last_width_ = width_;
    mask_last_height_ = height_;
    mask_update_failed_ = false;
    auto update = [&](MaskBuffers &state, bool offscreen) {
        MaskSize size = mask_target_size(state.layout, width_, height_,
            mask_texture_limit_, state.size);
        if (!size.width || size == state.size) return true;
        if ((int64_t)width_ * 11 * state.layout.width > (int64_t)mask_texture_limit_ * 10 ||
            (int64_t)height_ * 11 * state.layout.height > (int64_t)mask_texture_limit_ * 10)
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Live2D clipping precision limited by GPU maximum %d at %dx%d",
                mask_texture_limit_, width_, height_);
        const int count = offscreen ? renderer->GetOffscreenRenderTextureCount() :
            renderer->GetDrawableRenderTextureCount();
        using Target = Csm::Rendering::CubismRenderTarget_OpenGLES2;
        TargetBindings bindings;
        struct PendingTargets {
            ~PendingTargets() { for (auto &target : items) target.DestroyRenderTarget(); }
            std::vector<Target> items;
        } pending;
        try {
            pending.items.resize((size_t)count);
        } catch (const std::bad_alloc &) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                "Cannot allocate Live2D mask handles; retaining previous masks");
            return false;
        }
        bool ready = bongo_cat_gl_clear_errors();
        for (auto &target : pending.items) {
            if (!ready) break;
            ready = target.CreateRenderTarget((Csm::csmUint32)size.width,
                (Csm::csmUint32)size.height);
            glBindTexture(GL_TEXTURE_2D, target.GetColorBuffer());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, target.GetRenderTexture());
            ready = ready && target.GetColorBuffer() && target.GetRenderTexture() &&
                glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            GLenum error = glGetError();
            ready = ready && error == GL_NO_ERROR;
        }
        if (ready) {
            if (offscreen) renderer->SetOffscreenClippingMaskBufferSize(
                (float)size.width, (float)size.height);
            else renderer->SetDrawableClippingMaskBufferSize(
                (float)size.width, (float)size.height);
            for (int i = 0; i < count; ++i) {
                Target *target = offscreen ? renderer->GetOffscreenMaskBuffer(i) :
                    renderer->GetDrawableMaskBuffer(i);
                bindings.replace(*target, pending.items[(size_t)i]);
                target->DestroyRenderTarget();
                *target = pending.items[(size_t)i];
                pending.items[(size_t)i] = Target(); // Transfer the SDK handles.
            }
            state.size = size;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                "Cannot allocate Live2D %s masks at %dx%d; retaining previous masks",
                offscreen ? "offscreen" : "drawable", size.width, size.height);
        }
        return ready;
    };
    bool drawables = update(drawable_masks_, false);
    bool offscreens = update(offscreen_masks_, true);
    mask_update_failed_ = !drawables || !offscreens;
    return !mask_update_failed_;
}

} // namespace bongo_cat
