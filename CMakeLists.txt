cmake_minimum_required(VERSION 3.24)
set(BONGO_CAT_VERSION "1.4.1")
project(BongoCat VERSION ${BONGO_CAT_VERSION} DESCRIPTION "BongoCat" HOMEPAGE_URL "https://github.com/vladelaina/BongoCat" LANGUAGES C CXX)
include(cmake/Version.cmake)
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
      src/live2d/cubism_bridge.cpp src/live2d/cubism_bridge_load.cpp
      src/live2d/cubism_bridge_visual.cpp
      src/live2d/cubism_expression.cpp
      src/live2d/cubism_model_load.cpp
      src/live2d/cubism_model_renderer.cpp
      src/live2d/cubism_model_idle.cpp
      src/live2d/cubism_model_update.cpp
      src/live2d/cubism_parameter_overrides.cpp
      src/live2d/cubism_motion_playback.cpp
      src/live2d/cubism_motion_preview.cpp
      src/live2d/cubism_motion_selection.cpp
      src/live2d/cubism_motion_state.cpp src/live2d/cubism_viewer_look.cpp
      src/live2d/cubism_model_bounds.cpp
      src/live2d/cubism_model_viewport.cpp
      src/live2d/cubism_model_validate.cpp)
    set(BONGO_CAT_CUBISM_ENABLED ON)
  endif()
elseif(BONGO_CAT_REQUIRE_CUBISM)
  message(FATAL_ERROR "Cubism SDK missing at ${BONGO_CAT_CUBISM_SDK}")
else()
  message(WARNING "Cubism SDK missing; building the diagnostic backend")
endif()
add_library(bongo_cat_core STATIC
  src/core/config_defaults.c src/core/config_model_state.c src/core/config_session_defaults.c src/core/config_names.c src/core/config_fields.c src/core/config_io.c src/core/config_settings_save.c
  src/core/config_file.c src/core/config_session_io.c
  src/core/error.c
  src/core/file.c src/core/i18n.c
  src/core/input_state.c src/core/mver_pointer.c
  src/core/mouse_tracking.c src/core/sha256.c src/core/utf8.c
  src/core/model_catalog.c
  src/core/model_behavior.c
  src/core/path.c src/core/path_io.c src/core/json.c src/core/shortcut.c
  src/core/update_release.c src/core/update_version.c)
target_include_directories(bongo_cat_core PUBLIC "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include)
target_link_libraries(bongo_cat_core PUBLIC yyjson PRIVATE bongo_cat_warnings)
include(cmake/CubismAssets.cmake)
if(WIN32)
  target_sources(bongo_cat_core PRIVATE
    src/platform/windows/windows_utf8.c)
  target_include_directories(bongo_cat_core PRIVATE
    src/platform/windows)
endif()

include(cmake/RuntimeSources.cmake)
add_library(bongo_cat_runtime STATIC
  ${BONGO_CAT_RUNTIME_SOURCES} ${BONGO_CAT_LIVE2D_SOURCES})
target_include_directories(bongo_cat_runtime PUBLIC "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include)
target_include_directories(bongo_cat_runtime PRIVATE
  ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
target_include_directories(bongo_cat_runtime SYSTEM PRIVATE
  ${BONGO_CAT_STB_INCLUDE_DIR} ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
target_link_libraries(bongo_cat_runtime PRIVATE bongo_cat_warnings)
target_link_libraries(bongo_cat_runtime PUBLIC bongo_cat_core SDL3::SDL3-static OpenGL::GL ${BONGO_CAT_MINIAUDIO_TARGET})
if(MSVC)
  set_source_files_properties(src/media/stb_image_impl.c src/media/image_resize.c src/ui/backend/nuklear_impl.c
    PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(src/media/stb_image_impl.c src/media/image_resize.c src/ui/backend/nuklear_impl.c
    PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(BONGO_CAT_CUBISM_ENABLED)
  target_link_libraries(bongo_cat_runtime PUBLIC Framework Live2DCubismCore glew_s)
  target_compile_definitions(bongo_cat_runtime PUBLIC BONGO_CAT_HAS_CUBISM=1)
endif()
target_compile_definitions(bongo_cat_runtime PRIVATE
  BONGO_CAT_DEV_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
  BONGO_CAT_DEV_LOCALE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/locales")

include(cmake/PlatformRuntime.cmake)

add_executable(bongo_cat src/main.c)
target_link_libraries(bongo_cat PRIVATE bongo_cat_runtime bongo_cat_warnings)
set_target_properties(bongo_cat PROPERTIES OUTPUT_NAME "BongoCat")
bongo_cat_enable_release_ipo(Framework bongo_cat_core bongo_cat_runtime bongo_cat)

include(cmake/ValidationTools.cmake)
include(cmake/ApplicationResources.cmake)
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
add_custom_target(check-localization ALL
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLocalization.cmake
  VERBATIM)
add_custom_target(check-platform-runtime-safety ALL
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPlatformRuntimeSafety.cmake
  VERBATIM)
add_custom_target(check-cubism-user-model-safety ALL
  COMMAND ${CMAKE_COMMAND}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCubismUserModelSafety.cmake
  VERBATIM)
include(cmake/Tests.cmake)
include(cmake/Packaging.cmake)
