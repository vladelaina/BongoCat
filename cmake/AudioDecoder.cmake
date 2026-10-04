# Own the decoder translation unit so WAV, FLAC and Ogg/Vorbis work equally
# with downloaded dependencies and system packages. No extra codec DLLs.
if(EXISTS "${BONGO_CAT_STB_INCLUDE_DIR}/stb_vorbis.c")
  set(BONGO_CAT_VORBIS_HEADER "stb_vorbis.c")
elseif(EXISTS "${BONGO_CAT_STB_INCLUDE_DIR}/stb_vorbis.h")
  set(BONGO_CAT_VORBIS_HEADER "stb_vorbis.h")
else()
  message(FATAL_ERROR "Install the complete stb headers, including stb_vorbis.c or stb_vorbis.h")
endif()
if(BONGO_CAT_FETCH_DEPS)
  set_property(TARGET miniaudio PROPERTY SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/media/audio/miniaudio_impl.c")
  target_include_directories(miniaudio SYSTEM PRIVATE "${BONGO_CAT_STB_INCLUDE_DIR}")
else()
  add_library(bongo_cat_audio_decoder STATIC src/media/audio/miniaudio_impl.c)
  if(BONGO_CAT_MINIAUDIO_INCLUDE_DIR)
    target_include_directories(bongo_cat_audio_decoder SYSTEM PUBLIC
      "${BONGO_CAT_MINIAUDIO_INCLUDE_DIR}")
  else()
    target_include_directories(bongo_cat_audio_decoder SYSTEM PUBLIC
      "$<TARGET_PROPERTY:${BONGO_CAT_MINIAUDIO_TARGET},INTERFACE_INCLUDE_DIRECTORIES>")
  endif()
  target_include_directories(bongo_cat_audio_decoder SYSTEM PRIVATE
    "${BONGO_CAT_STB_INCLUDE_DIR}")
  find_package(Threads REQUIRED)
  target_link_libraries(bongo_cat_audio_decoder PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  if(UNIX)
    target_link_libraries(bongo_cat_audio_decoder PRIVATE m)
  endif()
  set(BONGO_CAT_MINIAUDIO_TARGET bongo_cat_audio_decoder)
endif()
target_compile_definitions(${BONGO_CAT_MINIAUDIO_TARGET} PUBLIC
  MA_NO_ENCODING MA_NO_GENERATION MA_NO_CUSTOM)
target_compile_definitions(${BONGO_CAT_MINIAUDIO_TARGET} PRIVATE
  BONGO_CAT_VORBIS_HEADER="${BONGO_CAT_VORBIS_HEADER}")
