#include "windows_autostart_internal.h"
extern "C" {
#include "bongo_cat/common.h"
}

namespace bongo_autostart {
HRESULT create_task(ITaskService *service, ITaskFolder *folder,
    const std::wstring &name, const std::wstring &sid) {
    wchar_t executable[BONGO_CAT_PATH_CAP];
    DWORD length = GetModuleFileNameW(nullptr, executable, BONGO_CAT_PATH_CAP);
    if (!length || length >= BONGO_CAT_PATH_CAP)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::wstring directory(executable);
    const auto separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return E_INVALIDARG;
    directory.resize(separator + 1);
    Com<ITaskDefinition> task;
    HRESULT hr = service->NewTask(0, &task.p);
    Com<IPrincipal> principal;
    if (SUCCEEDED(hr)) hr = task->get_Principal(&principal.p);
    if (SUCCEEDED(hr)) hr = principal->put_UserId(Bstr(sid.c_str()));
    if (SUCCEEDED(hr)) hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (SUCCEEDED(hr)) hr = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    Com<ITaskSettings> settings;
    if (SUCCEEDED(hr)) hr = task->get_Settings(&settings.p);
    if (SUCCEEDED(hr)) hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
    if (SUCCEEDED(hr)) hr = settings->put_StartWhenAvailable(VARIANT_TRUE);
    if (SUCCEEDED(hr)) hr = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if (SUCCEEDED(hr)) hr = settings->put_Enabled(VARIANT_TRUE);
    Com<ITriggerCollection> triggers;
    Com<ITrigger> trigger;
    Com<ILogonTrigger> logon;
    if (SUCCEEDED(hr)) hr = task->get_Triggers(&triggers.p);
    if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger.p);
    if (SUCCEEDED(hr)) hr = trigger->QueryInterface(IID_ILogonTrigger,
        reinterpret_cast<void **>(&logon.p));
    if (SUCCEEDED(hr)) hr = logon->put_UserId(Bstr(sid.c_str()));
    if (SUCCEEDED(hr)) hr = logon->put_Delay(Bstr(L"PT10S"));
    Com<IActionCollection> actions;
    Com<IAction> action;
    Com<IExecAction> exec;
    if (SUCCEEDED(hr)) hr = task->get_Actions(&actions.p);
    if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, &action.p);
    if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction,
        reinterpret_cast<void **>(&exec.p));
    if (SUCCEEDED(hr)) hr = exec->put_Path(Bstr(executable));
    if (SUCCEEDED(hr)) hr = exec->put_Arguments(Bstr(L"--autostart"));
    if (SUCCEEDED(hr)) hr = exec->put_WorkingDirectory(Bstr(directory.c_str()));
    Com<IRegisteredTask> registered;
    Bstr user(sid.c_str());
    VARIANT user_value;
    VariantInit(&user_value);
    user_value.vt = VT_BSTR;
    user_value.bstrVal = user.p;
    VARIANT empty;
    VariantInit(&empty);
    if (SUCCEEDED(hr)) hr = folder->RegisterTaskDefinition(Bstr(name.c_str()),
        task.p, TASK_CREATE_OR_UPDATE, user_value, empty,
        TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered.p);
    return hr;
}
}
