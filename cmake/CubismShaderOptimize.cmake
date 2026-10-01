function(bongo_cat_replace_cubism_text variable needle replacement label)
  string(FIND "${${variable}}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Cubism 5 r.5 shader patch mismatch: ${label}")
  endif()
  string(REPLACE "${needle}" "${replacement}" updated "${${variable}}")
  set(${variable} "${updated}" PARENT_SCOPE)
endfunction()

function(bongo_cat_remove_cubism_section variable anchor start_marker end_marker label)
  set(text "${${variable}}")
  string(FIND "${text}" "${anchor}" anchor_position)
  if(anchor_position EQUAL -1)
    message(FATAL_ERROR "Cubism 5 r.5 shader patch mismatch: ${label} anchor")
  endif()
  string(SUBSTRING "${text}" ${anchor_position} -1 anchored)
  string(FIND "${anchored}" "${start_marker}" relative_start)
  if(relative_start EQUAL -1)
    message(FATAL_ERROR "Cubism 5 r.5 shader patch mismatch: ${label} start")
  endif()
  math(EXPR start_position "${anchor_position} + ${relative_start}")
  string(SUBSTRING "${text}" ${start_position} -1 section)
  string(FIND "${section}" "${end_marker}" relative_end)
  if(relative_end EQUAL -1)
    message(FATAL_ERROR "Cubism 5 r.5 shader patch mismatch: ${label} end")
  endif()
  string(SUBSTRING "${text}" 0 ${start_position} prefix)
  string(SUBSTRING "${section}" ${relative_end} -1 suffix)
  set(${variable} "${prefix}${suffix}" PARENT_SCOPE)
endfunction()

function(bongo_cat_optimize_cubism_shaders target)
  set(shader_dir "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL")
  set(source_path "${shader_dir}/CubismShader_OpenGLES2.cpp")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism")
  set(output_source "${output_dir}/CubismShader_OpenGLES2.cpp")
  file(READ "${source_path}" source)
  string(REPLACE "\r\n" "\n" source "${source}")
  # The uploader selects trilinear filtering after building a complete,
  # alpha-safe mip chain and retains linear filtering if that work fails.
  # Do not overwrite that per-texture decision on every drawable.
  bongo_cat_replace_cubism_text(source
    "glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);"
    "/* Model upload owns minification filtering and fallback. */"
    "model texture minification ownership")

  set(compile_anchor [=[
_shaderSets[ShaderNames_MultMaskedInvertedPremultipliedAlpha]->ShaderProgram = _shaderSets[ShaderNames_NormalMaskedInvertedPremultipliedAlpha]->ShaderProgram;]=])
  set(block_start [=[    {
        csmUint32 offset = ShaderNames_NormalAtop;]=])
  set(compile_end [=[#endif

    // Copy]=])
  bongo_cat_remove_cubism_section(source "${compile_anchor}" "${block_start}"
    "${compile_end}" "eager blend compilation")

  set(setup_anchor [=[SetShaderSet(*_shaderSets[ShaderNames_MultMaskedInvertedPremultipliedAlpha], MaskType_MaskedInvertedPremultipliedAlpha);]=])
  set(setup_end [=[}

void CubismShader_OpenGLES2::SetupShaderProgramForDrawable]=])
  bongo_cat_remove_cubism_section(source "${setup_anchor}" "${block_start}"
    "${setup_end}" "eager blend setup")

  set(lazy_body [=[
    if (!shaderSet->ShaderProgram)
    {
        static const csmChar* const vertexShaders[] =
        {
            "FrameworkShaders/VertShaderSrcBlend.vert",
            "FrameworkShaders/VertShaderSrcMaskedBlend.vert",
            "FrameworkShaders/VertShaderSrcMaskedBlend.vert",
            "FrameworkShaders/VertShaderSrcBlend.vert",
            "FrameworkShaders/VertShaderSrcMaskedBlend.vert",
            "FrameworkShaders/VertShaderSrcMaskedBlend.vert"
        };
        static const csmChar* const fragmentShaders[] =
        {
            "FrameworkShaders/FragShaderSrcBlend.frag",
            "FrameworkShaders/FragShaderSrcMaskBlend.frag",
            "FrameworkShaders/FragShaderSrcMaskInvertedBlend.frag",
            "FrameworkShaders/FragShaderSrcPremultipliedAlphaBlend.frag",
            "FrameworkShaders/FragShaderSrcMaskPremultipliedAlphaBlend.frag",
            "FrameworkShaders/FragShaderSrcMaskInvertedPremultipliedAlphaBlend.frag"
        };
        csmInt32 colorBlendMode = blendMode.GetColorBlendType();
        if (colorBlendMode >= Core::csmColorBlendType_Add) colorBlendMode -= 2;
        shaderSet->ShaderProgram = LoadShaderProgramFromFile(vertexShaders[offset],
            fragmentShaders[offset], colorBlendMode, blendMode.GetAlphaBlendType());
        if (shaderSet->ShaderProgram)
            SetShaderSet(*shaderSet, static_cast<MaskType>(offset), true);
    }]=])

  set(drawable_setup [=[    const csmInt32 shaderNameBegin = GetShaderNamesBegin(model.GetDrawableBlendModeType(index));
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];]=])
  set(drawable_prefix [=[    const csmBlendMode blendMode = model.GetDrawableBlendModeType(index);
    const csmInt32 shaderNameBegin = GetShaderNamesBegin(blendMode);
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];]=])
  set(drawable_lazy "${drawable_prefix}${lazy_body}")
  bongo_cat_replace_cubism_text(source "${drawable_setup}" "${drawable_lazy}"
    "drawable lazy shader call")

  set(offscreen_setup [=[    const csmInt32 shaderNameBegin = GetShaderNamesBegin(model.GetOffscreenBlendModeType(offscreenIndex));
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];]=])
  set(offscreen_prefix [=[    const csmBlendMode blendMode = model.GetOffscreenBlendModeType(offscreenIndex);
    const csmInt32 shaderNameBegin = GetShaderNamesBegin(blendMode);
    CubismShaderSet* shaderSet = _shaderSets[shaderNameBegin + offset];]=])
  set(offscreen_lazy "${offscreen_prefix}${lazy_body}")
  bongo_cat_replace_cubism_text(source "${offscreen_setup}" "${offscreen_lazy}"
    "offscreen lazy shader call")

  if(APPLE)
    bongo_cat_core_profile_patch_shader(source)
  endif()

  file(MAKE_DIRECTORY "${output_dir}")
  file(REMOVE "${output_dir}/CubismShader_OpenGLES2.hpp")
  file(WRITE "${output_source}" "${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_path}")
  get_target_property(framework_sources ${target} SOURCES)
  list(REMOVE_ITEM framework_sources "${source_path}")
  set_property(TARGET ${target} PROPERTY SOURCES "${framework_sources}")
  target_sources(${target} PRIVATE "${output_source}")
  target_include_directories(${target} PRIVATE "${shader_dir}")
endfunction()
