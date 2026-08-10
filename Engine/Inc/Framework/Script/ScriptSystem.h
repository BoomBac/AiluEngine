#pragma once
#ifndef __SCRIPT_SYSTEM_H__
#define __SCRIPT_SYSTEM_H__

#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/Quaternion.h"
#include "Scene/Entity.h"

#include <filesystem>
#include <unordered_set>

#if AILU_ENABLE_LUA_SCRIPTING
#include <sol/sol.hpp>
#endif

#include "generated/ScriptSystem.gen.h"

namespace Ailu
{
#if AILU_ENABLE_LUA_SCRIPTING
    void RegisterGeneratedLuaBindings(sol::state &lua);
#endif
    class ScriptSystem;
    namespace SceneManagement
    {
        class Scene;
    }
    namespace ECS
    {
        struct ScriptComponent;
    }

    ASTRUCT()
    struct AILU_API ScriptEngine
    {
        GENERATED_BODY()

        AFUNCTION(Script)
        static void Log(const String &message);
        AFUNCTION(Script)
        static f32 Time();
        AFUNCTION(Script)
        static f32 DeltaTime();
        AFUNCTION(Script)
        static f32 FixedDeltaTime();
        AFUNCTION(Script)
        static f32 RenderAlpha();
    };

    ASTRUCT()
    struct AILU_API ScriptTransform
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        Vector3f GetLocalPosition() const;
        AFUNCTION(Script)
        void SetLocalPosition(const Vector3f &position) const;
        AFUNCTION(Script)
        Math::Quaternion GetLocalRotation() const;
        AFUNCTION(Script)
        void SetLocalRotation(const Math::Quaternion &rotation) const;
        AFUNCTION(Script)
        Vector3f GetLocalScale() const;
        AFUNCTION(Script)
        void SetLocalScale(const Vector3f &scale) const;
        Vector3f GetPosition() const;
        Math::Quaternion GetRotation() const;
        Vector3f GetScale() const;
    };

    ASTRUCT()
    struct AILU_API ScriptEntity
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        String GetName() const;
        AFUNCTION(Script)
        void SetName(const String &name) const;
        AFUNCTION(Script)
        String GetGuid() const;
        AFUNCTION(Script)
        ScriptTransform GetTransform() const;
        AFUNCTION(Script)
        void Destroy() const;
    };

    ASTRUCT()
    struct AILU_API ScriptScene
    {
        GENERATED_BODY()
        SceneManagement::Scene *_scene = nullptr;

        AFUNCTION(Script)
        bool IsValid() const;
        AFUNCTION(Script)
        ScriptEntity FindEntity(const String &guid) const;
        AFUNCTION(Script)
        ScriptEntity FindEntityByName(const String &name) const;
        AFUNCTION(Script)
        ScriptEntity CreateEntity(const String &name) const;
    };

    ASTRUCT()
    struct AILU_API ScriptInput
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        bool IsPressed(const String &action_name) const;
        AFUNCTION(Script)
        bool IsDown(const String &action_name) const;
        AFUNCTION(Script)
        f32 GetFloat(const String &action_name) const;
        AFUNCTION(Script)
        Vector2f GetVector2(const String &action_name) const;
    };

    ASTRUCT()
    struct AILU_API ScriptTime
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        f32 GetDeltaTime() const;
        AFUNCTION(Script)
        f32 GetFixedDeltaTime() const;
        AFUNCTION(Script)
        f32 GetRenderAlpha() const;
        AFUNCTION(Script)
        f32 GetTime() const;
    };

    class AILU_API ScriptSystem final : public IRuntimeModule
    {
    public:
        static ScriptSystem &Get();

        int Initialize() final;
        void Finalize() final;
        void Tick(f32 delta_time) final;

        bool RunFile(const String &path);
        bool RunString(const String &code, const String &chunk_name = "runtime_chunk");
        i32 GetGlobalInt(const String &name, i32 fallback = 0) const;
        f64 GetGlobalNumber(const String &name, f64 fallback = 0.0) const;
        bool GetGlobalBool(const String &name, bool fallback = false) const;
        void OnScriptFileChanged(const std::filesystem::path &path);
        void FixedUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 fixed_delta_time);
        void UpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time);
        void LateUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time, f32 render_alpha);
        void DestroyComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component);
        bool SynchronizeComponentProperties(ECS::ScriptComponent &component);
        bool IsEnabled() const;
        f32 GetDeltaTime() const { return _last_delta_time; }
        f32 GetFixedDeltaTime() const { return _last_fixed_delta_time; }
        f32 GetRenderAlpha() const { return _last_render_alpha; }

