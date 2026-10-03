#include "cubism_model.hpp"
#include "bongo_cat/file.h"

#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Pixel = std::array<unsigned char, 4>;
const Pixel original{17, 91, 203, 255}, changed{231, 42, 80, 255};

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void write_texture(const std::string &path, const Pixel &pixel) {
    std::array<Pixel, 16> pixels;
    pixels.fill(pixel);
    FILE *file = bongo_cat_file_open(path.c_str(), "wb");
    require(file != nullptr, "Cannot create PNG fixture");
    bool written = stbi_write_png_to_func([](void *output, void *data, int size) {
        std::fwrite(data, 1, (size_t)size, static_cast<FILE *>(output));
    }, file, 4, 4, 4, pixels.data(), 16) != 0;
    written = !std::ferror(file) && written;
    written = std::fclose(file) == 0 && written;
    require(written, "Cannot write PNG fixture");
}

class Fixtures {
public:
    Fixtures() {
        char *base = SDL_GetPrefPath("BongoCatTests", "TextureSharing");
        require(base != nullptr, SDL_GetError());
        root = base;
        SDL_free(base);
        root += std::to_string(SDL_GetTicksNS());
        require(SDL_CreateDirectory(root.c_str()), SDL_GetError());
        for (const char *name : {"first", "second"}) {
            std::string directory = root + "/" + name;
            require(SDL_CreateDirectory(directory.c_str()), SDL_GetError());
            require(SDL_CopyFile(BONGO_CAT_NATIVE_SOURCE_DIR
                "/resources/assets/models/standard/demomodel.moc3",
                (directory + "/model.moc3").c_str()), "Cannot copy fixture moc3");
            FILE *file = bongo_cat_file_open((directory + "/model.model3.json").c_str(), "wb");
            require(file != nullptr, "Cannot create fixture settings");
            const char json[] = "{\"Version\":3,\"FileReferences\":{\"Moc\":\"model.moc3\","
                "\"Textures\":[\"atlas.png\",\"atlas.png\",\"atlas.png\"]}}";
            bool written = std::fwrite(json, 1, sizeof(json) - 1, file) == sizeof(json) - 1;
            written = std::fclose(file) == 0 && written;
            require(written, "Cannot write fixture settings");
            write_texture(directory + "/atlas.png", original);
        }
    }
    ~Fixtures() {
        for (const char *name : {"first", "second"}) {
            std::string directory = root + "/" + name;
            for (const char *file : {"atlas.png", "model.moc3", "model.model3.json"})
                SDL_RemovePath((directory + "/" + file).c_str());
            SDL_RemovePath(directory.c_str());
        }
        SDL_RemovePath(root.c_str());
    }
    std::string directory(const char *name) const { return root + "/" + name; }
private:
    std::string root;
};

