#include "windows_autostart_internal.h"

namespace bongo_autostart {
HRESULT require_elevation() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return HRESULT_FROM_WIN32(GetLastError());
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    BOOL read = GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &size);
    DWORD code = read ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!read) return HRESULT_FROM_WIN32(code);
    return elevation.TokenIsElevated ? S_OK : E_ACCESSDENIED;
}

HRESULT configure(Mode mode, const std::wstring &sid) {
    if (mode == Mode::Administrator) {
        HRESULT hr = require_elevation();
        if (FAILED(hr)) return hr;
    }
    Apartment apartment;
    if (FAILED(apartment.hr) && apartment.hr != RPC_E_CHANGED_MODE)
        return apartment.hr;
    Com<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITaskService,
        reinterpret_cast<void **>(&service.p));
    VARIANT empty;
    VariantInit(&empty);
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    Com<ITaskFolder> folder;
    if (SUCCEEDED(hr)) hr = service->GetFolder(Bstr(L"\\"), &folder.p);
    /* Do not pretend to disable a task if its service cannot be reached. */
    if (FAILED(hr)) return hr;
    std::wstring name = L"BongoCat.Autostart." + sid;
    Com<IRegisteredTask> previous;
    hr = folder->GetTask(Bstr(name.c_str()), &previous.p);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
        hr != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) return hr;
    if (previous.p) {
        hr = require_elevation();
        if (FAILED(hr)) return hr;
    }
    Bstr previous_xml;
    Bstr previous_security;
    if (previous.p) {
        hr = previous->get_Xml(&previous_xml.p);
        if (SUCCEEDED(hr)) hr = previous->GetSecurityDescriptor(
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION, &previous_security.p);
        if (FAILED(hr)) return hr;
    }
    ShortcutTransaction shortcut;
    hr = shortcut.begin();
    if (FAILED(hr)) return hr;
    bool task_changed = false;
    if (mode == Mode::Administrator) {
        hr = create_task(service.p, folder.p, name, sid);
        task_changed = SUCCEEDED(hr);
        if (SUCCEEDED(hr)) hr = shortcut.apply(false);
    } else {
        hr = shortcut.apply(mode == Mode::Standard);
        if (SUCCEEDED(hr) && previous.p) {
            hr = folder->DeleteTask(Bstr(name.c_str()), 0);
            task_changed = SUCCEEDED(hr);
        }
    }
    if (SUCCEEDED(hr)) hr = shortcut.commit(mode == Mode::Standard);
    if (SUCCEEDED(hr)) return S_OK;

    /* Restore both parts, not merely the UI switches. Keep the original
       task's definition and ACL, including a disabled task's enabled flag. */
    HRESULT task_rollback = S_OK;
    if (task_changed) {
        if (previous.p) {
            VARIANT security;
            VariantInit(&security);
            security.vt = VT_BSTR;
            security.bstrVal = previous_security.p;
            Com<IRegisteredTask> restored;
            task_rollback = folder->RegisterTask(Bstr(name.c_str()),
                previous_xml, TASK_CREATE_OR_UPDATE, empty, empty,
                TASK_LOGON_INTERACTIVE_TOKEN, security, &restored.p);
        } else {
            task_rollback = folder->DeleteTask(Bstr(name.c_str()), 0);
        }
    }
    HRESULT shortcut_rollback = shortcut.rollback();
    if (FAILED(task_rollback)) return task_rollback;
    if (FAILED(shortcut_rollback)) return shortcut_rollback;
    return hr;
}
}
