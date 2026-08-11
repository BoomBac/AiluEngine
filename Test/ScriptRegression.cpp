// T00: Script System Regression Baseline
// Covers: RunString/RunFile, engine bindings, lifecycle, multi-instance,
//         error handling, hot reload baseline, Entity handle API
//
// All instance state verification goes through Lua globals (RunString +
// GetGlobalInt/Bool/Number), avoiding direct sol::table access that would
// pull Lua C API symbols into the test executable.
//
// Usage: ScriptRegression.exe [--verbose]
// Exit code: 0 = all tests passed, 1 = test failure, 2 = Lua backend disabled

#include <Framework/Common/Log.h>
#include <Framework/Common/ResourceMgr.h>
#include <Framework/Common/TimeMgr.h>
#include <Framework/Script/ScriptSystem.h>
#include <Assets/Asset.h>
#include <Assets/ScriptAsset.h>
#include <Scene/Scene.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace Ailu;

// ============================================================================
// Test infrastructure (same style as ScriptSmoke.cpp + Test.cpp)
// ============================================================================
namespace
{
    static bool s_verbose = false;

    struct TestStats
    {
        u32 _passed = 0u;
        u32 _failed = 0u;
    };

    bool Check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "  FAIL: " << message << std::endl;
            return false;
        }
        if (s_verbose)
            std::cout << "  OK:   " << message << std::endl;
        return true;
    }

    void RunTest(TestStats &stats, const char *name, bool (*test_func)())
    {
        std::cout << "[" << name << "]" << std::endl;
        bool passed = false;
        try
        {
            passed = test_func();
        }
        catch (const std::exception &e)
        {
            std::cerr << "  EXCEPTION: " << e.what() << std::endl;
            passed = false;
        }
        catch (...)
        {
            std::cerr << "  UNKNOWN EXCEPTION" << std::endl;
            passed = false;
        }

        std::cout << "  Result: " << (passed ? "PASS" : "FAIL") << std::endl;
        if (passed)
            ++stats._passed;
        else
            ++stats._failed;
    }

    fs::path ScriptPath(const char *name)
    {
        return fs::path(AILU_SOURCE_ROOT) / "Engine" / "Res" / "Scripts" / name;
    }

    fs::path LuaDeclarationPath()
    {
        return fs::path(AILU_SOURCE_ROOT) / ".ailu" / "lua" / "ailu_engine.lua";
    }

    ECS::ScriptComponent &AddScript(SceneManagement::Scene &scene, ECS::Entity entity, const String &path)
    {
        const Guid script_guid = Guid::Generate();
        const WString asset_path = L"runtime://script_" + ToWChar(script_guid.ToString()) + L".alasset";
        auto asset = MakeScope<Asset>(script_guid, ScriptAsset::StaticType(), asset_path);
        asset->_p_obj = MakeRef<ScriptAsset>(path);
        ResourceMgr::Get().RegisterAsset(std::move(asset));
        return scene.GetRegister().AddComponent<ECS::ScriptComponent>(entity, script_guid);
    }
} // namespace

