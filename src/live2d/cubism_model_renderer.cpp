#include "cubism_model.hpp"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace bongo_cat {

static size_t mask_combination_count(Csm::csmInt32 object_count,
    const Csm::csmInt32 *mask_counts, const Csm::csmInt32 *const *masks) {
    std::set<std::vector<Csm::csmInt32>> combinations;
    for (Csm::csmInt32 i = 0; i < object_count; ++i) {
        if (mask_counts[i] <= 0) continue;
        std::vector<Csm::csmInt32> ids(masks[i], masks[i] + mask_counts[i]);
        // Cubism shares a clipping context for the same IDs in any order.
        std::sort(ids.begin(), ids.end());
        combinations.insert(std::move(ids));
    }
    // Include hidden objects so expressions cannot exceed the allocated capacity.
    return combinations.size();
}

static Csm::csmInt32 mask_buffer_count(size_t count) {
    if (count <= (size_t)Csm::Rendering::ClippingMaskMaxCountOnDefault) return 1;
    const size_t capacity = Csm::Rendering::ClippingMaskMaxCountOnMultiRenderTexture;
    return (Csm::csmInt32)(1 + (count - 1) / capacity);
}

void NativeModel::bind_textures() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;
    for (size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i])
            renderer->BindTexture((Csm::csmInt32)i, textures_[i]);
    renderer->IsPremultipliedAlpha(true);
}

void NativeModel::release_textures() {
    if (!textures_.empty())
        glDeleteTextures((GLsizei)textures_.size(), textures_.data());
    textures_.clear();
    texture_alpha_.clear();
    triangle_alpha_.clear();
}

void NativeModel::release_renderer() {
    DeleteRenderer();
#ifdef CSM_TARGET_MAC_GL
    core_buffers_.release();
#endif
    renderer_width_ = 0;
    renderer_height_ = 0;
    mask_texture_limit_ = 0;
    mask_buffer_size_ = 0;
    mask_layout_divisions_ = 1;
    mask_last_width_ = mask_last_height_ = 0;
}

void NativeModel::release_render_resources() {
    release_textures();
    release_renderer();
}

void NativeModel::update_mask_buffers() {
    if (width_ == mask_last_width_ && height_ == mask_last_height_) return;
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer || !_model || mask_texture_limit_ <= 0) return;
    mask_last_width_ = width_;
    mask_last_height_ = height_;
    const bool drawable_masks = _model->IsUsingMasking();
    const bool offscreen_masks = _model->IsUsingMaskingForOffscreen();
    if (!drawable_masks && !offscreen_masks) return;

    // Account for the actual atlas subdivision, including hidden masks.
    const int extent = std::min(std::max(width_, height_), mask_texture_limit_);
    const int requested = std::max(512, mask_layout_divisions_ * extent);
    const int size = std::min(mask_texture_limit_, ((requested + 511) / 512) * 512);
    if (size == mask_buffer_size_) return;
    // Grow immediately, but avoid grow/shrink churn near a size boundary.
    if (size < mask_buffer_size_ && requested > mask_buffer_size_ - 768) return;

    GLint previous_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    auto prepare = [size, &previous_texture](
        Csm::Rendering::CubismRenderTarget_OpenGLES2 *buffer) {
        const bool was_bound = previous_texture != 0 &&
            (GLuint)previous_texture == buffer->GetColorBuffer();
        // Allocate now so the SDK cannot replace the filtering on first draw.
        buffer->CreateRenderTarget((Csm::csmUint32)size, (Csm::csmUint32)size);
        if (was_bound) previous_texture = (GLint)buffer->GetColorBuffer();
        glBindTexture(GL_TEXTURE_2D, buffer->GetColorBuffer());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    if (drawable_masks) {
        renderer->SetDrawableClippingMaskBufferSize((float)size, (float)size);
        for (int i = 0; i < renderer->GetDrawableRenderTextureCount(); ++i)
            prepare(renderer->GetDrawableMaskBuffer(i));
    }
    if (offscreen_masks) {
        renderer->SetOffscreenClippingMaskBufferSize((float)size, (float)size);
        for (int i = 0; i < renderer->GetOffscreenRenderTextureCount(); ++i)
            prepare(renderer->GetOffscreenMaskBuffer(i));
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
    mask_buffer_size_ = size;
}

bool NativeModel::create_renderer(BongoCatError *error) {
    if (!SDL_GL_GetCurrentContext()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot create the Live2D renderer without an OpenGL context");
        return false;
    }
    if (!bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
            "Cannot clear the OpenGL error state before creating the Live2D renderer");
        return false;
    }
#ifdef CSM_TARGET_MAC_GL
    CoreProfileBinding binding(core_buffers_);
#endif
    const size_t drawable_combinations = mask_combination_count(
        _model->GetDrawableCount(), _model->GetDrawableMaskCounts(),
        _model->GetDrawableMasks());
    const size_t offscreen_combinations = mask_combination_count(
        _model->GetOffscreenCount(), _model->GetOffscreenMaskCounts(),
        _model->GetOffscreenMasks());
    const Csm::csmInt32 buffer_count = std::max(
        mask_buffer_count(drawable_combinations), mask_buffer_count(offscreen_combinations));
    CreateRenderer((Csm::csmUint32)width_, (Csm::csmUint32)height_,
        buffer_count);
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (renderer) {
        GLint texture_limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
        mask_texture_limit_ = std::min(4096, (int)texture_limit);
        const size_t per_buffer = (std::max(drawable_combinations,
            offscreen_combinations) + (size_t)buffer_count - 1) / (size_t)buffer_count;
        mask_layout_divisions_ = per_buffer <= 4 ? 1 : (per_buffer <= 16 ? 2 : 3);
        const uint64_t total_buffers = (uint64_t)buffer_count *
            ((drawable_combinations > 0 ? 1u : 0u) +
             (offscreen_combinations > 0 ? 1u : 0u));
        // Target 32 MiB of RGBA8 masks per model, retaining a 256px floor.
        // This excludes model atlases, blend targets, and driver overhead.
        constexpr uint64_t mask_budget = 32ull * 1024 * 1024;
        while (mask_texture_limit_ > 256 && total_buffers * 4 *
            (uint64_t)mask_texture_limit_ * (uint64_t)mask_texture_limit_ > mask_budget)
            mask_texture_limit_ = std::max(256, mask_texture_limit_ - 256);
        bind_textures();
        update_mask_buffers();
    }
    GLenum renderer_error = glGetError();
    if (renderer && renderer_error == GL_NO_ERROR) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "Cannot create the Live2D renderer (OpenGL 0x%x)",
        (unsigned)renderer_error);
    release_renderer();
    return false;
}

} // namespace bongo_cat
