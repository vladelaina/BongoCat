# macOS core-profile shaders and buffer-backed drawing.
set(BONGO_CAT_CUBISM_SHADER_SOURCE_DIR
  "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard")
if(APPLE)
  set(BONGO_CAT_CUBISM_SHADER_SOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism-shaders")
endif()

function(bongo_cat_core_profile_convert_shader input output)
  file(READ "${input}" text)
  string(REPLACE "\r\n" "\n" text "${text}")
  string(REPLACE "#version 120" "#version 330" text "${text}")
  get_filename_component(name "${input}" NAME)
  if(name STREQUAL "FragShaderSrcColorBlend.frag" OR
      name STREQUAL "FragShaderSrcAlphaBlend.frag")
    # Appended blend-mode snippets: pure functions without legacy syntax.
  elseif(input MATCHES "\\.vert$")
    string(REPLACE "attribute " "in " text "${text}")
    string(REPLACE "varying " "out " text "${text}")
  else()
    string(REPLACE "varying " "in " text "${text}")
    string(REPLACE "texture2D(" "texture(" text "${text}")
    string(REPLACE "gl_FragColor" "BongoCatFragColor" text "${text}")
    string(REPLACE "#version 330\n"
      "#version 330\n\nout vec4 BongoCatFragColor;\n" text "${text}")
  endif()
  foreach(legacy IN ITEMS "attribute " "varying " "texture2D(" "gl_FragColor"
      "#version 120")
    string(FIND "${text}" "${legacy}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR "Core-profile shader conversion failed for ${name}: "
        "'${legacy}' remains")
    endif()
  endforeach()
  file(WRITE "${output}" "${text}")
endfunction()

function(bongo_cat_core_profile_prepare_shaders)
  set(source_dir "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard")
  file(MAKE_DIRECTORY "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}")
  file(GLOB shader_files CONFIGURE_DEPENDS "${source_dir}/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${shader_files})
  foreach(input IN LISTS shader_files)
    get_filename_component(name "${input}" NAME)
    bongo_cat_core_profile_convert_shader("${input}"
      "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}/${name}")
  endforeach()
endfunction()

function(bongo_cat_core_profile_patch_shader variable)
  set(source "${${variable}}")
  string(ASCII 239 187 191 bom)
  string(REPLACE "${bom}" "" source "${source}")
  foreach(spec IN ITEMS
      "AttributePositionLocation|vertexArray|model.GetDrawableVertexCount(index) * sizeof(csmFloat32) * 2|0"
      "AttributeTexCoordLocation|uvArray|model.GetDrawableVertexCount(index) * sizeof(csmFloat32) * 2|1"
      "AttributePositionLocation|renderTargetVertexArray|sizeof(renderTargetVertexArray)|0"
      "AttributeTexCoordLocation|renderTargetUvArray|sizeof(renderTargetUvArray)|1"
      "AttributeTexCoordLocation|renderTargetReverseUvArray|sizeof(renderTargetReverseUvArray)|1")
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 attribute)
    list(GET fields 1 data)
    list(GET fields 2 size)
    list(GET fields 3 slot)
    bongo_cat_replace_cubism_text(source
      "glVertexAttribPointer(shaderSet->${attribute}, 2, GL_FLOAT, GL_FALSE, sizeof(csmFloat32) * 2, ${data});"
      "bongo_cat::CoreProfileBinding::attribute(${slot}, shaderSet->${attribute}, ${data}, ${size});"
      "core-profile ${data}")
  endforeach()
  if(source MATCHES "glVertexAttribPointer[ \t\r\n]*\\(")
    message(FATAL_ERROR "Unpatched Cubism client-side vertex attributes")
  endif()
  set(${variable} "#include \"cubism_core_profile.hpp\"\n${source}" PARENT_SCOPE)
endfunction()

function(bongo_cat_core_profile_patch_renderer target)
  set(source_path "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/CubismRenderer_OpenGLES2.cpp")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism")
  set(output_source "${output_dir}/CubismRenderer_OpenGLES2.cpp")
  file(READ "${source_path}" source)
  string(ASCII 239 187 191 bom)
  string(REPLACE "${bom}" "" source "${source}")
  string(REPLACE "\r\n" "\n" source "${source}")
  bongo_cat_replace_cubism_text(source
    "glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indexArray);"
    "bongo_cat::CoreProfileBinding::draw(indexCount, indexArray);"
    "core-profile mesh indices")
  bongo_cat_replace_cubism_text(source
    "glDrawElements(GL_TRIANGLES, sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), GL_UNSIGNED_SHORT, ModelRenderTargetIndexArray);"
    "bongo_cat::CoreProfileBinding::draw(sizeof(ModelRenderTargetIndexArray) / sizeof(csmUint16), ModelRenderTargetIndexArray);"
    "core-profile composite indices")
  if(source MATCHES "glDrawElements[ \t\r\n]*\\(")
    message(FATAL_ERROR "Unpatched Cubism client-side index draw")
  endif()
  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${output_source}" "#include \"cubism_core_profile.hpp\"\n${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")
  get_target_property(framework_sources ${target} SOURCES)
  list(REMOVE_ITEM framework_sources "${source_path}")
  set_property(TARGET ${target} PROPERTY SOURCES "${framework_sources}")
  target_sources(${target} PRIVATE "${output_source}")
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/live2d"
    "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL")
endfunction()
