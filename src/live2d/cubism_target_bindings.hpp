#pragma once

#include <Rendering/OpenGL/CubismRenderTarget_OpenGLES2.hpp>

namespace bongo_cat {

// SDK target creation changes bindings and treats a null upload pointer as
// an offset if a pixel-unpack buffer is bound. Isolate allocation from callers.
class TargetBindings final {
public:
    TargetBindings() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    ~TargetBindings() {
        glBindTexture(GL_TEXTURE_2D, (GLuint)texture_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack_);
    }
    TargetBindings(const TargetBindings &) = delete;
    TargetBindings &operator=(const TargetBindings &) = delete;

    void replace(const Csm::Rendering::CubismRenderTarget_OpenGLES2 &old,
        const Csm::Rendering::CubismRenderTarget_OpenGLES2 &replacement) {
        if (texture_ && (GLuint)texture_ == old.GetColorBuffer())
            texture_ = (GLint)replacement.GetColorBuffer();
        if (draw_ && (GLuint)draw_ == old.GetRenderTexture())
            draw_ = (GLint)replacement.GetRenderTexture();
        if (read_ && (GLuint)read_ == old.GetRenderTexture())
            read_ = (GLint)replacement.GetRenderTexture();
    }

private:
    GLint texture_ = 0, draw_ = 0, read_ = 0, unpack_ = 0;
};

} // namespace bongo_cat