class Session {
public:
    Session() {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
        window = SDL_CreateWindow("Texture sharing", 32, 32,
            SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        context = SDL_GL_CreateContext(window);
        if (!context) {
            SDL_DestroyWindow(window);
            throw std::runtime_error(SDL_GetError());
        }
        BongoCatError error{};
        framework = bongo_cat_live2d_create(BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets", &error);
        if (!framework) {
            SDL_GL_DestroyContext(context);
            SDL_DestroyWindow(window);
            throw std::runtime_error(error.message);
        }
    }
    ~Session() {
        SDL_GL_MakeCurrent(window, context);
        bongo_cat_live2d_destroy(framework);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
    }
    void use() { require(SDL_GL_MakeCurrent(window, context), SDL_GetError()); }
private:
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    BongoCatLive2D *framework = nullptr;
};

class Model {
public:
    Model(Session &session, const std::string &directory, bool direct = false)
        : session_(session) {
        session_.use();
        model_ = std::make_unique<bongo_cat::NativeModel>();
        BongoCatError error{};
        require(model_->load(directory.c_str(), "model.model3.json", direct,
            nullptr, nullptr, &error), error.message);
        model_->reshape(32, 32);
        require(model_->load_textures(&error, nullptr, nullptr), error.message);
        require(glGetError() == GL_NO_ERROR, "Model upload GL error");
    }
    ~Model() { reset(); }
    void reset() {
        if (model_) {
            session_.use();
            model_.reset();
        }
    }
    GLuint texture() {
        session_.use();
        const auto &textures = model_->GetRenderer<
            Csm::Rendering::CubismRenderer_OpenGLES2>()->GetBindedTextures();
        require(textures.GetSize() == 3, "Fixture did not bind all three texture slots");
        GLuint first = textures.Begin()->Second;
        for (auto entry = textures.Begin(); entry != textures.End(); ++entry)
            require(entry->Second == first,
                "Repeated identical atlas references do not share storage");
        return first;
    }
private:
    Session &session_;
    std::unique_ptr<bongo_cat::NativeModel> model_;
};

void pixels(GLuint texture, const Pixel &expected) {
    require(glIsTexture(texture) != GL_FALSE, "Expected live texture is missing");
    glBindTexture(GL_TEXTURE_2D, texture);
    for (int level = 0, size = 4; level < 3; ++level, size /= 2) {
        GLint width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &height);
        require(width == size && height == size, "Shared texture dimensions/mips changed");
        std::vector<unsigned char> values((size_t)size * size * 4);
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, values.data());
        for (size_t i = 0; i < values.size(); ++i)
            require(values[i] == expected[i % 4], "Shared texture texel changed");
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    require(glGetError() == GL_NO_ERROR, "Texture readback GL error");
}

void sharing_lifetime(Session &session, const Fixtures &fixtures) {
    Model first(session, fixtures.directory("first"));
    Model second(session, fixtures.directory("second"));
    GLuint texture = first.texture();
    require(second.texture() == texture, "Identical bytes in different directories were not shared");
    pixels(texture, original);
    first.reset();
    pixels(texture, original);
    second.reset();
    require(!glIsTexture(texture), "Weak cache retained texture after its final owner");
}

void changed_source(Session &session, const Fixtures &fixtures) {
    Model first(session, fixtures.directory("first"));
    Model before(session, fixtures.directory("second"));
    GLuint old_texture = first.texture();
    require(before.texture() == old_texture, "Initial fixture textures are not shared");

    std::string path = fixtures.directory("second") + "/atlas.png";
    auto file = std::filesystem::u8path(path);
    auto timestamp = std::filesystem::last_write_time(file);
    SDL_PathInfo old_info{}, new_info{};
    require(SDL_GetPathInfo(path.c_str(), &old_info), "Cannot stat original fixture");
    write_texture(path, changed);
    std::filesystem::last_write_time(file, timestamp);
    require(SDL_GetPathInfo(path.c_str(), &new_info), "Cannot stat changed fixture");
    require(old_info.size == new_info.size && old_info.modify_time == new_info.modify_time,
        "Fixture size or modification timestamp changed unexpectedly");
#ifdef _WIN32
    require(old_info.create_time == new_info.create_time, "Fixture creation timestamp changed");
#endif

    Model after(session, fixtures.directory("second"));
    GLuint new_texture = after.texture();
    require(new_texture != old_texture, "Content edit reused stale texture despite changed bytes");
    pixels(new_texture, changed);
    pixels(old_texture, original);
    Model direct(session, fixtures.directory("first"), true);
    GLuint direct_texture = direct.texture();
    require(direct_texture != old_texture, "Different decoder policies incorrectly shared a texture");
    pixels(direct_texture, original);
    direct.reset();
    require(!glIsTexture(direct_texture), "Direct texture was retained after final owner");
    first.reset();
    pixels(old_texture, original);
    before.reset();
    require(!glIsTexture(old_texture), "Original content retained after final owner");
    pixels(new_texture, changed);
    after.reset();
    require(!glIsTexture(new_texture), "Changed content retained after final owner");
}

void separate_contexts(Session &first_context, const Fixtures &fixtures) {
    Model first(first_context, fixtures.directory("first"));
    GLuint first_texture = first.texture();
    {
        Session second_context;
        require(!glIsTexture(first_texture), "Contexts unexpectedly share texture objects");
        // A fresh context can reuse the same numeric name. Its unrelated
        // pixels must never be mistaken for the first context's atlas.
        GLuint marker_texture = 0;
        glGenTextures(1, &marker_texture);
        glBindTexture(GL_TEXTURE_2D, marker_texture);
        const Pixel marker{0, 255, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, marker.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        Model second(second_context, fixtures.directory("first"));
        GLuint second_texture = second.texture();
        require(second_texture != marker_texture, "Texture reused across unrelated GL contexts");
        pixels(second_texture, original);
        second.reset();
        require(!glIsTexture(second_texture) && glIsTexture(marker_texture),
            "Second-context cleanup deleted unrelated texture storage");
        glDeleteTextures(1, &marker_texture);
    }
    first_context.use();
    pixels(first_texture, original);
    first.reset();
    require(!glIsTexture(first_texture), "First-context texture retained after final owner");
    require(glGetError() == GL_NO_ERROR, "Context isolation GL error");
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
    int result = 1;
    try {
        Fixtures fixtures;
        Session session;
        sharing_lifetime(session, fixtures);
        changed_source(session, fixtures);
        separate_contexts(session, fixtures);
        std::puts("Texture sharing: exact texels/mips, content identity, final-owner cleanup and context isolation passed");
        result = 0;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "Texture sharing failed: %s\n", exception.what());
    }
    SDL_Quit();
    return result;
}
