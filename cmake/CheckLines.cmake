cmake_minimum_required(VERSION 3.24)

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

  set(LIMIT 500)

  if(COUNT GREATER LIMIT)
    list(APPEND FAILED "${RELATIVE_FILE}: ${COUNT} (limit ${LIMIT})")
  endif()
endforeach()

if(FAILED)
  list(JOIN FAILED "\n" MESSAGE_TEXT)
  message(FATAL_ERROR "Files over their line limit:\n${MESSAGE_TEXT}")
endif()

message(STATUS "Line policy passed")
