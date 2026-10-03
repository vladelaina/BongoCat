function(bongo_cat_sdl_replace_section variable begin end replacement)
  set(source "${${variable}}")
  string(FIND "${source}" "${begin}" first)
  if(first LESS 0)
    message(FATAL_ERROR "Pinned SDL Windows patch mismatch: ${begin}")
  endif()
  string(LENGTH "${begin}" begin_size)
  math(EXPR after_begin "${first} + ${begin_size}")
  string(SUBSTRING "${source}" ${after_begin} -1 tail)
  string(FIND "${tail}" "${begin}" duplicate)
  string(FIND "${tail}" "${end}" last)
  if(NOT duplicate EQUAL -1 OR last LESS 0)
    message(FATAL_ERROR "Ambiguous pinned SDL Windows patch: ${begin}")
  endif()
  string(SUBSTRING "${source}" 0 ${first} prefix)
  string(SUBSTRING "${tail}" ${last} -1 suffix)
  set(${variable} "${prefix}${replacement}${suffix}" PARENT_SCOPE)
endfunction()

function(bongo_cat_sdl_generated_source target original output source)
  get_target_property(sources ${target} SOURCES)
  if(NOT "${original}" IN_LIST sources)
    message(FATAL_ERROR "Pinned SDL target no longer contains ${original}")
  endif()
  set(marked "#define BONGO_CAT_MODIFIED_SDL_WINDOWS_BACKEND 1\n${source}")
  if(EXISTS "${output}")
    file(READ "${output}" previous)
  endif()
  if(NOT marked STREQUAL previous)
    file(WRITE "${output}" "${marked}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
  list(REMOVE_ITEM sources "${original}")
  set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
  target_sources(${target} PRIVATE "${output}")
endfunction()

function(bongo_cat_trim_sdl_windows target source_root)
  set(windows_dir "${source_root}/src/video/windows")
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/sdl-windows")
  file(MAKE_DIRECTORY "${output_dir}")
  target_include_directories(${target} PRIVATE "${windows_dir}")

  set(window_path "${windows_dir}/SDL_windowswindow.c")
  file(READ "${window_path}" window)
  string(REPLACE "\r\n" "\n" window "${window}")
  bongo_cat_sdl_replace_section(window
    "        if (data->keyboard_hook) {" "        ReleaseDC(data->hwnd, data->hdc);" "")
  bongo_cat_sdl_replace_section(window
    "static void WIN_GrabKeyboard(SDL_Window *window)"
    "bool WIN_SetWindowMouseRect(" "")
  bongo_cat_sdl_replace_section(window
    "bool WIN_SetWindowKeyboardGrab(" "\n#endif // !defined(SDL_PLATFORM_XBOXONE)"
    [=[bool WIN_SetWindowKeyboardGrab(SDL_VideoDevice *_this, SDL_Window *window, bool grabbed)
{
    (void)_this;
    (void)window;
    return grabbed ? SDL_Unsupported() : true;
}
]=])
  bongo_cat_sdl_replace_section(window
    "void WIN_RaiseWindow(" "void WIN_MaximizeWindow("
    [=[void WIN_RaiseWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
#if !defined(SDL_PLATFORM_XBOXONE) && !defined(SDL_PLATFORM_XBOXSERIES)
    const bool activate = SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, true);
    SDL_WindowData *data = window->internal;
    HWND hwnd = data->hwnd;
    if (activate) {
        SetForegroundWindow(hwnd);
        if ((window->flags & SDL_WINDOW_POPUP_MENU) && !(window->flags & SDL_WINDOW_NOT_FOCUSABLE)) {
            WIN_SetKeyboardFocus(window, window->parent == SDL_GetKeyboardFocus());
        }
    } else {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, data->copybits_flag | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
#endif
}

]=])
  bongo_cat_sdl_generated_source(${target} "${window_path}"
    "${output_dir}/SDL_windowswindow.c" "${window}")

  set(events_path "${windows_dir}/SDL_windowsevents.c")
  file(READ "${events_path}" events)
  string(REPLACE "\r\n" "\n" events "${events}")
  bongo_cat_sdl_replace_section(events
    "LRESULT CALLBACK\nWIN_KeyboardHookProc(" "static bool WIN_SwapButtons(" "")
  bongo_cat_sdl_generated_source(${target} "${events_path}"
    "${output_dir}/SDL_windowsevents.c" "${events}")
  set(BONGO_CAT_SDL_WINDOWS_TRIMMED TRUE PARENT_SCOPE)
endfunction()
