#ifndef BONGO_CAT_WINDOWS_AUTOSTART_INTERNAL_H
#define BONGO_CAT_WINDOWS_AUTOSTART_INTERNAL_H

#include <windows.h>
#include <taskschd.h>
#include <string>

namespace bongo_autostart {
enum class Mode { Disabled, Standard, Administrator };

template<class T> struct Com {
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { if (p) p->Release(); }
    T *operator->() const { return p; }
};
struct Bstr {
    BSTR p = nullptr;
    Bstr() = default;
    explicit Bstr(const wchar_t *s) : p(SysAllocString(s)) {}
    Bstr(const Bstr &) = delete;
    Bstr &operator=(const Bstr &) = delete;
    ~Bstr() { SysFreeString(p); }
    operator BSTR() const { return p; }
};
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Apartment() = default;
    Apartment(const Apartment &) = delete;
    Apartment &operator=(const Apartment &) = delete;
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

HRESULT require_elevation();
HRESULT configure(Mode mode, const std::wstring &sid);
HRESULT create_task(ITaskService *service, ITaskFolder *folder,
    const std::wstring &name, const std::wstring &sid);

/* The transaction owns a copy of the old shortcut and restores it unless
   commit succeeds. Registry approval is changed only at commit. */
class ShortcutTransaction {
public:
    HRESULT begin();
    HRESULT apply(bool enabled);
    HRESULT commit(bool enable_standard);
    HRESULT rollback();
    ~ShortcutTransaction();
    ShortcutTransaction() = default;
    ShortcutTransaction(const ShortcutTransaction &) = delete;
    ShortcutTransaction &operator=(const ShortcutTransaction &) = delete;
private:
    std::wstring path;
    std::wstring backup;
    bool existed = false;
    bool changed = false;
    bool committed = false;
};
}
#endif
