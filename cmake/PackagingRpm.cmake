if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    BONGO_CAT_PACKAGE_PLATFORM STREQUAL "linux-x64" AND
    BONGO_CAT_CUBISM_ENABLED)
  add_custom_target(package-rpm
    COMMAND bash "${CMAKE_SOURCE_DIR}/packaging/linux/build-rpm.sh"
      "${CMAKE_BINARY_DIR}"
    DEPENDS bongo_cat
    COMMENT "Building the BongoCat RPM package"
    VERBATIM)
endif()