// ============================================================================
// Test 1: Basic Lua Execution
// ============================================================================
bool TestBasicLua()
{
    auto &ss = ScriptSystem::Get();

    // 1a. RunString: execute code and read back globals
    if (!Check(ss.RunString("reg_runstring_val = 99", "test_runstring"),
               "RunString should succeed"))
        return false;
    if (!Check(ss.GetGlobalInt("reg_runstring_val", -1) == 99,
               "RunString: global should be 99"))
        return false;

    // 1b. engine.time() binding
    TimeMgr::TickTimeSinceLoad = 42.0f;
    ss.Tick(0.016f);
    if (!Check(ss.RunString("reg_time_val = engine.time()", "test_time"),
               "RunString for engine.time() should succeed"))
        return false;
    f64 time_val = ss.GetGlobalNumber("reg_time_val", -1.0);
    if (!Check(std::fabs(time_val - 42.0) < 1e-6,
               "engine.time() should return " + std::to_string(time_val) + " (expected 42.0)"))
        return false;

    // 1c. engine.delta_time() binding
    if (!Check(ss.RunString("reg_dt_val = engine.delta_time()", "test_dt"),
               "RunString for engine.delta_time() should succeed"))
        return false;
    f64 dt_val = ss.GetGlobalNumber("reg_dt_val", -1.0);
    if (!Check(std::fabs(dt_val - 0.016) < 1e-6,
               "engine.delta_time() should return " + std::to_string(dt_val) + " (expected 0.016)"))
        return false;

    // 1d. engine.fixed_delta_time() binding
    if (!Check(ss.RunString("reg_fdt_val = engine.fixed_delta_time()", "test_fdt"),
               "RunString for engine.fixed_delta_time() should succeed"))
        return false;

    // 1e. engine.render_alpha() binding
    if (!Check(ss.RunString("reg_ra_val = engine.render_alpha()", "test_ra"),
               "RunString for engine.render_alpha() should succeed"))
        return false;

    // 1f. engine.log() binding (should not crash)
    if (!Check(ss.RunString("engine.log('T00 regression: basic log test')", "test_log"),
               "engine.log() should succeed"))
        return false;

    // 1g. RunFile: load test_basic.lua standalone
    fs::path basic_path = ScriptPath("test_basic.lua");
    if (!Check(fs::exists(basic_path), "test_basic.lua file should exist"))
        return false;
    if (!Check(ss.RunFile(basic_path.string()), "RunFile(test_basic.lua) should succeed"))
        return false;
    if (!Check(ss.GetGlobalBool("test_basic_loaded", false),
               "test_basic.lua: test_basic_loaded should be true"))
        return false;

    return true;
}

// ============================================================================
// Test 2: Script Lifecycle (OnCreate/OnFixedUpdate/OnUpdate/OnLateUpdate/OnDestroy)
// ============================================================================
bool TestLifecycle()
{
    auto &ss = ScriptSystem::Get();

    // Reset the lifecycle global record
    ss.RunString("lifecycle_record = {}; reg_lc_done = false");

    SceneManagement::Scene scene("RegTest_Lifecycle", false);
    ECS::Entity entity = scene.AddObject("LifecycleEntity");
    if (!Check(entity != ECS::kInvalidEntity, "Entity creation should succeed"))
        return false;

    fs::path script_path = ScriptPath("test_lifecycle.lua");
    if (!Check(fs::exists(script_path), "test_lifecycle.lua should exist"))
        return false;

    auto &comp = AddScript(scene, entity, script_path.string());

    // Drive lifecycle in order
    ss.FixedUpdateComponent(&scene, entity, comp, 0.02f);   // OnCreate + OnFixedUpdate
    ss.UpdateComponent(&scene, entity, comp, 0.016f);        // OnUpdate
    ss.LateUpdateComponent(&scene, entity, comp, 0.016f, 0.5f); // OnLateUpdate
    ss.DestroyComponent(&scene, entity, comp);                 // OnDestroy

    // Count each event from the global lifecycle_record
    ss.RunString(
        "reg_lc_init = 0; reg_lc_fixed = 0; reg_lc_update = 0; "
        "reg_lc_late = 0; reg_lc_destroy = 0; "
        "reg_lc_order_str = ''; "
        "for _, v in ipairs(lifecycle_record) do "
        "  if v == 'OnCreate' then reg_lc_init = reg_lc_init + 1 "
        "  elseif v == 'OnFixedUpdate' then reg_lc_fixed = reg_lc_fixed + 1 "
        "  elseif v == 'OnUpdate' then reg_lc_update = reg_lc_update + 1 "
        "  elseif v == 'OnLateUpdate' then reg_lc_late = reg_lc_late + 1 "
        "  elseif v == 'OnDestroy' then reg_lc_destroy = reg_lc_destroy + 1 "
        "  end; "
        "  reg_lc_order_str = reg_lc_order_str .. v .. '|'; "
        "end; "
        "reg_lc_total = #lifecycle_record");

    bool all_ok = true;
    all_ok &= Check(ss.GetGlobalInt("reg_lc_init", -1) == 1,
                    "OnCreate count: " + std::to_string(ss.GetGlobalInt("reg_lc_init", -1)));
    all_ok &= Check(ss.GetGlobalInt("reg_lc_fixed", -1) == 1,
                    "OnFixedUpdate count: " + std::to_string(ss.GetGlobalInt("reg_lc_fixed", -1)));
    all_ok &= Check(ss.GetGlobalInt("reg_lc_update", -1) == 1,
                    "OnUpdate count: " + std::to_string(ss.GetGlobalInt("reg_lc_update", -1)));
    all_ok &= Check(ss.GetGlobalInt("reg_lc_late", -1) == 1,
                    "OnLateUpdate count: " + std::to_string(ss.GetGlobalInt("reg_lc_late", -1)));
    all_ok &= Check(ss.GetGlobalInt("reg_lc_destroy", -1) == 1,
                    "OnDestroy count: " + std::to_string(ss.GetGlobalInt("reg_lc_destroy", -1)));
    all_ok &= Check(ss.GetGlobalInt("reg_lc_total", -1) == 5,
                    "Total lifecycle events: " + std::to_string(ss.GetGlobalInt("reg_lc_total", -1)));

    // Verify order: OnCreate first, OnDestroy last
    ss.RunString(
        "reg_lc_first_ok = (lifecycle_record[1] == 'OnCreate'); "
        "reg_lc_last_ok = (lifecycle_record[#lifecycle_record] == 'OnDestroy')");
    all_ok &= Check(ss.GetGlobalBool("reg_lc_first_ok", false),
                    "First event should be OnCreate");
    all_ok &= Check(ss.GetGlobalBool("reg_lc_last_ok", false),
                    "Last event should be OnDestroy");

    return all_ok;
}

