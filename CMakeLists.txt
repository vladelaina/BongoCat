cmake_minimum_required(VERSION 3.24)
project(BongoCat VERSION 0.1.0 DESCRIPTION "BongoCat" HOMEPAGE_URL "https://bongocat.pet" LANGUAGES C CXX)
include(FetchContent)
include(CTest)
option(BONGO_CAT_FETCH_DEPS "Download pinned open-source dependencies" ON)
option(BONGO_CAT_REQUIRE_CUBISM "Fail configuration when Cubism SDK is absent" OFF)
option(BONGO_CAT_WARNINGS_AS_ERRORS "Treat native source warnings as errors" OFF)
set(BONGO_CAT_CUBISM_SDK "${CMAKE_CURRENT_SOURCE_DIR}/vendor/CubismSdkForNative"
  CACHE PATH "Path to Cubism SDK for Native")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(cmake/ReleaseOptimize.cmake)

add_library(bongo_cat_warnings INTERFACE)
if(MSVC)
  target_compile_options(bongo_cat_warnings INTERFACE /W4 /permissive- /utf-8)
  target_compile_options(bongo_cat_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
  if(BONGO_CAT_WARNINGS_AS_ERRORS)
    target_compile_options(bongo_cat_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(bongo_cat_warnings INTERFACE -Wall -Wextra)
  if(BONGO_CAT_WARNINGS_AS_ERRORS)
    target_compile_options(bongo_cat_warnings INTERFACE -Werror)
  endif()
endif()
include(cmake/Dependencies.cmake)
find_package(OpenGL REQUIRED)
set(BONGO_CAT_LIVE2D_SOURCES src/live2d/live2d_stub.c)
set(BONGO_CAT_CUBISM_ENABLED OFF)
set(CUBISM_CORE_HEADER "${BONGO_CAT_CUBISM_SDK}/Core/include/Live2DCubismCore.h")
if(EXISTS "${CUBISM_CORE_HEADER}")
  if(WIN32 AND NOT MSVC)
    if(BONGO_CAT_REQUIRE_CUBISM)
      message(FATAL_ERROR "Cubism Core for Windows requires Visual Studio 2022")
    endif()
    message(WARNING "Cubism SDK found but MinGW is unsupported; using diagnostic backend")
  else()
    include(cmake/Cubism.cmake)
    set(BONGO_CAT_LIVE2D_SOURCES
      src/live2d/cubism_bridge.cpp src/live2d/cubism_model_load.cpp
      src/live2d/cubism_model_update.cpp src/live2d/cubism_viewer_look.cpp
      src/live2d/cubism_model_bounds.cpp
      src/live2d/cubism_model_validate.cpp)
    set(BONGO_CAT_CUBISM_ENABLED ON)
  endif()
elseif(BONGO_CAT_REQUIRE_CUBISM)
  message(FATAL_ERROR "Cubism SDK missing at ${BONGO_CAT_CUBISM_SDK}")
else()
  message(WARNING "Cubism SDK missing; building the diagnostic backend")
endif()

add_library(bongo_cat_core STATIC
  src/core/config_defaults.c src/core/config_io.c
  src/core/config_file.c src/core/config_session_io.c
  src/core/error.c
  src/core/file.c src/core/i18n.c
  src/core/input_state.c src/core/mver_pointer.c
  src/core/mouse_tracking.c src/core/sha256.c src/core/utf8.c
  src/core/model_catalog.c
  src/core/model_behavior.c
  src/core/path.c src/core/path_io.c src/core/json.c)
target_sources(bongo_cat_core PRIVATE src/core/shortcut.c)
target_include_directories(bongo_cat_core PUBLIC include)
target_link_libraries(bongo_cat_core PUBLIC yyjson PRIVATE bongo_cat_warnings)
include(cmake/CubismAssets.cmake)
if(WIN32)
  target_sources(bongo_cat_core PRIVATE src/core/windows_utf8.c)
endif()

add_library(bongo_cat_runtime STATIC
  src/core/app_state.c
  src/media/audio.c
  src/media/image.c src/media/image_resize.c src/media/image_alpha.c
  src/media/stb_image_impl.c
  src/platform/memory.c src/render/gl_api.c
  src/render/overlay.c src/render/mver_pointer_overlay.c src/render/mver_pointer_overlay_draw.c
  src/runtime/app.c src/runtime/config_store.c
  src/runtime/assets.c src/runtime/startup.c
  src/runtime/frame_audit.c src/runtime/frame_clock.c src/runtime/modal_frame.c
  src/runtime/gamepad.c
  src/runtime/live2d_audit.c src/runtime/live2d_visual_audit.c
  src/runtime/live2d_viewer_audit.c
  src/runtime/memory_policy.c src/runtime/mouse.c src/runtime/mouse_mapping.c
  src/runtime/model_files.c src/runtime/model_update.c
  src/runtime/model_storage.c
  src/runtime/model_import.c
  src/runtime/model_import_package.c
  src/runtime/model_import_discovery.c
  src/runtime/model_import_format.c
  src/runtime/model_import_portable.c src/runtime/model_import_portable_scan.c
  src/runtime/model_import_mver_patch.c
  src/runtime/model_import_mver.c src/runtime/model_import_mver_image.c
  src/runtime/model_import_mver_shortcut.c src/runtime/model_import_mver_effect.c
  src/runtime/model_import_mver_motion.c src/runtime/model_import_mver_labels.c src/runtime/model_import_report.c
  src/runtime/model_import_mver_metadata.c src/runtime/model_import_metadata.c
  src/runtime/runtime_flow.c
  src/runtime/shortcuts.c
  src/runtime/tray.c
  src/runtime/window.c src/runtime/window_display.c src/runtime/window_drag.c
  src/runtime/window_geometry.c
  src/runtime/window_menu_behavior.c src/runtime/window_menu_preview.c
  src/runtime/window_hit.c src/runtime/window_wheel.c
  src/ui/nuklear_impl.c
  src/ui/ui_backend.c src/ui/ui_resize_cache.c src/ui/ui_scale.c src/ui/ui_cursor.c
  src/ui/ui_animation.c src/ui/ui_paint.c src/ui/ui_paint_cache.c src/ui/ui_paint_border.c src/ui/ui_paint_shell.c
  src/ui/ui_font.c src/ui/ui_font_atlas.c src/ui/ui_font_atlas_sources.c src/ui/ui_font_atlas_ranges.c src/ui/ui_font_atlas_upload.c src/ui/ui_font_reload.c
  src/ui/ui_input.c
  src/ui/ui_catime.c src/ui/ui_catime_tabs.c src/ui/ui_catime_theme.c src/ui/ui_catime_icons.c
  src/ui/ui_native_theme.c
  src/ui/preferences.c src/ui/preferences_scale.c src/ui/preferences_render.c src/ui/preferences_language.c
  src/ui/preferences_assets.c src/ui/preferences_about.c src/ui/preferences_about_community.c
  src/ui/preferences_live_resize.c
  src/ui/preferences_dialog.c src/ui/preferences_import.c src/ui/preferences_overlay.c
  src/ui/preferences_behavior_dialog.c src/ui/preferences_behavior_rename.c src/ui/preferences_behavior_row.c
  src/ui/preferences_notice.c
  src/ui/preferences_model.c src/ui/preferences_model_cover.c src/ui/preferences_model_glyphs.c
  src/ui/preferences_pages.c src/ui/preferences_theme.c
  src/ui/preferences_shortcuts.c src/ui/preferences_shortcut_clear.c src/ui/preferences_smoke.c
  src/ui/preferences_widgets.c src/ui/preferences_controls.c src/ui/preferences_combo.c src/ui/preferences_toggle.c
  ${BONGO_CAT_LIVE2D_SOURCES})
target_include_directories(bongo_cat_runtime PUBLIC include)
target_include_directories(bongo_cat_runtime SYSTEM PRIVATE
  ${BONGO_CAT_STB_INCLUDE_DIR} ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
target_link_libraries(bongo_cat_runtime PRIVATE bongo_cat_warnings)
target_link_libraries(bongo_cat_runtime PUBLIC bongo_cat_core SDL3::SDL3-static OpenGL::GL ${BONGO_CAT_MINIAUDIO_TARGET})
if(MSVC)
  set_source_files_properties(src/media/stb_image_impl.c src/media/image_resize.c src/ui/nuklear_impl.c
    PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(src/media/stb_image_impl.c src/media/image_resize.c src/ui/nuklear_impl.c
    PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(BONGO_CAT_CUBISM_ENABLED)
  target_link_libraries(bongo_cat_runtime PUBLIC Framework Live2DCubismCore glew_s)
  target_compile_definitions(bongo_cat_runtime PUBLIC BONGO_CAT_HAS_CUBISM=1)
endif()
target_compile_definitions(bongo_cat_runtime PRIVATE
  BONGO_CAT_DEV_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
  BONGO_CAT_DEV_LOCALE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/locales")

if(WIN32)
  enable_language(RC)
  target_sources(bongo_cat_runtime PRIVATE src/platform/windows.c src/platform/windows_menu.c src/platform/windows_directory.c
    src/platform/windows_startup.c src/platform/windows_borderless.c src/platform/windows_capture.c
    src/platform/windows_pointer.c src/platform/windows_direct_input.c src/platform/windows_input.c src/platform/windows_popup.c
    src/platform/windows_layered.c src/platform/windows_tray.c
    src/platform/windows_assets.c)
  target_link_libraries(bongo_cat_runtime PRIVATE dinput8 dxguid dwmapi ole32 shell32 user32 uuid windowscodecs)
elseif(APPLE)
  enable_language(OBJC)
  target_sources(bongo_cat_runtime PRIVATE src/platform/macos.m src/platform/macos_input.m
    src/platform/macos_keys.m src/platform/macos_menu.m)
  target_link_libraries(bongo_cat_runtime PRIVATE "-framework Cocoa"
    "-framework ApplicationServices")
else()
  find_package(X11 REQUIRED)
  if(NOT TARGET X11::Xi OR NOT TARGET X11::Xfixes)
    message(FATAL_ERROR "Linux native build requires XInput2 and Xfixes development packages")
  endif()
  target_sources(bongo_cat_runtime PRIVATE src/platform/linux.c src/platform/linux_menu.c
    src/platform/linux_x11.c)
  target_link_libraries(bongo_cat_runtime PRIVATE X11::X11 X11::Xi X11::Xfixes m)
  target_link_libraries(bongo_cat_core PRIVATE m)
endif()

add_executable(bongo_cat src/main.c)
target_link_libraries(bongo_cat PRIVATE bongo_cat_runtime bongo_cat_warnings)
set_target_properties(bongo_cat PROPERTIES OUTPUT_NAME "BongoCat")
bongo_cat_enable_release_ipo(Framework bongo_cat_core bongo_cat_runtime bongo_cat)

if(WIN32)
  add_library(bongo_cat_tool_support STATIC EXCLUDE_FROM_ALL src/tools/windows_tool.c)
  target_include_directories(bongo_cat_tool_support PUBLIC src/tools)
  target_link_libraries(bongo_cat_tool_support PUBLIC bongo_cat_warnings)

  add_executable(cubism_viewer_desktop_capture EXCLUDE_FROM_ALL
    src/tools/cubism_viewer_desktop_capture.c)
  target_compile_definitions(cubism_viewer_desktop_capture PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(cubism_viewer_desktop_capture PRIVATE
    bongo_cat_tool_support bongo_cat_warnings d3d11 dxgi dxguid user32)

  add_library(bongo_cat_validation_image STATIC EXCLUDE_FROM_ALL
    src/tools/validation_image.c src/tools/validation_gdiplus.c)
  target_include_directories(bongo_cat_validation_image PUBLIC src/tools)
  target_link_libraries(bongo_cat_validation_image PUBLIC
    bongo_cat_warnings windowscodecs gdiplus ole32)

  add_executable(cubism_viewer_drag_capture EXCLUDE_FROM_ALL
    src/tools/cubism_viewer_drag_capture.c)
  target_compile_definitions(cubism_viewer_drag_capture PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(cubism_viewer_drag_capture PRIVATE
    bongo_cat_validation_image bongo_cat_tool_support bongo_cat_warnings
    user32 gdi32 ole32)

  add_executable(cubism_viewer_blind_test EXCLUDE_FROM_ALL
    src/tools/cubism_viewer_blind_test.c)
  target_compile_definitions(cubism_viewer_blind_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(cubism_viewer_blind_test PRIVATE
    bongo_cat_validation_image bongo_cat_tool_support bongo_cat_warnings ole32)

  add_executable(mver_blind_metrics EXCLUDE_FROM_ALL
    src/tools/mver_blind_metrics.c)
  target_compile_definitions(mver_blind_metrics PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(mver_blind_metrics PRIVATE
    bongo_cat_validation_image bongo_cat_warnings ole32)

  add_executable(mver_phase_metrics EXCLUDE_FROM_ALL
    src/tools/mver_phase_metrics.c)
  target_compile_definitions(mver_phase_metrics PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(mver_phase_metrics PRIVATE
    bongo_cat_validation_image bongo_cat_warnings ole32)

  add_custom_target(bongo_cat_validation_tools
    DEPENDS cubism_viewer_desktop_capture cubism_viewer_drag_capture
      cubism_viewer_blind_test mver_blind_metrics mver_phase_metrics)
endif()
if(APPLE)
  set_source_files_properties(resources/icons/icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  target_sources(bongo_cat PRIVATE resources/icons/icon.icns)
  set_target_properties(bongo_cat PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "BongoCat"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.bongocat.desktop
    MACOSX_BUNDLE_ICON_FILE icon.icns
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
  add_custom_command(TARGET bongo_cat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_BUNDLE_CONTENT_DIR:bongo_cat>/MacOS/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
      "$<TARGET_BUNDLE_CONTENT_DIR:bongo_cat>/MacOS/assets"
    VERBATIM)
endif()

if(WIN32)
  target_sources(bongo_cat_runtime PRIVATE src/platform/windows_keys.c)
  add_executable(bongo_cat_asset_packer src/tools/asset_packer.c)
  target_compile_definitions(bongo_cat_asset_packer PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(bongo_cat_asset_packer PRIVATE bongo_cat_warnings)
  set(BONGO_CAT_ASSET_STAGE "${CMAKE_CURRENT_BINARY_DIR}/embedded-assets")
  set(BONGO_CAT_ASSET_PACK "${CMAKE_CURRENT_BINARY_DIR}/bongo-cat-assets.pak")
  file(GLOB_RECURSE BONGO_CAT_ASSET_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/*")
  bongo_cat_configure_embedded_cubism_assets()
  add_custom_command(OUTPUT "${BONGO_CAT_ASSET_PACK}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${BONGO_CAT_ASSET_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BONGO_CAT_ASSET_STAGE}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets" "${BONGO_CAT_ASSET_STAGE}/assets"
    ${BONGO_CAT_ASSET_EXTRA_COMMANDS}
    COMMAND "$<TARGET_FILE:bongo_cat_asset_packer>"
      "${BONGO_CAT_ASSET_STAGE}/assets" "${BONGO_CAT_ASSET_PACK}"
    DEPENDS ${BONGO_CAT_ASSET_INPUTS} bongo_cat_asset_packer
    VERBATIM)
  add_custom_target(bongo_cat_asset_pack DEPENDS "${BONGO_CAT_ASSET_PACK}")
  file(TO_CMAKE_PATH "${BONGO_CAT_ASSET_PACK}" BONGO_CAT_ASSET_PACK_RC)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico" BONGO_CAT_ICON_RC)
  set(BONGO_CAT_WINDOWS_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows.manifest")
  if(MSVC)
    set(BONGO_CAT_MANIFEST_RESOURCE "")
    target_link_options(bongo_cat PRIVATE "/MANIFEST:EMBED"
      "/MANIFESTINPUT:${BONGO_CAT_WINDOWS_MANIFEST}")
  else()
    file(TO_CMAKE_PATH "${BONGO_CAT_WINDOWS_MANIFEST}" BONGO_CAT_MANIFEST_RC)
    set(BONGO_CAT_MANIFEST_RESOURCE "1 RT_MANIFEST \"${BONGO_CAT_MANIFEST_RC}\"")
  endif()
  configure_file(cmake/windows_resources.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc" @ONLY)
  set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc"
    APPEND PROPERTY OBJECT_DEPENDS "${BONGO_CAT_ASSET_PACK}")
  target_sources(bongo_cat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc")
  add_dependencies(bongo_cat bongo_cat_asset_pack)
  set_target_properties(bongo_cat PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(MSVC)
  set_property(SOURCE src/main.c APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
  get_target_property(BONGO_CAT_RUNTIME_SOURCES bongo_cat_runtime SOURCES)
  foreach(BONGO_CAT_SOURCE IN LISTS BONGO_CAT_RUNTIME_SOURCES)
    if(BONGO_CAT_SOURCE MATCHES "\\.c$")
      set_property(SOURCE "${BONGO_CAT_SOURCE}" APPEND PROPERTY
        COMPILE_OPTIONS "/experimental:c11atomics")
    endif()
  endforeach()
endif()

bongo_cat_stage_cubism_assets(bongo_cat)
add_custom_target(check-lines ALL
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLines.cmake
  VERBATIM)
include(cmake/ShortcutTests.cmake)
if(BUILD_TESTING)
  add_executable(bongo_cat_core_tests tests/test_main.c tests/test_config.c tests/test_input.c tests/test_models.c tests/test_mver_pointer.c tests/test_shortcut.c src/platform/windows_keys.c)
  target_link_libraries(bongo_cat_core_tests PRIVATE bongo_cat_core bongo_cat_warnings $<$<PLATFORM_ID:Windows>:user32>)
  target_compile_definitions(bongo_cat_core_tests PRIVATE BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME core COMMAND bongo_cat_core_tests)
  add_executable(bongo_cat_i18n_tests tests/test_i18n.c)
  target_link_libraries(bongo_cat_i18n_tests PRIVATE bongo_cat_core bongo_cat_warnings)
  target_compile_definitions(bongo_cat_i18n_tests PRIVATE BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME i18n COMMAND bongo_cat_i18n_tests)
  add_executable(bongo_cat_ui_tests tests/test_nuklear.c src/ui/nuklear_impl.c src/ui/ui_paint_border.c)
  target_include_directories(bongo_cat_ui_tests SYSTEM PRIVATE ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_ui_tests PRIVATE bongo_cat_warnings)
  add_test(NAME ui COMMAND bongo_cat_ui_tests)
  add_executable(bongo_cat_app_state_tests tests/test_app_state.c src/core/app_state.c)
  target_link_libraries(bongo_cat_app_state_tests PRIVATE bongo_cat_warnings)
  target_include_directories(bongo_cat_app_state_tests PRIVATE include)
  add_test(NAME app-state COMMAND bongo_cat_app_state_tests)
  add_executable(bongo_cat_mver_import_tests tests/test_mver_import.c tests/test_mver_pointer_import.c)
  target_include_directories(bongo_cat_mver_import_tests PRIVATE src/runtime src/ui)
  target_include_directories(bongo_cat_mver_import_tests SYSTEM PRIVATE ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_mver_import_tests PRIVATE bongo_cat_runtime)
  target_compile_definitions(bongo_cat_mver_import_tests PRIVATE BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    set_property(SOURCE tests/test_mver_import.c tests/test_windows_capture.c APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
  endif()
  add_test(NAME model-import-unit COMMAND bongo_cat_mver_import_tests)
  if(WIN32)
    add_executable(bongo_cat_windows_capture_tests tests/test_windows_capture.c)
    target_include_directories(bongo_cat_windows_capture_tests PRIVATE src/platform)
    target_link_libraries(bongo_cat_windows_capture_tests PRIVATE bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME windows-capture COMMAND bongo_cat_windows_capture_tests)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(bongo_cat_ui_tests PRIVATE m)
    target_link_libraries(bongo_cat_app_state_tests PRIVATE m)
  endif()
endif()
include(cmake/Packaging.cmake)
