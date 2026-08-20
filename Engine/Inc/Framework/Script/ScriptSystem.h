#pragma once
#ifndef __SCRIPT_SYSTEM_H__
#define __SCRIPT_SYSTEM_H__

#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Math/Guid.h"
#include "Animation/AnimationEvent.h"
#include "Framework/Script/ScriptCamera.h"
#include "Framework/Script/ScriptEngine.h"
#include "Framework/Script/ScriptEntity.h"
#include "Framework/Script/ScriptInput.h"
#include "Framework/Script/ScriptLuaBindingRegistry.h"
#include "Framework/Script/ScriptPhysics2D.h"
#include "Framework/Script/ScriptScene.h"
#include "Framework/Script/ScriptTime.h"
#include "Input/InputSystem.h"
#include "Scene/Component.h"

#include <filesystem>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>

#if AILU_ENABLE_LUA_SCRIPTING
#include <sol/sol.hpp>
#endif

namespace Ailu
{
    using ScriptSubscriptionHandle = u64;

    namespace SceneManagement { class Scene; }
    namespace ECS { struct ScriptComponent; }

    struct PhysicsContact2D;
    class AILU_API Physics2DWorld;

    class AILU_API ScriptSystem final : public IRuntimeModule
    {
#if AILU_ENABLE_LUA_SCRIPTING
        struct ScriptInstanceKey;
        struct ScriptInstance;
#endif
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
        void LateUpdateComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component, f32 delta_time,
                                 f32 render_alpha);
        void DestroyComponent(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component);
        bool SynchronizeComponentProperties(ECS::ScriptComponent &component);
        bool IsEnabled() const;
        f32 GetDeltaTime() const { return _last_delta_time; }
        f32 GetFixedDeltaTime() const { return _last_fixed_delta_time; }
        f32 GetRenderAlpha() const { return _last_render_alpha; }
        ScriptInput &GetInput() { return _input; }
        void DispatchPhysicsContact(SceneManagement::Scene *scene, const PhysicsContact2D &contact);
        void DispatchAnimationEvents(SceneManagement::Scene *scene, std::span<const AnimationEventMessage> events);
        void OnSceneDestroyed(SceneManagement::Scene *scene);

#if AILU_ENABLE_LUA_SCRIPTING
        template<typename EventViewType>
        ScriptSubscriptionHandle BindLuaDelegate(EventViewType event_view, sol::protected_function callback)
        {
            if (_currently_invoking_instance == nullptr || !callback.valid())
                return 0u;
            const auto owner = FindInstanceKey(_currently_invoking_instance);
            if (!owner.has_value())
                return 0u;
            const auto delegate_handle = event_view.Subscribe([this, owner = *owner, callback = std::move(callback)](auto &&...args) mutable
            {
                auto instance_iter = _instances.find(owner);
                if (instance_iter == _instances.end() || instance_iter->second._faulted || !instance_iter->second._is_enabled)
                    return;
                InvokeLuaCallback(instance_iter->second, callback, std::forward<decltype(args)>(args)...);
            });
            return RegisterSubscription(*owner, [event_view, delegate_handle]() mutable { event_view.Unsubscribe(delegate_handle); });
        }

        template<typename EventViewType, typename KeyType>
        ScriptSubscriptionHandle BindLuaEventRouter(EventViewType event_view, const KeyType &key,
                                                     sol::protected_function callback)
        {
            if (_currently_invoking_instance == nullptr || !callback.valid())
                return 0u;
            const auto owner = FindInstanceKey(_currently_invoking_instance);
            if (!owner.has_value())
                return 0u;
            const auto event_handle = event_view.Subscribe(key, [this, owner = *owner, callback = std::move(callback)](auto &&...args) mutable
            {
                auto instance_iter = _instances.find(owner);
                if (instance_iter == _instances.end() || instance_iter->second._faulted || !instance_iter->second._is_enabled)
                    return;
                InvokeLuaCallback(instance_iter->second, callback, std::forward<decltype(args)>(args)...);
            });
            return RegisterSubscription(*owner, [event_view, event_handle]() mutable { event_view.Unsubscribe(event_handle); });
        }

