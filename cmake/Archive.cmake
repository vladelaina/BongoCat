if(BONGO_CAT_FETCH_DEPS)
  FetchContent_Declare(miniz
    URL https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.1.0
    URL_HASH SHA256=09569fc19d060ac9f5999ba9356728c2494ebe6a24ac0eb0a6b6ae3d396cfea6
    SOURCE_SUBDIR bongocat-unused)
  FetchContent_MakeAvailable(miniz)
  # Build only the library, without changing the parent's build options.
  file(WRITE "${miniz_BINARY_DIR}/miniz_export.h"
    "#pragma once\n#define MINIZ_EXPORT\n")
  add_library(bongo_cat_archive STATIC
    "${miniz_SOURCE_DIR}/miniz.c" "${miniz_SOURCE_DIR}/miniz_zip.c"
    "${miniz_SOURCE_DIR}/miniz_tinfl.c" "${miniz_SOURCE_DIR}/miniz_tdef.c")
  target_include_directories(bongo_cat_archive SYSTEM PUBLIC
    "${miniz_SOURCE_DIR}" "${miniz_BINARY_DIR}")
else()
  find_package(miniz CONFIG REQUIRED)
  add_library(bongo_cat_archive INTERFACE)
  target_link_libraries(bongo_cat_archive INTERFACE miniz::miniz)
endif()
