/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/InputSystem.h"
#include "Framework/Common/Log.h"
#include <algorithm>
#include <sstream>

namespace Ailu
{
    // ========================================================================
    //  Construction
    // ========================================================================
    InputSystem::InputSystem()
    {
        LOG_INFO("InputSystem created");
    }

    InputSystem::~InputSystem()
    {
        RemoveAllContexts();
        _devices.clear();
        LOG_INFO("InputSystem destroyed");
    }

    // ========================================================================
    //  Device management
    // ========================================================================
    void InputSystem::RegisterDevice(Scope<InputDevice> device)
    {
        if (!device)
            return;

        const u32 id = device->GetDeviceId();
        _device_id_to_device[id] = device.get();
        _devices.push_back(std::move(device));

        if (_on_device_connected)
            _on_device_connected(_devices.back().get());

        LOG_INFO("InputSystem: Registered device '{}' (id={})",
                 _devices.back()->GetDeviceName(), id);
    }

    void InputSystem::RemoveDevice(u32 device_id)
    {
        auto it = _device_id_to_device.find(device_id);
        if (it == _device_id_to_device.end())
            return;

        for (auto iter = _devices.begin(); iter != _devices.end(); ++iter)
        {
            if ((*iter)->GetDeviceId() == device_id)
            {
                if (_on_device_disconnected)
                    _on_device_disconnected(device_id);
                _devices.erase(iter);
                break;
            }
        }
        _device_id_to_device.erase(it);
    }

    InputDevice *InputSystem::GetDevice(u32 device_id) const
    {
        auto it = _device_id_to_device.find(device_id);
        return it != _device_id_to_device.end() ? it->second : nullptr;
    }

    InputDevice *InputSystem::FindDeviceByType(EInputDeviceType type) const
    {
        for (const auto &device : _devices)
        {
            if (device->GetDeviceType() == type)
                return device.get();
        }
        return nullptr;
    }

    // ========================================================================
    //  Control path resolution
    // ========================================================================
    ResolvedInputControl InputSystem::ResolveControlPath(StringView path) const
    {
        // Expected format: "<DeviceType>/control_name"
        // e.g. "<Keyboard>/space", "<Mouse>/left_button", "<Gamepad>/left_stick"

        ResolvedInputControl result;

        String path_str(path);

        // Extract device type
        const size_t open_pos = path_str.find('<');
        const size_t close_pos = path_str.find('>');
        if (open_pos == String::npos || close_pos == String::npos || close_pos <= open_pos)
        {
            LOG_WARNING("InputSystem: Invalid control path '{}' – missing <DeviceType>", path_str);
            return result;
        }

        const String device_type_str = path_str.substr(open_pos + 1, close_pos - open_pos - 1);

        // Validate separator: must be "/>" i.e. "/" immediately after ">"
        if (path_str.size() <= close_pos + 1 || path_str[close_pos + 1] != '/')
        {
            LOG_WARNING("InputSystem: Invalid control path '{}' – missing '/' separator after device type", path_str);
            return result;
        }

        const String control_name = path_str.substr(close_pos + 2); // skip ">/"
        if (control_name.empty())
        {
            LOG_WARNING("InputSystem: Invalid control path '{}' – missing control name", path_str);
            return result;
        }

        static const HashMap<String, EInputDeviceType> kDeviceTypeMap = {
            {"Keyboard", EInputDeviceType::kKeyboard},
            {"Mouse", EInputDeviceType::kMouse},
            {"Gamepad", EInputDeviceType::kGamepad},
        };

        auto type_it = kDeviceTypeMap.find(device_type_str);
        if (type_it == kDeviceTypeMap.end())
        {
            LOG_WARNING("InputSystem: Unknown device type '{}' in path '{}'", device_type_str, path_str);
            return result;
        }

        const EInputDeviceType target_type = type_it->second;

        // Find the first matching device
        for (const auto &device : _devices)
        {
            if (device->GetDeviceType() == target_type && device->IsConnected())
            {
                u16 control_index = device->FindControlIndex(control_name);
                if (control_index != InputConstants::kInvalidControlIndex)
                {
                    result._device = device.get();
                    result._control_index = control_index;
                    result._value_type = device->GetControlDesc(control_index)._value_type;
                    return result;
                }
            }
        }

        LOG_WARNING("InputSystem: Could not resolve control path '{}' – control '{}' not found",
                    path_str, control_name);
        return result;
    }

