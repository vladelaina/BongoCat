function(bongo_cat_configure_embedded_cubism_assets)
  set(BONGO_CAT_ASSET_EXTRA_COMMANDS PARENT_SCOPE)
  if(NOT BONGO_CAT_CUBISM_ENABLED)
    return()
  endif()
  file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
    "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}/*")
  set(BONGO_CAT_ASSET_INPUTS ${BONGO_CAT_ASSET_INPUTS} ${shader_inputs} PARENT_SCOPE)
  set(BONGO_CAT_ASSET_EXTRA_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}"
      "${BONGO_CAT_ASSET_STAGE}/assets/FrameworkShaders" PARENT_SCOPE)
endfunction()

function(bongo_cat_stage_cubism_assets target)
  if(NOT BONGO_CAT_CUBISM_ENABLED)
    return()
  endif()
  get_target_property(is_bundle ${target} MACOSX_BUNDLE)
  if(APPLE AND is_bundle)
    set(shader_destination
      "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/assets/FrameworkShaders")
  else()
    set(shader_destination "$<TARGET_FILE_DIR:${target}>/FrameworkShaders")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}"
      "${shader_destination}"
    VERBATIM)
  if(UNIX AND NOT APPLE AND target STREQUAL "bongo_cat")
    install(DIRECTORY
      "${BONGO_CAT_CUBISM_SHADER_SOURCE_DIR}/"
      DESTINATION assets/FrameworkShaders COMPONENT Runtime)
  endif()
endfunction()
