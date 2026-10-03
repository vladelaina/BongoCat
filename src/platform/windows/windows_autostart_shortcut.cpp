#include "windows_autostart_internal.h"
#include <shlobj.h>
#include <shobjidl.h>
extern "C" {
#include "bongo_cat/common.h"
}

namespace bongo_autostart {
namespace {
bool missing(DWORD code) {
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}
HRESULT remove_shortcut(const std::wstring &path) {
    if (DeleteFileW(path.c_str())) return S_OK;
    DWORD code = GetLastError();
    return missing(code) ? S_OK : HRESULT_FROM_WIN32(code);
}
HRESULT approve_shortcut() {
    HKEY key = nullptr;
    LONG code = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder",
        0, KEY_SET_VALUE, &key);
    if (code == ERROR_FILE_NOT_FOUND) return S_OK;
    if (code != ERROR_SUCCESS) return HRESULT_FROM_WIN32(code);
    code = RegDeleteValueW(key, BONGO_CAT_NAME_W L".lnk");
    RegCloseKey(key);
    return code == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(code);
}
}

HRESULT ShortcutTransaction::begin() {
    PWSTR directory = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DONT_VERIFY,
        nullptr, &directory);
    if (FAILED(hr)) return hr;
    if (!directory) return E_FAIL;
    path = std::wstring(directory) + L"\\" BONGO_CAT_NAME_W L".lnk";
    CoTaskMemFree(directory);
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD code = GetLastError();
        return missing(code) ? S_OK : HRESULT_FROM_WIN32(code);
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    existed = true;
    /* Back up in the same directory, retaining the exact previous .lnk data.
       A failed copy cannot lead to modifying the original shortcut. */
    std::wstring parent = path.substr(0, path.find_last_of(L'\\'));
    wchar_t temporary[MAX_PATH];
    if (!GetTempFileNameW(parent.c_str(), L"bca", 0, temporary))
        return HRESULT_FROM_WIN32(GetLastError());
    backup = temporary;
    if (!CopyFileW(path.c_str(), backup.c_str(), FALSE))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT ShortcutTransaction::apply(bool enabled) {
    if (!enabled) {
        HRESULT hr = remove_shortcut(path);
        if (SUCCEEDED(hr)) changed = existed;
        return hr;
    }
    PWSTR directory = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_CREATE,
        nullptr, &directory);
    CoTaskMemFree(directory);
    if (FAILED(hr)) return hr;
    wchar_t executable[BONGO_CAT_PATH_CAP];
    DWORD length = GetModuleFileNameW(nullptr, executable, BONGO_CAT_PATH_CAP);
    if (!length) return HRESULT_FROM_WIN32(GetLastError());
    if (length >= BONGO_CAT_PATH_CAP)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::wstring working(executable);
    auto separator = working.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return E_INVALIDARG;
    working.resize(separator + 1);
    Com<IShellLinkW> link;
    Com<IPersistFile> persist;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void **>(&link.p));
    if (SUCCEEDED(hr)) hr = link->SetPath(executable);
    if (SUCCEEDED(hr)) hr = link->SetArguments(L"--autostart");
    if (SUCCEEDED(hr)) hr = link->SetWorkingDirectory(working.c_str());
    if (SUCCEEDED(hr)) hr = link->SetDescription(L"Start BongoCat when signing in");
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_IPersistFile,
        reinterpret_cast<void **>(&persist.p));
    if (SUCCEEDED(hr)) {
        changed = true; // Save can fail after partially writing the file.
        hr = persist->Save(path.c_str(), TRUE);
    }
    return hr;
}

HRESULT ShortcutTransaction::commit(bool enable_standard) {
    /* Do not clear a user's disabled state when turning autostart off. */
    HRESULT hr = enable_standard ? approve_shortcut() : S_OK;
    if (SUCCEEDED(hr)) committed = true;
    return hr;
}

HRESULT ShortcutTransaction::rollback() {
    if (committed || !changed) return S_OK;
    HRESULT hr = existed
        ? (CopyFileW(backup.c_str(), path.c_str(), FALSE) ? S_OK
            : HRESULT_FROM_WIN32(GetLastError()))
        : remove_shortcut(path);
    if (SUCCEEDED(hr)) changed = false;
    return hr;
}

ShortcutTransaction::~ShortcutTransaction() {
    /* Preserve the backup if Windows prevents restoring it. */
    if (SUCCEEDED(rollback()) && !backup.empty()) DeleteFileW(backup.c_str());
}
}
