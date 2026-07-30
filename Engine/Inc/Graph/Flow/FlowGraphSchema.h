#pragma once

#include "Graph/GraphSchema.h"

namespace Ailu
{
    class AILU_API FlowGraphSchema final : public IGraphSchema
    {
    public:
        GraphConnectionResponse CanConnect(const GraphDocument &document, const GraphPinData &source,
                                           const GraphPinData &target) const override;
        void CollectNodeActions(const GraphDocument &document, const GraphPinData *source_pin,
                                Vector<GraphNodeAction> &actions) const override;
        bool AllowsCycles() const override { return true; }
    };
} // namespace Ailu
