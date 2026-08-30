#include "Framework/Script/ScriptSystem.h"
#include "Framework/Math/MatrixMath.h"
#include "Framework/Math/Transform.h"
#include "Physics/2D/Physics2D.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Physics/2D/Physics2DSystem.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/StackTrace.h"
#include "Framework/Common/TimeMgr.h"
#include "Assets/ScriptAsset.h"
#include "Assets/Asset.h"
#include "Input/InputActionAsset.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "pch.h"
#include <sol/utility/is_integer.hpp>

namespace Ailu
{
    namespace fs = std::filesystem;

#if AILU_ENABLE_LUA_SCRIPTING
    Vector<ScriptLuaBindingFunction> &ScriptLuaBindingRegister::GetFunctions()
    {
        static Vector<ScriptLuaBindingFunction> s_functions;
        return s_functions;
    }

    ScriptLuaBindingRegister::ScriptLuaBindingRegister(ScriptLuaBindingFunction function)
    {
        GetFunctions().emplace_back(function);
    }

    void ScriptLuaBindingRegister::RegisterAll(sol::state &lua)
    {
        for (const ScriptLuaBindingFunction function : GetFunctions())
            function(lua);
    }
#endif

    namespace
    {
        ScriptEntity MakeScriptEntity(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            ScriptEntity result;
            result._scene = scene;
            result._entity = entity;
            return result;
        }

        ScriptScene MakeScriptScene(SceneManagement::Scene *scene)
        {
            ScriptScene result;
            result._scene = scene;
            return result;
        }

        ScriptAssetValue MakeScriptAssetValue(const Guid &guid, const String &asset_type)
        {
            ScriptAssetValue result;
            result._guid = guid;
            result._asset_type = asset_type;
            return result;
        }

        String TrimScriptPropertyText(String text)
        {
            const auto is_space = [](char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; };
            while (!text.empty() && is_space(text.front()))
                text.erase(text.begin());
            while (!text.empty() && is_space(text.back()))
                text.pop_back();
            return text;
        }

        bool IsLuaIdentifier(const String &text)
        {
            if (text.empty() || (!std::isalpha(static_cast<unsigned char>(text.front())) && text.front() != '_'))
                return false;
            return std::all_of(text.begin() + 1, text.end(), [](char character)
            {
                return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
            });
        }

        void ParseScriptPropertyDeclarations(const String &source, Vector<std::pair<String, String>> &declarations)
        {
            std::istringstream stream(source);
            String line;
            bool property_marker = false;
            while (std::getline(stream, line))
            {
                const String trimmed_line = TrimScriptPropertyText(line);
                if (trimmed_line.find("-- @property") != String::npos)
                {
                    property_marker = true;
                    continue;
                }
                if (!property_marker || trimmed_line.empty() || trimmed_line.starts_with("--"))
                    continue;

                const size_t comment_begin = line.find("--");
                const String assignment = TrimScriptPropertyText(line.substr(0, comment_begin));
                const String type_hint = comment_begin == String::npos
                    ? String{}
                    : TrimScriptPropertyText(line.substr(comment_begin + 2));
                const size_t equals = assignment.find('=');
                const size_t dot = assignment.rfind('.', equals == String::npos ? String::npos : equals);
                if (equals == String::npos || dot == String::npos)
                {
                    property_marker = false;
                    continue;
                }

                const String name = TrimScriptPropertyText(assignment.substr(dot + 1, equals - dot - 1));
                if (!IsLuaIdentifier(name))
                {
                    property_marker = false;
                    continue;
                }

                declarations.emplace_back(name, type_hint);
                property_marker = false;
            }
        }

        String NormalizeScriptPropertyType(String type_name)
        {
            std::transform(type_name.begin(), type_name.end(), type_name.begin(), [](char character)
            {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            });
            return type_name;
        }

