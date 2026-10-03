# Locate ISCC at package time: CI installs it after configuring the application.
foreach(template BongoCat install-lifecycle)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/${template}.iss.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${template}.iss" @ONLY)
endforeach()
add_custom_target(package-installer
  COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/build-installer.ps1"
    -BuildDir "${CMAKE_BINARY_DIR}" -Configuration "$<CONFIG>"
    -PackageName "${BONGO_CAT_PACKAGE_NAME}"
  DEPENDS bongo_cat
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Building the BongoCat Inno Setup installer"
  VERBATIM)
