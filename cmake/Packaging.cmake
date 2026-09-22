install(TARGETS bongo_cat
  RUNTIME DESTINATION . COMPONENT Runtime
  BUNDLE DESTINATION . COMPONENT Runtime)
if(WIN32)
  install(FILES LICENSE DESTINATION . COMPONENT Runtime)
  install(FILES resources/assets/models/LICENSE DESTINATION assets/models COMPONENT Runtime)
endif()
set(BONGO_CAT_PACKAGE_PRODUCT "BongoCat")
if(NOT BONGO_CAT_CUBISM_ENABLED)
  set(BONGO_CAT_PACKAGE_PRODUCT "BongoCat-Diagnostic")
  install(FILES cmake/DiagnosticBuildNotice.txt DESTINATION .
    COMPONENT Runtime)
endif()
if(UNIX AND NOT APPLE)
  install(DIRECTORY resources/assets DESTINATION . COMPONENT Runtime
    PATTERN "LICENSE" EXCLUDE)
endif()

include(cmake/PackagingPlatform.cmake)

set(BONGO_CAT_PACKAGE_NAME
  "${BONGO_CAT_PACKAGE_PRODUCT}-${PROJECT_VERSION}-${BONGO_CAT_PACKAGE_PLATFORM}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/bongocat-package-name.txt"
  CONTENT "${BONGO_CAT_PACKAGE_NAME}\n")

set(CPACK_PACKAGE_NAME "${BONGO_CAT_PACKAGE_PRODUCT}")
set(CPACK_PACKAGE_VENDOR "vladelaina")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${BONGO_CAT_PACKAGE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALL_CMAKE_PROJECTS
  "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")

if(WIN32)
  set(BONGO_CAT_PORTABLE_EXECUTABLE
    "${CMAKE_BINARY_DIR}/dist/${BONGO_CAT_PACKAGE_NAME}-portable.exe")
  add_custom_command(OUTPUT "${BONGO_CAT_PORTABLE_EXECUTABLE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bongo_cat>"
      "${BONGO_CAT_PORTABLE_EXECUTABLE}"
    DEPENDS bongo_cat
    COMMENT "Building the BongoCat Windows portable executable"
    VERBATIM)
  add_custom_target(package-portable
    DEPENDS "${BONGO_CAT_PORTABLE_EXECUTABLE}")
endif()

include(CPack)
include(cmake/PackagingAppImage.cmake)
include(cmake/PackagingRpm.cmake)
if(WIN32)
  include(cmake/PackagingInno.cmake)
endif()