// ============================================================================
// Test 3: Multiple Entity Instance State Isolation
// ============================================================================
bool TestMultipleInstance()
{
    auto &ss = ScriptSystem::Get();

    // Reset globals
    ss.RunString("reg_inst_counter_A = -999; reg_inst_counter_B = -999; "
                 "reg_inst_fixed_A = -999; reg_inst_fixed_B = -999");

    SceneManagement::Scene scene("RegTest_MultiInstance", false);

    ECS::Entity entity_a = scene.AddObject("InstanceA");
    ECS::Entity entity_b = scene.AddObject("InstanceB");
    if (!Check(entity_a != ECS::kInvalidEntity && entity_b != ECS::kInvalidEntity,
               "Both entities should be created"))
        return false;

    // Set distinct names for the Lua-side global-key dispatch
    if (auto *tag_a = scene.GetRegister().GetComponent<ECS::TagComponent>(entity_a))
        tag_a->_name = "InstanceA";
    if (auto *tag_b = scene.GetRegister().GetComponent<ECS::TagComponent>(entity_b))
        tag_b->_name = "InstanceB";

    fs::path script_path = ScriptPath("test_instance_state.lua");
    if (!Check(fs::exists(script_path), "test_instance_state.lua should exist"))
        return false;

    AddScript(scene, entity_a, script_path.string());
    AddScript(scene, entity_b, script_path.string());
    auto *comp_a = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity_a);
    auto *comp_b = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity_b);
    if (!Check(comp_a != nullptr && comp_b != nullptr, "Script components should be created"))
        return false;

    // Initialize both (OnCreate runs, sets counter=0 and writes to globals)
    ss.FixedUpdateComponent(&scene, entity_a, *comp_a, 0.02f);
    ss.FixedUpdateComponent(&scene, entity_b, *comp_b, 0.02f);

    // Verify initialization: both counters should be 0
    if (!Check(ss.GetGlobalInt("reg_inst_counter_InstanceA", -999) == 0,
               "Entity A counter init: " + std::to_string(ss.GetGlobalInt("reg_inst_counter_InstanceA", -999))))
        return false;
    if (!Check(ss.GetGlobalInt("reg_inst_counter_InstanceB", -999) == 0,
               "Entity B counter init: " + std::to_string(ss.GetGlobalInt("reg_inst_counter_InstanceB", -999))))
        return false;

    // Update Entity A: 3 times, Entity B: 1 time
    ss.UpdateComponent(&scene, entity_a, *comp_a, 0.016f);
    ss.UpdateComponent(&scene, entity_a, *comp_a, 0.016f);
    ss.UpdateComponent(&scene, entity_a, *comp_a, 0.016f);
    ss.UpdateComponent(&scene, entity_b, *comp_b, 0.016f);

    bool all_ok = true;
    all_ok &= Check(ss.GetGlobalInt("reg_inst_counter_InstanceA", -999) == 3,
                    "Entity A counter after 3 updates: " +
                    std::to_string(ss.GetGlobalInt("reg_inst_counter_InstanceA", -999)));
    all_ok &= Check(ss.GetGlobalInt("reg_inst_counter_InstanceB", -999) == 1,
                    "Entity B counter after 1 update: " +
                    std::to_string(ss.GetGlobalInt("reg_inst_counter_InstanceB", -999)));

    // FixedUpdate counters: both got 1 (from initialization)
    all_ok &= Check(ss.GetGlobalInt("reg_inst_fixed_InstanceA", -999) == 1,
                    "Entity A fixed_counter: " +
                    std::to_string(ss.GetGlobalInt("reg_inst_fixed_InstanceA", -999)));
    all_ok &= Check(ss.GetGlobalInt("reg_inst_fixed_InstanceB", -999) == 1,
                    "Entity B fixed_counter: " +
                    std::to_string(ss.GetGlobalInt("reg_inst_fixed_InstanceB", -999)));

    // Cleanup
    ss.DestroyComponent(&scene, entity_a, *comp_a);
    ss.DestroyComponent(&scene, entity_b, *comp_b);

    return all_ok;
}

