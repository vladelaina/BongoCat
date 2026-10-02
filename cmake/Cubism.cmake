set(CUBISM_CORE_PATH "${BONGO_CAT_CUBISM_SDK}/Core")
set(CUBISM_FRAMEWORK_PATH "${BONGO_CAT_CUBISM_SDK}/Framework")
set(CUBISM_GLEW_PATH "${BONGO_CAT_CUBISM_SDK}/Samples/OpenGL/thirdParty/glew")

# GLEW 2.2.0 declares an obsolete policy floor. Keep its unchanged upstream
# project compatible with current CMake releases without patching vendor files.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

foreach(PATH IN ITEMS CUBISM_FRAMEWORK_PATH CUBISM_GLEW_PATH)
  if(NOT EXISTS "${${PATH}}")
    message(FATAL_ERROR "Incomplete Cubism SDK: ${${PATH}} is missing")
  endif()
endforeach()

add_library(Live2DCubismCore STATIC IMPORTED GLOBAL)
if(WIN32)
  if(MSVC_VERSION LESS 1930 OR MSVC_VERSION GREATER_EQUAL 1950)
    message(FATAL_ERROR "Cubism Windows build requires Visual Studio 2022")
  endif()
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CUBISM_WINDOWS_ARCH "x86_64")
  else()
    set(CUBISM_WINDOWS_ARCH "x86")
  endif()
  set(CUBISM_CORE_LIBRARY
    "${CUBISM_CORE_PATH}/lib/windows/${CUBISM_WINDOWS_ARCH}/143/Live2DCubismCore_MT.lib")
elseif(APPLE)
  if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 CUBISM_ARCH)
  else()
    set(CUBISM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  set(CUBISM_CORE_LIBRARY
    "${CUBISM_CORE_PATH}/lib/macos/${CUBISM_ARCH}/libLive2DCubismCore.a")
else()
  set(CUBISM_CORE_LIBRARY
    "${CUBISM_CORE_PATH}/lib/linux/x86_64/libLive2DCubismCore.a")
endif()
if(NOT EXISTS "${CUBISM_CORE_LIBRARY}")
  message(FATAL_ERROR "Cubism Core library missing: ${CUBISM_CORE_LIBRARY}")
endif()
set_target_properties(Live2DCubismCore PROPERTIES
  IMPORTED_LOCATION "${CUBISM_CORE_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_CORE_PATH}/include")

set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CUBISM_GLEW_PATH}/build/cmake" "${CMAKE_BINARY_DIR}/cubism-glew"
  EXCLUDE_FROM_ALL)
set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory("${CUBISM_FRAMEWORK_PATH}" "${CMAKE_BINARY_DIR}/cubism-framework")
include(cmake/CubismUserModelSafety.cmake)
bongo_cat_harden_cubism_user_model(Framework)
include(cmake/CubismShaderOptimize.cmake)
bongo_cat_optimize_cubism_shaders(Framework)
include(cmake/CubismJsonNumbers.cmake)
bongo_cat_fix_cubism_json_numbers(Framework)

if(WIN32)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL GLEW_NO_GLU)
elseif(APPLE)
  target_compile_definitions(Framework PUBLIC CSM_TARGET_MAC_GL GLEW_NO_GLU)
else()
  target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL GLEW_NO_GLU)
endif()
target_include_directories(Framework SYSTEM PUBLIC
  "${CUBISM_FRAMEWORK_PATH}/src"
  "${CUBISM_CORE_PATH}/include"
  "${CUBISM_GLEW_PATH}/include")
target_link_libraries(Framework PUBLIC Live2DCubismCore glew_s)
