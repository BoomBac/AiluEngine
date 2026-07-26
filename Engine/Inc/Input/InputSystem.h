/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputSystem – central orchestrator for the Input Action System
 *
 * InputSystem is the single entry-point for all input processing. It:
 *   1. Manages InputDevice instances
 *   2. Maintains the Context stack (priority-ordered)
 *   3. Resolves control paths at load time
 *   4. Drives per-frame: Poll → Context → ActionMap → Action → Binding
 *   5. Tracks consumption and blocked contexts
 *   6. Provides action lookup for gameplay code
 *
 * Usage:
 *   // At startup
 *   _input_system = MakeScope<InputSystem>();
 *   _input_system->LoadAsset(my_input_asset);
 *   _input_system->PushContext("Gameplay");
 *
 *   // Every frame
 *   _input_system->Update(delta_time);
 *
 *   // In gameplay code
 *   if (auto *jump = _input_system->FindAction("Jump"))
 *   {
 *       if (jump->WasPerformedThisFrame())
 *           character->Jump();
 *   }
 *
 *   // On pause
 *   _input_system->PushContext("PauseMenu");  // blocks lower contexts
 *   // On resume
 *   _input_system->PopContext("PauseMenu");
 */

#pragma once
#ifndef __INPUT_SYSTEM_H__
#define __INPUT_SYSTEM_H__

#include "InputTypes.h"
#include "InputValue.h"
#include "InputControl.h"
#include "InputDevice.h"
#include "InputBinding.h"
#include "InputAction.h"
#include "InputActionMap.h"
#include "InputContext.h"
#include "InputActionAsset.h"
#include "Framework/Core/String.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include <functional>
#include <unordered_set>

namespace Ailu
{
    // ========================================================================
    //  InputSystem
    // ========================================================================
    class AILU_API InputSystem
    {
    public:
        // --- Event types ---
        using DeviceConnectedCallback = std::function<void(InputDevice *)>;
        using DeviceDisconnectedCallback = std::function<void(u32 device_id)>;
        using ActionEventCallback = std::function<void(const InputActionEvent &)>;

        InputSystem();
        ~InputSystem();

        // Non-copyable
        InputSystem(const InputSystem &) = delete;
        InputSystem &operator=(const InputSystem &) = delete;

        // ====================================================================
        //  Device management
        // ====================================================================
        void RegisterDevice(Scope<InputDevice> device);
        void RemoveDevice(u32 device_id);

        [[nodiscard]] InputDevice *GetDevice(u32 device_id) const;
        [[nodiscard]] InputDevice *FindDeviceByType(EInputDeviceType type) const;
        [[nodiscard]] const Vector<Scope<InputDevice>> &GetDevices() const { return _devices; }

        void SetDeviceConnectedCallback(const DeviceConnectedCallback &cb) { _on_device_connected = cb; }
        void SetDeviceDisconnectedCallback(const DeviceDisconnectedCallback &cb) { _on_device_disconnected = cb; }

        // ====================================================================
        //  Control path resolution
        // ====================================================================
        /// @brief Parse a control path like "<Keyboard>/space" into a resolved runtime reference
        [[nodiscard]] ResolvedInputControl ResolveControlPath(StringView path) const;

        // ====================================================================
        //  Asset loading
        // ====================================================================
        /// @brief Load an InputActionAsset, resolving all binding paths and creating runtime state
        void LoadAsset(const InputActionAsset &asset);

        // ====================================================================
        //  Context management
        // ====================================================================
        void PushContext(Ref<InputContext> context);
        void PushContext(const String &context_name);
        void PopContext(StringView context_name);
        void RemoveAllContexts();

        [[nodiscard]] InputContext *FindContext(StringView name);
        [[nodiscard]] const InputContext *FindContext(StringView name) const;

        // ====================================================================
        //  Action queries (for gameplay code)
        // ====================================================================
        [[nodiscard]] InputAction *FindAction(StringView name);
        [[nodiscard]] const InputAction *FindAction(StringView name) const;

        [[nodiscard]] InputAction *FindActionById(u32 id);
        [[nodiscard]] const InputAction *FindActionById(u32 id) const;

        void AddActionEventListener(const ActionEventCallback &cb) { _action_event_listeners.push_back(cb); }

        // ====================================================================
        //  Per-frame update
        // ====================================================================
        void Update(f32 delta_time);

        // ====================================================================
        //  Utilities
        // ====================================================================
        [[nodiscard]] InputDevice *GetLastActiveDevice() const { return _last_active_device; }
        [[nodiscard]] f64 GetCurrentTime() const { return _current_time; }

        /// @brief Dump debug info (devices, contexts, actions, consumed controls)
        void DebugPrint() const;

    private:
        void PollDevices();
        void UpdateContexts(f32 delta_time);
        void ProcessActionMap(InputActionMap &action_map, f32 delta_time);
        void ResetFrameState();
        void SortContexts();
        void DispatchActionEvent(const InputActionEvent &event);

        // --- Consume controls used by an action's bindings ---
        void ConsumeActionControls(const InputAction &action);

        // --- Check if all controls that feed an action are consumed ---
        [[nodiscard]] bool AreActionControlsConsumed(const InputAction &action) const;

    private:
        // --- Devices ---
        Vector<Scope<InputDevice>> _devices;
        HashMap<u32, InputDevice *> _device_id_to_device;

        // --- Active contexts (sorted by priority each frame) ---
        Vector<Ref<InputContext>> _active_contexts;

        // --- Runtime action maps (instantiated from asset) ---
        Vector<Ref<InputActionMap>> _runtime_action_maps;

        // --- Action lookup ---
        HashMap<String, InputAction *> _action_name_to_ptr;
        HashMap<u32, InputAction *> _action_id_to_ptr;

        // --- Per-frame state ---
        InputConsumptionState _consumption_state;
        InputDevice *_last_active_device = nullptr;
        f64 _current_time = 0.0;
        f32 _last_delta_time = 0.0f;

        // --- Callbacks ---
        DeviceConnectedCallback _on_device_connected;
        DeviceDisconnectedCallback _on_device_disconnected;
        Vector<ActionEventCallback> _action_event_listeners;

        // --- Persisted context templates (loaded from asset) ---
        HashMap<String, Ref<InputContext>> _context_templates;
    };

} // namespace Ailu

#endif // !__INPUT_SYSTEM_H__
