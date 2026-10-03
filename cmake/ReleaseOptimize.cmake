option(BONGO_CAT_OPTIMIZE_RELEASE_SIZE
  "Enable conservative size and dead-code optimization for Release and MinSizeRel builds" ON)
option(BONGO_CAT_OPTIMIZE_RELEASE_IPO
  "Enable compiler link-time optimization for native project targets" ON)

include(CheckIPOSupported)
if(BONGO_CAT_OPTIMIZE_RELEASE_IPO)
  set(BONGO_CAT_TRY_COMPILE_CONFIGURATION "${CMAKE_TRY_COMPILE_CONFIGURATION}")
  set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  check_ipo_supported(RESULT BONGO_CAT_IPO_SUPPORTED OUTPUT BONGO_CAT_IPO_ERROR
    LANGUAGES C CXX)
  set(CMAKE_TRY_COMPILE_CONFIGURATION "${BONGO_CAT_TRY_COMPILE_CONFIGURATION}")
  if(NOT BONGO_CAT_IPO_SUPPORTED)
    message(STATUS "Native IPO unavailable; continuing without it: ${BONGO_CAT_IPO_ERROR}")
  endif()
endif()

function(bongo_cat_enable_release_ipo)
  foreach(target IN LISTS ARGN)
    if(BONGO_CAT_OPTIMIZE_RELEASE_IPO AND BONGO_CAT_IPO_SUPPORTED AND
        NOT MINGW AND TARGET "${target}")
      set_property(TARGET "${target}" PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
      set_property(TARGET "${target}" PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endif()
  endforeach()
endfunction()

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Keep the Windows loader's control-flow enforcement enabled in release
# binaries as well as the compiler-side indirect-call instrumentation.
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
  add_compile_options($<$<CONFIG:Release,MinSizeRel>:/guard:cf>)
  add_link_options($<$<CONFIG:Release,MinSizeRel>:/guard:cf>)
endif()

# Apply before FetchContent so static dependencies use the same settings. LTO
# is enabled only for the explicitly selected targets above; prebuilt and
# other third-party archives keep their existing LTO settings.
if(BONGO_CAT_OPTIMIZE_RELEASE_SIZE)
  if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    # Omit unused inline COMDATs and incremental-link padding. Keep the
    # existing runtime, exception handling and floating-point semantics.
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:/O1>
      $<$<CONFIG:Release,MinSizeRel>:/Gy> $<$<CONFIG:Release,MinSizeRel>:/Gw>
      $<$<CONFIG:Release,MinSizeRel>:/Zc:inline>)
    add_link_options($<$<CONFIG:Release,MinSizeRel>:/OPT:REF>
      $<$<CONFIG:Release,MinSizeRel>:/OPT:ICF>
      $<$<CONFIG:Release,MinSizeRel>:/INCREMENTAL:NO>)
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    add_compile_options($<$<CONFIG:Release,MinSizeRel>:-Os>
      $<$<CONFIG:Release,MinSizeRel>:-ffunction-sections>
      $<$<CONFIG:Release,MinSizeRel>:-fdata-sections>)
    if(APPLE)
      add_link_options($<$<CONFIG:Release,MinSizeRel>:-Wl,-dead_strip>)
    else()
      add_link_options($<$<CONFIG:Release,MinSizeRel>:-Wl,--gc-sections>
        $<$<CONFIG:Release,MinSizeRel>:-s>)
    endif()
  endif()
endif()
