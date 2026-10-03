#define COBJMACROS
#include "windows_package.h"
#include "bongo_cat/common.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>

/* desktop7:Shortcut creates a file link. Convert only that existing link to
   the AppsFolder item so Explorer uses packaged application activation for
   both Open and Run as administrator. Keep the manifest-owned filename so
   deployment still owns creation/removal; never recreate a user-deleted link. */
static HRESULT repair_shortcut(void) {
    typedef LONG (WINAPI *GetApplicationId)(UINT32 *, PWSTR);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    GetApplicationId get_id = kernel ? (GetApplicationId)(void *)
        GetProcAddress(kernel, "GetCurrentApplicationUserModelId") : NULL;
    if (!get_id) return S_FALSE;
    wchar_t app_id[BONGO_CAT_PATH_CAP];
    UINT32 capacity = BONGO_CAT_PATH_CAP;
    LONG code = get_id(&capacity, app_id);
    if (code != ERROR_SUCCESS) return HRESULT_FROM_WIN32(code);

    PWSTR desktop = NULL;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_Desktop, KF_FLAG_DONT_VERIFY,
        NULL, &desktop);
    if (FAILED(hr)) return hr;
    wchar_t path[BONGO_CAT_PATH_CAP], temporary[MAX_PATH] = {0};
    int length = swprintf(path, BONGO_CAT_PATH_CAP, L"%ls\\BongoCat.lnk", desktop);
    if (length < 0 || length >= BONGO_CAT_PATH_CAP) {
        CoTaskMemFree(desktop);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        code = (LONG)GetLastError();
        CoTaskMemFree(desktop);
        return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
            ? S_FALSE : HRESULT_FROM_WIN32(code);
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        CoTaskMemFree(desktop);
        return S_FALSE;
    }

    IShellLinkW *old_link = NULL, *app_link = NULL;
    IPersistFile *old_file = NULL, *app_file = NULL;
    PIDLIST_ABSOLUTE item = NULL;
    wchar_t target[BONGO_CAT_PATH_CAP] = {0}, executable[BONGO_CAT_PATH_CAP];
    wchar_t arguments[BONGO_CAT_PATH_CAP] = {0}, parsing_name[BONGO_CAT_PATH_CAP];
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
        &IID_IShellLinkW, (void **)&old_link);
    if (FAILED(hr)) goto done;
    hr = IShellLinkW_QueryInterface(old_link, &IID_IPersistFile, (void **)&old_file);
    if (FAILED(hr)) goto done;
    hr = IPersistFile_Load(old_file, path, STGM_READ);
    if (FAILED(hr)) goto done;
    hr = IShellLinkW_GetPath(old_link, target, BONGO_CAT_PATH_CAP, NULL, SLGP_RAWPATH);
    if (FAILED(hr)) goto done;
    DWORD executable_length = GetModuleFileNameW(NULL, executable, BONGO_CAT_PATH_CAP);
    if (!executable_length || executable_length >= BONGO_CAT_PATH_CAP) {
        hr = E_FAIL;
        goto done;
    }
    /* Native app links have no filesystem target. Also leave links belonging
       to the standalone edition or customized by the user untouched. */
    if (_wcsicmp(target, executable) != 0) { hr = S_FALSE; goto done; }
    hr = IShellLinkW_GetArguments(old_link, arguments, BONGO_CAT_PATH_CAP);
    if (FAILED(hr)) goto done;
    if (arguments[0]) { hr = S_FALSE; goto done; }
    length = swprintf(parsing_name, BONGO_CAT_PATH_CAP, L"shell:AppsFolder\\%ls", app_id);
    if (length < 0 || length >= BONGO_CAT_PATH_CAP) {
        hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        goto done;
    }
    hr = SHParseDisplayName(parsing_name, NULL, &item, 0, NULL);
    if (FAILED(hr)) goto done;
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
        &IID_IShellLinkW, (void **)&app_link);
    if (FAILED(hr)) goto done;
    hr = IShellLinkW_SetIDList(app_link, item);
    if (FAILED(hr)) goto done;
    hr = IShellLinkW_QueryInterface(app_link, &IID_IPersistFile, (void **)&app_file);
    if (FAILED(hr)) goto done;
    if (!GetTempFileNameW(desktop, L"bcs", 0, temporary)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }
    hr = IPersistFile_Save(app_file, temporary, TRUE);
    if (SUCCEEDED(hr) && !ReplaceFileW(path, temporary, NULL, 0, NULL, NULL))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, path, NULL);
done:
    if (temporary[0]) DeleteFileW(temporary);
    CoTaskMemFree(item);
    if (app_file) IPersistFile_Release(app_file);
    if (app_link) IShellLinkW_Release(app_link);
    if (old_file) IPersistFile_Release(old_file);
    if (old_link) IShellLinkW_Release(old_link);
    CoTaskMemFree(desktop);
    return hr;
}

void bongo_cat_windows_package_repair_shortcut(void) {
    if (!bongo_cat_windows_is_packaged()) return;
    HRESULT apartment = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HRESULT hr = SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE
        ? repair_shortcut() : apartment;
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (FAILED(hr)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot update MSIX desktop application shortcut: 0x%08lx", (unsigned long)hr);
}
