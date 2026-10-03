#include "cubism_runtime.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Resources {
    std::vector<GLuint> textures, framebuffers;

    explicit Resources(BongoCatLive2D *runtime) {
        auto *renderer = runtime->model->GetRenderer<
            Csm::Rendering::CubismRenderer_OpenGLES2>();
        const auto &bindings = renderer->GetBindedTextures();
        for (auto it = bindings.Begin(); it != bindings.End(); ++it)
            textures.push_back(it->Second);
        if (runtime->model->GetModel()->IsUsingMasking()) {
            for (int i = 0; i < renderer->GetDrawableRenderTextureCount(); ++i) {
                auto *mask = renderer->GetDrawableMaskBuffer(i);
                textures.push_back(mask->GetColorBuffer());
                framebuffers.push_back(mask->GetRenderTexture());
            }
        }
        require(!textures.empty(), "Fixture has no texture resources");
    }

    void check(bool alive) const {
        for (GLuint texture : textures)
            require((glIsTexture(texture) != GL_FALSE) == alive,
                alive ? "Active texture was released" : "Replaced texture is still retained");
        for (GLuint framebuffer : framebuffers)
            require((glIsFramebuffer(framebuffer) != GL_FALSE) == alive,
                alive ? "Active mask was released" : "Replaced mask is still retained");
        require(glGetError() == GL_NO_ERROR, "Resource lifetime GL error");
    }

    void check_replaced(const Resources &current) const {
        for (GLuint texture : textures) {
            bool shared = std::find(current.textures.begin(), current.textures.end(),
                texture) != current.textures.end();
            require((glIsTexture(texture) != GL_FALSE) == shared,
                shared ? "Shared active texture was released" : "Old exclusive texture is still retained");
        }
        for (GLuint framebuffer : framebuffers)
            require(!glIsFramebuffer(framebuffer), "Old mask framebuffer is still retained");
        current.check(true);
    }
};

struct Progress {
    BongoCatLive2D *runtime;
    bongo_cat::NativeModel *previous;
    bool completed = false;
    bool preserved = true;
};

void loading(void *userdata, float fraction) {
    auto &progress = *static_cast<Progress *>(userdata);
    progress.preserved = progress.preserved && progress.runtime->model == progress.previous;
    if (fraction == 1.0f) progress.completed = true;
}

void replace(BongoCatLive2D *runtime, const char *mode = "standard") {
    Progress progress{runtime, runtime->model};
    BongoCatError error{};
    std::string directory = BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models/";
    directory += mode;
    BongoCatResult result = bongo_cat_live2d_load(runtime, directory.c_str(),
        "cat.model3.json", false, nullptr, loading, &progress, &error);
    require(result == BONGO_CAT_OK, error.message);
    require(progress.completed && progress.preserved,
        "Replacement changed the active model before loading completed");
    require(runtime->model != progress.previous, "Replacement model was not installed");
}

struct Target {
    Csm::Rendering::CubismRenderTarget_OpenGLES2 target;
    static constexpr int size = 128;
    Target() {
        require(target.CreateRenderTarget(size, size), "Cannot create capture target");
    }
    ~Target() { target.DestroyRenderTarget(); }
    void draw(BongoCatLive2D *runtime) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.GetRenderTexture());
        glViewport(0, 0, size, size);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        bongo_cat_live2d_draw(runtime);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        require(glGetError() == GL_NO_ERROR, "Model draw failed");
    }
    std::vector<unsigned char> read() const {
        std::vector<unsigned char> pixels((size_t)size * size * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, target.GetRenderTexture());
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        require(glGetError() == GL_NO_ERROR, "Capture read failed");
        return pixels;
    }
};

void model_lifetime() {
    BongoCatError error{};
    using Runtime = std::unique_ptr<BongoCatLive2D, decltype(&bongo_cat_live2d_destroy)>;
    Runtime runtime(bongo_cat_live2d_create(BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets",
        &error), bongo_cat_live2d_destroy);
    require(runtime != nullptr, error.message);
    bongo_cat_live2d_resize(runtime.get(), Target::size, Target::size);
    replace(runtime.get());

    // No draw/update/present follows these switches, matching a hidden or
    // minimized window. Replaced resources must not await future frames.
    for (int i = 0; i < 5; ++i) {
        Resources previous(runtime.get());
        previous.check(true);
        replace(runtime.get(), i % 2 == 0 ? "keyboard" : "standard");
        previous.check_replaced(Resources(runtime.get()));
    }

    replace(runtime.get());

    auto *active = runtime->model;
    Resources current(runtime.get());
    require(bongo_cat_live2d_load(runtime.get(),
        BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models/standard",
        "missing-lifetime-test.model3.json", false, nullptr, nullptr, nullptr,
        &error) != BONGO_CAT_OK, "Invalid replacement unexpectedly loaded");
    require(runtime->model == active, "Failed replacement discarded the active model");
    current.check(true);

    bongo_cat_live2d_prepare_viewer_audit(runtime.get());
    bongo_cat_live2d_update(runtime.get(), 1.0f / 60.0f);
    Target capture;
    capture.draw(runtime.get());
    const auto expected = capture.read();
    bool visible = false;
    for (size_t i = 3; i < expected.size(); i += 4) visible = visible || expected[i] != 0;
    require(visible, "Fixture capture is empty");

    // Submit another frame, then replace without waiting for presentation.
    // GL owns queued draws even after their texture names are deleted.
    capture.draw(runtime.get());
    replace(runtime.get());
    current.check_replaced(Resources(runtime.get()));
    require(capture.read() == expected, "Deleting the previous model changed its submitted frame");
    bongo_cat_live2d_prepare_viewer_audit(runtime.get());
    bongo_cat_live2d_update(runtime.get(), 1.0f / 60.0f);
    capture.draw(runtime.get());
    require(capture.read() == expected, "Replacement frame differs after resource cleanup");
    std::puts("Model lifetime: hidden switches release old textures/masks; failed loads and submitted frames preserved");
}
} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_Window *window = SDL_CreateWindow("Model lifetime", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = 1;
    try {
        require(context != nullptr, SDL_GetError());
        model_lifetime();
        result = 0;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "Model lifetime failed: %s\n", exception.what());
    }
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
