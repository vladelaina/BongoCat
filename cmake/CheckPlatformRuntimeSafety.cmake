if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE PRODUCTION_FILES
  "${ROOT}/src/*.c" "${ROOT}/src/*.cc" "${ROOT}/src/*.cpp"
  "${ROOT}/src/*.cxx" "${ROOT}/src/*.m" "${ROOT}/src/*.mm"
  "${ROOT}/src/*.h" "${ROOT}/src/*.hh" "${ROOT}/src/*.hpp"
  "${ROOT}/src/*.hxx" "${ROOT}/include/*.h" "${ROOT}/include/*.hh"
  "${ROOT}/include/*.hpp" "${ROOT}/include/*.hxx")
list(FILTER PRODUCTION_FILES EXCLUDE REGEX "[/\\\\]src[/\\\\]tools[/\\\\]")

set(FORBIDDEN_APIS
  OpenProcess ReadProcessMemory WriteProcessMemory VirtualAllocEx
  VirtualProtectEx CreateRemoteThread CreateRemoteThreadEx NtOpenProcess
  NtReadVirtualMemory NtWriteVirtualMemory NtCreateThreadEx
  RtlCreateUserThread QueueUserAPC SetThreadContext DebugActiveProcess
  DebugBreakProcess MiniDumpWriteDump CreateToolhelp32Snapshot Process32First
  Process32Next EnumProcesses EnumProcessModules AdjustTokenPrivileges
  LookupPrivilegeValue OpenSCManager CreateService StartService ControlService
  DeviceIoControl NtLoadDriver AttachThreadInput SendInput keybd_event
  mouse_event SetWindowsHookEx SetWindowsHookExA SetWindowsHookExW DirectInput8Create
  SetCursorPos SetPhysicalCursorPos ClipCursor BlockInput
  SDL_SetWindowRelativeMouseMode
  SDL_SetWindowKeyboardGrab
  CGEventPost CGEventPostToPid CGEventCreateKeyboardEvent
  CGEventCreateMouseEvent CGWarpMouseCursorPosition
  CGAssociateMouseAndMouseCursorPosition IOHIDManagerCreate
  IOHIDEventSystemClientCreate IOServiceOpen task_for_pid mach_vm_read
  mach_vm_read_overwrite mach_vm_write mach_vm_allocate mach_vm_protect
  AuthorizationCreate AuthorizationExecuteWithPrivileges SMJobBless
  XTestFakeKeyEvent XTestFakeButtonEvent XTestFakeMotionEvent XWarpPointer
  XGrabKey XGrabButton process_vm_readv process_vm_writev ptrace ioctl
  setuid seteuid setgid setegid capset)
set(FORBIDDEN_TOKENS
  SE_DEBUG_NAME SeDebugPrivilege PROCESS_VM_READ PROCESS_VM_WRITE
  PROCESS_VM_OPERATION PROCESS_ALL_ACCESS THREAD_SET_CONTEXT
  kCGHIDEventTap kCGHeadInsertEventTap IOConnectCall
  /dev/uinput CAP_SYS_PTRACE CAP_SYS_ADMIN
  WH_KEYBOARD_LL WH_MOUSE_LL RIDEV_NOLEGACY RIDEV_CAPTUREMOUSE
  SDL_WINDOW_KEYBOARD_GRABBED SDL_HINT_FORCE_RAISEWINDOW
  RIDEV_NOHOTKEYS RIDEV_APPKEYS RIDEV_EXCLUDE RIDEV_EXINPUTSINK)
set(FAILURES "")

foreach(FILE IN LISTS PRODUCTION_FILES)
  file(READ "${FILE}" SOURCE)
  file(RELATIVE_PATH RELATIVE_FILE "${ROOT}" "${FILE}")
  string(REPLACE "\\" "/" RELATIVE_FILE "${RELATIVE_FILE}")
  foreach(API IN LISTS FORBIDDEN_APIS)
    string(REGEX MATCH
      "(^|[^A-Za-z0-9_])${API}[ \t\r\n]*\\(" MATCHED "${SOURCE}")
    if(MATCHED)
      list(APPEND FAILURES "${RELATIVE_FILE}: forbidden production API ${API}")
    endif()
  endforeach()
  foreach(TOKEN IN LISTS FORBIDDEN_TOKENS)
    string(FIND "${SOURCE}" "${TOKEN}" POSITION)
    if(NOT POSITION EQUAL -1)
      list(APPEND FAILURES "${RELATIVE_FILE}: forbidden production token ${TOKEN}")
    endif()
  endforeach()
