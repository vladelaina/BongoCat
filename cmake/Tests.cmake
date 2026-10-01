if(BUILD_TESTING)
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
    add_executable(bongo_cat_linux_window_tests tests/platform/test_linux_window.c)
    target_include_directories(bongo_cat_linux_window_tests PRIVATE
      ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS} tests/support)
    target_link_libraries(bongo_cat_linux_window_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
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
    src/runtime/model/model_behavior_state.c)
  target_link_libraries(bongo_cat_app_state_tests PRIVATE bongo_cat_warnings)
  target_include_directories(bongo_cat_app_state_tests PRIVATE
    "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include tests/support)
  add_test(NAME app-state COMMAND bongo_cat_app_state_tests)

  set(BONGO_CAT_MVER_IMPORT_TEST_SOURCES
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
    target_link_libraries(bongo_cat_macos_click_through_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME macos-click-through COMMAND bongo_cat_macos_click_through_tests)
    set_tests_properties(macos-click-through PROPERTIES
      TIMEOUT 60 SKIP_RETURN_CODE 77)
  endif()

  if(WIN32)
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
  endif()

  if(APPLE)
    add_test(NAME macos-bundle-icon COMMAND ${CMAKE_COMMAND}
      "-DBUNDLE=$<TARGET_BUNDLE_DIR:bongo_cat>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckMacOSBundleIcon.cmake")
  endif()
endif()