// ============================================================================
// Test 4: Shared ScriptPrototype
// ============================================================================
bool TestSharedPrototype()
{
    auto &ss = ScriptSystem::Get();
    ss.RunString("reg_prototype_top_level_count = 0");

    SceneManagement::Scene scene("RegTest_SharedPrototype", false);
    fs::path script_path = ScriptPath("test_prototype.lua");
    if (!Check(fs::exists(script_path), "test_prototype.lua should exist"))
        return false;

    Vector<ECS::Entity> entities;
    entities.reserve(10u);
    for (u32 index = 0u; index < 10u; ++index)
    {
        const ECS::Entity entity = scene.AddObject(std::format("PrototypeEntity{}", index));
        if (!Check(entity != ECS::kInvalidEntity, "Prototype test entity should be created"))
            return false;
        entities.push_back(entity);
        AddScript(scene, entity, script_path.string());
    }

    for (const ECS::Entity entity : entities)
    {
        auto *component = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity);
        if (!Check(component != nullptr, "Prototype test ScriptComponent should be created"))
            return false;
        ss.FixedUpdateComponent(&scene, entity, *component, 0.02f);
    }

    bool all_ok = Check(ss.GetGlobalInt("reg_prototype_top_level_count", -1) == 1,
                        "Shared ScriptPrototype top-level chunk should execute once for 10 entities");
    for (const ECS::Entity entity : entities)
    {
        auto *component = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity);
        ss.DestroyComponent(&scene, entity, *component);
    }
    return all_ok;
}

// ============================================================================
// Test 5: Script Facade API
// ============================================================================
bool TestFacadeApi()
{
    auto &ss = ScriptSystem::Get();
    ss.RunString("reg_facade_started = false; reg_facade_position_ok = false; reg_facade_guid_ok = false; reg_facade_find_name_ok = false; "
                 "reg_facade_created_ok = false; reg_facade_time_ok = false; reg_facade_input_safe = false");

    SceneManagement::Scene scene("RegTest_Facade", false);
    const ECS::Entity entity = scene.AddObject("FacadeEntity");
    fs::path script_path = ScriptPath("test_facade.lua");
    if (!Check(entity != ECS::kInvalidEntity && fs::exists(script_path), "Facade test entity and script should exist"))
        return false;

    auto &component = AddScript(scene, entity, script_path.string());
    ss.FixedUpdateComponent(&scene, entity, component, 0.02f);

    bool all_ok = true;
    all_ok &= Check(ss.GetGlobalBool("reg_facade_started", false), "Script facade OnCreate should execute");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_position_ok", false), "ScriptTransform should use formal local-position setter");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_guid_ok", false), "ScriptScene should find an entity by persistent Guid");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_find_name_ok", false), "ScriptScene should find an entity by name");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_created_ok", false), "ScriptScene should create an entity");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_time_ok", false), "ScriptTime properties should be available");
    all_ok &= Check(ss.GetGlobalBool("reg_facade_input_safe", false), "ScriptInput should safely handle an absent InputSystem");
    ss.DestroyComponent(&scene, entity, component);
    return all_ok;
}

