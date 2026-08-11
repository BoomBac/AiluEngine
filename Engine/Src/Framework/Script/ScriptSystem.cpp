#include "Framework/Script/ScriptSystem.h"
#include "Physics/2D/Physics2D.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/StackTrace.h"
#include "Framework/Common/TimeMgr.h"
#include "Assets/ScriptAsset.h"
#include "Assets/Asset.h"
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

    }

        void ScriptEngine::Log(const String &message) { LOG_INFO("[Lua] {}", message); }
        f32 ScriptEngine::Time() { return TimeMgr::TickTimeSinceLoad; }
        f32 ScriptEngine::DeltaTime() { return ScriptSystem::Get().GetDeltaTime(); }
        f32 ScriptEngine::FixedDeltaTime() { return ScriptSystem::Get().GetFixedDeltaTime(); }
        f32 ScriptEngine::RenderAlpha() { return ScriptSystem::Get().GetRenderAlpha(); }

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

        bool ScriptTransform::IsValid() const
        {
            return _scene != nullptr && _scene->IsValidEntity(_entity) &&
                   _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) != nullptr;
        }

        Vector3f ScriptTransform::GetLocalPosition() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr ? transform->GetLocalPosition() : Vector3f::kZero;
        }

        void ScriptTransform::SetLocalPosition(const Vector3f &position) const
        {
            if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
                transform->SetLocalPosition(position);
        }

        Math::Quaternion ScriptTransform::GetLocalRotation() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr ? transform->GetLocalRotation() : Math::Quaternion::Identity();
        }

        void ScriptTransform::SetLocalRotation(const Math::Quaternion &rotation) const
        {
            if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
                transform->SetLocalRotation(rotation);
        }

        Vector3f ScriptTransform::GetLocalScale() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr ? transform->GetLocalScale() : Vector3f::kOne;
        }

        void ScriptTransform::SetLocalScale(const Vector3f &scale) const
        {
            if (auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr)
                transform->SetLocalScale(scale);
        }

        Vector3f ScriptTransform::GetPosition() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr && !transform->_world_dirty ? transform->GetPosition() : GetLocalPosition();
        }

        Math::Quaternion ScriptTransform::GetRotation() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr && !transform->_world_dirty ? transform->GetRotation() : GetLocalRotation();
        }

        Vector3f ScriptTransform::GetScale() const
        {
            const auto *transform = IsValid() ? _scene->GetRegister().GetComponent<ECS::TransformComponent>(_entity) : nullptr;
            return transform != nullptr && !transform->_world_dirty ? transform->GetScale() : GetLocalScale();
        }

        bool ScriptEntity::IsValid() const
        {
            return _scene != nullptr && _scene->IsValidEntity(_entity);
        }

        String ScriptEntity::GetName() const
        {
            if (!IsValid())
                return {};
            const auto *tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(_entity);
            return tag != nullptr ? tag->_name : String{};
        }

        void ScriptEntity::SetName(const String &name) const
        {
            if (IsValid())
                _scene->RenameEntity(_entity, name);
        }

        String ScriptEntity::GetGuid() const
        {
            return IsValid() ? _scene->GetEntityGuid(_entity).ToString() : String{};
        }

        ScriptTransform ScriptEntity::GetTransform() const
        {
            return {_scene, _entity};
        }

    void ScriptEntity::Destroy() const
        {
            if (IsValid())
                _scene->RemoveObject(_entity);
        }

        bool ScriptScene::IsValid() const
        {
            return _scene != nullptr;
        }

        ScriptEntity ScriptScene::FindEntity(const String &guid) const
        {
            if (!IsValid())
                return {};
            const ECS::Entity entity = _scene->FindEntity(Guid(guid));
            return {_scene, entity};
        }

        ScriptEntity ScriptScene::FindEntityByName(const String &name) const
        {
            if (!IsValid())
                return {};
            u32 index = 0u;
            for (const auto &tag : _scene->GetRegister().View<ECS::TagComponent>())
            {
                const ECS::Entity entity = _scene->GetRegister().GetEntity<ECS::TagComponent>(index++);
                if (tag._name == name)
                    return {_scene, entity};
            }
            return {_scene, ECS::kInvalidEntity};
        }

        ScriptEntity ScriptScene::CreateEntity(const String &name) const
        {
            if (!IsValid())
                return {};
            return {_scene, _scene->AddObject(name)};
        }

        bool ScriptInput::IsPressed(const String &action_name) const
        {
            auto *application = Application::Instance();
            if (application == nullptr || application->GetInputSystemPtr() == nullptr)
                return false;
            const auto *action = application->GetInputSystem().FindAction(action_name);
            return action != nullptr && action->WasPerformedThisFrame();
        }

        bool ScriptInput::IsDown(const String &action_name) const
        {
            auto *application = Application::Instance();
            if (application == nullptr || application->GetInputSystemPtr() == nullptr)
                return false;
            const auto *action = application->GetInputSystem().FindAction(action_name);
            return action != nullptr && action->GetValue().AsButton();
        }

        f32 ScriptInput::GetFloat(const String &action_name) const
        {
            auto *application = Application::Instance();
            if (application == nullptr || application->GetInputSystemPtr() == nullptr)
                return 0.0f;
            const auto *action = application->GetInputSystem().FindAction(action_name);
            return action != nullptr ? action->GetValue().AsAxis1D() : 0.0f;
        }

        Vector2f ScriptInput::GetVector2(const String &action_name) const
        {
            auto *application = Application::Instance();
            if (application == nullptr || application->GetInputSystemPtr() == nullptr)
                return Vector2f::kZero;
            const auto *action = application->GetInputSystem().FindAction(action_name);
            return action != nullptr ? action->GetValue().AsAxis2D() : Vector2f::kZero;
        }

        f32 ScriptTime::GetDeltaTime() const
        {
            return ScriptSystem::Get().GetDeltaTime();
        }

        f32 ScriptTime::GetFixedDeltaTime() const
        {
            return ScriptSystem::Get().GetFixedDeltaTime();
        }

        f32 ScriptTime::GetRenderAlpha() const
        {
            return ScriptSystem::Get().GetRenderAlpha();
        }

        f32 ScriptTime::GetTime() const
        {
            return TimeMgr::TickTimeSinceLoad;
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
        for (auto &[key, instance] : _instances)
            InvokeComponentMethod(instance, instance._on_destroy);
        _instances.clear();
        _prototypes.clear();
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

    bool ScriptPhysics2D::IsValidBody(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() && Physics2D::IsValidBody(*entity._scene, entity._entity);
    }

    void ScriptPhysics2D::SetPosition(const ScriptEntity &entity, const Vector2f &position)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetPosition(*entity._scene, entity._entity, position);
    }

    Vector2f ScriptPhysics2D::GetPosition(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() ? Physics2D::GetPosition(*entity._scene, entity._entity) : Vector2f::kZero;
    }

    void ScriptPhysics2D::SetLinearVelocity(const ScriptEntity &entity, const Vector2f &velocity)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetLinearVelocity(*entity._scene, entity._entity, velocity);
    }

    Vector2f ScriptPhysics2D::GetLinearVelocity(const ScriptEntity &entity)
    {
        return entity._scene != nullptr && entity.IsValid() ? Physics2D::GetLinearVelocity(*entity._scene, entity._entity) : Vector2f::kZero;
    }

    void ScriptPhysics2D::SetAngularVelocity(const ScriptEntity &entity, f32 velocity)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::SetAngularVelocity(*entity._scene, entity._entity, velocity);
    }

    void ScriptPhysics2D::AddForce(const ScriptEntity &entity, const Vector2f &force)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::AddForce(*entity._scene, entity._entity, force);
    }

    void ScriptPhysics2D::AddImpulse(const ScriptEntity &entity, const Vector2f &impulse)
    {
        if (entity._scene != nullptr && entity.IsValid()) Physics2D::AddImpulse(*entity._scene, entity._entity, impulse);
    }

    void ScriptSystem::Tick(f32 delta_time)
    {
        _last_delta_time = delta_time;
#if AILU_ENABLE_LUA_SCRIPTING
        PruneInvalidInstances();
#endif
        while (!_reload_queue.empty())
        {
            const Guid script_asset = _reload_queue.front();
            _reload_queue.pop();
            _pending_reload_assets.erase(script_asset.ToString());
#if AILU_ENABLE_LUA_SCRIPTING
            ProcessReload(script_asset);
#endif
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

        SynchronizeScriptProperties(component, prototype_iter->second._prototype);
        return true;
    }

    void ScriptSystem::RegisterCoreBindings()
    {
        RegisterGeneratedLuaBindings(_lua);
        _lua.new_usertype<Vector2f>("Vec2", sol::constructors<Vector2f(), Vector2f(f32, f32)>(), "x", &Vector2f::x, "y", &Vector2f::y);
        _lua.new_usertype<Vector3f>("Vec3", sol::constructors<Vector3f(), Vector3f(f32, f32, f32)>(), "x", &Vector3f::x, "y", &Vector3f::y,
                                    "z", &Vector3f::z);
        _lua.new_usertype<Math::Quaternion>("Quaternion",
                                             sol::constructors<Math::Quaternion(), Math::Quaternion(f32, f32, f32, f32)>(),
                                             "x", &Math::Quaternion::x, "y", &Math::Quaternion::y, "z", &Math::Quaternion::z,
                                             "w", &Math::Quaternion::w);
        _lua["time"] = ScriptTime{};
        _lua["input"] = ScriptInput{};
        _lua["physics2d"] = ScriptPhysics2D{};
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
        instance._instance["entity"] = ScriptEntity{key._scene, key._entity};
        instance._instance["scene"] = ScriptScene{key._scene};
        SynchronizeScriptProperties(component, prototype_iter->second._prototype);
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

        const ScriptEntity handle{scene, entity};
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

    void ScriptSystem::SynchronizeScriptProperties(ECS::ScriptComponent &component, const sol::table &prototype)
    {
        for (ECS::ScriptPropertyData &property : component._properties)
            property._is_orphan = true;

        const sol::object schema_object = prototype["__properties"];
        if (!schema_object.valid() || schema_object.get_type() != sol::type::table)
            return;

        const sol::table schema = schema_object.as<sol::table>();
        for (const auto &[name_object, definition_object] : schema)
        {
            if (name_object.get_type() != sol::type::string || definition_object.get_type() != sol::type::table)
                continue;
            const String name = name_object.as<String>();
            const sol::table definition = definition_object.as<sol::table>();
            const String type_name = definition.get_or<String>("type", "");
            ECS::EScriptPropertyType type;
            if (type_name == "bool") type = ECS::EScriptPropertyType::kBool;
            else if (type_name == "int") type = ECS::EScriptPropertyType::kInt;
            else if (type_name == "float") type = ECS::EScriptPropertyType::kFloat;
            else if (type_name == "string") type = ECS::EScriptPropertyType::kString;
            else if (type_name == "vec2") type = ECS::EScriptPropertyType::kVector2;
            else if (type_name == "vec3") type = ECS::EScriptPropertyType::kVector3;
            else if (type_name == "vec4") type = ECS::EScriptPropertyType::kVector4;
            else if (type_name == "color") type = ECS::EScriptPropertyType::kColor;
            else if (type_name == "entity") type = ECS::EScriptPropertyType::kEntity;
            else if (type_name == "asset") type = ECS::EScriptPropertyType::kAsset;
            else continue;

            const auto property_iter = std::find_if(component._properties.begin(), component._properties.end(), [&name, type](
                const ECS::ScriptPropertyData &property) { return property._name == name && property._type == type; });
            if (property_iter != component._properties.end())
            {
                property_iter->_is_orphan = false;
                continue;
            }

            ECS::ScriptPropertyData property;
            property._name = name;
            property._type = type;
            const sol::object default_value = definition["default"];
            if (type == ECS::EScriptPropertyType::kBool) property._bool_value = default_value.is<bool>() && default_value.as<bool>();
            else if (type == ECS::EScriptPropertyType::kInt) property._int_value = default_value.is<i32>() ? default_value.as<i32>() : 0;
            else if (type == ECS::EScriptPropertyType::kFloat) property._float_value = default_value.is<f32>() ? default_value.as<f32>() : 0.0f;
            else if (type == ECS::EScriptPropertyType::kString) property._string_value = default_value.is<String>() ? default_value.as<String>() : "";
            else if (type == ECS::EScriptPropertyType::kVector2) property._vector_value = default_value.is<Vector2f>() ? default_value.as<Vector2f>() : Vector2f::kZero;
            else if (type == ECS::EScriptPropertyType::kVector3) property._vector_value = default_value.is<Vector3f>() ? default_value.as<Vector3f>() : Vector3f::kZero;
            else if (type == ECS::EScriptPropertyType::kVector4 || type == ECS::EScriptPropertyType::kColor)
                property._vector_value = default_value.is<Vector4f>() ? default_value.as<Vector4f>() : Vector4f::kZero;
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
            case ECS::EScriptPropertyType::kEntity: instance[property._name] = ScriptEntity{scene, scene->FindEntity(property._guid_value)}; break;
            case ECS::EScriptPropertyType::kAsset: instance[property._name] = property._guid_value.ToString(); break;
            }
        }
    }

    void ScriptSystem::CacheLifecycleFunctions(ScriptInstance &instance)
    {
        auto cache_function = [&instance](const char *name, sol::protected_function &function)
        {
            sol::object object = instance._instance[name];
            if (object.valid() && object.get_type() == sol::type::function)
                function = object.as<sol::protected_function>();
        };
        cache_function("OnCreate", instance._on_create);
        if (!instance._on_create.valid())
            cache_function("OnInit", instance._on_create);
        cache_function("OnEnable", instance._on_enable);
        cache_function("OnDisable", instance._on_disable);
        cache_function("OnFixedUpdate", instance._on_fixed_update);
        cache_function("OnUpdate", instance._on_update);
        cache_function("OnLateUpdate", instance._on_late_update);
        cache_function("OnDestroy", instance._on_destroy);
        cache_function("OnReload", instance._on_reload);
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
            if (!LoadComponentInstance(key, *component, {key._scene, key._entity}))
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
            iter = _instances.erase(iter);
        }
    }
#endif

#if !AILU_ENABLE_LUA_SCRIPTING
    bool ScriptSystem::SynchronizeComponentProperties(ECS::ScriptComponent &component)
    {
        (void) component;
        return false;
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
            _instances.erase(iter);
        }
#else
        (void) scene;
        (void) entity;
        (void) component;
#endif
    }
}
