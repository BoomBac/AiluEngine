#include "Framework/Script/ScriptSystem.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/StackTrace.h"
#include "Framework/Common/TimeMgr.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "pch.h"

namespace Ailu
{
    namespace fs = std::filesystem;

    namespace
    {
        std::optional<fs::path> ResolveScriptPath(const String &path)
        {
            if (path.empty())
                return std::nullopt;

            fs::path direct_path(path);
            if (fs::exists(direct_path))
                return fs::weakly_canonical(direct_path);

            fs::path resource_path(ToChar(ResourceMgr::GetResSysPath(ToWChar(path))));
            if (fs::exists(resource_path))
                return fs::weakly_canonical(resource_path);

            return std::nullopt;
        }

            String NormalizeScriptKey(const fs::path &path)
            {
                return PathUtils::FormatFilePath(path.string());
            }

    #if AILU_ENABLE_LUA_SCRIPTING
            template<typename... Args>
            bool InvokeOptionalMethod(sol::protected_function &traceback, sol::table &instance, const String &method_name, Args &&...args)
            {
                sol::object object = instance[method_name];
                if (!object.valid() || object.get_type() != sol::type::function)
                    return true;

                sol::protected_function function = object.as<sol::protected_function>();
                if (traceback.valid())
                {
                    function.set_error_handler(traceback);
                }
                sol::protected_function_result result = function(instance, std::forward<Args>(args)...);
                return result.valid();
            }
    #endif
    }

        bool ScriptEntityHandle::IsValid() const
        {
            if (_scene == nullptr || _entity == ECS::kInvalidEntity)
                return false;
            return _scene->GetRegister().GetComponent<ECS::TagComponent>(_entity) != nullptr;
        }

        String ScriptEntityHandle::GetName() const
        {
            if (!IsValid())
                return {};
            if (const auto *tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(_entity))
                return tag->_name;
            return {};
        }

        Array<f32, 3> ScriptEntityHandle::GetPosition() const
        {
            if (!IsValid())
                return {0.0f, 0.0f, 0.0f};
            if (const auto *transform = _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity))
                return {transform->_local_transform._position.x, transform->_local_transform._position.y, transform->_local_transform._position.z};
            return {0.0f, 0.0f, 0.0f};
        }

        void ScriptEntityHandle::SetPosition(f32 x, f32 y, f32 z) const
        {
            if (!IsValid())
                return;
            if (auto *transform = _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity))
            {
                transform->_local_transform._position = Vector3f(x, y, z);
                //_scene->MarkDirty();
            }
        }

    ScriptSystem &ScriptSystem::Get()
    {
        static ScriptSystem s_script_system;
        return s_script_system;
    }

    int ScriptSystem::Initialize()
    {
        if (_is_initialized)
            return 0;

#if AILU_ENABLE_LUA_SCRIPTING
        _lua.open_libraries(sol::lib::base,
                            sol::lib::coroutine,
                            sol::lib::debug,
                            sol::lib::math,
                            sol::lib::os,
                            sol::lib::package,
                            sol::lib::string,
                            sol::lib::table);
        RegisterCoreBindings();
        _traceback = _lua["debug"]["traceback"];
        LOG_INFO("ScriptSystem initialized with Lua backend");
#else
        LOG_WARNING("ScriptSystem initialized without Lua backend. Enable AILU_ENABLE_LUA_SCRIPTING and provide Lua/sol2 to activate scripting.");
#endif

        _is_initialized = true;
        return 0;
    }

    void ScriptSystem::Finalize()
    {
#if AILU_ENABLE_LUA_SCRIPTING
        _traceback = sol::protected_function();
        _lua = sol::state();
#endif
        _loaded_script_files.clear();
        _pending_reload_files.clear();
        while (!_reload_queue.empty())
            _reload_queue.pop();
        _last_delta_time = 0.0f;
        _last_fixed_delta_time = 0.0f;
        _last_render_alpha = 0.0f;
        _is_initialized = false;
    }

    void ScriptSystem::Tick(f32 delta_time)
    {
        _last_delta_time = delta_time;
        while (!_reload_queue.empty())
        {
            const fs::path reload_path = _reload_queue.front();
            _reload_queue.pop();
            _pending_reload_files.erase(NormalizeScriptKey(reload_path));
            LOG_INFO("ScriptSystem hot reload: {}", reload_path.string());
            RunFile(reload_path.string());
        }
    }

    bool ScriptSystem::RunFile(const String &path)
    {
        if (!_is_initialized)
        {
            Initialize();
        }

#if AILU_ENABLE_LUA_SCRIPTING
        auto resolved = ResolveScriptPath(path);
        if (!resolved.has_value())
        {
            LOG_ERROR("ScriptSystem::RunFile failed, script not found: {}", path);
            return false;
        }

        sol::load_result chunk = _lua.load_file(resolved->string());
        if (!ExecuteChunk(std::move(chunk), resolved->string()))
            return false;

        _loaded_script_files[NormalizeScriptKey(*resolved)] = *resolved;
        return true;
#else
        LOG_WARNING("ScriptSystem::RunFile ignored because Lua backend is disabled: {}", path);
        return false;
#endif
    }

    bool ScriptSystem::RunString(const String &code, const String &chunk_name)
    {
        if (!_is_initialized)
        {
            Initialize();
        }

#if AILU_ENABLE_LUA_SCRIPTING
        sol::load_result chunk = _lua.load(code, chunk_name);
        return ExecuteChunk(std::move(chunk), chunk_name);
#else
        LOG_WARNING("ScriptSystem::RunString ignored because Lua backend is disabled: {}", chunk_name);
        return false;
#endif
    }

    i32 ScriptSystem::GetGlobalInt(const String &name, i32 fallback) const
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (!_is_initialized)
            return fallback;
        return _lua[name].get_or(fallback);