// ============================================================================
// Test 6: AHT-generated LuaLS declarations stay synchronized with bindings
// ============================================================================
bool TestGeneratedLuaDeclarations()
{
    auto &ss = ScriptSystem::Get();
    if (!Check(ss.RunString("reg_t05_render_alpha_binding = time:get_render_alpha() >= 0.0", "test_t05_binding"),
               "Generated ScriptTime binding should be callable"))
        return false;
    if (!Check(ss.GetGlobalBool("reg_t05_render_alpha_binding", false),
               "Generated ScriptTime binding should return a number"))
        return false;

    const fs::path declaration_path = LuaDeclarationPath();
    if (!Check(fs::exists(declaration_path), "AHT should generate .ailu/lua/ailu_engine.lua"))
        return false;
    std::ifstream declaration_file(declaration_path);
    const std::string declaration(std::istreambuf_iterator<char>(declaration_file), {});
    bool all_ok = true;
    all_ok &= Check(declaration.find("---@class ScriptTime") != std::string::npos,
                    "LuaLS declarations should contain ScriptTime");
    all_ok &= Check(declaration.find("function ScriptTime:get_render_alpha() end") != std::string::npos,
                    "LuaLS declarations should contain generated get_render_alpha API");
    all_ok &= Check(declaration.find("function AiluScript:OnUpdate(dt) end") != std::string::npos,
                    "LuaLS declarations should contain lifecycle API");
    return all_ok;
}

// ============================================================================
// Test 6: Lua Error Handling
// ============================================================================
bool TestErrorHandling()
{
    auto &ss = ScriptSystem::Get();

    // Reset globals
    ss.RunString("reg_err_init_called = false; reg_err_update_tried = false; "
                 "reg_err_healthy_counter = -1");

    SceneManagement::Scene scene("RegTest_ErrorHandling", false);

    ECS::Entity entity_a = scene.AddObject("ErrorEntity");
    ECS::Entity entity_b = scene.AddObject("HealthyEntity");
    if (!Check(entity_a != ECS::kInvalidEntity && entity_b != ECS::kInvalidEntity,
               "Both entities should be created"))
        return false;

    if (auto *tag_b = scene.GetRegister().GetComponent<ECS::TagComponent>(entity_b))
        tag_b->_name = "HealthyEntity";

    fs::path error_path = ScriptPath("test_error.lua");
    fs::path healthy_path = ScriptPath("test_instance_state.lua");
    if (!Check(fs::exists(error_path) && fs::exists(healthy_path),
               "Test script files should exist"))
        return false;

    AddScript(scene, entity_a, error_path.string());
    AddScript(scene, entity_b, healthy_path.string());
    auto *comp_a = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity_a);
    auto *comp_b = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity_b);
    if (!Check(comp_a != nullptr && comp_b != nullptr, "Script components should be created"))
        return false;

    // Initialize both
    ss.FixedUpdateComponent(&scene, entity_a, *comp_a, 0.02f);
    ss.FixedUpdateComponent(&scene, entity_b, *comp_b, 0.02f);

    bool all_ok = true;
    all_ok &= Check(ss.GetGlobalBool("reg_err_init_called", false),
                    "Error script OnCreate should be called");

    // Update Entity A (error script) — must NOT crash the process
    ss.UpdateComponent(&scene, entity_a, *comp_a, 0.016f);

    // The error script set reg_err_update_tried=true in OnUpdate BEFORE error()
    all_ok &= Check(ss.GetGlobalBool("reg_err_update_tried", false),
                    "Error script OnUpdate should be reached (error() called after flag is set)");

    // Entity B (healthy) must still work normally
    ss.UpdateComponent(&scene, entity_b, *comp_b, 0.016f);
    ss.UpdateComponent(&scene, entity_b, *comp_b, 0.016f);

    all_ok &= Check(ss.GetGlobalInt("reg_inst_counter_HealthyEntity", -1) == 2,
                    "Healthy script counter after 2 updates: " +
                    std::to_string(ss.GetGlobalInt("reg_inst_counter_HealthyEntity", -1)) +
                    " (expected 2) — other script's error must not affect it");

    // Record current baseline behavior: the error script's instance is not
    // fault-isolated. It is still callable, and the error is per-invocation.
    // This is expected behavior for T00.

    // Cleanup
    ss.DestroyComponent(&scene, entity_a, *comp_a);
    ss.DestroyComponent(&scene, entity_b, *comp_b);

    return all_ok;
}

