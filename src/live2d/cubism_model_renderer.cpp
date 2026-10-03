#include "cubism_model.hpp"
#include "bongo_cat/model_memory.h"
#include "bongo_cat/resource_trace.h"
#include "cubism_target_bindings.hpp"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_video.h>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>

namespace bongo_cat {

namespace {
class DrawFrame final {
public:
    explicit DrawFrame(Csm::Rendering::CubismOffscreenManager_OpenGLES2 *manager)
        : manager_(manager), srgb_(glIsEnabled(GL_FRAMEBUFFER_SRGB)) {
        manager_->BeginFrameProcess();
        // Cubism's RGBA8 blend equations operate on authored color values.
        if (srgb_) glDisable(GL_FRAMEBUFFER_SRGB);
        // Modern blend targets may also be allocated lazily inside DrawModel.
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_);
        if (unpack_) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    ~DrawFrame() {
        if (unpack_) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack_);
        if (srgb_) glEnable(GL_FRAMEBUFFER_SRGB);
        manager_->EndFrameProcess();
    }
    DrawFrame(const DrawFrame &) = delete;
    DrawFrame &operator=(const DrawFrame &) = delete;
private:
    Csm::Rendering::CubismOffscreenManager_OpenGLES2 *manager_;
    GLboolean srgb_;
    GLint unpack_ = 0;
};
} // namespace

void NativeModel::draw() {
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (!_model || !renderer || width_ <= 0 || height_ <= 0) return;
#ifdef CSM_TARGET_MAC_GL
    CoreProfileBinding binding(core_buffers_);
#endif
    const bool first_frame = trim_offscreen_pool_;
    const bool resources_changed = first_frame ||
        width_ != mask_last_width_ || height_ != mask_last_height_;
    update_mask_buffers();
    auto *manager = Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    Csm::CubismMatrix44 projection;
    build_projection(projection, viewport_width_, viewport_height_);
    apply_viewport_projection(projection);
    visual_state_ = BongoCatLive2DVisualState{};
    visual_state_.fit_scale = 1.0f;
    visual_state_.mver_projection = render_options_.mver_projection;
    visual_projection_.SetMatrix(projection.GetArray());
    visual_state_cached_ = false;
    visual_state_ready_ = true;
    renderer->SetMvpMatrix(&projection);
    renderer->SetModelColor(1.0f, 1.0f, 1.0f, _model->GetModelOpacity());
    {
        DrawFrame frame(manager);
        renderer->DrawModel();
    }
    if (trim_offscreen_pool_) {
        // The first completed frame establishes how many targets this model
        // needs. Drop unused targets retained by a previously loaded model.
        manager->ReleaseStaleRenderTextures();
        trim_offscreen_pool_ = false;
    }
    if (resources_changed) {
        // Cubism creates each clipping manager only when the model uses that
        // mask type. Its count getters dereference those optional managers.
        const int drawable_count = _model->IsUsingMasking()
            ? renderer->GetDrawableRenderTextureCount() : 0;
        const int offscreen_count = _model->IsUsingMaskingForOffscreen()
            ? renderer->GetOffscreenRenderTextureCount() : 0;
        double mask_mib = drawable_count * bongo_cat_model_texture_mib(
            drawable_masks_.size.width, drawable_masks_.size.height, false) +
            offscreen_count * bongo_cat_model_texture_mib(
                offscreen_masks_.size.width, offscreen_masks_.size.height, false);
        bongo_cat_resource_trace_render(mask_mib,
            (unsigned)manager->GetOffscreenRenderTargetListSize(), width_, height_);
        if (first_frame) bongo_cat_model_memory_log("renderer-first-frame",
            "window=%dx%d viewport=%dx%d drawable_masks=%dx%dx%d "
            "offscreen_masks=%dx%dx%d mask_rgba8_est_mib=%.1f pool_targets=%u",
            width_, height_, viewport_width_, viewport_height_,
            drawable_masks_.size.width, drawable_masks_.size.height, drawable_count,
            offscreen_masks_.size.width, offscreen_masks_.size.height, offscreen_count,
            mask_mib, (unsigned)manager->GetOffscreenRenderTargetListSize());
    }
}

void NativeModel::release_renderer() {
    DeleteRenderer();
#ifdef CSM_TARGET_MAC_GL
    core_buffers_.release();
#endif
    renderer_width_ = 0;
    renderer_height_ = 0;
    mask_texture_limit_ = 0;
    drawable_masks_ = {};
    offscreen_masks_ = {};
    mask_update_failed_ = false;
    mask_last_width_ = mask_last_height_ = 0;
    visual_state_ready_ = false;
    trim_offscreen_pool_ = true;
}

void NativeModel::release_render_resources() {
    release_textures();
    release_renderer();
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
    const int buffer_count = prepare_mask_layout();
    TargetBindings bindings;
    CreateRenderer((Csm::csmUint32)width_, (Csm::csmUint32)height_,
        buffer_count);
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    GLenum renderer_error = glGetError();
    if (renderer && renderer_error == GL_NO_ERROR) {
        GLint texture_limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
        renderer_error = glGetError();
        if (renderer_error != GL_NO_ERROR || texture_limit <= 0) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                "Cannot query the Live2D clipping texture size limit");
            release_renderer();
            return false;
        }
        mask_texture_limit_ = (int)texture_limit;
        bind_textures();
        if (!update_mask_buffers()) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
                "Cannot allocate full-resolution Live2D clipping masks");
            release_renderer();
            return false;
        }
    }
    if (renderer_error == GL_NO_ERROR) renderer_error = glGetError();
    if (renderer && renderer_error == GL_NO_ERROR) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM,
        "Cannot create the Live2D renderer (OpenGL 0x%x)",
        (unsigned)renderer_error);
    release_renderer();
    return false;
}

} // namespace bongo_cat