endforeach()

# Intentional sensitive capabilities remain confined to reviewed modules.
set(SENSITIVE_RULES
  "GetAsyncKeyState|src/platform/windows/windows_keys.c"
  "RegisterRawInputDevices|src/platform/windows/windows_input_registration.c"
  "GetRegisteredRawInputDevices|src/platform/windows/windows_input_registration.c"
  "GetRawInputData|src/platform/windows/windows_input_receiver.c"
  "SDL_HINT_WINDOWS_RAW_KEYBOARD|src/runtime/shell/window.c|src/platform/windows/windows_input.c"
  "BitBlt|src/platform/windows/windows_capture_probe.c"
  "PrintWindow|src/platform/windows/windows_capture_probe.c"
  "UpdateLayeredWindow|src/platform/windows/windows_layered.c"
  "CGEventTapCreate|src/platform/macos/macos_input.m"
  "CGEventTapEnable|src/platform/macos/macos_input.m"
  "CGPreflightListenEventAccess|src/platform/macos/macos_input.m"
  "CGRequestListenEventAccess|src/platform/macos/macos_input.m"
  "/dev/input|src/platform/linux/linux_evdev_devices.c"
  "XISelectEvents|src/platform/linux/linux_x11.c"
  "XFixesSetWindowShapeRegion|src/platform/linux/linux_x11.c"
  "XSendEvent|src/platform/linux/linux.c|src/platform/linux/linux_x11.c"
  "XGrabPointer|src/platform/linux/linux_menu.c"
  "XGrabKeyboard|src/platform/linux/linux_menu.c"
  "SDL_INIT_GAMEPAD|src/runtime/input/gamepad.c"
  "SDL_OpenGamepad|src/runtime/input/gamepad.c"
  "SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS|src/runtime/input/gamepad.c"
  "SDL_CreateProcessWithProperties|src/runtime/model/multi_pet_process.c"
  "SDL_KillProcess|src/runtime/model/multi_pet_process.c")
foreach(RULE IN LISTS SENSITIVE_RULES)
  string(REPLACE "|" ";" PARTS "${RULE}")
  list(POP_FRONT PARTS CAPABILITY)
  foreach(FILE IN LISTS PRODUCTION_FILES)
    file(READ "${FILE}" SOURCE)
    string(FIND "${SOURCE}" "${CAPABILITY}" POSITION)
    if(POSITION EQUAL -1)
      continue()
    endif()
    file(RELATIVE_PATH RELATIVE_FILE "${ROOT}" "${FILE}")
    string(REPLACE "\\" "/" RELATIVE_FILE "${RELATIVE_FILE}")
    list(FIND PARTS "${RELATIVE_FILE}" OWNER_POSITION)
    if(OWNER_POSITION EQUAL -1)
      list(JOIN PARTS ", " OWNERS)
      list(APPEND FAILURES
        "${RELATIVE_FILE}: ${CAPABILITY} is owned by ${OWNERS}")
    endif()
  endforeach()
endforeach()

# The reviewed evdev exception permits read-only observation, not device writes
# or permission changes.
file(READ "${ROOT}/src/platform/linux/linux_evdev_devices.c" LINUX_EVDEV)
foreach(TOKEN O_WRONLY O_RDWR O_CREAT O_TRUNC)
  string(FIND "${LINUX_EVDEV}" "${TOKEN}" POSITION)
  if(NOT POSITION EQUAL -1)
    list(APPEND FAILURES "linux_evdev_devices.c: forbidden open flag ${TOKEN}")
  endif()
endforeach()
foreach(TOKEN O_RDONLY O_NONBLOCK O_CLOEXEC O_NOFOLLOW)
  string(FIND "${LINUX_EVDEV}" "${TOKEN}" POSITION)
  if(POSITION EQUAL -1)
    list(APPEND FAILURES "linux_evdev_devices.c: missing open safeguard ${TOKEN}")
  endif()
endforeach()
foreach(API chmod fchmod chown fchown system popen)
  string(REGEX MATCH "(^|[^A-Za-z0-9_])${API}[ \t\r\n]*\\(" MATCHED "${LINUX_EVDEV}")
  if(MATCHED)
    list(APPEND FAILURES "linux_evdev_devices.c: forbidden API ${API}")
  endif()
endforeach()

