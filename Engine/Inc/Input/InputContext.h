/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputContext – defines an input "mode" with priority, blocking, and consumption
 *
 * An InputContext is a named set of InputActionMaps active together.
 * Contexts are pushed/popped on a priority stack. Higher priority = processed first.
 *
 * Contexts handle:
 *   - Which ActionMaps are active
 *   - Priority ordering
 *   - Blocking lower contexts (e.g. pause menu)
 *   - Control-level consumption
 *
 * Contexts do NOT handle: device polling, interaction timing, action state machines.
 */

#pragma once
#ifndef __INPUT_CONTEXT_H__
#define __INPUT_CONTEXT_H__

#include "InputTypes.h"
#include "InputControl.h"
#include "InputActionMap.h"
#include "Framework/Core/String.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/Containers/Vector.h"
#include <unordered_set>

namespace Ailu
{
    // ========================================================================
    //  InputConsumptionState – per-frame tracking of consumed controls
    // ========================================================================
    struct AILU_API InputConsumptionState
    {
        std::unordered_set<u64> _consumed_control_ids;

        [[nodiscard]] bool IsConsumed(u64 control_id) const
        {
            return _consumed_control_ids.find(control_id) != _consumed_control_ids.end();
        }

        void Consume(u64 control_id)
        {
            _consumed_control_ids.insert(control_id);
        }

        void Reset()
        {
            _consumed_control_ids.clear();
        }
    };

    // ========================================================================
    //  InputContext
    // ========================================================================
    class AILU_API InputContext
    {
    public:
        InputContext() = default;
        explicit InputContext(const String &name) : _name(name) {}

        // --- Properties ---
        [[nodiscard]] const String &GetName() const { return _name; }
        void SetName(const String &name) { _name = name; }

        [[nodiscard]] i32 GetPriority() const { return _priority; }
        void SetPriority(i32 priority) { _priority = priority; }

        /// @brief When true, no lower-priority contexts will be processed after this one
        [[nodiscard]] bool BlocksLowerContexts() const { return _block_lower_contexts; }
        void SetBlocksLowerContexts(bool block) { _block_lower_contexts = block; }

        /// @brief When true, controls used by this context are marked consumed
        [[nodiscard]] bool ConsumeInput() const { return _consume_input; }
        void SetConsumeInput(bool consume) { _consume_input = consume; }

        [[nodiscard]] bool IsActive() const { return _active; }
        void SetActive(bool active) { _active = active; }

        // --- Action Maps ---
        [[nodiscard]] Vector<Ref<InputActionMap>> &GetActionMaps() { return _action_maps; }
        [[nodiscard]] const Vector<Ref<InputActionMap>> &GetActionMaps() const { return _action_maps; }

        void AddActionMap(Ref<InputActionMap> action_map)
        {
            _action_maps.push_back(std::move(action_map));
        }

        // --- Lifecycle ---
        void Activate();
        void Deactivate();

    private:
        String _name;
        i32 _priority = 0;

        bool _consume_input = true;
        bool _block_lower_contexts = false;
        bool _active = false;

        Vector<Ref<InputActionMap>> _action_maps;
    };

} // namespace Ailu

#endif // !__INPUT_CONTEXT_H__