        std::optional<ECS::EScriptPropertyType> ResolveScriptPropertyType(const sol::object &value, const String &type_hint)
        {
            const String normalized_hint = NormalizeScriptPropertyType(type_hint);
            if (normalized_hint == "bool" || normalized_hint == "boolean") return ECS::EScriptPropertyType::kBool;
            if (normalized_hint == "int" || normalized_hint == "integer") return ECS::EScriptPropertyType::kInt;
            if (normalized_hint == "float" || normalized_hint == "number") return ECS::EScriptPropertyType::kFloat;
            if (normalized_hint == "string") return ECS::EScriptPropertyType::kString;
            if (normalized_hint == "vec2" || normalized_hint == "vector2") return ECS::EScriptPropertyType::kVector2;
            if (normalized_hint == "vec3" || normalized_hint == "vector3") return ECS::EScriptPropertyType::kVector3;
            if (normalized_hint == "vec4" || normalized_hint == "vector4") return ECS::EScriptPropertyType::kVector4;
            if (normalized_hint == "color") return ECS::EScriptPropertyType::kColor;
            if (normalized_hint == "entity") return ECS::EScriptPropertyType::kEntity;
            if (normalized_hint == "asset") return ECS::EScriptPropertyType::kAsset;
            if (value.is<ScriptAssetValue>()) return ECS::EScriptPropertyType::kAsset;
            if (value.is<bool>()) return ECS::EScriptPropertyType::kBool;
            if (value.is<String>()) return ECS::EScriptPropertyType::kString;
            if (value.is<Vector2f>()) return ECS::EScriptPropertyType::kVector2;
            if (value.is<Vector3f>()) return ECS::EScriptPropertyType::kVector3;
            if (value.is<Vector4f>()) return ECS::EScriptPropertyType::kVector4;
            if (value.get_type() == sol::type::number)
                return sol::utility::is_integer(value) ? ECS::EScriptPropertyType::kInt : ECS::EScriptPropertyType::kFloat;
            return std::nullopt;
        }

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

    }

        std::optional<fs::path> ResolveScriptAssetPath(const Guid &script_asset)
        {
            if (script_asset == Guid::EmptyGuid())
                return std::nullopt;
            const WString &asset_path = ResourceMgr::Get().GuidToAssetPath(script_asset);
            Asset *asset = asset_path.empty() ? nullptr : ResourceMgr::Get().GetAsset(asset_path);
            if (asset != nullptr && asset->As<ScriptAsset>() != nullptr)
                return ResolveScriptPath(asset->As<ScriptAsset>()->SourceFile());
            Ref<ScriptAsset> loaded_asset = ResourceMgr::Get().Load<ScriptAsset>(script_asset);
            return loaded_asset == nullptr ? std::nullopt : ResolveScriptPath(loaded_asset->SourceFile());
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
        if (auto *application = Application::Instance(); application != nullptr && application->GetInputSystemPtr() != nullptr)
        {
            _input_event_listener_id = application->GetInputSystem().AddActionEventListener(
                [this](const InputActionEvent &event) { DispatchInputEvent(event); });
        }
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
        if (_input_event_listener_id != 0u)
        {
            if (auto *application = Application::Instance(); application != nullptr && application->GetInputSystemPtr() != nullptr)
                application->GetInputSystem().RemoveActionEventListener(_input_event_listener_id);
            _input_event_listener_id = 0u;
        }
        for (auto &[key, instance] : _instances)
        {
            InvokeComponentMethod(instance, instance._on_destroy);
            ClearSubscriptions(instance);
        }
        _instances.clear();
        _prototypes.clear();
        _subscriptions.clear();
        _collider_event_sources.clear();
        _animator_event_sources.clear();
        ClearPhysicsContactBridges();
        _currently_invoking_instance = nullptr;
        _next_subscription_id = 1u;
        _traceback = sol::protected_function();
        _lua = sol::state();
#endif
        _loaded_script_files.clear();
        _pending_reload_assets.clear();
        while (!_reload_queue.empty())
            _reload_queue.pop();
        _last_delta_time = 0.0f;
        _last_fixed_delta_time = 0.0f;
        _last_render_alpha = 0.0f;
        _is_initialized = false;
    }

    std::optional<ScriptSystem::ScriptInstanceKey> ScriptSystem::FindInstanceKey(const ScriptInstance *instance) const
    {
        const auto iter = std::find_if(_instances.begin(), _instances.end(), [instance](const auto &pair)
        {
            return &pair.second == instance;
        });
        return iter != _instances.end() ? std::optional<ScriptInstanceKey>(iter->first) : std::nullopt;
    }

    ScriptSubscriptionHandle ScriptSystem::RegisterSubscription(const ScriptInstanceKey &owner, std::function<void()> unsubscribe)
    {
        const ScriptSubscriptionHandle subscription_id = _next_subscription_id++;
        _subscriptions.emplace(subscription_id, ScriptSubscription{std::move(unsubscribe)});
        auto instance_iter = _instances.find(owner);
        if (instance_iter != _instances.end())
            instance_iter->second._subscriptions.emplace_back(subscription_id);
        return subscription_id;
    }

