#pragma once
#ifndef __SCRIPT_SYSTEM_H__
#define __SCRIPT_SYSTEM_H__

#include "Framework/Interface/IRuntimeModule.h"
#include "Scene/Entity.hpp"

#include <unordered_set>

#if AILU_ENABLE_LUA_SCRIPTING
#include <sol/sol.hpp>
#endif

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }
    namespace ECS
    {
        struct ScriptComponent;
    }

    struct AILU_API ScriptEntityHandle
    {
        SceneManagement::Scene *_scene = nullptr;
        ECS::Entity _entity = ECS::kInvalidEntity;

        bool IsValid() const;
        String GetName() const;
        Array<f32, 3> GetPosition() const;
        void SetPosition(f32 x, f32 y, f32 z) const;
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
        void OnScriptFileChanged(const fs::path &path);
        void UpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time);
        void DestroyComponent(ECS::ScriptComponent &component);
        bool IsEnabled() const;

#if AILU_ENABLE_LUA_SCRIPTING
        sol::state &GetState() { return _lua; }
        const sol::state &GetState() const { return _lua; }
#endif

    private:
        ScriptSystem() = default;

#if AILU_ENABLE_LUA_SCRIPTING
        void RegisterCoreBindings();
        bool ExecuteChunk(sol::load_result &&chunk, const String &chunk_name);
        void ReportError(const String &source, const sol::error &error) const;
        bool LoadComponentInstance(const String &path, const ScriptEntityHandle &entity, ECS::ScriptComponent &component);

    private:
        sol::state _lua;
        sol::protected_function _traceback;
#endif
        HashMap<String, fs::path> _loaded_script_files;
        HashMap<String, u32> _script_versions;
        std::unordered_set<String> _pending_reload_files;
        Queue<fs::path> _reload_queue;
        bool _is_initialized = false;
        f32 _last_delta_time = 0.0f;
    };
}

#endif