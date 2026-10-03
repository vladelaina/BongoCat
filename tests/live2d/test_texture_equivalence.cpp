#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include "bongo_cat/json.h"
#include "bongo_cat/model.h"
extern "C" {
#include "bongo_cat/sha256.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

// This compares two upload paths through the production renderer. It is not
// an editor reference: all model state and fixed simulation steps are identical.
namespace {
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void check_gl(const char *stage) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        char message[160];
        std::snprintf(message, sizeof(message), "%s: OpenGL error 0x%x",
            stage, (unsigned)error);
        throw std::runtime_error(message);
    }
}

struct Motion { std::string group; int index; };
struct Manifest {
    std::vector<Motion> motions;
    std::vector<std::string> parameters;
    int expressions = 0;
};

Manifest read_manifest(const std::string &directory, const char *setting) {
    using Document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    Document doc(bongo_cat_model_json_read((directory + "/" + setting).c_str(),
        nullptr), yyjson_doc_free);
    require(doc != nullptr, "Cannot read model settings");
    yyjson_val *files = yyjson_obj_get(yyjson_doc_get_root(doc.get()), "FileReferences");
    Manifest result;
    size_t i, count;
    yyjson_val *key, *value;
    yyjson_obj_foreach(yyjson_obj_get(files, "Motions"), i, count, key, value) {
        for (size_t motion = 0; motion < yyjson_arr_size(value); ++motion)
            result.motions.push_back({yyjson_get_str(key), (int)motion});
    }
    result.expressions = (int)yyjson_arr_size(yyjson_obj_get(files, "Expressions"));
    const char *display = yyjson_get_str(yyjson_obj_get(files, "DisplayInfo"));
    if (display && *display) {
        Document cdi(bongo_cat_model_json_read((directory + "/" + display).c_str(),
            nullptr), yyjson_doc_free);
        require(cdi != nullptr, "Cannot read parameter display information");
        yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(cdi.get()),
            "Parameters"), i, count, value) {
            const char *id = yyjson_get_str(yyjson_obj_get(value, "Id"));
            if (id && *id) result.parameters.emplace_back(id);
        }
    }
    // Also exercise common input parameters in exports without display info.
    if (result.parameters.empty()) {
        result.parameters = {"ParamAngleX", "ParamAngleY", "ParamAngleZ",
            "ParamEyeBallX", "ParamEyeBallY", "ParamMouseX", "ParamMouseY",
            "ParamMouseLeftDown", "ParamMouseRightDown", "CatParamLeftHandDown",
            "CatParamRightHandDown", "A", "A1", "Space"};
    }
    return result;
}

struct Session {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    Session() {
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
        window = SDL_CreateWindow("Texture equivalence", 32, 32,
            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        context = SDL_GL_CreateContext(window);
        if (!context) {
            SDL_DestroyWindow(window);
            throw std::runtime_error(SDL_GetError());
        }
    }
    ~Session() {
        // The SDK's offscreen singleton outlives CubismFramework::Dispose().
        // Its handles belong to this context, never to the next comparison run.
        if (context)
            Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
    }
};

struct Target {
    GLuint framebuffer = 0, texture = 0;
    int width = 0, height = 0;
    ~Target() {
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
        if (texture) glDeleteTextures(1, &texture);
    }
    void resize(int w, int h) {
        if (width == w && height == h) return;
        if (!framebuffer) glGenFramebuffers(1, &framebuffer);
        if (!texture) glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, texture, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "Incomplete capture framebuffer");
        width = w;
        height = h;
        check_gl("capture allocation");
    }
    void draw(BongoCatLive2D *model) const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        bongo_cat_live2d_draw(model);
        check_gl("model draw");
    }
};

struct Capture { std::string label, digest; size_t visible = 0; };

class Sequence {
public:
    Sequence(BongoCatLive2D *model, std::vector<Capture> &reference, bool compare)
        : model_(model), reference_(reference), compare_(compare) {}

    void size(int width, int height) {
        target_.resize(width, height);
        bongo_cat_live2d_resize(model_, width, height);
    }
    void reset() {
        bongo_cat_live2d_prepare_viewer_audit(model_);
        bongo_cat_live2d_set_expression(model_, -1);
        bongo_cat_live2d_set_mirror(model_, false);
        bongo_cat_live2d_set_centered_dragging(model_, 0, 0);
    }
    void advance(int frames) {
        for (int frame = 0; frame < frames; ++frame) {
            bongo_cat_live2d_update(model_, 1.0f / 60.0f);
            target_.draw(model_);
        }
    }
    void capture(const std::string &label, bool require_visible = false) {
        pixels_.resize((size_t)target_.width * (size_t)target_.height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, target_.framebuffer);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, target_.width, target_.height,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
        check_gl("frame readback");
        Capture result;
        result.label = label;
        for (size_t i = 3; i < pixels_.size(); i += 4)
            if (pixels_[i]) ++result.visible;
        require(!require_visible || result.visible > 0, "Empty model frame: " + label);
        char digest[65];
        bongo_cat_sha256_bytes(pixels_.data(), pixels_.size(), digest);
        result.digest = digest;
        if (compare_) {
            require(position_ < reference_.size(), "Unexpected extra frame: " + label);
            const Capture &expected = reference_[position_];
            require(expected.label == result.label && expected.digest == result.digest &&
                expected.visible == result.visible, "Pixel mismatch: " + label +
                " expected=" + expected.digest + " actual=" + result.digest);
        } else reference_.push_back(result);
        ++position_;
    }
    void finish() const {
        require(position_ == reference_.size(), "Frame count mismatch");
    }
private:
    BongoCatLive2D *model_;
    std::vector<Capture> &reference_;
    bool compare_;
    size_t position_ = 0;
    Target target_;
    std::vector<unsigned char> pixels_;
};

