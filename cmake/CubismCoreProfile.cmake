# Adapt the pinned compatibility renderer to macOS's existing OpenGL core context.
# Generated sources keep the shared SDK and its class layouts unchanged.
function(bongo_cat_core_shader_attributes variable)
  set(source "${${variable}}")
  bongo_cat_replace_cubism_text(source "#include \"CubismShader_OpenGLES2.hpp\""
    "#include \"CubismShader_OpenGLES2.hpp\"\n#include \"cubism_core_profile.hpp\"" "core adapter include")
  foreach(stream IN ITEMS Position TexCoord)
    if(stream STREQUAL "Position")
      set(buffer 0)
      set(mesh vertexArray)
      set(quad renderTargetVertexArray)
    else()
      set(buffer 1)
      set(mesh uvArray)
      set(quad renderTargetUvArray)
    endif()
    set(prefix "glVertexAttribPointer(shaderSet->Attribute${stream}Location, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, ")
    set(call "bongo_cat::CubismCoreProfile::attribute(shaderSet->Attribute${stream}Location, ")
    bongo_cat_replace_cubism_text(source "${prefix}${mesh});"
      "${call}model.GetDrawableVertexCount(index), ${mesh}, ${buffer});"
      "core drawable ${stream}")
    bongo_cat_replace_cubism_text(source "${prefix}${quad});"
      "${call}4, ${quad}, ${buffer});" "core quad ${stream}")
    if(stream STREQUAL "TexCoord")
      bongo_cat_replace_cubism_text(source "${prefix}renderTargetReverseUvArray);"
        "${call}4, renderTargetReverseUvArray, ${buffer});" "core reversed UV")
    endif()
  endforeach()
  set(${variable} "${source}" PARENT_SCOPE)
endfunction()

function(bongo_cat_core_renderer target)
  set(renderer_dir "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL")
  set(input "${renderer_dir}/CubismRenderer_OpenGLES2.cpp")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism/CubismRenderer_OpenGLES2.cpp")
  file(READ "${input}" source)
  string(REPLACE "\r\n" "\n" source "${source}")
  bongo_cat_replace_cubism_text(source "#include \"CubismRenderer_OpenGLES2.hpp\""
    "#include \"CubismRenderer_OpenGLES2.hpp\"\n#include \"cubism_core_profile.hpp\"" "core adapter include")
  bongo_cat_replace_cubism_text(source
    "glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indexArray);"
    "bongo_cat::CubismCoreProfile::draw(indexCount, indexArray);" "core mesh indices")
  bongo_cat_replace_cubism_text(source
    "glDrawElements(GL_TRIANGLES, sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), GL_UNSIGNED_SHORT, ModelRenderTargetIndexArray);"
    "bongo_cat::CubismCoreProfile::draw(sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), ModelRenderTargetIndexArray);"
    "core quad indices")
  file(WRITE "${output}" "${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
  get_target_property(sources ${target} SOURCES)
  list(REMOVE_ITEM sources "${input}")
  set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
  target_sources(${target} PRIVATE "${output}")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/live2d")
  target_link_libraries(${target} PRIVATE SDL3::SDL3-static)
endfunction()
