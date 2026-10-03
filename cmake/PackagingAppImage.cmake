if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    BONGO_CAT_PACKAGE_PLATFORM STREQUAL "linux-x64")
  add_custom_target(package-appimage
    COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/linux/build-appimage.sh"
      "${CMAKE_BINARY_DIR}"
    DEPENDS bongo_cat
    COMMENT "Building the BongoCat Linux AppImage"
    VERBATIM)
endif()
