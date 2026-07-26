/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/InputActionAsset.h"

namespace Ailu
{
    InputActionAsset::InputActionAsset()
        : Object("InputActionAsset")
    {
    }

    InputActionAsset::InputActionAsset(const String &name)
        : Object(name)
    {
    }

    InputActionMap *InputActionAsset::FindActionMap(StringView name)
    {
        for (auto &action_map : _action_maps)
        {
            if (action_map.GetName() == name)
                return &action_map;
        }
        return nullptr;
    }

    const InputActionMap *InputActionAsset::FindActionMap(StringView name) const
    {
        for (const auto &action_map : _action_maps)
        {
            if (action_map.GetName() == name)
                return &action_map;
        }
        return nullptr;
    }

    InputContext *InputActionAsset::FindContext(StringView name)
    {
        for (auto &context : _contexts)
        {
            if (context.GetName() == name)
                return &context;
        }
        return nullptr;
    }

    const InputContext *InputActionAsset::FindContext(StringView name) const
    {
        for (const auto &context : _contexts)
        {
            if (context.GetName() == name)
                return &context;
        }
        return nullptr;
    }

} // namespace Ailu
