# The live /co SVG embeds PNG, JPEG and WebP avatars. Decode all in memory
# with C libraries; only the decoder is linked, no command-line tools installed.
if(BONGO_CAT_FETCH_DEPS)
  # Keep the decoder static without changing the parent's library policy.
  function(bongo_cat_add_about_webp)
    if(POLICY CMP0168)
      # Download directly on CMake 3.30+, without an MSBuild sub-project.
      cmake_policy(SET CMP0168 NEW)
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
    set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(about_webp
      URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
      URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      # Populate only; manually add with EXCLUDE_FROM_ALL for CMake 3.24 too.
      SOURCE_SUBDIR bongo-download-only)
    FetchContent_MakeAvailable(about_webp)
    if(NOT TARGET webpdecoder)
      add_subdirectory("${about_webp_SOURCE_DIR}" "${about_webp_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
    target_include_directories(bongo_cat_runtime SYSTEM PRIVATE "${about_webp_SOURCE_DIR}/src")
    target_link_libraries(bongo_cat_runtime PRIVATE webpdecoder)
  endfunction()
  bongo_cat_add_about_webp()
else()
  find_path(BONGO_CAT_WEBP_INCLUDE_DIR webp/decode.h REQUIRED)
  find_library(BONGO_CAT_WEBP_LIBRARY NAMES webpdecoder webp REQUIRED)
  target_include_directories(bongo_cat_runtime SYSTEM PRIVATE "${BONGO_CAT_WEBP_INCLUDE_DIR}")
  target_link_libraries(bongo_cat_runtime PRIVATE "${BONGO_CAT_WEBP_LIBRARY}")
endif()
