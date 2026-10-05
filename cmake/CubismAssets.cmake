function(bongo_cat_configure_embedded_cubism_assets)
  set(BONGO_CAT_ASSET_EXTRA_COMMANDS PARENT_SCOPE)
  if(NOT BONGO_CAT_CUBISM_ENABLED)
    return()
  endif()
  file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
    "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard/*")
  set(BONGO_CAT_ASSET_INPUTS ${BONGO_CAT_ASSET_INPUTS} ${shader_inputs} PARENT_SCOPE)
  set(BONGO_CAT_ASSET_EXTRA_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard"
      "${BONGO_CAT_ASSET_STAGE}/assets/FrameworkShaders" PARENT_SCOPE)
endfunction()

function(bongo_cat_stage_cubism_assets target)
  if(NOT BONGO_CAT_CUBISM_ENABLED)
    return()
  endif()
  set(shader_source "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard")
  if(APPLE)
    # The pinned SDK's desktop shaders target compatibility GLSL 1.20.
    # Keep the vendor tree intact; macOS uses the application's 4.1 core context.
    set(shader_source "${CMAKE_CURRENT_BINARY_DIR}/generated/cubism/FrameworkShaders")
    file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
      "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard/*")
    foreach(input IN LISTS shader_inputs)
      file(RELATIVE_PATH name
        "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard" "${input}")
      file(READ "${input}" shader)
      if(shader MATCHES "#version 120")
        string(REPLACE "#version 120" "#version 410 core" shader "${shader}")
        if(name MATCHES "\\.vert$")
          string(REPLACE "attribute " "in " shader "${shader}")
          string(REPLACE "varying " "out " shader "${shader}")
        else()
          string(REPLACE "varying " "in " shader "${shader}")
          string(REPLACE "#version 410 core" "#version 410 core\nout vec4 bongo_fragment_color;" shader "${shader}")
          string(REPLACE "gl_FragColor" "bongo_fragment_color" shader "${shader}")
        endif()
      endif()
      # Blend functions are appended snippets, without a version or output.
      string(REPLACE "texture2D(" "texture(" shader "${shader}")
      file(WRITE "${shader_source}/${name}" "${shader}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    endforeach()
  endif()
  set(shader_destination "$<TARGET_FILE_DIR:${target}>/FrameworkShaders")
  if(APPLE)
    set(shader_destination "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/FrameworkShaders")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${shader_source}"
      "${shader_destination}")
  if(UNIX AND NOT APPLE)
    install(DIRECTORY
      "${CUBISM_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard/"
      DESTINATION assets/FrameworkShaders)
  endif()
endfunction()