#else
        (void) name;
        return fallback;
#endif
    }

    f64 ScriptSystem::GetGlobalNumber(const String &name, f64 fallback) const
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (!_is_initialized)
            return fallback;
        return _lua[name].get_or(fallback);
#else
        (void) name;
        return fallback;
#endif
    }

    bool ScriptSystem::GetGlobalBool(const String &name, bool fallback) const
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (!_is_initialized)
            return fallback;
        return _lua[name].get_or(fallback);
#else
        (void) name;
        return fallback;
#endif
    }

    void ScriptSystem::OnScriptFileChanged(const fs::path &path)
    {
        if (path.extension() != ".lua")
            return;

        auto resolved = ResolveScriptPath(path.string());
        if (!resolved.has_value())
            return;

        const String key = NormalizeScriptKey(*resolved);
        _script_versions[key]++;
        if (!_loaded_script_files.contains(key))
            return;
        if (_pending_reload_files.contains(key))
            return;

        _pending_reload_files.emplace(key);
        _reload_queue.emplace(*resolved);
    }

    bool ScriptSystem::IsEnabled() const
    {
#if AILU_ENABLE_LUA_SCRIPTING
        return _is_initialized;
#else
        return false;
#endif
    }

#if AILU_ENABLE_LUA_SCRIPTING
    void ScriptSystem::RegisterCoreBindings()
    {
        _lua.new_usertype<ScriptEntityHandle>("Entity",
                                              "is_valid", &ScriptEntityHandle::IsValid,
                                              "get_name", &ScriptEntityHandle::GetName,
                                              "get_position", &ScriptEntityHandle::GetPosition,
                                              "set_position", &ScriptEntityHandle::SetPosition);
        sol::table engine = _lua["engine"].get_or_create<sol::table>();
        engine.set_function("log", [](const String &message)
        {
            LOG_INFO("[Lua] {}", message);
        });
        engine.set_function("time", []()
        {
            return TimeMgr::TickTimeSinceLoad;
        });
        engine.set_function("delta_time", [this]()
        {
            return _last_delta_time;
        });
        engine.set_function("fixed_delta_time", [this]()
        {
            return _last_fixed_delta_time;
        });
        engine.set_function("render_alpha", [this]()
        {
            return _last_render_alpha;
        });
    }

    bool ScriptSystem::LoadComponentInstance(const String &path, const ScriptEntityHandle &entity, ECS::ScriptComponent &component)
    {
        auto resolved = ResolveScriptPath(path);
        if (!resolved.has_value())
        {
            LOG_ERROR("ScriptSystem::LoadComponentInstance failed, script not found: {}", path);
            return false;
        }

        sol::load_result chunk = _lua.load_file(resolved->string());
        if (!chunk.valid())
        {
            sol::error error = chunk;
            ReportError(resolved->string(), error);
            return false;
        }

        sol::protected_function function = chunk;
        if (_traceback.valid())
        {
            function.set_error_handler(_traceback);
        }
        sol::protected_function_result result = function();
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(resolved->string(), error);
            return false;
        }

        sol::object object = result.get<sol::object>();
        if (object.get_type() != sol::type::table)
        {
            LOG_ERROR("Lua script must return a table: {}", resolved->string());
            return false;
        }

        component.ResetRuntime();
        component._resolved_script_path = NormalizeScriptKey(*resolved);
        component._loaded_script_version = _script_versions[component._resolved_script_path];
        component._instance = object.as<sol::table>();
        (*component._instance)["entity"] = entity;
        component._is_initialized = true;
        _loaded_script_files[component._resolved_script_path] = *resolved;
        LOG_INFO("ScriptComponent instance created for entity '{}' with script '{}'", entity.GetName(), *resolved);
        return true;
    }

    bool ScriptSystem::ExecuteChunk(sol::load_result &&chunk, const String &chunk_name)
    {
        if (!chunk.valid())
        {
            sol::error error = chunk;
            ReportError(chunk_name, error);
            return false;
        }

        sol::protected_function function = chunk;
        if (_traceback.valid())
        {
            function.set_error_handler(_traceback);
        }
        sol::protected_function_result result = function();
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(chunk_name, error);
            return false;
        }
        return true;
    }

    void ScriptSystem::ReportError(const String &source, const sol::error &error) const
    {
        LOG_ERROR("Lua Error [{}]: {}", source, error.what());
        LOG_ERROR("{}", StackTrace::Capture());
    }

    bool ScriptSystem::EnsureComponentReady(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component)
    {
        if (scene == nullptr || component._script_path.empty())
            return false;

        const ScriptEntityHandle handle{scene, entity};
        if (!handle.IsValid())
            return false;

        const String current_path = component._resolved_script_path;
        const bool needs_reload = !current_path.empty() && _script_versions[current_path] > component._loaded_script_version;
        if (!component._is_initialized || needs_reload)
        {
            if (!LoadComponentInstance(component._script_path, handle, component))
                return false;

            if (!InvokeComponentMethod(component, "OnInit"))
            {
                component.ResetRuntime();
                return false;
            }
        }

        return component._instance.has_value();
    }

    bool ScriptSystem::InvokeComponentMethod(ECS::ScriptComponent &component, const String &method_name)
    {
        if (!component._instance.has_value())
            return false;

        sol::object object = (*component._instance)[method_name];
        if (!object.valid() || object.get_type() != sol::type::function)
            return true;

        sol::protected_function function = object.as<sol::protected_function>();
        if (_traceback.valid())
            function.set_error_handler(_traceback);
        sol::protected_function_result result = function(*component._instance);
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(component._resolved_script_path.empty() ? component._script_path : component._resolved_script_path, error);
            return false;
        }

        return true;
    }

    bool ScriptSystem::InvokeComponentMethod(ECS::ScriptComponent &component, const String &method_name, f32 arg0)
    {
        if (!component._instance.has_value())
            return false;

        sol::object object = (*component._instance)[method_name];
        if (!object.valid() || object.get_type() != sol::type::function)
            return true;

        sol::protected_function function = object.as<sol::protected_function>();
        if (_traceback.valid())
            function.set_error_handler(_traceback);
        sol::protected_function_result result = function(*component._instance, arg0);
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(component._resolved_script_path.empty() ? component._script_path : component._resolved_script_path, error);
            return false;
        }
        return true;
    }

    bool ScriptSystem::InvokeComponentMethod(ECS::ScriptComponent &component, const String &method_name, f32 arg0, f32 arg1)
    {
        if (!component._instance.has_value())
            return false;

        sol::object object = (*component._instance)[method_name];
        if (!object.valid() || object.get_type() != sol::type::function)
            return true;

        sol::protected_function function = object.as<sol::protected_function>();
        if (_traceback.valid())
            function.set_error_handler(_traceback);
        sol::protected_function_result result = function(*component._instance, arg0, arg1);
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(component._resolved_script_path.empty() ? component._script_path : component._resolved_script_path, error);
            return false;
        }
        return true;
    }