    // ========================================================================
    //  Asset loading
    // ========================================================================
    void InputSystem::LoadAsset(const InputActionAsset &asset)
    {
        // --- Store context templates ---
        _context_templates.clear();
        for (const auto &context : asset.GetContexts())
        {
            auto ctx_ref = MakeRef<InputContext>(context);
            _context_templates[context.GetName()] = ctx_ref;
        }

        // --- Instantiate action maps ---
        _runtime_action_maps.clear();
        _action_name_to_ptr.clear();
        _action_id_to_ptr.clear();

        for (const auto &action_map : asset.GetActionMaps())
        {
            auto map_ref = MakeRef<InputActionMap>(action_map);
            _runtime_action_maps.push_back(map_ref);

            // Resolve binding paths
            for (auto &action : map_ref->GetActions())
            {
                // Resolve regular bindings
                for (auto &binding : action.GetBindings())
                {
                    if (binding._is_composite || binding._is_part_of_composite)
                        continue;

                    binding._resolved_control = ResolveControlPath(binding._control_path);
                }

                // Resolve composite bindings
                if (auto &composite = action.GetComposite())
                {
                    for (auto *child : composite->GetChildBindings())
                    {
                        child->_resolved_control = ResolveControlPath(child->_control_path);
                    }
                }

                // Build lookup
                _action_name_to_ptr[action.GetName()] = &action;
                _action_id_to_ptr[action.GetId()] = &action;
            }
        }

        // --- Wire up context templates to action maps ---
        for (auto &[name, ctx] : _context_templates)
        {
            Vector<Ref<InputActionMap>> resolved_maps;
            for (const auto &map_name_ref : ctx->GetActionMaps())
            {
                // Find the runtime action map by name
                for (auto &rt_map : _runtime_action_maps)
                {
                    if (rt_map->GetName() == map_name_ref->GetName())
                    {
                        resolved_maps.push_back(rt_map);
                        break;
                    }
                }
            }
            ctx->GetActionMaps() = std::move(resolved_maps);
        }

        LOG_INFO("InputSystem: Loaded asset '{}' with {} action maps, {} contexts",
                 asset.Name(), _runtime_action_maps.size(), _context_templates.size());
    }

    // ========================================================================
    //  Context management
    // ========================================================================
    void InputSystem::PushContext(Ref<InputContext> context)
    {
        if (!context)
            return;

        // Don't push duplicates
        for (const auto &existing : _active_contexts)
        {
            if (existing == context)
                return;
        }

        context->Activate();
        _active_contexts.push_back(std::move(context));
    }

    void InputSystem::PushContext(const String &context_name)
    {
        auto it = _context_templates.find(context_name);
        if (it != _context_templates.end())
        {
            // Create a fresh instance from the template
            auto instance = MakeRef<InputContext>(*it->second);
            PushContext(std::move(instance));
        }
        else
        {
            LOG_WARNING("InputSystem: Cannot push unknown context '{}'", context_name);
        }
    }

    void InputSystem::PopContext(StringView context_name)
    {
        for (auto it = _active_contexts.begin(); it != _active_contexts.end(); ++it)
        {
            if ((*it)->GetName() == context_name)
            {
                (*it)->Deactivate();
                _active_contexts.erase(it);
                return;
            }
        }
        LOG_WARNING("InputSystem: Cannot pop context '{}' – not found in active contexts", context_name);
    }

    void InputSystem::RemoveAllContexts()
    {
        for (auto &ctx : _active_contexts)
            ctx->Deactivate();
        _active_contexts.clear();
    }

    InputContext *InputSystem::FindContext(StringView name)
    {
        for (auto &ctx : _active_contexts)
        {
            if (ctx->GetName() == name)
                return ctx.get();
        }
        auto it = _context_templates.find(String(name));
        return it != _context_templates.end() ? it->second.get() : nullptr;
    }

    const InputContext *InputSystem::FindContext(StringView name) const
    {
        for (const auto &ctx : _active_contexts)
        {
            if (ctx->GetName() == name)
                return ctx.get();
        }
        auto it = _context_templates.find(String(name));
        return it != _context_templates.end() ? it->second.get() : nullptr;
    }

    // ========================================================================
    //  Action queries
    // ========================================================================
    InputAction *InputSystem::FindAction(StringView name)
    {
        auto it = _action_name_to_ptr.find(String(name));
        return it != _action_name_to_ptr.end() ? it->second : nullptr;
    }

    const InputAction *InputSystem::FindAction(StringView name) const
    {
        auto it = _action_name_to_ptr.find(String(name));
        return it != _action_name_to_ptr.end() ? it->second : nullptr;
    }

    InputAction *InputSystem::FindActionById(u32 id)
    {
        auto it = _action_id_to_ptr.find(id);
        return it != _action_id_to_ptr.end() ? it->second : nullptr;
    }

    const InputAction *InputSystem::FindActionById(u32 id) const
    {
        auto it = _action_id_to_ptr.find(id);
        return it != _action_id_to_ptr.end() ? it->second : nullptr;
    }

