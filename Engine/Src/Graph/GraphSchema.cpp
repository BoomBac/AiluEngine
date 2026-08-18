#include "Graph/GraphDocument.h"
#include "Graph/Flow/FlowGraphSchema.h"

namespace Ailu
{
    namespace
    {
        class AnimationControllerGraphSchema final : public IGraphSchema
        {
        public:
            GraphConnectionResponse CanConnect(const GraphDocument &document, const GraphPinData &source,
                                               const GraphPinData &target) const override
            {
                (void)document;
                if (source._kind != EGraphPinKind::kExecution || target._kind != EGraphPinKind::kExecution)
                    return GraphConnectionResponse::Disallow("Animation transitions require execution pins.");
                if (source._direction == target._direction)
                    return GraphConnectionResponse::Disallow("Transition pins must have opposite directions.");
                const GraphPinData &output = source._direction == EGraphPinDirection::kOutput ? source : target;
                const GraphPinData &input = source._direction == EGraphPinDirection::kInput ? source : target;
                if (output._direction != EGraphPinDirection::kOutput || input._direction != EGraphPinDirection::kInput)
                    return GraphConnectionResponse::Disallow("Invalid transition pin direction.");
                return GraphConnectionResponse::Allow();
            }

            void CollectNodeActions(const GraphDocument &document, const GraphPinData *source_pin,
                                    Vector<GraphNodeAction> &actions) const override
            {
                (void)document;
                if (source_pin == nullptr || source_pin->_direction == EGraphPinDirection::kOutput)
                    actions.emplace_back(GraphNodeAction{"Animation.State", "State", "Animation", "Animation state"});
            }

            bool CanDeleteNode(const GraphDocument &document, const GraphNodeData &node) const override
            {
                (void)document;
                return node._node_type == "Animation.State";
            }

            bool CanCreateNode(const GraphDocument &document, StringView node_type) const override
            {
                (void)document;
                return node_type == "Animation.State";
            }

            bool AllowsCycles() const override
            {
                return true;
            }
        };
    }

    GraphConnectionResponse GraphConnectionResponse::Allow()
    {
        return {EGraphConnectionAction::kAllow, "", ""};
    }

    GraphConnectionResponse GraphConnectionResponse::ReplaceInput()
    {
        return {EGraphConnectionAction::kReplaceInput, "", ""};
    }

    GraphConnectionResponse GraphConnectionResponse::Disallow(String message)
    {
        return {EGraphConnectionAction::kDisallow, std::move(message), ""};
    }

    GraphConnectionResponse IGraphSchema::CanConnect(const GraphDocument &document, const GraphPinData &source,
                                                     const GraphPinData &target) const
    {
        if (source._id == Guid::EmptyGuid() || target._id == Guid::EmptyGuid() || source._id == target._id)
            return GraphConnectionResponse::Disallow("Pin cannot connect to itself.");
        if (source._direction == target._direction)
            return GraphConnectionResponse::Disallow("Pins must have opposite directions.");
        if (source._kind != target._kind)
            return GraphConnectionResponse::Disallow("Execution pins and value pins cannot be mixed.");
        if (source._kind == EGraphPinKind::kValue && source._value_type != target._value_type)
            return GraphConnectionResponse::Disallow("Value pin types are not compatible.");

        const GraphPinData &input_pin = source._direction == EGraphPinDirection::kInput ? source : target;
        const auto links = document.FindLinksForPin(input_pin._id);
        return links.empty() ? GraphConnectionResponse::Allow() : GraphConnectionResponse::ReplaceInput();
    }

    void IGraphSchema::CollectNodeActions(const GraphDocument &document, const GraphPinData *source_pin,
                                          Vector<GraphNodeAction> &actions) const
    {
        (void) document;
        const Vector<const GraphNodeDesc *> node_descs = GraphNodeRegistry::Get().FindNodes("");
        for (const GraphNodeDesc *desc : node_descs)
        {
            if (!CanCreateNode(document, desc->_type_id))
                continue;
            if (source_pin != nullptr)
            {
                bool has_compatible_pin = false;
                for (const GraphPinDesc &pin : desc->_pins)
                {
                    if (pin._direction == source_pin->_direction || pin._kind != source_pin->_kind)
                        continue;
                    if (pin._kind == EGraphPinKind::kExecution || pin._value_type == source_pin->_value_type)
                    {
                        has_compatible_pin = true;
                        break;
                    }
                }
                if (!has_compatible_pin)
                    continue;
            }
            actions.emplace_back(GraphNodeAction{desc->_type_id, desc->_display_name, desc->_category, desc->_tooltip});
        }
    }

    bool IGraphSchema::CanDeleteNode(const GraphDocument &document, const GraphNodeData &node) const
    {
        (void) document;
        return HasGraphFlag(node._flags, EGraphNodeFlag::kCanDelete) && !HasGraphFlag(node._flags, EGraphNodeFlag::kEntryNode);
    }

    bool IGraphSchema::CanCreateNode(const GraphDocument &document, StringView node_type) const
    {
        (void) document;
        return GraphNodeRegistry::Get().FindNode(node_type) != nullptr;
    }

    bool IGraphSchema::AllowsCycles() const
    {
        return false;
    }

    Scope<IGraphSchema> CreateGraphSchema(StringView schema_type)
    {
        if (schema_type == "FlowGraphSchema")
            return MakeScope<FlowGraphSchema>();
        if (schema_type == "AnimationControllerGraphSchema")
            return MakeScope<AnimationControllerGraphSchema>();
        return MakeScope<IGraphSchema>();
    }
} // namespace Ailu
