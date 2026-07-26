/*
 * @author    : BoomBac
 * @created   : 2026.7
 */

#include "Input/InputActionMap.h"

namespace Ailu
{
    InputActionMap::InputActionMap(const InputActionMap &other)
        : _name(other._name), _id(other._id), _actions(other._actions), _enabled(other._enabled)
    {
        RebuildLookup();
    }

    InputActionMap &InputActionMap::operator=(const InputActionMap &other)
    {
        if (this == &other)
            return *this;

        _name = other._name;
        _id = other._id;
        _actions = other._actions;
        _enabled = other._enabled;
        RebuildLookup();
        return *this;
    }

    void InputActionMap::Enable()
    {
        if (_enabled)
            return;

        _enabled = true;
        for (auto &action : _actions)
            action.Enable();
    }

    void InputActionMap::Disable()
    {
        if (!_enabled)
            return;

        _enabled = false;
        for (auto &action : _actions)
            action.Disable();
    }

    void InputActionMap::Update(const InputActionUpdateContext &context)
    {
        if (!_enabled)
            return;

        for (auto &action : _actions)
            action.Update(context);
    }

    void InputActionMap::AddAction(const InputAction &action)
    {
        _actions.push_back(action);
        RebuildLookup();
    }

    void InputActionMap::AddAction(InputAction &&action)
    {
        _actions.push_back(std::move(action));
        RebuildLookup();
    }

    InputAction *InputActionMap::FindAction(StringView name)
    {
        auto it = _action_name_to_index.find(String(name));
        if (it != _action_name_to_index.end() && it->second < _actions.size())
            return &_actions[it->second];
        return nullptr;
    }

    const InputAction *InputActionMap::FindAction(StringView name) const
    {
        auto it = _action_name_to_index.find(String(name));
        if (it != _action_name_to_index.end() && it->second < _actions.size())
            return &_actions[it->second];
        return nullptr;
    }

    InputAction *InputActionMap::FindActionById(u32 id)
    {
        auto it = _action_id_to_index.find(id);
        if (it != _action_id_to_index.end() && it->second < _actions.size())
            return &_actions[it->second];
        return nullptr;
    }

    const InputAction *InputActionMap::FindActionById(u32 id) const
    {
        auto it = _action_id_to_index.find(id);
        if (it != _action_id_to_index.end() && it->second < _actions.size())
            return &_actions[it->second];
        return nullptr;
    }

    void InputActionMap::RebuildLookup()
    {
        _action_name_to_index.clear();
        _action_id_to_index.clear();
        for (u32 i = 0; i < static_cast<u32>(_actions.size()); ++i)
        {
            const auto &action = _actions[i];
            _action_name_to_index[action.GetName()] = i;
            _action_id_to_index[action.GetId()] = i;
        }
    }

} // namespace Ailu
