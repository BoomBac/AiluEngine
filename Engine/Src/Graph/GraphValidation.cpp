#include "Graph/GraphValidation.h"
#include "Graph/GraphDocument.h"
#include "Graph/GraphNodeRegistry.h"
#include <set>

namespace Ailu
{
    namespace
    {
        void AddMessage(Vector<GraphValidationMessage> &messages, EGraphValidationSeverity severity, String message,
                        Guid node_id = Guid::EmptyGuid(), Guid pin_id = Guid::EmptyGuid(), Guid link_id = Guid::EmptyGuid())
        {
            messages.emplace_back(GraphValidationMessage{severity, std::move(message), node_id, pin_id, link_id});
        }
    } // namespace

    void GraphValidation::Validate(const GraphDocument &document, Vector<GraphValidationMessage> &messages)
    {
        std::set<String> node_ids;
        std::set<String> pin_ids;
        std::set<String> link_ids;
        u32 entry_node_count = 0u;

        for (const GraphNodeData &node : document.Nodes())
        {
            if (node._id == Guid::EmptyGuid())
                AddMessage(messages, EGraphValidationSeverity::kError, "Node has an empty id.", node._id);
            if (!node_ids.emplace(node._id.ToString()).second)
                AddMessage(messages, EGraphValidationSeverity::kError, "Duplicate node id.", node._id);
            if (GraphNodeRegistry::Get().FindNode(node._node_type) == nullptr)
                AddMessage(messages, EGraphValidationSeverity::kError, "Node type is not registered.", node._id);
            if (HasGraphFlag(node._flags, EGraphNodeFlag::kEntryNode))
                ++entry_node_count;

            for (const GraphPinData &pin : node._pins)
            {
                if (pin._id == Guid::EmptyGuid())
                    AddMessage(messages, EGraphValidationSeverity::kError, "Pin has an empty id.", node._id, pin._id);
                if (!pin_ids.emplace(pin._id.ToString()).second)
                    AddMessage(messages, EGraphValidationSeverity::kError, "Duplicate pin id.", node._id, pin._id);
                if (pin._kind == EGraphPinKind::kExecution && !pin._value_type.empty() && pin._value_type != "exec")
                    AddMessage(messages, EGraphValidationSeverity::kWarning, "Execution pin has a non exec value type.", node._id, pin._id);
                if (pin._kind == EGraphPinKind::kValue && pin._value_type.empty())
                    AddMessage(messages, EGraphValidationSeverity::kWarning, "Value pin type is empty.", node._id, pin._id);
            }
        }

        if (document.Asset() != nullptr && document.Asset()->SchemaType() == "FlowGraphSchema" && entry_node_count != 1u)
            AddMessage(messages, EGraphValidationSeverity::kError, "Flow graph must contain exactly one entry node.");

        HashMap<Guid, u32, GuidHasher> input_link_counts;
        for (const GraphLinkData &link : document.Links())
        {
            if (link._id == Guid::EmptyGuid())
                AddMessage(messages, EGraphValidationSeverity::kError, "Link has an empty id.", Guid::EmptyGuid(),
                           Guid::EmptyGuid(), link._id);
            if (!link_ids.emplace(link._id.ToString()).second)
                AddMessage(messages, EGraphValidationSeverity::kError, "Duplicate link id.", Guid::EmptyGuid(),
                           Guid::EmptyGuid(), link._id);

            const GraphPinData *output_pin = document.FindPin(link._output_pin);
            const GraphPinData *input_pin = document.FindPin(link._input_pin);
            if (output_pin == nullptr || input_pin == nullptr)
            {
                AddMessage(messages, EGraphValidationSeverity::kError, "Link references a missing pin.", Guid::EmptyGuid(),
                           Guid::EmptyGuid(), link._id);
                continue;
            }
            if (output_pin->_direction != EGraphPinDirection::kOutput || input_pin->_direction != EGraphPinDirection::kInput)
            {
                AddMessage(messages, EGraphValidationSeverity::kError, "Link pin directions are invalid.", Guid::EmptyGuid(),
                           Guid::EmptyGuid(), link._id);
            }
            if (output_pin->_kind != input_pin->_kind)
            {
                AddMessage(messages, EGraphValidationSeverity::kError, "Link connects incompatible pin kinds.", Guid::EmptyGuid(),
                           Guid::EmptyGuid(), link._id);
            }
            if (output_pin->_kind == EGraphPinKind::kValue && output_pin->_value_type != input_pin->_value_type)
            {
                AddMessage(messages, EGraphValidationSeverity::kError, "Link connects incompatible value types.",
                           Guid::EmptyGuid(), Guid::EmptyGuid(), link._id);
            }
            const u32 input_link_count = ++input_link_counts[link._input_pin];
            const bool allows_multiple_input_links = document.Schema() != nullptr &&
                                                     document.Schema()->AllowsMultipleInputLinks();
            if (input_link_count > 1u && !allows_multiple_input_links)
            {
                AddMessage(messages, EGraphValidationSeverity::kError, "Input pin has multiple links.", Guid::EmptyGuid(),
                           link._input_pin, link._id);
            }
        }
    }
} // namespace Ailu
