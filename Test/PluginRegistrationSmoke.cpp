#include "Framework/Plugin/PluginManager.h"
#include "Framework/Common/Log.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Ailu;

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class RegistrationAppender final : public IAppender
{
public:
    void Print(const LogMessage& message) override
    {
        if (!_manager)
            return;
        const bool unload = message._message == L"[Plugin] com.ailu.test.lifecycle:unload";
        const bool destroy = message._message == L"[Plugin] com.ailu.test.lifecycle:destroy";
        if (unload || destroy)
        {
            _clean_before_teardown = _clean_before_teardown && !_manager->HasRegistration(_handle)
                && _manager->Registrations(_owner).empty()
                && _manager->GetPluginToken("com.ailu.test.lifecycle")._value == 0;
            _new_registration_rejected = _new_registration_rejected
                && _manager->RegisterHostRecord(_owner, "during_unload")._value == 0;
            ++_observed_teardown_calls;
        }
    }
    PluginManager* _manager = nullptr;
    PluginToken _owner;
    PluginRegistrationHandle _handle;
    bool _clean_before_teardown = true;
    bool _new_registration_rejected = true;
    u32 _observed_teardown_calls = 0;
};

int main()
{
    LogMgr::Init();
    auto* appender = new RegistrationAppender;
    LogMgr::Get().AddAppender(appender);
    int result = 0;
    try
    {
        wchar_t executable_path[32768]{};
        Require(GetModuleFileNameW(nullptr, executable_path, 32768) != 0, "Executable path");
        const auto root = std::filesystem::path(executable_path).parent_path();
        const auto first_path = root / L"Plugins/LifecycleProbe/AiluPluginLifecycle.dll";
        const auto second_path = root / L"Plugins/LifecycleProbe/AiluPluginSecond.dll";
        PluginManager manager;
        Require(manager.Initialize() && manager.LoadPlugin(first_path.wstring())
            && manager.LoadPlugin(second_path.wstring()), "Load owners");
        const auto first_owner = manager.GetPluginToken("com.ailu.test.lifecycle");
        const auto second_owner = manager.GetPluginToken("com.ailu.test.second");
        Require(first_owner._value != 0 && second_owner._value != 0 && first_owner != second_owner,
            "Explicit distinct owner tokens");
        Require(manager.RegisterHostRecord({}, "invalid")._value == 0
            && manager.RegisterHostRecord(first_owner, "")._value == 0, "Invalid registration rejected");
        const auto first_handle = manager.RegisterHostRecord(first_owner, "first_record");
        const auto second_handle = manager.RegisterHostRecord(second_owner, "second_record");
        Require(manager.HasRegistration(first_handle) && manager.HasRegistration(second_handle), "Records active");
        const auto copied_records = manager.Registrations(first_owner);
        Require(copied_records.size() == 1 && copied_records[0]._owner == first_owner
            && copied_records[0]._handle == first_handle && copied_records[0]._name == "first_record"
            && copied_records[0]._type == EPluginRegistrationType::kHostRecord,
            "Host metadata carries owner/type/handle");
        Require(manager.ReleaseAllRegistrations(first_owner) == 1 && !manager.HasRegistration(first_handle)
            && manager.HasRegistration(second_handle), "ReleaseAll isolates owners");
        Require(!manager.ReleaseRegistration(first_handle) && manager.ReleaseRegistration(second_handle)
            && !manager.ReleaseRegistration(second_handle), "Individual release idempotent");
        const auto unload_handle = manager.RegisterHostRecord(first_owner, "unload_record");
        Require(unload_handle._value != first_handle._value, "Registration handle generation not reused");
        appender->_manager = &manager;
        appender->_owner = first_owner;
        appender->_handle = unload_handle;
        Require(manager.UnloadPlugin("com.ailu.test.lifecycle"), "Unload registered owner");
        Require(appender->_observed_teardown_calls == 2 && appender->_clean_before_teardown
            && appender->_new_registration_rejected, "Release precedes OnUnload/Destroy and new registration rejected");
        appender->_manager = nullptr;
        Require(!manager.HasRegistration(unload_handle) && manager.Registrations(first_owner).empty()
            && !GetModuleHandleW(first_path.c_str()), "Unload leaves no registration/module");
        Require(copied_records[0]._name == "first_record", "Engine-owned metadata snapshot survives unload");
        Require(manager.LoadPlugin(first_path.wstring()), "Reload owner");
        const auto reloaded_owner = manager.GetPluginToken("com.ailu.test.lifecycle");
        Require(reloaded_owner != first_owner && manager.RegisterHostRecord(first_owner, "stale")._value == 0,
            "Stale owner cannot register after reload");
        const auto fresh_handle = manager.RegisterHostRecord(reloaded_owner, "fresh");
        bool wrong_register = true, wrong_release = true;
        std::thread wrong_thread([&]
        {
            wrong_register = manager.RegisterHostRecord(reloaded_owner, "worker")._value != 0;
            wrong_release = manager.ReleaseRegistration(fresh_handle);
            manager.ReleaseAllRegistrations(reloaded_owner);
        });
        wrong_thread.join();
        Require(!wrong_register && !wrong_release && manager.HasRegistration(fresh_handle), "Owner-thread registry");
        {
            PluginManager other_manager;
            Require(other_manager.Initialize() && other_manager.LoadPlugin(first_path.wstring()), "Other manager");
            const auto other_owner = other_manager.GetPluginToken("com.ailu.test.lifecycle");
            const auto other_handle = other_manager.RegisterHostRecord(other_owner, "other");
            Require(other_owner != reloaded_owner
                && other_manager.RegisterHostRecord(reloaded_owner, "foreign")._value == 0
                && !other_manager.HasRegistration(fresh_handle) && !manager.HasRegistration(other_handle),
                "Tokens and handles never alias across managers");
            other_manager.Shutdown();
            Require(!other_manager.HasRegistration(other_handle), "Other manager shutdown cleans records");
        }
        manager.Shutdown();
        Require(!manager.HasRegistration(fresh_handle) && !GetModuleHandleW(first_path.c_str())
            && !GetModuleHandleW(second_path.c_str()), "Shutdown releases all owners and modules");
        Require(manager.Initialize() && manager.LoadPlugin(first_path.wstring()), "Reinitialize and load");
        Require(manager.GetPluginToken("com.ailu.test.lifecycle") != reloaded_owner
            && manager.RegisterHostRecord(reloaded_owner, "old_session")._value == 0,
            "No token reuse after reinitialize");
        manager.Shutdown();
        std::cout << "M6 PluginRegistration: PASS (ownership, cleanup ordering, stale handles, affinity, isolation)\n";
    }
    catch (const std::exception& error)
    {
        appender->_manager = nullptr;
        std::cerr << "M6 PluginRegistration: FAIL: " << error.what() << '\n';
        result = 1;
    }
    LogMgr::Shutdown();
    return result;
}