        sol::state &GetState() { return _lua; }
        const sol::state &GetState() const { return _lua; }
        bool Unsubscribe(ScriptSubscriptionHandle subscription_id);
        ScriptCollider2D::EventViews GetColliderEventViews(SceneManagement::Scene *scene, ECS::Entity entity);
        ScriptAnimator::AnimationEventRouter::EventView GetAnimatorEventView(SceneManagement::Scene *scene,
                                                                               ECS::Entity entity);
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
            sol::protected_function _on_create, _on_enable, _on_disable, _on_fixed_update, _on_update, _on_late_update, _on_destroy,
                                    _on_reload;
            Vector<ScriptSubscriptionHandle> _subscriptions;
        };
        struct ScriptColliderEventSource
        {
            ScriptCollider2D::CollisionEventRouter _on_collision_enter;
            ScriptCollider2D::CollisionEventRouter _on_collision_exit;
            ScriptCollider2D::CollisionEventRouter _on_trigger_enter;
            ScriptCollider2D::CollisionEventRouter _on_trigger_exit;
        };
        struct ScriptAnimatorEventSource
        {
            ScriptAnimator::AnimationEventRouter _on_event;
        };
        struct ScriptPropertyDeclaration
        {
            String _name;
            String _type_hint;
            String _asset_type;
        };

        struct ScriptPrototype
        {
            String _resolved_path;
            u32 _version = 0u;
            sol::table _prototype;
            Vector<ScriptPropertyDeclaration> _property_declarations;
        };
        struct ScriptSubscription { std::function<void()> _unsubscribe; };

        void RegisterCoreBindings();
        bool ExecuteChunk(sol::load_result &&chunk, const String &chunk_name);
        void ReportError(const String &source, const sol::error &error) const;
        bool LoadComponentInstance(const ScriptInstanceKey &key, ECS::ScriptComponent &component, const ScriptEntity &entity);
        bool LoadPrototype(const Guid &script_asset, const std::filesystem::path &resolved_path, ScriptPrototype &prototype);
        void SynchronizeScriptProperties(ECS::ScriptComponent &component, const ScriptPrototype &prototype);
        void InjectScriptProperties(const ECS::ScriptComponent &component, SceneManagement::Scene *scene, sol::table &instance) const;
        bool EnsureComponentReady(SceneManagement::Scene *scene, ECS::Entity entity, ECS::ScriptComponent &component);
        bool SynchronizeComponentEnabledState(ScriptInstance &instance, bool is_enabled);
        void CacheLifecycleFunctions(ScriptInstance &instance);
        std::optional<ScriptInstanceKey> FindInstanceKey(const ScriptInstance *instance) const;
        ScriptSubscriptionHandle RegisterSubscription(const ScriptInstanceKey &owner, std::function<void()> unsubscribe);
        void ClearSubscriptions(ScriptInstance &instance);
        void DispatchInputEvent(const InputActionEvent &event);
        template<typename... Args>
        bool InvokeLuaCallback(ScriptInstance &instance, sol::protected_function &callback, Args &&...args)
        {
            ScriptInstance *previous_instance = _currently_invoking_instance;
            _currently_invoking_instance = &instance;
            if (_traceback.valid()) callback.set_error_handler(_traceback);
            sol::protected_function_result result = callback(std::forward<Args>(args)...);
            _currently_invoking_instance = previous_instance;
            if (result.valid()) return true;
            sol::error error = result;
            ReportError(instance._resolved_script_path.empty() ? instance._script_asset.ToString() : instance._resolved_script_path, error);
            instance._faulted = true;
            return false;
        }

        template<typename... Args>
        bool InvokeComponentMethod(ScriptInstance &instance, sol::protected_function &function, Args &&...args)
        {
            if (!function.valid()) return true;
            ScriptInstance *previous_instance = _currently_invoking_instance;
            _currently_invoking_instance = &instance;
            if (_traceback.valid()) function.set_error_handler(_traceback);
            sol::protected_function_result result = function(instance._instance, std::forward<Args>(args)...);
            _currently_invoking_instance = previous_instance;
            if (result.valid()) return true;
            sol::error error = result;
            ReportError(instance._resolved_script_path.empty() ? instance._script_asset.ToString() : instance._resolved_script_path, error);
            instance._faulted = true;
            return false;
        }
        bool ProcessReload(const Guid &script_asset);
        void RebuildInstancesForPrototype(const Guid &script_asset);
        void PruneInvalidInstances();
        void EnsurePhysicsContactBridge(SceneManagement::Scene *scene);
        void ClearPhysicsContactBridges();

        sol::state _lua;
        sol::protected_function _traceback;
        HashMap<ScriptInstanceKey, ScriptInstance, ScriptInstanceKeyHasher> _instances;
        HashMap<String, ScriptPrototype> _prototypes;
        HashMap<ScriptSubscriptionHandle, ScriptSubscription> _subscriptions;
        ScriptInstance *_currently_invoking_instance = nullptr;
        ScriptSubscriptionHandle _next_subscription_id = 1u;
        InputSystem::ActionEventListenerId _input_event_listener_id = 0u;
        HashMap<Physics2DWorld *, u32> _physics_contact_bridges;
        HashMap<ScriptInstanceKey, Scope<ScriptColliderEventSource>, ScriptInstanceKeyHasher> _collider_event_sources;
        HashMap<ScriptInstanceKey, Scope<ScriptAnimatorEventSource>, ScriptInstanceKeyHasher> _animator_event_sources;
#endif
        ScriptInput _input;
        HashMap<String, std::filesystem::path> _loaded_script_files;
        HashMap<String, u32> _script_versions;
        std::unordered_set<String> _pending_reload_assets;
        Queue<Guid> _reload_queue;
        bool _is_initialized = false;
        f32 _last_delta_time = 0.0f, _last_fixed_delta_time = 0.0f, _last_render_alpha = 0.0f;
    };
}

#endif
