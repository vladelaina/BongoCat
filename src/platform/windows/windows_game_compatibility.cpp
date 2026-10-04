extern "C" {
#include "windows_game_compatibility.h"
#include "windows_startup.h"
}
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace {
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { if (value) CloseHandle(value); }
};
HANDLE restart_commit;
HANDLE restart_abort;
constexpr wchar_t restart_prefix[] = L"--compat-restart=";

bool elevated(HANDLE token) {
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    return GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &size) && elevation.TokenIsElevated;
}

std::wstring sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> bytes(size);
    if (!size || !GetTokenInformation(token, TokenUser, bytes.data(), size, &size))
        return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(bytes.data())->User.Sid,
            &text)) return {};
    std::wstring result(text);
    LocalFree(text);
    return result;
}

std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

bool failure(BongoCatError *error, DWORD code) {
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Windows permission restart failed (%lu)", static_cast<unsigned long>(code));
    return false;
}

DWORD wait_ready(HANDLE ready, HANDLE process) {
    HANDLE handles[] = {ready, process};
    ULONGLONG deadline = GetTickCount64() + 30000;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return WAIT_TIMEOUT;
        DWORD result = MsgWaitForMultipleObjectsEx(2, handles,
            static_cast<DWORD>(deadline - now), QS_SENDMESSAGE, 0);
        if (result != WAIT_OBJECT_0 + 2) return result;
        MSG message;
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
}

bool launch(bool administrator, BongoCatError *error) {
    if (restart_commit) return failure(error, ERROR_BUSY);
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        return failure(error, GetLastError());
    std::wstring user = sid(token.value);
    GUID guid;
    wchar_t guid_text[40];
    if (user.empty() || FAILED(CoCreateGuid(&guid)) ||
        !StringFromGUID2(guid, guid_text, 40)) return failure(error, ERROR_INVALID_DATA);
    std::wstring name = L"Local\\BongoCat.PermissionRestart." + std::wstring(guid_text);
    // Both privilege levels must acknowledge the handoff; only this user can open it.
    std::wstring sddl = L"D:P(A;;GA;;;" + user + L")S:(ML;;NW;;;ME)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
            SDDL_REVISION_1, &descriptor, nullptr)) return failure(error, GetLastError());
    SECURITY_ATTRIBUTES security = {sizeof(security), descriptor, FALSE};
    Handle ready, commit, abort;
    ready.value = CreateEventW(&security, TRUE, FALSE, (name + L".ready").c_str());
    commit.value = CreateEventW(&security, TRUE, FALSE, (name + L".commit").c_str());
    abort.value = CreateEventW(&security, TRUE, FALSE, (name + L".abort").c_str());
    DWORD code = GetLastError();
    LocalFree(descriptor);
    if (!ready.value || !commit.value || !abort.value) return failure(error, code);

    wchar_t executable[BONGO_CAT_PATH_CAP];
    DWORD length = GetModuleFileNameW(nullptr, executable, BONGO_CAT_PATH_CAP);
    if (!length || length >= BONGO_CAT_PATH_CAP) return failure(error, ERROR_BAD_PATHNAME);
    DWORD directory_length = GetCurrentDirectoryW(0, nullptr);
    std::vector<wchar_t> directory(directory_length);
    if (!directory_length || !GetCurrentDirectoryW(directory_length, directory.data()))
        return failure(error, GetLastError());
    int count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return failure(error, GetLastError());
    std::wstring parameters;
    for (int i = 1; i < count; ++i) {
        if (wcsncmp(arguments[i], L"--compat-", 9) == 0) continue;
        parameters += quote(arguments[i]) + L" ";
    }
    LocalFree(arguments);
    parameters += quote(std::wstring(restart_prefix) + guid_text) + L" " +
        quote(L"--compat-user=" + user) +
        (administrator ? L" --compat-level=1" : L" --compat-level=0");
    Handle process;
    if (administrator) {
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = executable;
        info.lpParameters = parameters.c_str();
        info.lpDirectory = directory.data();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) return failure(error, GetLastError());
        process.value = info.hProcess;
    } else {
        // ShellExecute from an elevated process inherits elevation. Use the desktop user's token.
        DWORD shell_pid = 0;
        GetWindowThreadProcessId(GetShellWindow(), &shell_pid);
        Handle shell, shell_token;
        shell.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
        if (!shell.value || !OpenProcessToken(shell.value,
                TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
                TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, &shell_token.value))
            return failure(error, GetLastError());
        if (elevated(shell_token.value) || sid(shell_token.value) != user)
            return failure(error, ERROR_ACCESS_DENIED);
        std::wstring command = quote(executable) + L" " + parameters;
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info = {};
        Handle primary;
        if (!DuplicateTokenEx(shell_token.value, MAXIMUM_ALLOWED, nullptr,
                SecurityImpersonation, TokenPrimary, &primary.value))
            return failure(error, GetLastError());
        BOOL created = CreateProcessAsUserW(primary.value, executable,
            &command[0], nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
            nullptr, directory.data(), &startup, &info);
        if (!created)
            created = CreateProcessWithTokenW(primary.value, LOGON_WITH_PROFILE,
                executable, &command[0], 0, nullptr, directory.data(), &startup, &info);
        if (!created) return failure(error, GetLastError());
        CloseHandle(info.hThread);
        process.value = info.hProcess;
    }
    if (!process.value || wait_ready(ready.value, process.value) != WAIT_OBJECT_0) {
        SetEvent(abort.value);
        return failure(error, ERROR_PROCESS_ABORTED);
    }
    restart_commit = commit.value;
    restart_abort = abort.value;
    commit.value = abort.value = nullptr;
    return true;
}

