#include "Input/InputBinding.h"

namespace Ailu
{
    InputBinding::InputBinding(const InputBinding &other)
        : _name(other._name),
          _control_path(other._control_path),
          _groups(other._groups),
          _resolved_control(other._resolved_control),
          _is_composite(other._is_composite),
          _is_part_of_composite(other._is_part_of_composite),
          _composite_part_name(other._composite_part_name),
          _is_consumed(other._is_consumed)
    {
        _processors.reserve(other._processors.size());
        for (const auto &processor : other._processors)
        {
            if (processor)
                _processors.emplace_back(processor->Clone());
        }

        _interactions.reserve(other._interactions.size());
        for (const auto &interaction : other._interactions)
        {
            if (interaction)
                _interactions.emplace_back(interaction->Clone());
        }
    }

    InputBinding &InputBinding::operator=(const InputBinding &other)
    {
        if (this == &other)
            return *this;

        _name = other._name;
        _control_path = other._control_path;
        _groups = other._groups;
        _resolved_control = other._resolved_control;
        _is_composite = other._is_composite;
        _is_part_of_composite = other._is_part_of_composite;
        _composite_part_name = other._composite_part_name;
        _is_consumed = other._is_consumed;

        _processors.clear();
        _processors.reserve(other._processors.size());
        for (const auto &processor : other._processors)
        {
            if (processor)
                _processors.emplace_back(processor->Clone());
        }

        _interactions.clear();
        _interactions.reserve(other._interactions.size());
        for (const auto &interaction : other._interactions)
        {
            if (interaction)
                _interactions.emplace_back(interaction->Clone());
        }

        return *this;
    }
} // namespace Ailu