// ============================================================================
// Test 5: Hot Reload Baseline (record current behavior only)
// ============================================================================
bool TestHotReloadBaseline()
{
    auto &ss = ScriptSystem::Get();

    fs::path script_path = ScriptPath("test_basic.lua");
    if (!Check(fs::exists(script_path), "test_basic.lua should exist"))
        return false;

    // Reset global set by test_basic.lua
    ss.RunString("test_basic_loaded = false");

    SceneManagement::Scene scene("RegTest_HotReload", false);
    ECS::Entity entity = scene.AddObject("ReloadEntity");
    if (!Check(entity != ECS::kInvalidEntity, "Entity creation should succeed"))
        return false;

    auto &comp = AddScript(scene, entity, script_path.string());

    // Initialize — triggers LoadComponentInstance which records the file version
    ss.FixedUpdateComponent(&scene, entity, comp, 0.02f);

    bool all_ok = true;
    all_ok &= Check(ss.GetGlobalBool("test_basic_loaded", false),
                    "Script instance should be initialized");

    // 5a. Simulate a file change and verify the hot-reload queue processes
    ss.OnScriptFileChanged(script_path);
    ss.Tick(0.016f); // Process the reload queue

    // Verify engine doesn't crash after reload processing
    all_ok &= Check(true, "Engine survived hot-reload queue processing");

    // 5b. Verify component still works after reload notification
    ss.UpdateComponent(&scene, entity, comp, 0.016f);
    all_ok &= Check(ss.GetGlobalBool("test_basic_loaded", false),
                    "Script instance should still be initialized after hot reload");

    // NOTE: Current implementation triggers RunFile() on reload but does NOT
    // automatically rebuild ScriptComponent instances. Components re-check
    // version in EnsureComponentReady() on the next update cycle.
    // This is the baseline behavior recorded for T08 improvement.

    // Cleanup
    ss.DestroyComponent(&scene, entity, comp);

    return all_ok;
}