#if AILU_ENABLE_LUA_SCRIPTING
        sol::state &GetState() { return _lua; }
        const sol::state &GetState() const { return _lua; }
#endif

    private:
        ScriptSystem() = default;

#if AILU_ENABLE_LUA_SCRIPTING
        struct ScriptInstanceKey
        {
            SceneManagement::Scene *_scene = nullptr;
            ECS::Entity _entity = ECS::kInvalidEntity;

            bool operator==(const ScriptInstanceKey &other) const = default;
        };

        struct ScriptInstanceKeyHasher
        {
            size_t operator()(const ScriptInstanceKey &key) const
            {
                return std::hash<SceneManagement::Scene *>{}(key._scene) ^ (std::hash<ECS::Entity>{}(key._entity) << 1u);
            }
        };

        struct ScriptInstance
        {
            Guid _script_asset = Guid::EmptyGuid();
            String _resolved_script_path;
            u32 _loaded_script_version = 0u;
            bool _is_initialized = false;
            bool _is_enabled = false;
            bool _faulted = false;
            sol::table _instance;
            sol::protected_function _on_create;
            sol::protected_function _on_enable;
            sol::protected_function _on_disable;
            sol::protected_function _on_fixed_update;
            sol::protected_function _on_update;
            sol::protected_function _on_late_update;
            sol::protected_function _on_destroy;
            sol::protected_function _on_reload;
        };

        struct ScriptPrototype
        {
            String _resolved_path;
            u32 _version = 0u;
            sol::table _prototype;
        };

        void RegisterCoreBindings();
        bool ExecuteChunk(sol::load_result &&chunk, const String &chunk_name);
        void ReportError(const String &source, const sol::error &error) const;
        bool LoadComponentInstance(const ScriptInstanceKey &key, ECS::ScriptComponent &component, const ScriptEntity &entity);
        bool LoadPrototype(const Guid &script_asset, const std::filesystem::path &resolved_path, ScriptPrototype &prototype);
        void SynchronizeScriptProperties(ECS::ScriptComponent &component, const sol::table &prototype);
        void InjectScriptProperties(const ECS::ScriptComponent &component, SceneManagement::Scene *scene, sol::table &instance) const;
        bool EnsureComponentReady(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component);
        bool SynchronizeComponentEnabledState(ScriptInstance &instance, bool is_enabled);
        void CacheLifecycleFunctions(ScriptInstance &instance);

        template<typename... Args>
        bool InvokeComponentMethod(ScriptInstance &instance, sol::protected_function &function, Args &&...args)
        {
            if (!function.valid())
                return true;

            if (_traceback.valid())
                function.set_error_handler(_traceback);
            sol::protected_function_result result = function(instance._instance, std::forward<Args>(args)...);
            if (result.valid())
                return true;

            sol::error error = result;
            ReportError(instance._resolved_script_path.empty() ? instance._script_asset.ToString() : instance._resolved_script_path, error);
            instance._faulted = true;
            return false;
        }

        bool ProcessReload(const Guid &script_asset);
        void RebuildInstancesForPrototype(const Guid &script_asset);
        void PruneInvalidInstances();

    private:
        sol::state _lua;
        sol::protected_function _traceback;
        HashMap<ScriptInstanceKey, ScriptInstance, ScriptInstanceKeyHasher> _instances;
        HashMap<String, ScriptPrototype> _prototypes;
#endif
        HashMap<String, std::filesystem::path> _loaded_script_files;
        HashMap<String, u32> _script_versions;
        std::unordered_set<String> _pending_reload_assets;
        Queue<Guid> _reload_queue;
        bool _is_initialized = false;
        f32 _last_delta_time = 0.0f;
        f32 _last_fixed_delta_time = 0.0f;
        f32 _last_render_alpha = 0.0f;
    };
}

#endif
