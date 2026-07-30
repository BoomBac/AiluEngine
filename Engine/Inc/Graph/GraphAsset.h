#pragma once

#include "Graph/GraphTypes.h"
#include "Objects/Object.h"
#include "generated/GraphAsset.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API GraphAsset : public Object
    {
        GENERATED_BODY()

    public:
        GraphAsset();
        explicit GraphAsset(const String &name);

        const Vector<GraphNodeData> &Nodes() const { return _nodes; }
        const Vector<GraphLinkData> &Links() const { return _links; }
        const Vector<GraphCommentData> &Comments() const { return _comments; }

        Vector<GraphNodeData> &MutableNodes() { return _nodes; }
        Vector<GraphLinkData> &MutableLinks() { return _links; }
        Vector<GraphCommentData> &MutableComments() { return _comments; }

        u32 Version() const { return _version; }
        const String &SchemaType() const { return _schema_type; }
        void SchemaType(const String &schema_type) { _schema_type = schema_type; }

    private:
        APROPERTY()
        u32 _version = 1u;
        APROPERTY()
        String _schema_type = "FlowGraphSchema";
        APROPERTY()
        Vector<GraphNodeData> _nodes;
        APROPERTY()
        Vector<GraphLinkData> _links;
        APROPERTY()
        Vector<GraphCommentData> _comments;
    };
} // namespace Ailu
