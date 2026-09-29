#include "cubism_target_bindings.hpp"
#include "bongo_cat/model.h"
#include "test.h"

#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <SDL3/SDL.h>

int bongo_cat_test_failures;
using Target = Csm::Rendering::CubismRenderTarget_OpenGLES2;

static void check_bindings(GLuint texture, GLuint draw, GLuint read, GLuint unpack) {
    GLint actual = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &actual); CHECK(actual == GL_TEXTURE3);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); CHECK((GLuint)actual == texture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actual); CHECK((GLuint)actual == draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &actual); CHECK((GLuint)actual == read);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual); CHECK((GLuint)actual == unpack);
}

static void target_bindings() {
    Target read, draw;
    CHECK(read.CreateRenderTarget(8, 8));
    CHECK(draw.CreateRenderTarget(8, 8));
    GLuint unpack = 0;
    glGenBuffers(1, &unpack);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, draw.GetColorBuffer());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.GetRenderTexture());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read.GetRenderTexture());
    // A live unpack PBO must not turn SDK null allocations into PBO reads.
    {
        bongo_cat::TargetBindings bindings;
        Target temporary;
        CHECK(temporary.CreateRenderTarget(1408, 768));
        glBindFramebuffer(GL_FRAMEBUFFER, temporary.GetRenderTexture());
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        CHECK(glGetError() == GL_NO_ERROR);
        temporary.DestroyRenderTarget();
    }
    check_bindings(draw.GetColorBuffer(), draw.GetRenderTexture(), read.GetRenderTexture(), unpack);
    // Stack unwinding must also return ownership of GL state to the caller.
    try {
        bongo_cat::TargetBindings bindings;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        throw 1;
    } catch (int) {}
    check_bindings(draw.GetColorBuffer(), draw.GetRenderTexture(), read.GetRenderTexture(), unpack);
    // Replacing a bound target must restore its replacement, never a deleted ID.
    {
        bongo_cat::TargetBindings bindings;
        Target replacement;
        CHECK(replacement.CreateRenderTarget(64, 32));
        bindings.replace(draw, replacement);
        draw.DestroyRenderTarget();
        draw = replacement;
    }
    check_bindings(draw.GetColorBuffer(), draw.GetRenderTexture(), read.GetRenderTexture(), unpack);
    CHECK(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    CHECK(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteBuffers(1, &unpack);
    read.DestroyRenderTarget();
    draw.DestroyRenderTarget();
    glActiveTexture(GL_TEXTURE0);
    CHECK(glGetError() == GL_NO_ERROR);
}

static void offscreen_pool() {
    auto *manager = Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    manager->BeginFrameProcess();
    Target *targets[3];
    GLuint textures[3], framebuffers[3];
    for (int i = 0; i < 3; ++i) {
        targets[i] = manager->GetOffscreenRenderTarget(32, 16);
        textures[i] = targets[i]->GetColorBuffer();
        framebuffers[i] = targets[i]->GetRenderTexture();
    }
    for (auto *target : targets) manager->StopUsingRenderTexture(target);
    manager->EndFrameProcess();
    CHECK(manager->GetOffscreenRenderTargetListSize() == 3);
    // Switching to a simpler model keeps its one reusable target.
    manager->BeginFrameProcess();
    Target *active = manager->GetOffscreenRenderTarget(32, 16);
    CHECK(active == targets[0]);
    manager->StopUsingRenderTexture(active);
    manager->EndFrameProcess();
    manager->ReleaseStaleRenderTextures();
    CHECK(manager->GetOffscreenRenderTargetListSize() == 1);
    CHECK(glIsTexture(textures[0]) && glIsFramebuffer(framebuffers[0]));
    for (int i = 1; i < 3; ++i)
        CHECK(!glIsTexture(textures[i]) && !glIsFramebuffer(framebuffers[i]));
    // A model without modern offscreens releases the last unused target.
    manager->BeginFrameProcess();
    manager->EndFrameProcess();
    manager->ReleaseStaleRenderTextures();
    CHECK(manager->GetOffscreenRenderTargetListSize() == 0);
    CHECK(!glIsTexture(textures[0]) && !glIsFramebuffer(framebuffers[0]));
    CHECK(glGetError() == GL_NO_ERROR);
}

int main() {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Cubism render resources", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window != nullptr);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    CHECK(context != nullptr);
    if (context) {
        BongoCatError error{};
        BongoCatLive2D *runtime = bongo_cat_live2d_create("", &error);
        CHECK(runtime != nullptr);
        if (runtime) {
            target_bindings();
            offscreen_pool();
            bongo_cat_live2d_destroy(runtime);
        }
        SDL_GL_DestroyContext(context);
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
