#pragma once

#include "Graph/GraphTypes.h"

namespace Ailu
{
    class GraphDocument;

    enum class EGraphValidationSeverity : u8
    {
        kInfo,
        kWarning,
        kError
    };

    struct AILU_API GraphValidationMessage
    {
        EGraphValidationSeverity _severity = EGraphValidationSeverity::kInfo;
        String _message;
        Guid _node_id = Guid::EmptyGuid();
        Guid _pin_id = Guid::EmptyGuid();
        Guid _link_id = Guid::EmptyGuid();
    };

    class AILU_API GraphValidation
    {
    public:
        static void Validate(const GraphDocument &document, Vector<GraphValidationMessage> &messages);
    };
} // namespace Ailu
