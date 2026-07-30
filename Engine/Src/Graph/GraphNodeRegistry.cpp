#include "Graph/GraphNodeRegistry.h"
#include <algorithm>

namespace Ailu
{
    namespace
    {
        bool ContainsText(StringView text, StringView search_text)
        {
            if (search_text.empty())
                return true;
            return text.find(search_text) != StringView::npos;
        }
    } // namespace

    GraphNodeRegistry &GraphNodeRegistry::Get()
    {
        static GraphNodeRegistry s_registry;
        return s_registry;
    }

    bool GraphNodeRegistry::RegisterNode(GraphNodeDesc desc)
    {
        if (desc._type_id.empty() || _node_descs.contains(desc._type_id))
            return false;
        _node_descs.emplace(desc._type_id, std::move(desc));
        return true;
    }

    const GraphNodeDesc *GraphNodeRegistry::FindNode(StringView type_id) const
    {
        const auto it = _node_descs.find(String(type_id));
        return it != _node_descs.end() ? &it->second : nullptr;
    }

    Vector<const GraphNodeDesc *> GraphNodeRegistry::FindNodes(StringView search_text) const
    {
        Vector<const GraphNodeDesc *> nodes;
        for (const auto &[type_id, desc] : _node_descs)
        {
            if (ContainsText(desc._type_id, search_text) || ContainsText(desc._display_name, search_text) ||
                ContainsText(desc._category, search_text))
            {
                nodes.emplace_back(&desc);
            }
        }
        std::sort(nodes.begin(), nodes.end(), [](const GraphNodeDesc *lhs, const GraphNodeDesc *rhs)
        {
            return lhs->_display_name < rhs->_display_name;
        });
        return nodes;
    }

    Vector<const GraphNodeDesc *> GraphNodeRegistry::FindNodesByCategory(StringView category) const
    {
        Vector<const GraphNodeDesc *> nodes;
        for (const auto &[type_id, desc] : _node_descs)
        {
            if (desc._category == category)
                nodes.emplace_back(&desc);
        }
        std::sort(nodes.begin(), nodes.end(), [](const GraphNodeDesc *lhs, const GraphNodeDesc *rhs)
        {
            return lhs->_display_name < rhs->_display_name;
        });
        return nodes;
    }

    bool GraphNodeRegistry::InitializeNode(StringView type_id, GraphNodeData &node) const
    {
        const GraphNodeDesc *desc = FindNode(type_id);
        if (desc == nullptr)
            return false;

        node._id = Guid::Generate();
        node._node_type = desc->_type_id;
        node._display_name = desc->_display_name;
        node._size = desc->_default_size;
        node._flags = desc->_flags;
        node._pins.clear();
        node._pins.reserve(desc->_pins.size());

        for (const GraphPinDesc &pin_desc : desc->_pins)
        {
            GraphPinData pin;
            pin._id = Guid::Generate();
            pin._name = pin_desc._name;
            pin._value_type = pin_desc._value_type;
            pin._direction = pin_desc._direction;
            pin._kind = pin_desc._kind;
            pin._default_value = pin_desc._default_value;
            node._pins.emplace_back(std::move(pin));
        }
        return true;
    }

    void GraphNodeRegistry::Clear()
    {
        _node_descs.clear();
    }
} // namespace Ailu
