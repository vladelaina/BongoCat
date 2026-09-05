if(WIN32)
  enable_language(RC)
  target_sources(bongo_cat_runtime PRIVATE
    src/platform/windows/windows.c
    src/platform/windows/windows_menu.c
    src/platform/windows/windows_directory.c
    src/platform/windows/windows_package.c
    src/platform/windows/windows_startup.c
    src/platform/windows/windows_update_handoff.c
    src/platform/windows/windows_borderless.c
    src/platform/windows/windows_capture.c
    src/platform/windows/windows_transparency.c
    src/platform/windows/windows_pointer.c
    src/platform/windows/windows_dialog.c
    src/platform/windows/windows_direct_input.c
    src/platform/windows/windows_input.c
    src/platform/windows/windows_popup.c
    src/platform/windows/windows_layered.c
    src/platform/windows/windows_layered_state.c
    src/platform/windows/windows_opacity.c
    src/platform/windows/windows_tray.c
    src/platform/windows/windows_assets.c
    src/platform/windows/windows_diagnostics.c
    src/platform/windows/windows_capture_probe.c)
  target_include_directories(bongo_cat_runtime PRIVATE
    src/platform/windows)
  target_link_libraries(bongo_cat_runtime PRIVATE
    dinput8 dxguid dwmapi ole32 shell32 user32 uuid windowscodecs advapi32
    winhttp)
elseif(APPLE)
  find_package(CURL REQUIRED)
  enable_language(OBJC)
  target_sources(bongo_cat_runtime PRIVATE
    src/platform/macos/macos.m
    src/platform/macos/macos_preferences.m
    src/platform/macos/macos_input.m
    src/platform/macos/macos_keys.m
    src/platform/macos/macos_menu.m
    src/platform/macos/macos_tray.m)
  target_link_libraries(bongo_cat_runtime PRIVATE "-framework Cocoa"
    "-framework ApplicationServices" CURL::libcurl)
else()
  find_package(CURL REQUIRED)
  find_package(X11 REQUIRED)
  if(NOT TARGET X11::Xi OR NOT TARGET X11::Xfixes)
    message(FATAL_ERROR
      "Linux native build requires XInput2 and Xfixes development packages")
  endif()
  target_sources(bongo_cat_runtime PRIVATE
    src/platform/linux/linux.c
    src/platform/linux/linux_menu.c
    src/platform/linux/linux_x11.c)
  target_link_libraries(bongo_cat_runtime PRIVATE
    X11::X11 X11::Xi X11::Xfixes m CURL::libcurl)
  target_link_libraries(bongo_cat_core PRIVATE m)
endif()
