/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputActionMap – groups related actions (Gameplay, UI, Vehicle, …)
 *
 * ActionMaps organise actions, provide bulk enable/disable, and are the
 * basic unit that InputContext operates on. An ActionMap does NOT handle
 * priority or consumption — that's InputContext's job.
 */

#pragma once
#ifndef __INPUT_ACTION_MAP_H__
#define __INPUT_ACTION_MAP_H__

#include "InputAction.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"

namespace Ailu
{
    class AILU_API InputActionMap
    {
    public:
        InputActionMap() = default;
        explicit InputActionMap(const String &name) : _name(name) {}
        InputActionMap(const InputActionMap &other);
        InputActionMap(InputActionMap &&other) noexcept = default;
        InputActionMap &operator=(const InputActionMap &other);
        InputActionMap &operator=(InputActionMap &&other) noexcept = default;

        // --- Lifecycle ---
        void Enable();
        void Disable();
        void Update(const InputActionUpdateContext &context);

        // --- Queries ---
        [[nodiscard]] const String &GetName() const { return _name; }
        void SetName(const String &name) { _name = name; }

        [[nodiscard]] u32 GetId() const { return _id; }
        void SetId(u32 id) { _id = id; }

        [[nodiscard]] bool IsEnabled() const { return _enabled; }

        /// @brief Find an action by name (returns nullptr if not found)
        [[nodiscard]] InputAction *FindAction(StringView name);
        [[nodiscard]] const InputAction *FindAction(StringView name) const;

        /// @brief Find an action by ID (returns nullptr if not found)
        [[nodiscard]] InputAction *FindActionById(u32 id);
        [[nodiscard]] const InputAction *FindActionById(u32 id) const;

        // --- Actions ---
        [[nodiscard]] Vector<InputAction> &GetActions() { return _actions; }
        [[nodiscard]] const Vector<InputAction> &GetActions() const { return _actions; }

        void AddAction(const InputAction &action);
        void AddAction(InputAction &&action);

        /// @brief Number of actions in this map
        [[nodiscard]] size_t ActionCount() const { return _actions.size(); }

    private:
        void RebuildLookup();

    private:
        String _name;
        u32 _id = 0;
        Vector<InputAction> _actions;
        HashMap<String, u32> _action_name_to_index;
        HashMap<u32, u32> _action_id_to_index;
        bool _enabled = false;
    };

} // namespace Ailu

#endif // !__INPUT_ACTION_MAP_H__
