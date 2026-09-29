#include "cubism_render_resources.hpp"
#include "bongo_cat/model_memory.h"

#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

namespace bongo_cat {

unsigned release_offscreen_pool() {
    if (!SDL_GL_GetCurrentContext()) return 0;
    auto *offscreen =
        Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    if (!offscreen) return 0;
    const unsigned retained = offscreen->GetOffscreenRenderTargetListSize();
    /* At a model handoff an empty frame makes unused targets stale. Preserve
       any target still marked in use; diagnostics report what was retained. */
    offscreen->BeginFrameProcess();
    offscreen->EndFrameProcess();
    offscreen->ReleaseStaleRenderTextures();
    const unsigned remaining = offscreen->GetOffscreenRenderTargetListSize();
    const unsigned released = retained > remaining ? retained - remaining : 0;
    bongo_cat_model_memory_log("offscreen-release", "before=%u after=%u",
        retained, remaining);
    if (released) {
        SDL_Log("[runtime] Live2D offscreen pool released: targets=%u",
            released);
    }
    return released;
}

} // namespace bongo_cat
