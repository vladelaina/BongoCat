/* Task Scheduler has a C++ SDK interface; keep it behind the C platform API. */
extern "C" {
#include "bongo_cat/platform.h"
#include "windows_autostart.h"
}
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include "windows_autostart_internal.h"
#include <cstring>
#include <string>

using namespace bongo_autostart;

namespace {
constexpr char configure_argument[] = "--configure-autostart=";
constexpr size_t configure_argument_length = sizeof(configure_argument) - 1;
/* A normal/early exit of the application is not an acknowledgement. */
constexpr DWORD helper_success = 0xBCA00001u;

std::wstring user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    auto user = static_cast<TOKEN_USER *>(LocalAlloc(LPTR, size));
    LPWSTR sid = nullptr;
    std::wstring result;
    if (user && GetTokenInformation(token, TokenUser, user, size, &size) &&
        ConvertSidToStringSidW(user->User.Sid, &sid)) result = sid;
    LocalFree(sid);
    LocalFree(user);
    CloseHandle(token);
    return result;
}

HRESULT elevate(Mode mode, const std::wstring &sid) {
    wchar_t executable[BONGO_CAT_PATH_CAP];
    DWORD length = GetModuleFileNameW(nullptr, executable, BONGO_CAT_PATH_CAP);
    if (!length || length >= BONGO_CAT_PATH_CAP) return E_FAIL;
    std::wstring args;
    for (const char *p = configure_argument; *p; ++p)
        args.push_back(static_cast<unsigned char>(*p));
    args += std::to_wstring(static_cast<int>(mode)) + L" " + sid;
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable;
    info.lpParameters = args.c_str();
    std::wstring directory(executable);
    auto separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return E_INVALIDARG;
    directory.resize(separator + 1);
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) return HRESULT_FROM_WIN32(GetLastError());
    if (!info.hProcess) return E_FAIL;
    DWORD code = static_cast<DWORD>(E_FAIL);
    HRESULT hr = S_OK;
    /* The caller owns windows. Service synchronous window messages while
       waiting, without dispatching posted input and re-entering settings. */
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjectsEx(1, &info.hProcess, INFINITE,
            QS_SENDMESSAGE, 0);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            MSG message;
            PeekMessageW(&message, nullptr, 0, 0,
                PM_NOREMOVE | PM_QS_SENDMESSAGE);
            continue;
        }
        hr = wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_FAIL;
        break;
    }
    if (SUCCEEDED(hr) && !GetExitCodeProcess(info.hProcess, &code))
        hr = HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(info.hProcess);
    if (FAILED(hr)) return hr;
    return code == helper_success ? S_OK : (FAILED(static_cast<HRESULT>(code))
        ? static_cast<HRESULT>(code) : E_FAIL);
}
} // namespace

extern "C" bool bongo_cat_windows_autostart_command(int argc, char **argv,
    int *exit_code) {
    if (!exit_code || argc < 2 || !argv || !argv[1] ||
        strncmp(argv[1], configure_argument,
        configure_argument_length) != 0)
        return false;
    *exit_code = static_cast<int>(E_INVALIDARG);
    if (argc != 3 || !argv[2] ||
        strlen(argv[1]) != configure_argument_length + 1)
        return true;
    const char mode = argv[1][configure_argument_length];
    if (mode < '0' || mode > '2') return true;
    std::wstring sid = user_sid();
    /* SID strings are ASCII; widen argument bytes to avoid narrowing wchar_t. */
    std::wstring supplied_sid;
    for (const char *p = argv[2]; *p; ++p)
        supplied_sid.push_back(static_cast<unsigned char>(*p));
    /* Reject over-the-shoulder UAC credentials: never configure another user. */
    if (sid.empty() || sid != supplied_sid) {
        *exit_code = static_cast<int>(E_ACCESSDENIED);
        return true;
    }
    HRESULT hr = configure(static_cast<Mode>(mode - '0'), sid);
    *exit_code = hr == S_OK ? static_cast<int>(helper_success) : static_cast<int>(hr);
    return true;
}

extern "C" BongoCatResult bongo_cat_platform_set_autostart(bool enabled,
    bool administrator, BongoCatError *error) {
    std::wstring sid = user_sid();
    Mode mode = !enabled ? Mode::Disabled : administrator
        ? Mode::Administrator : Mode::Standard;
    HRESULT hr = sid.empty() ? E_FAIL : configure(mode, sid);
    if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD))
        hr = elevate(mode, sid);
    if (hr == S_OK) return BONGO_CAT_OK;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Cannot update Windows autostart (0x%08lx)", static_cast<unsigned long>(hr));
    return BONGO_CAT_ERROR_PLATFORM;
}
