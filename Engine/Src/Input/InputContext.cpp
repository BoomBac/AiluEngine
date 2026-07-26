/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/InputContext.h"

namespace Ailu
{
    void InputContext::Activate()
    {
        if (_active)
            return;

        _active = true;
        for (auto &action_map : _action_maps)
        {
            if (action_map)
                action_map->Enable();
        }
    }

    void InputContext::Deactivate()
    {
        if (!_active)
            return;

        _active = false;
        for (auto &action_map : _action_maps)
        {
            if (action_map)
                action_map->Disable();
        }
    }

} // namespace Ailu