file(READ "${ROOT}/cmake/windows.manifest.in" WINDOWS_MANIFEST)
string(FIND "${WINDOWS_MANIFEST}"
  "<requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>"
  MANIFEST_POSITION)
if(MANIFEST_POSITION EQUAL -1)
  list(APPEND FAILURES
    "cmake/windows.manifest.in: expected asInvoker with uiAccess=false")
endif()

file(READ "${ROOT}/packaging/windows/BongoCat.iss.in" WINDOWS_PACKAGING)
file(READ "${ROOT}/packaging/windows/install-lifecycle.iss.in" WINDOWS_INSTALL_LIFECYCLE)
string(APPEND WINDOWS_PACKAGING "\n${WINDOWS_INSTALL_LIFECYCLE}")
string(FIND "${WINDOWS_PACKAGING}" "PrivilegesRequired=lowest"
  INSTALLER_LEVEL_POSITION)
string(FIND "${WINDOWS_PACKAGING}"
  "DefaultDirName={code:GetInstallDir}"
  INSTALLER_ROOT_POSITION)
string(FIND "${WINDOWS_PACKAGING}"
  "{localappdata}\\Programs\\BongoCat" INSTALLER_DEFAULT_POSITION)
if(INSTALLER_LEVEL_POSITION EQUAL -1 OR INSTALLER_ROOT_POSITION EQUAL -1
    OR INSTALLER_DEFAULT_POSITION EQUAL -1)
  list(APPEND FAILURES
    "packaging/windows/BongoCat.iss.in: expected current-user Inno installation")
endif()

file(READ "${ROOT}/cmake/Info.plist.in" MACOS_INFO)
string(FIND "${MACOS_INFO}" "NSInputMonitoringUsageDescription"
  INPUT_MONITORING_POSITION)
string(FIND "${MACOS_INFO}" "NSAccessibilityUsageDescription"
  ACCESSIBILITY_POSITION)
if(INPUT_MONITORING_POSITION EQUAL -1 OR NOT ACCESSIBILITY_POSITION EQUAL -1)
  list(APPEND FAILURES
    "cmake/Info.plist.in: expected Input Monitoring without Accessibility")
endif()

file(READ "${ROOT}/src/platform/macos/macos_input.m" MACOS_INPUT)
foreach(TOKEN kCGSessionEventTap kCGTailAppendEventTap
    kCGEventTapOptionListenOnly)
  string(FIND "${MACOS_INPUT}" "${TOKEN}" POSITION)
  if(POSITION EQUAL -1)
    list(APPEND FAILURES
      "src/platform/macos/macos_input.m: missing safe event tap token ${TOKEN}")
  endif()
endforeach()

file(READ "${ROOT}/cmake/PlatformRuntime.cmake" PLATFORM_RUNTIME)
string(FIND "${PLATFORM_RUNTIME}" "X11::Xtst" XTEST_POSITION)
if(NOT XTEST_POSITION EQUAL -1)
  list(APPEND FAILURES
    "cmake/PlatformRuntime.cmake: XTest must not enter the Linux runtime")
endif()

file(READ "${ROOT}/cmake/Dependencies.cmake" DEPENDENCIES)
foreach(TOKEN 402fc52af4e731184ad6a704068b5ccd27d8f1b8
    e413151af71c23d316b6076a96a999342142afa792394eaf8a542a03503fc491)
  string(FIND "${DEPENDENCIES}" "${TOKEN}" POSITION)
  if(POSITION EQUAL -1)
    list(APPEND FAILURES
      "cmake/Dependencies.cmake: SDL dependency changed without safety review")
  endif()
endforeach()

file(READ "${ROOT}/cmake/ValidationTools.cmake" VALIDATION_TOOLS)
foreach(TOOL cubism_viewer_desktop_capture cubism_viewer_drag_capture)
  string(REGEX MATCH
    "add_executable\\(${TOOL}[ \t\r\n]+EXCLUDE_FROM_ALL" TOOL_ISOLATED
    "${VALIDATION_TOOLS}")
  if(NOT TOOL_ISOLATED)
    list(APPEND FAILURES
      "cmake/ValidationTools.cmake: ${TOOL} must remain EXCLUDE_FROM_ALL")
  endif()
endforeach()

if(FAILURES)
  list(REMOVE_DUPLICATES FAILURES)
  list(JOIN FAILURES "\n" MESSAGE_TEXT)
  message(FATAL_ERROR "Platform runtime safety policy failed:\n${MESSAGE_TEXT}")
endif()

message(STATUS "Platform runtime safety policy passed")
