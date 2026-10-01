set(BONGO_CAT_STB_INCLUDE_DIR "" CACHE PATH
  "Directory containing stb_image.h and stb_image_write.h")
set(BONGO_CAT_MINIAUDIO_INCLUDE_DIR "" CACHE PATH
  "Directory containing miniaudio.h (used when no miniaudio CMake package exists)")
set(BONGO_CAT_NUKLEAR_INCLUDE_DIR "" CACHE PATH
  "Directory containing nuklear.h")

function(bongo_cat_require_dependency_header variable header guidance)
  if(NOT EXISTS "${${variable}}/${header}")
    message(FATAL_ERROR
      "BONGO_CAT_FETCH_DEPS=OFF requires ${header}. ${guidance} "
      "or set -D${variable}=<directory containing ${header}>.")
  endif()
endfunction()

if(BONGO_CAT_FETCH_DEPS)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  # bongo_cat creates desktop OpenGL contexts and does not use SDL's alternate
  # graphics or software video backends. Keep the platform video driver and
  # dummy fallback enabled for diagnostics, while omitting unused APIs.
  set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
  set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
  set(SDL_METAL OFF CACHE BOOL "" FORCE)
  set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
  set(SDL_VIRTUAL_JOYSTICK OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
  set(SDL_POWER OFF CACHE BOOL "" FORCE)
  set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
  if(WIN32)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
  endif()
  set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "" FORCE)
  set(YYJSON_DISABLE_UTILS ON CACHE BOOL "" FORCE)
  set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
  set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)
  set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
  set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_ENCODING ON CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_CUSTOM ON CACHE BOOL "" FORCE)
  # Motion metadata may reference WAV/MP3 files, so keep both decoders enabled.
  # FORCE also repairs build trees created by older size-only configurations.
  set(MINIAUDIO_NO_WAV OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_FLAC OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_MP3 OFF CACHE BOOL "" FORCE)
  set(MINIAUDIO_NO_GENERATION ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/archive/402fc52af4e731184ad6a704068b5ccd27d8f1b8.tar.gz
    URL_HASH SHA256=e413151af71c23d316b6076a96a999342142afa792394eaf8a542a03503fc491)
  FetchContent_Declare(yyjson URL https://github.com/ibireme/yyjson/archive/ac8f6074e1fbc43ec496aa1404b460d08b55d7a5.tar.gz
    URL_HASH SHA256=bfc16e407ddb303c98e333920d5e01386afa42a15e0a750251342e04b074e736)
  FetchContent_Declare(stb URL https://github.com/nothings/stb/archive/31c1ad37456438565541f4919958214b6e762fb4.tar.gz
    URL_HASH SHA256=e4e3bba9c572a4a4148373a914d88ea0f0d11de8cc2c66739926e7eca0223319)
  FetchContent_Declare(miniaudio URL https://github.com/mackron/miniaudio/archive/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d.tar.gz
    URL_HASH SHA256=1a3a79b80fc6f0b0cc155e28b954a598e0ddfa2db64e2afa8466be88c476fa55)
  FetchContent_Declare(nuklear URL https://github.com/Immediate-Mode-UI/Nuklear/archive/8109cfbabe04f8705408c5d8ab1a6cd48649ccda.tar.gz
    URL_HASH SHA256=23e5e1b12e897f1d568eb703aa313b7224c9b75e1118764ceba477d13b8e39f4)
  FetchContent_MakeAvailable(SDL3 yyjson stb miniaudio nuklear)
  if(WIN32)
    include("${CMAKE_CURRENT_LIST_DIR}/SDLWindowsRuntime.cmake")
    bongo_cat_trim_sdl_windows(SDL3-static "${sdl3_SOURCE_DIR}")
  endif()
  set(BONGO_CAT_STB_INCLUDE_DIR "${stb_SOURCE_DIR}")
  set(BONGO_CAT_MINIAUDIO_INCLUDE_DIR "${miniaudio_SOURCE_DIR}")
  set(BONGO_CAT_NUKLEAR_INCLUDE_DIR "${nuklear_SOURCE_DIR}")
  set(BONGO_CAT_MINIAUDIO_TARGET miniaudio)
else()
  find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-static)
  find_package(yyjson CONFIG REQUIRED)

  if(NOT TARGET yyjson AND TARGET yyjson::yyjson)
    add_library(yyjson ALIAS yyjson::yyjson)
  elseif(NOT TARGET yyjson)
    message(FATAL_ERROR
      "The yyjson package was found but provides neither yyjson nor yyjson::yyjson")
  endif()

  if(NOT BONGO_CAT_STB_INCLUDE_DIR)
    find_path(BONGO_CAT_STB_INCLUDE_DIR NAMES stb_image.h PATH_SUFFIXES stb)
  endif()
  bongo_cat_require_dependency_header(BONGO_CAT_STB_INCLUDE_DIR stb_image.h
    "Install the stb development headers")
  bongo_cat_require_dependency_header(BONGO_CAT_STB_INCLUDE_DIR stb_image_write.h
    "Install the complete stb development headers")

  if(NOT BONGO_CAT_NUKLEAR_INCLUDE_DIR)
    find_path(BONGO_CAT_NUKLEAR_INCLUDE_DIR NAMES nuklear.h)
  endif()
  bongo_cat_require_dependency_header(BONGO_CAT_NUKLEAR_INCLUDE_DIR nuklear.h
    "Install the Nuklear development header")

  if(NOT BONGO_CAT_MINIAUDIO_INCLUDE_DIR)
    find_package(miniaudio CONFIG QUIET)
  endif()
  if(TARGET miniaudio)
    set(BONGO_CAT_MINIAUDIO_TARGET miniaudio)
  elseif(TARGET miniaudio::miniaudio)
    set(BONGO_CAT_MINIAUDIO_TARGET miniaudio::miniaudio)
  elseif(TARGET unofficial::miniaudio::miniaudio)
    set(BONGO_CAT_MINIAUDIO_TARGET unofficial::miniaudio::miniaudio)
  else()
    if(NOT BONGO_CAT_MINIAUDIO_INCLUDE_DIR)
      find_path(BONGO_CAT_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h
        PATH_SUFFIXES miniaudio)
    endif()
    bongo_cat_require_dependency_header(BONGO_CAT_MINIAUDIO_INCLUDE_DIR miniaudio.h
      "Install miniaudio headers or a miniaudio CMake package")

    # AudioDecoder.cmake supplies the implementation and the Vorbis backend.
    add_library(bongo_cat_miniaudio_system INTERFACE)
    target_include_directories(bongo_cat_miniaudio_system SYSTEM INTERFACE
      "${BONGO_CAT_MINIAUDIO_INCLUDE_DIR}")
    set(BONGO_CAT_MINIAUDIO_TARGET bongo_cat_miniaudio_system)
  endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Archive.cmake")
