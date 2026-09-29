#ifndef BONGO_CAT_CUBISM_RENDER_RESOURCES_HPP
#define BONGO_CAT_CUBISM_RENDER_RESOURCES_HPP

namespace bongo_cat {

/* Drop unused targets retained by Cubism's process-wide OpenGL offscreen pool.
   Returns the number of manager entries retired. The caller must have the
   owning OpenGL context current and must wait for submitted work first. */
unsigned release_offscreen_pool();

} // namespace bongo_cat

#endif
