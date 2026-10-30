cmake_policy(SET CMP0007 OLD)

if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE FILES
  "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
  "${ROOT}/src/*.cxx" "${ROOT}/src/*.m" "${ROOT}/src/*.mm"
  "${ROOT}/src/*.h" "${ROOT}/src/*.hh" "${ROOT}/src/*.hpp" "${ROOT}/src/*.hxx"
  "${ROOT}/include/*.h" "${ROOT}/include/*.hh"
  "${ROOT}/include/*.hpp" "${ROOT}/include/*.hxx"
  "${ROOT}/tests/*.c" "${ROOT}/tests/*.cc" "${ROOT}/tests/*.cpp"
  "${ROOT}/tests/*.cxx" "${ROOT}/tests/*.m" "${ROOT}/tests/*.mm"
  "${ROOT}/tests/*.h" "${ROOT}/tests/*.hh"
  "${ROOT}/tests/*.hpp" "${ROOT}/tests/*.hxx"
  "${ROOT}/cmake/*.cmake")
list(APPEND FILES "${ROOT}/CMakeLists.txt")

set(FAILED "")
foreach(FILE IN LISTS FILES)
  file(STRINGS "${FILE}" LINES)
  list(LENGTH LINES COUNT)
  file(RELATIVE_PATH RELATIVE_FILE "${ROOT}" "${FILE}")
  string(REPLACE "\\" "/" RELATIVE_FILE "${RELATIVE_FILE}")

  set(LIMIT 300)
  if(RELATIVE_FILE STREQUAL "src/live2d/cubism_model_load.cpp")
    set(LIMIT 358)
  elseif(RELATIVE_FILE STREQUAL "src/media/image.c")
    set(LIMIT 332)
  elseif(RELATIVE_FILE STREQUAL "src/runtime/model_import_portable.c")
    set(LIMIT 341)
  elseif(RELATIVE_FILE STREQUAL "src/ui/preferences_render.c")
    set(LIMIT 308)
  elseif(RELATIVE_FILE STREQUAL "src/ui/preferences_scale.c")
    set(LIMIT 307)
  elseif(RELATIVE_FILE STREQUAL "src/ui/preferences_model.c")
    set(LIMIT 304)
  elseif(RELATIVE_FILE STREQUAL "tests/test_mver_import.c")
    set(LIMIT 327)
  elseif(RELATIVE_FILE STREQUAL "src/tools/cubism_viewer_blind_test.c")
    set(LIMIT 376)
  elseif(RELATIVE_FILE STREQUAL "src/tools/cubism_viewer_desktop_capture.c")
    set(LIMIT 396)
  elseif(RELATIVE_FILE STREQUAL "src/tools/mver_phase_metrics.c")
    set(LIMIT 321)
  elseif(RELATIVE_FILE STREQUAL "CMakeLists.txt")
    set(LIMIT 336)
  endif()

  if(COUNT GREATER LIMIT)
    list(APPEND FAILED "${RELATIVE_FILE}: ${COUNT} (limit ${LIMIT})")
  endif()
endforeach()

if(FAILED)
  list(JOIN FAILED "\n" MESSAGE_TEXT)
  message(FATAL_ERROR "Files over their line limit:\n${MESSAGE_TEXT}")
endif()

message(STATUS "Line policy passed")