#endif

    void ScriptSystem::FixedUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 fixed_delta_time)
    {
        _last_fixed_delta_time = fixed_delta_time;
        if (scene == nullptr || component._script_path.empty())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        InvokeComponentMethod(component, "OnFixedUpdate", fixed_delta_time);
#else
        (void) scene;
        (void) entity;
        (void) component;
        (void) fixed_delta_time;
#endif
    }

    void ScriptSystem::UpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time)
    {
        _last_delta_time = delta_time;
        if (scene == nullptr || component._script_path.empty())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        InvokeComponentMethod(component, "OnUpdate", delta_time);
#else
        (void) scene;
        (void) entity;
        (void) component;
        (void) delta_time;
#endif
    }

    void ScriptSystem::LateUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time,
                                           f32 render_alpha)
    {
        _last_delta_time = delta_time;
        _last_render_alpha = render_alpha;
        if (scene == nullptr || component._script_path.empty())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        InvokeComponentMethod(component, "OnLateUpdate", delta_time, render_alpha);
#else
        (void) scene;
        (void) entity;
        (void) component;
        (void) delta_time;
        (void) render_alpha;
#endif
    }

    void ScriptSystem::DestroyComponent(ECS::ScriptComponent &component)
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (component._is_initialized && component._instance.has_value())
        {
            sol::object destroy_object = (*component._instance)["OnDestroy"];
            if (destroy_object.valid() && destroy_object.get_type() == sol::type::function)
            {
                sol::protected_function destroy = destroy_object.as<sol::protected_function>();
                if (_traceback.valid())
                {
                    destroy.set_error_handler(_traceback);
                }
                sol::protected_function_result result = destroy(*component._instance);
                if (!result.valid())
                {
                    sol::error error = result;
                    ReportError(component._resolved_script_path.empty() ? component._script_path : component._resolved_script_path, error);
                }
            }
        }
#endif
        component.ResetRuntime();
    }
}
