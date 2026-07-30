#include "Graph/Flow/FlowGraphSchema.h"
#include "Graph/GraphDocument.h"

namespace Ailu
{
    GraphConnectionResponse FlowGraphSchema::CanConnect(const GraphDocument &document, const GraphPinData &source,
                                                        const GraphPinData &target) const
    {
        return IGraphSchema::CanConnect(document, source, target);
    }

    void FlowGraphSchema::CollectNodeActions(const GraphDocument &document, const GraphPinData *source_pin,
                                             Vector<GraphNodeAction> &actions) const
    {
        IGraphSchema::CollectNodeActions(document, source_pin, actions);
    }
} // namespace Ailu