// ============================================================================
// Test 6: ScriptEntityHandle API
// ============================================================================
bool TestEntityHandle()
{
    auto &ss = ScriptSystem::Get();

    // Reset globals
    ss.RunString("reg_ent_init_ok = false; reg_ent_valid = false; "
                 "reg_ent_pos_x = -999; reg_ent_update_ok = false; "
                 "reg_ent_last_dt = -999");

    SceneManagement::Scene scene("RegTest_EntityHandle", false);
    ECS::Entity entity = scene.AddObject("HandleEntity");
    if (!Check(entity != ECS::kInvalidEntity, "Entity creation should succeed"))
        return false;

    if (auto *tag = scene.GetRegister().GetComponent<ECS::TagComponent>(entity))
        tag->_name = "HandleEntity";

    // Set entity position for verification (ScriptEntityHandle::GetPosition reads _local_transform._position)
    if (auto *transform = scene.GetRegister().GetComponent<ECS::TransformComponent>(entity))
    {
        transform->_local_transform._position = Vector3f(1.0f, 2.0f, 3.0f);
    }

    fs::path script_path = ScriptPath("test_basic.lua");
    auto &comp = AddScript(scene, entity, script_path.string());

    // Initialize + first update
    ss.FixedUpdateComponent(&scene, entity, comp, 0.02f);
    ss.UpdateComponent(&scene, entity, comp, 0.016f);

    bool all_ok = true;

    // Check globals set by test_basic.lua's OnCreate
    all_ok &= Check(ss.GetGlobalBool("reg_ent_init_HandleEntity", false),
                    "Entity handle: OnCreate should set init flag");
    all_ok &= Check(ss.GetGlobalBool("reg_ent_valid_HandleEntity", false),
                    "Entity handle: is_valid() should return true");
    all_ok &= Check(std::fabs(ss.GetGlobalNumber("reg_ent_pos_x_HandleEntity", -999.0) - 1.0) < 1e-6,
                    "Entity handle: get_position()[1] should be 1.0");

    // Check globals set by test_basic.lua's OnUpdate
    all_ok &= Check(ss.GetGlobalBool("reg_ent_update_HandleEntity", false),
                    "Entity handle: OnUpdate should set update flag");
    all_ok &= Check(std::fabs(ss.GetGlobalNumber("reg_ent_last_dt_HandleEntity", -999.0) - 0.016) < 1e-6,
                    "Entity handle: OnUpdate dt should be 0.016");

    // Verify ScriptEntityHandle C++ API
    ScriptEntityHandle handle{&scene, entity};
    all_ok &= Check(handle.IsValid(), "ScriptEntityHandle::IsValid() should be true");
    all_ok &= Check(handle.GetName() == "HandleEntity",
                    "ScriptEntityHandle::GetName() should return 'HandleEntity'");
    auto pos = handle.GetPosition();
    all_ok &= Check(std::fabs(pos[0] - 1.0f) < 1e-6f && std::fabs(pos[1] - 2.0f) < 1e-6f,
                    "ScriptEntityHandle::GetPosition() should return (1,2,3)");

    // Verify invalid handle
    ScriptEntityHandle invalid_handle{nullptr, ECS::kInvalidEntity};
    all_ok &= Check(!invalid_handle.IsValid(), "Null-handle should report invalid");

    // Cleanup
    ss.DestroyComponent(&scene, entity, comp);

    return all_ok;
}

// ============================================================================
// Main entry point
// ============================================================================
int main(int argc, char **argv)
{
#if !AILU_ENABLE_LUA_SCRIPTING
    std::cerr << "ScriptRegression requires AILU_ENABLE_LUA_SCRIPTING=ON" << std::endl;
    return 2;
#else
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v")
            s_verbose = true;
    }

    LogMgr::Init();
    TimeMgr::Init();

    auto &ss = ScriptSystem::Get();
    if (ss.Initialize() != 0)
    {
        std::cerr << "ScriptSystem::Initialize() failed" << std::endl;
        LogMgr::Shutdown();
        return 1;
    }

    TimeMgr::TickTimeSinceLoad = 12.5f;
    ss.Tick(0.016f);

    std::cout << "========================================" << std::endl;
    std::cout << "  AiluEngine T00 Script Regression" << std::endl;
    std::cout << "========================================" << std::endl;

    TestStats stats{};

    RunTest(stats, "Basic Lua Execution", TestBasicLua);
    RunTest(stats, "Script Lifecycle", TestLifecycle);
    RunTest(stats, "Multiple Instance Isolation", TestMultipleInstance);
    RunTest(stats, "Shared ScriptPrototype", TestSharedPrototype);
    RunTest(stats, "Script Facade API", TestFacadeApi);
    RunTest(stats, "Generated LuaLS Declarations", TestGeneratedLuaDeclarations);
    RunTest(stats, "Lua Error Handling", TestErrorHandling);
    RunTest(stats, "Hot Reload Baseline", TestHotReloadBaseline);
    RunTest(stats, "Entity Handle API", TestEntityHandle);

    std::cout << "========================================" << std::endl;
    std::cout << "  Passed: " << stats._passed << " / " << (stats._passed + stats._failed) << std::endl;
    std::cout << "  Failed: " << stats._failed << std::endl;
    std::cout << "========================================" << std::endl;

    ss.Finalize();
    LogMgr::Shutdown();

    if (stats._failed > 0u)
    {
        std::cerr << "ScriptRegression: " << stats._failed << " test(s) FAILED" << std::endl;
        return 1;
    }

    std::cout << "ScriptRegression: ALL TESTS PASSED" << std::endl;
    return 0;
#endif
}
