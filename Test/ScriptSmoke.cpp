#include <Framework/Common/Log.h>
#include <Framework/Common/TimeMgr.h>
#include <Framework/Script/ScriptSystem.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace
{
    bool Check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "ScriptSmoke failed: " << message << std::endl;
            return false;
        }
        return true;
    }
}

int main()
{
#if !AILU_ENABLE_LUA_SCRIPTING
    std::cerr << "ScriptSmoke requires AILU_ENABLE_LUA_SCRIPTING=ON" << std::endl;
    return 2;
#else
    using namespace Ailu;

    LogMgr::Init();
    auto &script_system = ScriptSystem::Get();
    if (!Check(script_system.Initialize() == 0, "ScriptSystem initialize"))
    {
        LogMgr::Shutdown();
        return 1;
    }

    TimeMgr::TickTimeSinceLoad = 12.5f;
    script_system.Tick(0.25f);

    if (!Check(script_system.RunString("runstring_value = 42; delta_time_value = engine.delta_time()", "script_smoke_runstring"), "RunString returned false"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }

    const int runstring_value = script_system.GetGlobalInt("runstring_value", -1);
    const double delta_time_value = script_system.GetGlobalNumber("delta_time_value", -1.0);
    if (!Check(runstring_value == 42, "RunString did not populate expected global"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }
    if (!Check(std::fabs(delta_time_value - 0.25) < 1e-6, "engine.delta_time binding returned unexpected value"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }

    const fs::path bootstrap_path = fs::path(AILU_SOURCE_ROOT) / "Engine" / "Res" / "Scripts" / "bootstrap.lua";
    if (!Check(fs::exists(bootstrap_path), "bootstrap.lua not found"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }
    if (!Check(script_system.RunFile(bootstrap_path.string()), "RunFile returned false for bootstrap.lua"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }

    const bool bootstrap_loaded = script_system.GetGlobalBool("bootstrap_loaded", false);
    const double bootstrap_last_time = script_system.GetGlobalNumber("bootstrap_last_time", -1.0);
    if (!Check(bootstrap_loaded, "bootstrap.lua did not set bootstrap_loaded"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }
    if (!Check(std::fabs(bootstrap_last_time - 12.5) < 1e-6, "bootstrap.lua did not observe engine.time()"))
    {
        script_system.Finalize();
        LogMgr::Shutdown();
        return 1;
    }

    script_system.Finalize();
    LogMgr::Shutdown();
    std::cout << "ScriptSmoke passed" << std::endl;
    return 0;
#endif
}