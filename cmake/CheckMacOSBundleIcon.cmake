# Verifies that the macOS bundle's app icon is declared, present, and decodable.
# Any one of those failing leaves Finder showing the system default icon.
#
# Run from a build directory:
#   cmake -DBUNDLE=<path to .app> -P cmake/CheckMacOSBundleIcon.cmake

if(NOT DEFINED BUNDLE)
  message(FATAL_ERROR "BUNDLE is required (path to the .app to inspect)")
endif()
if(NOT IS_DIRECTORY "${BUNDLE}")
  message(FATAL_ERROR "no application bundle at ${BUNDLE}")
endif()

set(plist "${BUNDLE}/Contents/Info.plist")
set(resources "${BUNDLE}/Contents/Resources")
if(NOT EXISTS "${plist}")
  message(FATAL_ERROR "bundle has no Contents/Info.plist: ${plist}")
endif()

# PlistBuddy reports an absent key on stderr; any other failure means the tool
# could not read the plist, which is a different diagnosis.
execute_process(
  COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleIconFile" "${plist}"
  RESULT_VARIABLE plist_result
  OUTPUT_VARIABLE icon_name
  ERROR_VARIABLE plist_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT plist_result EQUAL 0)
  if(plist_error MATCHES "Does Not Exist")
    message(FATAL_ERROR "Info.plist declares no CFBundleIconFile, so Finder falls "
      "back to the system default icon")
  endif()
  message(FATAL_ERROR "PlistBuddy could not read ${plist}: ${plist_error}")
endif()

set(icon_path "${resources}/${icon_name}")
if(NOT EXISTS "${icon_path}")
  set(icon_path "${resources}/${icon_name}.icns")
endif()
if(NOT EXISTS "${icon_path}")
  message(FATAL_ERROR "CFBundleIconFile names '${icon_name}', but ${resources} "
    "contains no such resource")
endif()

# Container bytes alone do not prove the image data is usable; a decode does. The
# uniquely named scratch directory is the only thing this run removes.
execute_process(
  COMMAND /usr/bin/mktemp -d "macos-bundle-icon.XXXXXXXX"
  RESULT_VARIABLE mktemp_result
  OUTPUT_VARIABLE scratch_dir
  ERROR_VARIABLE mktemp_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT mktemp_result EQUAL 0 OR NOT IS_DIRECTORY "${scratch_dir}")
  message(FATAL_ERROR "could not create a scratch directory: ${mktemp_error}")
endif()

execute_process(
  COMMAND iconutil -c iconset "${icon_path}" -o "${scratch_dir}/icon.iconset"
  RESULT_VARIABLE decode_result
  ERROR_VARIABLE decode_error
  OUTPUT_QUIET)
file(REMOVE_RECURSE "${scratch_dir}")
if(NOT decode_result EQUAL 0)
  message(FATAL_ERROR "iconutil cannot decode ${icon_path}: ${decode_error}")
endif()

message(STATUS "bundle icon OK: CFBundleIconFile='${icon_name}' -> ${icon_path}")