void run(const char *assets, const char *directory, const char *setting,
    const Manifest &manifest, bool direct, std::vector<Capture> &reference,
    bool shared = false) {
    // Context destruction also releases driver allocations between the two
    // loads; only compact frame digests survive the reference run.
    Session session;
    std::srand(1);
    BongoCatError error{};
    using Runtime = std::unique_ptr<BongoCatLive2D, decltype(&bongo_cat_live2d_destroy)>;
    Runtime runtime(bongo_cat_live2d_create(assets, &error), bongo_cat_live2d_destroy);
    require(runtime != nullptr, error.message);
    bongo_cat_live2d_resize(runtime.get(), 640, 640);
    require(bongo_cat_live2d_load(runtime.get(), directory, setting, direct,
        nullptr, nullptr, nullptr, &error) == BONGO_CAT_OK, error.message);
    if (shared) {
        std::srand(1);
        require(bongo_cat_live2d_load(runtime.get(), directory, setting, direct,
            nullptr, nullptr, nullptr, &error) == BONGO_CAT_OK, error.message);
    }
    check_gl("model load");
    Sequence sequence(runtime.get(), reference, !direct);
    const int sizes[][2] = {{320, 240}, {640, 640}, {1400, 1400}, {350, 700}, {700, 350}};
    for (const auto &size : sizes) {
        sequence.size(size[0], size[1]);
        for (int mirror = 0; mirror < 2; ++mirror) {
            sequence.reset();
            bongo_cat_live2d_set_mirror(runtime.get(), mirror != 0);
            for (int input = 0; input < 3; ++input) {
                float direction = (float)(input - 1);
                bongo_cat_live2d_set_centered_dragging(runtime.get(), direction, -direction);
                sequence.advance(15);
                sequence.capture("view:" + std::to_string(size[0]) + "x" +
                    std::to_string(size[1]) + ":" + std::to_string(mirror) + ":" +
                    std::to_string(input), true);
            }
        }
    }
    sequence.size(640, 640);
    for (const auto &motion : manifest.motions) {
        sequence.reset();
        require(bongo_cat_live2d_start_motion(runtime.get(), motion.group.c_str(),
            motion.index), "Cannot start motion " + motion.group + ":" +
            std::to_string(motion.index));
        int previous = 0;
        for (int frame : {1, 15, 45}) {
            sequence.advance(frame - previous);
            sequence.capture("motion:" + motion.group + ":" +
                std::to_string(motion.index) + ":" + std::to_string(frame));
            previous = frame;
        }
    }
    for (int expression = 0; expression < manifest.expressions; ++expression) {
        sequence.reset();
        require(bongo_cat_live2d_set_expression(runtime.get(), expression),
            "Cannot select expression " + std::to_string(expression));
        for (int sample = 0; sample < 3; ++sample) {
            sequence.advance(15);
            sequence.capture("expression:" + std::to_string(expression) + ":" +
                std::to_string(sample));
        }
    }
    // The exported parameter list includes authored mouse/key controls, so this
    // also covers custom key IDs instead of assuming only standard Cubism IDs.
    for (const auto &id : manifest.parameters) {
        sequence.reset();
        BongoCatParameterRange range{};
        if (!bongo_cat_live2d_parameter(runtime.get(), id.c_str(), &range)) continue;
        for (int maximum = 0; maximum < 2; ++maximum) {
            require(bongo_cat_live2d_set_parameter(runtime.get(), id.c_str(),
                maximum ? range.maximum : range.minimum), "Cannot set parameter " + id);
            sequence.advance(2);
            sequence.capture("parameter:" + id + ":" + std::to_string(maximum));
        }
    }
    sequence.finish();
    std::printf("%s: %zu frames, %zu motions, %d expressions; GL checks passed\n",
        direct ? "reference upload" : shared ? "shared texture" : "streaming upload", reference.size(),
        manifest.motions.size(), manifest.expressions);
}

int test_main(int argc, const char *const *argv) {
    if (argc != 1 && argc != 4) {
        std::fprintf(stderr, "Usage: %s [asset-root model-directory setting-file]\n", argv[0]);
        return 2;
    }
    const char *assets = argc == 4 ? argv[1] : BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets";
    const char *directory = argc == 4 ? argv[2] :
        BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models/standard";
    const char *setting = argc == 4 ? argv[3] : "cat.model3.json";
    int result = 1;
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        Manifest manifest = read_manifest(directory, setting);
        std::vector<Capture> reference;
        run(assets, directory, setting, manifest, true, reference);
        run(assets, directory, setting, manifest, false, reference);
        run(assets, directory, setting, manifest, false, reference, true);
        std::printf("PASS: all %zu RGBA frame SHA-256 digests match both optimized paths exactly\n",
            reference.size());
        result = 0;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "Texture equivalence failed: %s\n", exception.what());
    }
    SDL_Quit();
    return result;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> utf8;
    std::vector<const char *> arguments;
    for (int i = 0; i < argc; ++i) {
        int count = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (count <= 0) return 2;
        std::string argument((size_t)count, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &argument[0], count, nullptr, nullptr);
        argument.resize((size_t)count - 1);
        utf8.push_back(std::move(argument));
    }
    for (const auto &argument : utf8) arguments.push_back(argument.c_str());
    return test_main(argc, arguments.data());
}
#else
int main(int argc, char **argv) { return test_main(argc, argv); }
#endif