    bool ScriptSystem::Unsubscribe(ScriptSubscriptionHandle subscription_id)
    {
        const auto subscription_iter = _subscriptions.find(subscription_id);
        if (subscription_iter == _subscriptions.end())
            return false;

        if (subscription_iter->second._unsubscribe)
            subscription_iter->second._unsubscribe();
        _subscriptions.erase(subscription_iter);
        for (auto &[key, instance] : _instances)
        {
            std::erase(instance._subscriptions, subscription_id);
        }
        return true;
    }

    void ScriptSystem::ClearSubscriptions(ScriptInstance &instance)
    {
        const Vector<ScriptSubscriptionHandle> subscriptions = std::move(instance._subscriptions);
        instance._subscriptions.clear();
        for (const ScriptSubscriptionHandle subscription_id : subscriptions)
            Unsubscribe(subscription_id);
    }

    void ScriptSystem::DispatchInputEvent(const InputActionEvent &event)
    {
        if (event._action == nullptr)
            return;
        if (event._type == EInputActionEventType::kPerformed)
            _input.NotifyPerformed(event._action->GetName());
        else if (event._type == EInputActionEventType::kValueChanged)
            _input.NotifyValueChanged(event._action->GetName(), event._value._value.x);
    }

    void ScriptSystem::Tick(f32 delta_time)
    {
        _last_delta_time = delta_time;
#if AILU_ENABLE_LUA_SCRIPTING
        PruneInvalidInstances();
#endif
        while (Application::Get()._is_playing_mode && !_reload_queue.empty())
        {
            const Guid script_asset = _reload_queue.front();
            _reload_queue.pop();
            _pending_reload_assets.erase(script_asset.ToString());
#if AILU_ENABLE_LUA_SCRIPTING
            ProcessReload(script_asset);
#endif
        }
#if AILU_ENABLE_LUA_SCRIPTING
#endif
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

        const String path_key = NormalizeScriptKey(*resolved);
        Vector<Guid> changed_assets;
        for (const auto &[script_asset_key, loaded_path] : _loaded_script_files)
        {
            if (NormalizeScriptKey(loaded_path) != path_key)
                continue;
            changed_assets.emplace_back(Guid(script_asset_key));
        }
        for (const Guid &script_asset : changed_assets)
        {
            const String asset_key = script_asset.ToString();
            if (!_pending_reload_assets.emplace(asset_key).second)
                continue;
            _reload_queue.emplace(script_asset);
        }
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
    bool ScriptSystem::SynchronizeComponentProperties(ECS::ScriptComponent &component)
    {
        if (component._script_asset == Guid::EmptyGuid())
            return false;
        if (!_is_initialized)
            Initialize();

        const auto resolved_path = ResolveScriptAssetPath(component._script_asset);
        if (!resolved_path.has_value())
            return false;

        const String prototype_key = component._script_asset.ToString();
        auto prototype_iter = _prototypes.find(prototype_key);
        if (prototype_iter == _prototypes.end())
        {
            ScriptPrototype prototype;
            if (!LoadPrototype(component._script_asset, *resolved_path, prototype))
                return false;
            prototype_iter = _prototypes.find(prototype_key);
        }

        SynchronizeScriptProperties(component, prototype_iter->second);
        return true;
    }

        void ScriptSystem::RegisterCoreBindings()
    {
        ScriptLuaBindingRegister::RegisterAll(_lua);
        _lua.new_usertype<Vector2f>("Vector2f", "x", &Vector2f::x, "y", &Vector2f::y);
        _lua.set_function("Vec2", sol::overload([]() { return Vector2f{}; }, [](f32 x, f32 y) { return Vector2f{x, y}; }));
        _lua.new_usertype<Vector3f>("Vector3f", "x", &Vector3f::x, "y", &Vector3f::y, "z", &Vector3f::z);
        _lua.set_function("Vec3", sol::overload([]() { return Vector3f{}; }, [](f32 x, f32 y, f32 z) { return Vector3f{x, y, z}; }));
        _lua.new_usertype<Math::Quaternion>("Quaternion", "x", &Math::Quaternion::x, "y", &Math::Quaternion::y,
                                             "z", &Math::Quaternion::z, "w", &Math::Quaternion::w);
        _lua.set_function("Quaternion", sol::overload([]() { return Math::Quaternion{}; },
                                                       [](f32 x, f32 y, f32 z, f32 w) { return Math::Quaternion{x, y, z, w}; }));
        _lua.new_usertype<Math::Color>("Color", "r", &Math::Color::r, "g", &Math::Color::g, "b", &Math::Color::b,
                                       "a", &Math::Color::a);
        _lua.set_function("Color", sol::overload([]() { return Math::Color{}; },
                                                   [](f32 r, f32 g, f32 b, f32 a) { return Math::Color{r, g, b, a}; }));
    }

    bool ScriptSystem::LoadComponentInstance(const ScriptInstanceKey &key, ECS::ScriptComponent &component, const ScriptEntity &entity)
    {
        const Guid &script_asset = component._script_asset;
        auto resolved = ResolveScriptAssetPath(script_asset);
        if (!resolved.has_value())
        {
            LOG_ERROR("ScriptSystem::LoadComponentInstance failed, ScriptAsset {} could not be resolved", script_asset.ToString());
            return false;
        }

        const String prototype_key = script_asset.ToString();
        auto prototype_iter = _prototypes.find(prototype_key);
        if (prototype_iter == _prototypes.end())
        {
            ScriptPrototype prototype;
            if (!LoadPrototype(script_asset, *resolved, prototype))
                return false;
            prototype_iter = _prototypes.find(prototype_key);
        }

        ScriptInstance instance;
        instance._script_asset = script_asset;
        instance._resolved_script_path = prototype_key;
        instance._loaded_script_version = prototype_iter->second._version;
        instance._instance = _lua.create_table();
        instance._instance["entity"] = MakeScriptEntity(key._scene, key._entity);
        instance._instance["scene"] = MakeScriptScene(key._scene);
        SynchronizeScriptProperties(component, prototype_iter->second);
        InjectScriptProperties(component, key._scene, instance._instance);
        sol::table metatable = _lua.create_table();
        metatable["__index"] = prototype_iter->second._prototype;
        instance._instance[sol::metatable_key] = metatable;
        CacheLifecycleFunctions(instance);
        instance._is_initialized = true;
        _loaded_script_files[instance._resolved_script_path] = *resolved;
        _instances.insert_or_assign(key, std::move(instance));
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
        if (scene == nullptr || component._script_asset == Guid::EmptyGuid())
            return false;

        EnsurePhysicsContactBridge(scene);

        const ScriptEntity handle = MakeScriptEntity(scene, entity);
        if (!handle.IsValid())
            return false;

        const ScriptInstanceKey key{scene, entity};
        auto instance_iter = _instances.find(key);
        const bool needs_reload = instance_iter != _instances.end() && !instance_iter->second._resolved_script_path.empty() &&
                                  _script_versions[instance_iter->second._resolved_script_path] > instance_iter->second._loaded_script_version;
        if (instance_iter == _instances.end() || needs_reload)
        {
            if (!LoadComponentInstance(key, component, handle))
                return false;

            instance_iter = _instances.find(key);
            if (!InvokeComponentMethod(instance_iter->second, instance_iter->second._on_create))
                return false;
        }

        return SynchronizeComponentEnabledState(_instances.at(key),
                                                scene->GetRegister().IsComponentEnabled<ECS::ScriptComponent>(entity) &&
                                                    scene->IsEntityEnabled(entity));
    }

    bool ScriptSystem::SynchronizeComponentEnabledState(ScriptInstance &instance, bool is_enabled)
    {
        if (instance._faulted)
            return false;
        if (instance._is_enabled == is_enabled)
            return is_enabled;

        sol::protected_function &callback = is_enabled ? instance._on_enable : instance._on_disable;
        if (!InvokeComponentMethod(instance, callback))
            return false;
        instance._is_enabled = is_enabled;
        return is_enabled;
    }

    void ScriptSystem::SynchronizeScriptProperties(ECS::ScriptComponent &component, const ScriptPrototype &script_prototype)
    {
        for (ECS::ScriptPropertyData &property : component._properties)
            property._is_orphan = true;

        for (const ScriptPropertyDeclaration &declaration : script_prototype._property_declarations)
        {
            const String &name = declaration._name;
            const sol::object default_value = script_prototype._prototype[name];
            if (!default_value.valid())
                continue;
            const auto type = ResolveScriptPropertyType(default_value, declaration._type_hint);
            if (!type.has_value())
                continue;

            const auto property_iter = std::find_if(component._properties.begin(), component._properties.end(), [&name, type](
                const ECS::ScriptPropertyData &property) { return property._name == name && property._type == type.value(); });
            if (property_iter != component._properties.end())
            {
                property_iter->_is_orphan = false;
                if (property_iter->_type == ECS::EScriptPropertyType::kAsset)
                    property_iter->_asset_type = default_value.as<ScriptAssetValue>()._asset_type;
                continue;
            }

            ECS::ScriptPropertyData property;
            property._name = name;
            property._type = type.value();
            if (property._type == ECS::EScriptPropertyType::kBool) property._bool_value = default_value.as<bool>();
            else if (property._type == ECS::EScriptPropertyType::kInt) property._int_value = default_value.as<i32>();
            else if (property._type == ECS::EScriptPropertyType::kFloat) property._float_value = default_value.as<f32>();
            else if (property._type == ECS::EScriptPropertyType::kString) property._string_value = default_value.as<String>();
            else if (property._type == ECS::EScriptPropertyType::kVector2) property._vector_value = default_value.as<Vector2f>();
            else if (property._type == ECS::EScriptPropertyType::kVector3) property._vector_value = default_value.as<Vector3f>();
            else if (property._type == ECS::EScriptPropertyType::kVector4 || property._type == ECS::EScriptPropertyType::kColor)
                property._vector_value = default_value.is<Vector4f>() ? default_value.as<Vector4f>() : Vector4f::kZero;
            else if (property._type == ECS::EScriptPropertyType::kAsset)
            {
                const auto asset = default_value.as<ScriptAssetValue>();
                property._guid_value = asset._guid;
                property._asset_type = asset._asset_type;
            }
            component._properties.emplace_back(std::move(property));
        }
    }

    void ScriptSystem::InjectScriptProperties(const ECS::ScriptComponent &component, SceneManagement::Scene *scene,
                                              sol::table &instance) const
    {
        for (const ECS::ScriptPropertyData &property : component._properties)
        {
            if (property._is_orphan)
                continue;
            switch (property._type)
            {
            case ECS::EScriptPropertyType::kBool: instance[property._name] = property._bool_value; break;
            case ECS::EScriptPropertyType::kInt: instance[property._name] = property._int_value; break;
            case ECS::EScriptPropertyType::kFloat: instance[property._name] = property._float_value; break;
            case ECS::EScriptPropertyType::kString: instance[property._name] = property._string_value; break;
            case ECS::EScriptPropertyType::kVector2: instance[property._name] = Vector2f(property._vector_value.x, property._vector_value.y); break;
            case ECS::EScriptPropertyType::kVector3: instance[property._name] = Vector3f(property._vector_value.x, property._vector_value.y, property._vector_value.z); break;
            case ECS::EScriptPropertyType::kVector4:
            case ECS::EScriptPropertyType::kColor: instance[property._name] = property._vector_value; break;
            case ECS::EScriptPropertyType::kEntity: instance[property._name] = MakeScriptEntity(scene, scene->FindEntity(property._guid_value)); break;
            case ECS::EScriptPropertyType::kAsset: instance[property._name] = MakeScriptAssetValue(property._guid_value, property._asset_type); break;
            }
        }
    }

    void ScriptSystem::CacheLifecycleFunctions(ScriptInstance &instance)
    {
        auto cache_function = [&instance](const char *name, const char *legacy_name, sol::protected_function &function)
        {
            sol::object object = instance._instance[name];
            if ((!object.valid() || object.get_type() != sol::type::function) && legacy_name != nullptr)
                object = instance._instance[legacy_name];
            if (object.valid() && object.get_type() == sol::type::function)
                function = object.as<sol::protected_function>();
        };
        cache_function("on_create", "OnCreate", instance._on_create);
        cache_function("on_enable", "OnEnable", instance._on_enable);
        cache_function("on_disable", "OnDisable", instance._on_disable);
        cache_function("on_fixed_update", "OnFixedUpdate", instance._on_fixed_update);
        cache_function("on_update", "OnUpdate", instance._on_update);
        cache_function("on_late_update", "OnLateUpdate", instance._on_late_update);
        cache_function("on_destroy", "OnDestroy", instance._on_destroy);
        cache_function("on_reload", "OnReload", instance._on_reload);
    }

    ScriptCollider2D::EventViews ScriptSystem::GetColliderEventViews(SceneManagement::Scene *scene, ECS::Entity entity)
    {
        if (scene == nullptr || !scene->IsValidEntity(entity))
            return {};

        const ScriptInstanceKey key{scene, entity};
        auto event_source_iter = _collider_event_sources.find(key);
        if (event_source_iter == _collider_event_sources.end())
        {
            auto event_source = MakeScope<ScriptColliderEventSource>();
            event_source_iter = _collider_event_sources.emplace(key, std::move(event_source)).first;
        }

        ScriptColliderEventSource &event_source = *event_source_iter->second;
        return {event_source._on_collision_enter.GetEventView(), event_source._on_collision_exit.GetEventView(),
                event_source._on_trigger_enter.GetEventView(), event_source._on_trigger_exit.GetEventView()};
    }

    ScriptAnimator::AnimationEventRouter::EventView ScriptSystem::GetAnimatorEventView(SceneManagement::Scene *scene,
                                                                                        ECS::Entity entity)
    {
        if (scene == nullptr || !scene->IsValidEntity(entity))
            return {};

        const ScriptInstanceKey key{scene, entity};
        auto event_source_iter = _animator_event_sources.find(key);
        if (event_source_iter == _animator_event_sources.end())
        {
            auto event_source = MakeScope<ScriptAnimatorEventSource>();
            event_source_iter = _animator_event_sources.emplace(key, std::move(event_source)).first;
        }
        return event_source_iter->second->_on_event.GetEventView();
    }

    bool ScriptSystem::ProcessReload(const Guid &script_asset)
    {
        const auto resolved_path = ResolveScriptAssetPath(script_asset);
        if (!resolved_path.has_value())
        {
            LOG_ERROR("ScriptSystem hot reload failed, ScriptAsset {} could not be resolved", script_asset.ToString());
            return false;
        }

        LOG_INFO("ScriptSystem hot reload: ScriptAsset {} ({})", script_asset.ToString(), resolved_path->string());
        ScriptPrototype prototype;
        if (!LoadPrototype(script_asset, *resolved_path, prototype))
            return false;

        const String prototype_key = script_asset.ToString();
        _prototypes.at(prototype_key)._version = ++_script_versions[prototype_key];
        RebuildInstancesForPrototype(script_asset);
        return true;
    }

    void ScriptSystem::RebuildInstancesForPrototype(const Guid &script_asset)
    {
        Vector<ScriptInstanceKey> instance_keys;
        for (const auto &[key, instance] : _instances)
        {
            if (instance._script_asset == script_asset)
                instance_keys.emplace_back(key);
        }

        for (const ScriptInstanceKey &key : instance_keys)
        {
            if (key._scene == nullptr || !key._scene->IsValidEntity(key._entity))
                continue;
            auto *component = key._scene->GetRegister().GetComponent<ECS::ScriptComponent>(key._entity);
            if (component == nullptr || component->_script_asset != script_asset)
                continue;
            ClearSubscriptions(_instances.at(key));
            if (!LoadComponentInstance(key, *component, MakeScriptEntity(key._scene, key._entity)))
                continue;

            ScriptInstance &instance = _instances.at(key);
            if (!instance._on_reload.valid())
                InvokeComponentMethod(instance, instance._on_create);
            else
                InvokeComponentMethod(instance, instance._on_reload);
        }
    }

    bool ScriptSystem::LoadPrototype(const Guid &script_asset, const fs::path &resolved_path, ScriptPrototype &prototype)
    {
        sol::load_result chunk = _lua.load_file(resolved_path.string());
        if (!chunk.valid())
        {
            sol::error error = chunk;
            ReportError(resolved_path.string(), error);
            return false;
        }

        sol::protected_function function = chunk;
        if (_traceback.valid())
            function.set_error_handler(_traceback);
        sol::protected_function_result result = function();
        if (!result.valid())
        {
            sol::error error = result;
            ReportError(resolved_path.string(), error);
            return false;
        }

        sol::object object = result.get<sol::object>();
        if (!object.valid() || object.get_type() != sol::type::table)
        {
            LOG_ERROR("Lua script must return a table: {}", resolved_path.string());
            return false;
        }

        prototype._resolved_path = script_asset.ToString();
        prototype._version = _script_versions[prototype._resolved_path];
        prototype._prototype = object.as<sol::table>();
        String source;
        if (FileManager::ReadFile(ToWChar(resolved_path.string()), source))
        {
            Vector<std::pair<String, String>> declarations;
            ParseScriptPropertyDeclarations(source, declarations);
            for (auto &[name, type_hint] : declarations)
                prototype._property_declarations.push_back({std::move(name), std::move(type_hint)});
        }
        _loaded_script_files[prototype._resolved_path] = resolved_path;
        _prototypes.insert_or_assign(prototype._resolved_path, std::move(prototype));
        return true;
    }

    void ScriptSystem::PruneInvalidInstances()
    {
        for (auto iter = _instances.begin(); iter != _instances.end();)
        {
            const ScriptInstanceKey &key = iter->first;
            const bool is_valid = key._scene != nullptr && key._scene->IsValidEntity(key._entity) &&
                                  key._scene->GetRegister().HasComponent<ECS::ScriptComponent>(key._entity);
            if (is_valid)
            {
                ++iter;
                continue;
            }

            InvokeComponentMethod(iter->second, iter->second._on_destroy);
            ClearSubscriptions(iter->second);
            iter = _instances.erase(iter);
        }
        for (auto iter = _collider_event_sources.begin(); iter != _collider_event_sources.end();)
        {
            const ScriptInstanceKey &key = iter->first;
            if (key._scene == nullptr || !key._scene->IsValidEntity(key._entity) ||
                !key._scene->GetRegister().HasComponent<ECS::Collider2DComponent>(key._entity))
                iter = _collider_event_sources.erase(iter);
            else
                ++iter;
        }
    }

    void ScriptSystem::EnsurePhysicsContactBridge(SceneManagement::Scene *scene)
    {
        if (scene == nullptr)
            return;
        auto *physics_system = scene->GetRegister().GetSystem<ECS::Physics2DSystem>();
        if (physics_system == nullptr)
            return;
        Physics2DWorld *world = &physics_system->World();
        if (_physics_contact_bridges.contains(world))
            return;

        const u32 handle = world->_OnContact.Subscribe([this, scene](const PhysicsContact2D &contact)
        {
            DispatchPhysicsContact(scene, contact);
        });
        _physics_contact_bridges.emplace(world, handle);
    }

    void ScriptSystem::ClearPhysicsContactBridges()
    {
        for (const auto &[world, handle] : _physics_contact_bridges)
        {
            if (world == nullptr)
                continue;
            world->_OnContact.Unsubscribe(handle);
        }
        _physics_contact_bridges.clear();
    }

    void ScriptSystem::OnSceneDestroyed(SceneManagement::Scene *scene)
    {
        if (scene == nullptr)
            return;
        for (auto iter = _instances.begin(); iter != _instances.end();)
        {
            if (iter->first._scene != scene)
            {
                ++iter;
                continue;
            }

            InvokeComponentMethod(iter->second, iter->second._on_destroy);
            ClearSubscriptions(iter->second);
            iter = _instances.erase(iter);
        }
        for (auto iter = _collider_event_sources.begin(); iter != _collider_event_sources.end();)
        {
            if (iter->first._scene == scene)
                iter = _collider_event_sources.erase(iter);
            else
                ++iter;
        }
        for (auto iter = _animator_event_sources.begin(); iter != _animator_event_sources.end();)
        {
            if (iter->first._scene == scene)
                iter = _animator_event_sources.erase(iter);
            else
                ++iter;
        }
        auto *physics_system = scene->GetRegister().GetSystem<ECS::Physics2DSystem>();
        if (physics_system == nullptr)
            return;
        Physics2DWorld *world = &physics_system->World();
        const auto bridge_iter = _physics_contact_bridges.find(world);
        if (bridge_iter == _physics_contact_bridges.end())
            return;
        world->_OnContact.Unsubscribe(bridge_iter->second);
        _physics_contact_bridges.erase(bridge_iter);
    }

    void ScriptSystem::DispatchPhysicsContact(SceneManagement::Scene *scene, const PhysicsContact2D &contact)
    {
        if (scene == nullptr || !scene->IsValidEntity(contact._entity_a) || !scene->IsValidEntity(contact._entity_b))
            return;

        const ScriptEntity other_a = MakeScriptEntity(scene, contact._entity_b);
        const ScriptEntity other_b = MakeScriptEntity(scene, contact._entity_a);
        const ScriptContact2D hit_a{contact._point, contact._normal, contact._shape_a, contact._shape_b,
                                    contact._object_type_b};
        const ScriptContact2D hit_b{contact._point, Vector2f{-contact._normal.x, -contact._normal.y}, contact._shape_b,
                                    contact._shape_a, contact._object_type_a};
        const ECollisionChannel2D target_channel_a =
            static_cast<ECollisionChannel2D>(static_cast<u8>(contact._object_type_b));
        const ECollisionChannel2D target_channel_b =
            static_cast<ECollisionChannel2D>(static_cast<u8>(contact._object_type_a));

        const auto event_source_a = _collider_event_sources.find({scene, contact._entity_a});
        const auto event_source_b = _collider_event_sources.find({scene, contact._entity_b});
        switch (contact._type)
        {
        case EPhysicsContact2DType::kCollisionBegin:
            if (event_source_a != _collider_event_sources.end())
                event_source_a->second->_on_collision_enter.Invoke(target_channel_a, other_a, hit_a);
            if (event_source_b != _collider_event_sources.end())
                event_source_b->second->_on_collision_enter.Invoke(target_channel_b, other_b, hit_b);
            break;
        case EPhysicsContact2DType::kCollisionEnd:
            if (event_source_a != _collider_event_sources.end())
                event_source_a->second->_on_collision_exit.Invoke(target_channel_a, other_a, hit_a);
            if (event_source_b != _collider_event_sources.end())
                event_source_b->second->_on_collision_exit.Invoke(target_channel_b, other_b, hit_b);
            break;
        case EPhysicsContact2DType::kTriggerBegin:
            if (event_source_a != _collider_event_sources.end())
                event_source_a->second->_on_trigger_enter.Invoke(target_channel_a, other_a, hit_a);
            if (event_source_b != _collider_event_sources.end())
                event_source_b->second->_on_trigger_enter.Invoke(target_channel_b, other_b, hit_b);
            break;
        case EPhysicsContact2DType::kTriggerEnd:
            if (event_source_a != _collider_event_sources.end())
                event_source_a->second->_on_trigger_exit.Invoke(target_channel_a, other_a, hit_a);
            if (event_source_b != _collider_event_sources.end())
                event_source_b->second->_on_trigger_exit.Invoke(target_channel_b, other_b, hit_b);
            break;
        default:
            break;
        }
    }
#endif

    void ScriptSystem::DispatchAnimationEvents(SceneManagement::Scene *scene,
                                               std::span<const AnimationEventMessage> events)
    {
#if AILU_ENABLE_LUA_SCRIPTING
        if (scene == nullptr)
            return;
        for (const AnimationEventMessage &event : events)
        {
            const auto source_iter = _animator_event_sources.find({scene, event._entity});
            if (source_iter != _animator_event_sources.end())
                source_iter->second->_on_event.Invoke(event._event_id, event._event_id);
        }
#else
        (void) scene;
        (void) events;
#endif
    }

#if !AILU_ENABLE_LUA_SCRIPTING
    bool ScriptSystem::SynchronizeComponentProperties(ECS::ScriptComponent &component)
    {
        (void) component;
        return false;
    }

    void ScriptSystem::DispatchPhysicsContact(SceneManagement::Scene *, const PhysicsContact2D &)
    {
    }

    void ScriptSystem::OnSceneDestroyed(SceneManagement::Scene *)
    {
    }
#endif

    void ScriptSystem::FixedUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 fixed_delta_time)
    {
        _last_fixed_delta_time = fixed_delta_time;
        if (scene == nullptr || component._script_asset == Guid::EmptyGuid())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        PROFILE_BLOCK_CPU("Script.FixedUpdate")
        auto instance_iter = _instances.find({scene, entity});
        InvokeComponentMethod(instance_iter->second, instance_iter->second._on_fixed_update, fixed_delta_time);
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
        if (scene == nullptr || component._script_asset == Guid::EmptyGuid())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        PROFILE_BLOCK_CPU("Script.Update")
        auto instance_iter = _instances.find({scene, entity});
        InvokeComponentMethod(instance_iter->second, instance_iter->second._on_update, delta_time);
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
        if (scene == nullptr || component._script_asset == Guid::EmptyGuid())
            return;

#if AILU_ENABLE_LUA_SCRIPTING
        if (!EnsureComponentReady(scene, entity, component))
            return;
        PROFILE_BLOCK_CPU("Script.LateUpdate")
        auto instance_iter = _instances.find({scene, entity});
        InvokeComponentMethod(instance_iter->second, instance_iter->second._on_late_update, delta_time, render_alpha);
#else
        (void) scene;
        (void) entity;
        (void) component;
        (void) delta_time;
        (void) render_alpha;
#endif
    }

    void ScriptSystem::DestroyComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component)
    {
#if AILU_ENABLE_LUA_SCRIPTING
        (void) component;
        const auto iter = _instances.find({scene, entity});
        if (iter != _instances.end())
        {
            InvokeComponentMethod(iter->second, iter->second._on_destroy);
            ClearSubscriptions(iter->second);
            _instances.erase(iter);
        }
#else
        (void) scene;
        (void) entity;
        (void) component;
#endif
    }
}
