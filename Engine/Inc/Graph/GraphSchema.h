#pragma once

#include "Graph/GraphNodeRegistry.h"

namespace Ailu
{
    class GraphDocument;

    enum class EGraphConnectionAction : u8
    {
        kDisallow,
        kAllow,
        kReplaceInput,
        kInsertConversion
    };

    struct AILU_API GraphConnectionResponse
    {
        EGraphConnectionAction _action = EGraphConnectionAction::kDisallow;
        String _message;
        String _conversion_node_type;

        static GraphConnectionResponse Allow();
        static GraphConnectionResponse ReplaceInput();
        static GraphConnectionResponse Disallow(String message);
    };

    struct AILU_API GraphNodeAction
    {
        String _node_type;
        String _display_name;
        String _category;
        String _tooltip;
    };

    class AILU_API IGraphSchema
    {
    public:
        virtual ~IGraphSchema() = default;

        virtual GraphConnectionResponse CanConnect(const GraphDocument &document, const GraphPinData &source,
                                                   const GraphPinData &target) const;
        virtual void CollectNodeActions(const GraphDocument &document, const GraphPinData *source_pin,
                                        Vector<GraphNodeAction> &actions) const;
        virtual bool CanDeleteNode(const GraphDocument &document, const GraphNodeData &node) const;
        virtual bool CanCreateNode(const GraphDocument &document, StringView node_type) const;
        virtual bool AllowsCycles() const;
        virtual bool AllowsMultipleInputLinks() const;
    };

    AILU_API Scope<IGraphSchema> CreateGraphSchema(StringView schema_type);
} // namespace Ailu
