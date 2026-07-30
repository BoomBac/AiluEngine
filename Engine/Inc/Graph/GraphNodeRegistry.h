#pragma once

#include "Graph/GraphTypes.h"
#include "Framework/Core/Containers/Map.h"

namespace Ailu
{
    struct AILU_API GraphPinDesc
    {
        String _name;
        String _value_type;
        EGraphPinDirection _direction = EGraphPinDirection::kInput;
        EGraphPinKind _kind = EGraphPinKind::kValue;
        String _default_value;
    };

    struct AILU_API GraphNodeDesc
    {
        String _type_id;
        String _display_name;
        String _category;
        String _tooltip;
        Color _title_color = {0.24f, 0.24f, 0.24f, 1.0f};
        Vector2f _default_size = {180.0f, 100.0f};
        Vector<GraphPinDesc> _pins;
        u32 _flags = GraphFlag(EGraphNodeFlag::kCanDelete) | GraphFlag(EGraphNodeFlag::kCanDuplicate);
    };

    class AILU_API GraphNodeRegistry
    {
    public:
        static GraphNodeRegistry &Get();

        bool RegisterNode(GraphNodeDesc desc);
        const GraphNodeDesc *FindNode(StringView type_id) const;
        Vector<const GraphNodeDesc *> FindNodes(StringView search_text) const;
        Vector<const GraphNodeDesc *> FindNodesByCategory(StringView category) const;
        bool InitializeNode(StringView type_id, GraphNodeData &node) const;
        void Clear();

    private:
        HashMap<String, GraphNodeDesc> _node_descs;
    };
} // namespace Ailu