    // ========================================================================
    //  Per-frame update
    // ========================================================================
    void InputSystem::Update(f32 delta_time)
    {
        _current_time += static_cast<f64>(delta_time);
        _last_delta_time = delta_time;
        _consumption_state.Reset();

        PollDevices();
        UpdateContexts(delta_time);
    }

    void InputSystem::PollDevices()
    {
        for (auto &device : _devices)
        {
            if (device->IsConnected())
            {
                device->Poll();

                // Track last active device (simple: last polled device that has any actuation)
                // A more sophisticated implementation would check each control.
                _last_active_device = device.get();
            }
        }
    }

    void InputSystem::UpdateContexts(f32 delta_time)
    {
        SortContexts();

        InputActionUpdateContext action_ctx;
        action_ctx._time = _current_time;
        action_ctx._delta_time = delta_time;

        for (auto &context : _active_contexts)
        {
            if (!context->IsActive())
                continue;

            // Update all action maps in this context
            for (auto &action_map : context->GetActionMaps())
            {
                if (!action_map || !action_map->IsEnabled())
                    continue;

                action_map->Update(action_ctx);

                // Mark consumed controls
                if (context->ConsumeInput())
                {
                    for (auto &action : action_map->GetActions())
                    {
                        if (!action.IsEnabled())
                            continue;

                        if (action.WasPerformedThisFrame() ||
                            action.WasStartedThisFrame())
                        {
                            ConsumeActionControls(action);
                        }
                    }
                }

                // Dispatch action events
                for (auto &action : action_map->GetActions())
                {
                    if (!action.IsEnabled())
                        continue;

                    if (action.WasStartedThisFrame() ||
                        action.WasPerformedThisFrame() ||
                        action.WasCanceledThisFrame())
                    {
                        InputActionEvent event;
                        event._action = &action;
                        event._value = action.GetValue();
                        event._phase = action.GetPhase();
                        event._time = _current_time;

                        DispatchActionEvent(event);
                    }
                }
            }

            // Check if this context blocks lower contexts
            if (context->BlocksLowerContexts())
                break;
        }
    }

    void InputSystem::ResetFrameState()
    {
        _consumption_state.Reset();
    }

    void InputSystem::SortContexts()
    {
        std::sort(_active_contexts.begin(), _active_contexts.end(),
                  [](const Ref<InputContext> &lhs, const Ref<InputContext> &rhs) {
                      return lhs->GetPriority() > rhs->GetPriority();
                  });
    }

    void InputSystem::ConsumeActionControls(const InputAction &action)
    {
        for (const auto &binding : action.GetBindings())
        {
            if (binding._resolved_control.IsValid())
            {
                _consumption_state.Consume(binding._resolved_control.Hash());
            }
        }

        // Check composite children
        if (const auto &composite = action.GetComposite())
        {
            for (const auto *child : composite->GetChildBindings())
            {
                if (child->_resolved_control.IsValid())
                {
                    _consumption_state.Consume(child->_resolved_control.Hash());
                }
            }
        }
    }

    bool InputSystem::AreActionControlsConsumed(const InputAction &action) const
    {
        for (const auto &binding : action.GetBindings())
        {
            if (binding._resolved_control.IsValid() &&
                _consumption_state.IsConsumed(binding._resolved_control.Hash()))
                return true;
        }
        return false;
    }

    void InputSystem::DispatchActionEvent(const InputActionEvent &event)
    {
        for (auto &listener : _action_event_listeners)
        {
            if (listener)
                listener(event);
        }
    }

    // ========================================================================
    //  Debug
    // ========================================================================
    void InputSystem::DebugPrint() const
    {
        LOG_INFO("=== InputSystem Debug ===");
        LOG_INFO("  Devices ({}) :", _devices.size());
        for (const auto &device : _devices)
        {
            LOG_INFO("    [{}] {} ({}) connected={}",
                     device->GetDeviceId(), device->GetDeviceName(),
                     static_cast<int>(device->GetDeviceType()),
                     device->IsConnected());
        }

        LOG_INFO("  Active Contexts ({}) :", _active_contexts.size());
        for (const auto &ctx : _active_contexts)
        {
            LOG_INFO("    {} (priority={}, blocking={}, consuming={}, active={})",
                     ctx->GetName(), ctx->GetPriority(),
                     ctx->BlocksLowerContexts(), ctx->ConsumeInput(),
                     ctx->IsActive());
            for (const auto &map : ctx->GetActionMaps())
            {
                if (map)
                    LOG_INFO("      ActionMap: {} (enabled={}, actions={})",
                             map->GetName(), map->IsEnabled(), map->ActionCount());
            }
        }

        LOG_INFO("  Consumed controls: {}", _consumption_state._consumed_control_ids.size());
    }

} // namespace Ailu