void cancel_restart() {
    if (restart_abort) SetEvent(restart_abort);
    if (restart_commit) CloseHandle(restart_commit);
    if (restart_abort) CloseHandle(restart_abort);
    restart_commit = restart_abort = nullptr;
}
}

extern "C" bool bongo_cat_windows_game_compatibility_command(void) {
    int count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    std::wstring identifier, user;
    int level = -1;
    for (int i = 1; i < count; ++i) {
        if (wcsncmp(arguments[i], restart_prefix, 17) == 0) identifier = arguments[i] + 17;
        else if (wcsncmp(arguments[i], L"--compat-user=", 14) == 0) user = arguments[i] + 14;
        else if (wcscmp(arguments[i], L"--compat-level=1") == 0) level = 1;
        else if (wcscmp(arguments[i], L"--compat-level=0") == 0) level = 0;
    }
    LocalFree(arguments);
    if (identifier.empty()) return true;
    GUID guid;
    if (FAILED(CLSIDFromString(identifier.c_str(), &guid))) return false;
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) ||
        sid(token.value) != user || level < 0 || elevated(token.value) != (level == 1))
        return false;
    std::wstring name = L"Local\\BongoCat.PermissionRestart." + identifier;
    Handle ready, commit, abort;
    ready.value = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L".ready").c_str());
    commit.value = OpenEventW(SYNCHRONIZE, FALSE, (name + L".commit").c_str());
    abort.value = OpenEventW(SYNCHRONIZE, FALSE, (name + L".abort").c_str());
    if (!ready.value || !commit.value || !abort.value || !SetEvent(ready.value)) return false;
    Handle stopped;
    stopped.value = OpenEventW(SYNCHRONIZE, FALSE,
        bongo_cat_windows_instance_stopped_name());
    if (!stopped.value || WaitForSingleObject(stopped.value, 30000) != WAIT_OBJECT_0)
        return false;
    HANDLE events[] = {abort.value, commit.value};
    return WaitForMultipleObjects(2, events, FALSE, 120000) == WAIT_OBJECT_0 + 1;
}

extern "C" bool bongo_cat_windows_game_compatibility_elevated(bool *value,
    BongoCatError *error) {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        return failure(error, GetLastError());
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    if (!GetTokenInformation(token.value, TokenElevation, &elevation,
            sizeof(elevation), &size)) return failure(error, GetLastError());
    *value = elevation.TokenIsElevated != 0;
    return true;
}

extern "C" bool bongo_cat_windows_game_compatibility_launch(bool administrator,
    BongoCatError *error) {
    return launch(administrator, error);
}

extern "C" void bongo_cat_windows_game_compatibility_cancel(void) {
    cancel_restart();
}

extern "C" void bongo_cat_windows_game_compatibility_finish(void) {
    if (restart_commit) SetEvent(restart_commit);
    if (restart_commit) CloseHandle(restart_commit);
    if (restart_abort) CloseHandle(restart_abort);
    restart_commit = restart_abort = nullptr;
}
