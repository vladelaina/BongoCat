if(BUILD_TESTING)
  add_executable(bongo_cat_window_corner_tests tests/platform/test_window_corners.c)
  target_include_directories(bongo_cat_window_corner_tests PRIVATE
    src/runtime/shell tests/support)
  target_link_libraries(bongo_cat_window_corner_tests PRIVATE bongo_cat_warnings)
  add_test(NAME window-corner-policy COMMAND bongo_cat_window_corner_tests)
  add_executable(bongo_cat_gl_readback_tests tests/platform/test_gl_readback.c)
  target_include_directories(bongo_cat_gl_readback_tests PRIVATE tests/support)
  target_link_libraries(bongo_cat_gl_readback_tests PRIVATE SDL3::SDL3-static bongo_cat_warnings)
  add_test(NAME window-gl-readback COMMAND bongo_cat_gl_readback_tests)
  add_executable(bongo_cat_input_shape_tests tests/platform/test_linux_shape.c)
  target_include_directories(bongo_cat_input_shape_tests PRIVATE
    src/platform/linux tests/support include "${BONGO_CAT_GENERATED_INCLUDE_DIR}")
  target_link_libraries(bongo_cat_input_shape_tests PRIVATE SDL3::SDL3-static bongo_cat_warnings)
  if(MSVC)
    target_compile_options(bongo_cat_input_shape_tests PRIVATE /experimental:c11atomics)
  endif()
  add_test(NAME input-shape-mask COMMAND bongo_cat_input_shape_tests)
  add_executable(bongo_cat_mask_policy_tests tests/live2d/test_mask_policy.cpp)
  target_include_directories(bongo_cat_mask_policy_tests PRIVATE src/live2d tests/support)
  target_link_libraries(bongo_cat_mask_policy_tests PRIVATE bongo_cat_warnings)
  add_test(NAME live2d-mask-policy COMMAND bongo_cat_mask_policy_tests)
  add_executable(bongo_cat_texture_resolution_tests
    tests/live2d/test_texture_resolution.cpp src/live2d/cubism_texture_resolution.cpp)
  target_include_directories(bongo_cat_texture_resolution_tests PRIVATE src/live2d tests/support)
  target_link_libraries(bongo_cat_texture_resolution_tests PRIVATE bongo_cat_warnings)
  add_test(NAME live2d-texture-resolution COMMAND bongo_cat_texture_resolution_tests)
  add_executable(bongo_cat_frame_policy_tests tests/live2d/test_frame_policy.cpp)
  target_include_directories(bongo_cat_frame_policy_tests PRIVATE
    src/live2d tests/support include "${BONGO_CAT_GENERATED_INCLUDE_DIR}")
  target_link_libraries(bongo_cat_frame_policy_tests PRIVATE bongo_cat_warnings)
  add_test(NAME live2d-frame-policy COMMAND bongo_cat_frame_policy_tests)
  add_executable(bongo_cat_window_frame_tests tests/platform/test_window_frame.c)
  target_include_directories(bongo_cat_window_frame_tests PRIVATE
    tests/support ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(bongo_cat_window_frame_tests PRIVATE
    bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  if(MSVC)
    target_compile_options(bongo_cat_window_frame_tests PRIVATE /experimental:c11atomics)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(bongo_cat_window_frame_tests PRIVATE m)
  endif()
  add_test(NAME window-motion-frame COMMAND bongo_cat_window_frame_tests)
  add_test(NAME cubism-texture-sampling COMMAND ${CMAKE_COMMAND}
    "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DSDK=${BONGO_CAT_CUBISM_SDK}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCubismTextureSampling.cmake")
  add_executable(bongo_cat_multi_pet_shortcut_tests
    tests/core/test_multi_pet_shortcuts.c)
  target_include_directories(bongo_cat_multi_pet_shortcut_tests PRIVATE
    tests/support ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(bongo_cat_multi_pet_shortcut_tests PRIVATE
    bongo_cat_runtime bongo_cat_warnings)
  if(MSVC)
    target_compile_options(bongo_cat_multi_pet_shortcut_tests PRIVATE
      /experimental:c11atomics)
  endif()
  add_test(NAME multi-pet-shortcuts COMMAND bongo_cat_multi_pet_shortcut_tests)

  add_executable(bongo_cat_input_concurrent_tests tests/core/test_input_concurrent.c)
  target_include_directories(bongo_cat_input_concurrent_tests PRIVATE tests/support)
  target_link_libraries(bongo_cat_input_concurrent_tests PRIVATE
    bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  add_test(NAME input-concurrent COMMAND bongo_cat_input_concurrent_tests)
  set_tests_properties(input-concurrent PROPERTIES TIMEOUT 30)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(bongo_cat_linux_evdev_tests tests/platform/test_linux_evdev.c)
    target_include_directories(bongo_cat_linux_evdev_tests PRIVATE
      src/platform/linux tests/support)
    target_link_libraries(bongo_cat_linux_evdev_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME linux-evdev COMMAND bongo_cat_linux_evdev_tests)
    set_tests_properties(linux-evdev PROPERTIES TIMEOUT 15)
    add_executable(bongo_cat_linux_window_tests tests/platform/test_linux_window.c
      tests/platform/test_linux_click_through.c)
    target_include_directories(bongo_cat_linux_window_tests PRIVATE
      ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS} tests/support)
    target_link_libraries(bongo_cat_linux_window_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings X11::X11 X11::Xfixes X11::Xext)
    add_test(NAME linux-window COMMAND bongo_cat_linux_window_tests
      --ci-smoke --ci-ignore-global-input
      "--storage-root=${CMAKE_CURRENT_BINARY_DIR}/linux-window-data")
    set_tests_properties(linux-window PROPERTIES TIMEOUT 30
      ENVIRONMENT "BONGOCAT_ENABLE_EVDEV=0;BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN=1")
  endif()
  add_executable(bongo_cat_image_filter_tests tests/media/test_image_filter.c)
  target_include_directories(bongo_cat_image_filter_tests PRIVATE
    src/media tests/support)
  target_include_directories(bongo_cat_image_filter_tests SYSTEM PRIVATE
    ${BONGO_CAT_STB_INCLUDE_DIR})
  target_link_libraries(bongo_cat_image_filter_tests PRIVATE
    bongo_cat_runtime bongo_cat_warnings)
  add_test(NAME image-filter COMMAND bongo_cat_image_filter_tests)
  add_executable(bongo_cat_image_png_stream_tests tests/media/test_image_png_stream.c)
  target_include_directories(bongo_cat_image_png_stream_tests PRIVATE
    src/media tests/support)
  target_link_libraries(bongo_cat_image_png_stream_tests PRIVATE
    bongo_cat_runtime bongo_cat_archive bongo_cat_warnings)
  add_test(NAME image-png-stream COMMAND bongo_cat_image_png_stream_tests)
  add_executable(bongo_cat_image_texture_cache_tests tests/media/test_image_texture_cache.c)
  target_include_directories(bongo_cat_image_texture_cache_tests PRIVATE src/media tests/support)
  target_link_libraries(bongo_cat_image_texture_cache_tests PRIVATE
    bongo_cat_runtime bongo_cat_archive bongo_cat_warnings)
  add_test(NAME image-texture-cache COMMAND bongo_cat_image_texture_cache_tests)
  add_executable(bongo_cat_image_upload_fallback_tests
    tests/media/test_image_upload_fallback.c)
  target_include_directories(bongo_cat_image_upload_fallback_tests PRIVATE
    src/media tests/support)
  target_include_directories(bongo_cat_image_upload_fallback_tests SYSTEM PRIVATE
    ${BONGO_CAT_STB_INCLUDE_DIR})
  target_link_libraries(bongo_cat_image_upload_fallback_tests PRIVATE
    bongo_cat_runtime bongo_cat_warnings)
  add_test(NAME image-upload-fallback COMMAND bongo_cat_image_upload_fallback_tests)
  if(BONGO_CAT_CUBISM_ENABLED)
    add_executable(bongo_cat_overlay_layout_tests tests/media/test_overlay_layout.c)
    target_include_directories(bongo_cat_overlay_layout_tests PRIVATE tests/support)
    target_include_directories(bongo_cat_overlay_layout_tests SYSTEM PRIVATE
      ${BONGO_CAT_STB_INCLUDE_DIR})
    target_link_libraries(bongo_cat_overlay_layout_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME overlay-layout COMMAND bongo_cat_overlay_layout_tests)
  endif()
  if(APPLE)
    add_test(NAME macos-bundle-signature COMMAND /usr/bin/codesign
      --verify --strict --deep --verbose=2 "$<TARGET_BUNDLE_DIR:bongo_cat>")
  endif()

  add_executable(bongo_cat_hover_fade_tests
    tests/core/test_hover_fade.c src/runtime/input/mouse.c
    src/runtime/shell/modal_frame.c)
  target_include_directories(bongo_cat_hover_fade_tests PRIVATE
    "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include tests/support
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(bongo_cat_hover_fade_tests PRIVATE
    SDL3::SDL3-static bongo_cat_warnings)
  add_test(NAME hover-fade COMMAND bongo_cat_hover_fade_tests)

  add_executable(bongo_cat_audio_tests tests/media/test_audio.c)
  target_include_directories(bongo_cat_audio_tests PRIVATE src/media/audio tests/support)
  target_link_libraries(bongo_cat_audio_tests PRIVATE bongo_cat_runtime bongo_cat_warnings)
  target_compile_definitions(bongo_cat_audio_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME audio COMMAND bongo_cat_audio_tests)

  add_executable(bongo_cat_log_policy_tests tests/core/test_log_policy.c)
  target_link_libraries(bongo_cat_log_policy_tests PRIVATE
    bongo_cat_runtime bongo_cat_warnings)
  add_test(NAME log-policy COMMAND bongo_cat_log_policy_tests)

  add_executable(bongo_cat_core_tests
    tests/core/test_main.c
    tests/core/test_config.c
    tests/core/test_config_validation.c
    tests/core/test_language.c
    tests/core/test_input.c
    tests/core/test_models.c
    tests/core/test_mver_pointer.c
    tests/core/test_shortcut.c
    tests/core/test_sound_shortcut.c
    tests/core/test_update.c
    src/platform/windows/windows_keys.c)
  target_link_libraries(bongo_cat_core_tests PRIVATE
    bongo_cat_core bongo_cat_warnings $<$<PLATFORM_ID:Windows>:user32>)
  target_include_directories(bongo_cat_core_tests PRIVATE
    tests/support
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows>)
  target_compile_definitions(bongo_cat_core_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME core COMMAND bongo_cat_core_tests)

  add_executable(bongo_cat_i18n_tests tests/i18n/test_i18n.c)
  target_link_libraries(bongo_cat_i18n_tests PRIVATE
    bongo_cat_core bongo_cat_warnings)
  target_compile_definitions(bongo_cat_i18n_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME i18n COMMAND bongo_cat_i18n_tests)

  add_executable(bongo_cat_ui_tests
    tests/ui/test_nuklear.c
    src/ui/backend/nuklear_impl.c
    src/ui/rendering/ui_paint_border.c)
  target_include_directories(bongo_cat_ui_tests PRIVATE
    src/ui/backend src/ui/rendering)
  target_include_directories(bongo_cat_ui_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_ui_tests PRIVATE bongo_cat_warnings)
  add_test(NAME ui COMMAND bongo_cat_ui_tests)

  if(APPLE)
    # The refresh helper runs the real page code with the platform read replaced
    # per target, so the repaint decision is tested without touching TCC.
    add_executable(bongo_cat_input_monitoring_refresh_tests
      tests/ui/test_input_monitoring_refresh.c
      src/ui/preferences/preferences_pages.c)
    target_compile_definitions(bongo_cat_input_monitoring_refresh_tests PRIVATE
      bongo_cat_platform_input_monitoring_authorized=bongo_cat_test_input_monitoring_authorized)
    target_include_directories(bongo_cat_input_monitoring_refresh_tests PRIVATE
      ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
    target_include_directories(bongo_cat_input_monitoring_refresh_tests SYSTEM PRIVATE
      ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
    target_link_libraries(bongo_cat_input_monitoring_refresh_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME macos-input-monitoring-refresh
      COMMAND bongo_cat_input_monitoring_refresh_tests)
  endif()

  add_executable(bongo_cat_app_state_tests
    tests/core/test_app_state.c src/core/app_state.c
    src/runtime/model/model_behavior_state.c src/core/sound_shortcut.c)
  target_link_libraries(bongo_cat_app_state_tests PRIVATE bongo_cat_warnings)
  target_include_directories(bongo_cat_app_state_tests PRIVATE
    "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include tests/support)
  add_test(NAME app-state COMMAND bongo_cat_app_state_tests)

  add_executable(bongo_cat_gamepad_tests
    tests/core/test_gamepad.c src/runtime/input/gamepad.c
    src/core/app_state.c src/core/sound_shortcut.c)
  target_include_directories(bongo_cat_gamepad_tests PRIVATE
    "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include tests/support
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(bongo_cat_gamepad_tests PRIVATE
    SDL3::SDL3-static bongo_cat_warnings)
  add_test(NAME gamepad-state COMMAND bongo_cat_gamepad_tests)
  set_tests_properties(gamepad-state PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

  set(BONGO_CAT_MVER_IMPORT_TEST_SOURCES
    tests/model_import/test_mver_config.c
    tests/model_import/test_mver_audio.c
    tests/model_import/test_mver_import.c
    tests/model_import/test_model_import_source.c
    tests/model_import/test_model_import_archive.c
    tests/model_import/test_mver_manifest.c
    tests/model_import/test_tauri_portable.c
    tests/model_import/test_mver_container.c
    tests/model_import/test_mver_missing_motion.c
    tests/model_import/test_mver_pointer_import.c
    tests/ui/test_preferences_text.c
    tests/ui/test_preferences_import.c
    tests/model_import/test_mver_nearby_identity.c
    tests/model_import/test_mver_policy.c
    tests/model_import/test_model_import_identity.c
    tests/model_import/test_slim_package.c
    tests/model_import/test_mver_support.c)
  add_executable(bongo_cat_mver_import_tests
    ${BONGO_CAT_MVER_IMPORT_TEST_SOURCES})
  target_include_directories(bongo_cat_mver_import_tests PRIVATE
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_include_directories(bongo_cat_mver_import_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR} ${BONGO_CAT_STB_INCLUDE_DIR})
  target_link_libraries(bongo_cat_mver_import_tests PRIVATE
    bongo_cat_runtime bongo_cat_archive)
  target_compile_definitions(bongo_cat_mver_import_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    set_property(SOURCE ${BONGO_CAT_MVER_IMPORT_TEST_SOURCES}
      APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
    set_property(SOURCE tests/platform/test_windows_capture.c APPEND PROPERTY
      COMPILE_OPTIONS "/experimental:c11atomics")
  endif()
  add_test(NAME model-import-unit COMMAND bongo_cat_mver_import_tests)
  add_test(NAME model-import-notice COMMAND bongo_cat_mver_import_tests
    --import-notice)

  add_executable(bongo_cat_preferences_lifecycle_tests
    tests/ui/test_preferences_lifecycle.c)
  target_include_directories(bongo_cat_preferences_lifecycle_tests PRIVATE
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_include_directories(bongo_cat_preferences_lifecycle_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_preferences_lifecycle_tests PRIVATE bongo_cat_runtime)
  if(WIN32)
    target_sources(bongo_cat_preferences_lifecycle_tests PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc")
    add_dependencies(bongo_cat_preferences_lifecycle_tests bongo_cat_asset_pack)
  else()
    add_custom_command(TARGET bongo_cat_preferences_lifecycle_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
        "$<TARGET_FILE_DIR:bongo_cat_preferences_lifecycle_tests>/assets")
  endif()
  bongo_cat_stage_cubism_assets(bongo_cat_preferences_lifecycle_tests)
  if(MSVC)
    target_compile_options(bongo_cat_preferences_lifecycle_tests PRIVATE
      /experimental:c11atomics)
  endif()
  add_test(NAME preferences-lifecycle COMMAND bongo_cat_preferences_lifecycle_tests
    --ci-smoke --ci-ignore-global-input
    "--storage-root=${CMAKE_CURRENT_BINARY_DIR}/preferences-lifecycle-data")
  set_tests_properties(preferences-lifecycle PROPERTIES
    ENVIRONMENT "BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN=1" TIMEOUT 60)
  if(APPLE)
    set_tests_properties(preferences-lifecycle PROPERTIES DISABLED TRUE)
  endif()

  if(BONGO_CAT_CUBISM_ENABLED)
    add_executable(bongo_cat_core_profile_tests tests/live2d/test_core_profile.cpp)
    target_include_directories(bongo_cat_core_profile_tests PRIVATE src/live2d)
    target_link_libraries(bongo_cat_core_profile_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME live2d-core-profile COMMAND bongo_cat_core_profile_tests)
    set_tests_properties(live2d-core-profile PROPERTIES TIMEOUT 30)

    add_executable(bongo_cat_render_resources_tests tests/live2d/test_render_resources.cpp)
    target_include_directories(bongo_cat_render_resources_tests PRIVATE src/live2d tests/support)
    target_link_libraries(bongo_cat_render_resources_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME live2d-render-resources COMMAND bongo_cat_render_resources_tests)
    set_tests_properties(live2d-render-resources PROPERTIES TIMEOUT 30)

    add_executable(bongo_cat_model_lifetime_tests tests/live2d/test_model_lifetime.cpp)
    target_include_directories(bongo_cat_model_lifetime_tests PRIVATE src/live2d)
    target_link_libraries(bongo_cat_model_lifetime_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    target_compile_definitions(bongo_cat_model_lifetime_tests PRIVATE
      BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    bongo_cat_stage_cubism_assets(bongo_cat_model_lifetime_tests)
    add_test(NAME live2d-model-lifetime COMMAND bongo_cat_model_lifetime_tests)
    set_tests_properties(live2d-model-lifetime PROPERTIES TIMEOUT 60)

    add_executable(bongo_cat_texture_sharing_tests tests/live2d/test_texture_sharing.cpp)
    target_include_directories(bongo_cat_texture_sharing_tests PRIVATE src/live2d)
    target_include_directories(bongo_cat_texture_sharing_tests SYSTEM PRIVATE
      ${BONGO_CAT_STB_INCLUDE_DIR})
    target_link_libraries(bongo_cat_texture_sharing_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    target_compile_definitions(bongo_cat_texture_sharing_tests PRIVATE
      BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    bongo_cat_stage_cubism_assets(bongo_cat_texture_sharing_tests)
    add_test(NAME live2d-texture-sharing COMMAND bongo_cat_texture_sharing_tests)
    set_tests_properties(live2d-texture-sharing PROPERTIES TIMEOUT 60)

    add_executable(bongo_cat_texture_equivalence_tests
      tests/live2d/test_texture_equivalence.cpp)
    target_link_libraries(bongo_cat_texture_equivalence_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    target_compile_definitions(bongo_cat_texture_equivalence_tests PRIVATE
      BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    bongo_cat_stage_cubism_assets(bongo_cat_texture_equivalence_tests)
    add_test(NAME live2d-texture-equivalence COMMAND bongo_cat_texture_equivalence_tests)
    set_tests_properties(live2d-texture-equivalence PROPERTIES TIMEOUT 180)

    add_test(NAME model-startup-recovery COMMAND ${CMAKE_COMMAND}
      "-DEXECUTABLE=$<TARGET_FILE:bongo_cat_preferences_lifecycle_tests>"
      "-DASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/model-startup-recovery"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckModelStartupRecovery.cmake")
    set_tests_properties(model-startup-recovery PROPERTIES TIMEOUT 60)
    if(APPLE)
      set_tests_properties(model-startup-recovery PROPERTIES DISABLED TRUE)
    endif()
    add_executable(bongo_cat_motion_state_tests
      tests/live2d/test_motion_state.cpp)
    target_include_directories(bongo_cat_motion_state_tests PRIVATE
      src/live2d tests/support)
    target_link_libraries(bongo_cat_motion_state_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME live2d-motion-state COMMAND bongo_cat_motion_state_tests)
  endif()

  if(APPLE)
    add_executable(bongo_cat_macos_click_through_tests
      tests/platform/test_macos_click_through.m)
    target_include_directories(bongo_cat_macos_click_through_tests PRIVATE
      ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
    target_link_libraries(bongo_cat_macos_click_through_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME macos-click-through COMMAND bongo_cat_macos_click_through_tests)
    set_tests_properties(macos-click-through PROPERTIES
      TIMEOUT 60 SKIP_RETURN_CODE 77)
  endif()

  if(WIN32)
    add_executable(bongo_cat_windows_presentation_tests
      tests/platform/test_windows_presentation.c
      tests/platform/test_windows_click_through.c)
    target_include_directories(bongo_cat_windows_presentation_tests PRIVATE
      src/platform/windows src/ui/rendering tests/support)
    target_link_libraries(bongo_cat_windows_presentation_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings dwmapi user32 gdi32)
    add_test(NAME windows-presentation COMMAND bongo_cat_windows_presentation_tests)
    set_tests_properties(windows-presentation PROPERTIES
      SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 60 LABELS "interactive;graphics")
    add_executable(bongo_cat_windows_gl_readback_tests
      tests/platform/test_windows_gl_readback.c)
    target_include_directories(bongo_cat_windows_gl_readback_tests PRIVATE
      src/platform/windows tests/support)
    target_link_libraries(bongo_cat_windows_gl_readback_tests PRIVATE
      SDL3::SDL3-static bongo_cat_warnings)
    add_test(NAME windows-gl-readback COMMAND bongo_cat_windows_gl_readback_tests)
    add_executable(bongo_cat_windows_input_tests
      tests/platform/test_windows_input.c
      tests/platform/test_windows_relative.c
      tests/platform/test_windows_mouse_mapping.c
      tests/platform/test_windows_pointer_detection.c
      tests/platform/test_windows_raw_receiver.c)
    if(MSVC)
      # Visual Studio can evaluate target language options as C++ with Cubism.
      set_property(SOURCE
        tests/platform/test_windows_input.c
        tests/platform/test_windows_relative.c
        tests/platform/test_windows_mouse_mapping.c
        tests/platform/test_windows_pointer_detection.c
        tests/platform/test_windows_raw_receiver.c
        APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
    endif()
    target_include_directories(bongo_cat_windows_input_tests PRIVATE
      tests/support src/platform/windows src/runtime/input)
    target_link_libraries(bongo_cat_windows_input_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings user32)
    add_test(NAME windows-input COMMAND bongo_cat_windows_input_tests)
    set_tests_properties(windows-input PROPERTIES TIMEOUT 30)

    add_executable(bongo_cat_windows_capture_tests
      tests/platform/test_windows_capture.c)
    target_include_directories(bongo_cat_windows_capture_tests PRIVATE
      src/platform/windows)
    target_link_libraries(bongo_cat_windows_capture_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME windows-capture COMMAND bongo_cat_windows_capture_tests)
  endif()

  if(UNIX AND NOT APPLE)
    target_link_libraries(bongo_cat_ui_tests PRIVATE m)
    target_link_libraries(bongo_cat_app_state_tests PRIVATE m)
    target_link_libraries(bongo_cat_gamepad_tests PRIVATE m)
  endif()

  if(APPLE)
    add_test(NAME macos-bundle-icon COMMAND ${CMAKE_COMMAND}
      "-DBUNDLE=$<TARGET_BUNDLE_DIR:bongo_cat>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckMacOSBundleIcon.cmake")
  endif()
endif()
