#include "Graph/Flow/FlowGraphNodes.h"

namespace Ailu
{
    namespace
    {
        void RegisterNodeOnce(GraphNodeDesc desc)
        {
            GraphNodeRegistry::Get().RegisterNode(std::move(desc));
        }
    } // namespace

    void RegisterFlowGraphNodes()
    {
        GraphNodeDesc entry;
        entry._type_id = "Flow.Entry";
        entry._display_name = "Entry";
        entry._category = "Flow";
        entry._tooltip = "Graph execution entry point.";
        entry._title_color = {0.8f, 0.15f, 0.1f, 1.0f};
        entry._flags = GraphFlag(EGraphNodeFlag::kEntryNode);
        entry._pins.push_back({"Then", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        RegisterNodeOnce(std::move(entry));

        GraphNodeDesc branch;
        branch._type_id = "Flow.Branch";
        branch._display_name = "Branch";
        branch._category = "Flow";
        branch._tooltip = "Chooses an execution path based on a boolean condition.";
        branch._title_color = {0.2f, 0.55f, 0.25f, 1.0f};
        branch._pins.push_back({"Exec", "", EGraphPinDirection::kInput, EGraphPinKind::kExecution, ""});
        branch._pins.push_back({"Condition", "bool", EGraphPinDirection::kInput, EGraphPinKind::kValue, "false"});
        branch._pins.push_back({"True", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        branch._pins.push_back({"False", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        RegisterNodeOnce(std::move(branch));

        GraphNodeDesc sequence;
        sequence._type_id = "Flow.Sequence";
        sequence._display_name = "Sequence";
        sequence._category = "Flow";
        sequence._tooltip = "Runs multiple execution outputs in order.";
        sequence._title_color = {0.22f, 0.42f, 0.7f, 1.0f};
        sequence._pins.push_back({"Exec", "", EGraphPinDirection::kInput, EGraphPinKind::kExecution, ""});
        sequence._pins.push_back({"Then 0", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        sequence._pins.push_back({"Then 1", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        RegisterNodeOnce(std::move(sequence));

        GraphNodeDesc print;
        print._type_id = "Flow.Print";
        print._display_name = "Print";
        print._category = "Flow";
        print._tooltip = "Prints a string value.";
        print._title_color = {0.24f, 0.48f, 0.48f, 1.0f};
        print._pins.push_back({"Exec", "", EGraphPinDirection::kInput, EGraphPinKind::kExecution, ""});
        print._pins.push_back({"Value", "string", EGraphPinDirection::kInput, EGraphPinKind::kValue, ""});
        print._pins.push_back({"Then", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        RegisterNodeOnce(std::move(print));

        GraphNodeDesc reroute;
        reroute._type_id = "Flow.Reroute";
        reroute._display_name = "Reroute";
        reroute._category = "Flow";
        reroute._tooltip = "Reroutes an execution wire for cleaner layout.";
        reroute._title_color = {0.36f, 0.36f, 0.42f, 1.0f};
        reroute._default_size = {18.0f, 18.0f};
        reroute._pins.push_back({"In", "", EGraphPinDirection::kInput, EGraphPinKind::kExecution, ""});
        reroute._pins.push_back({"Out", "", EGraphPinDirection::kOutput, EGraphPinKind::kExecution, ""});
        RegisterNodeOnce(std::move(reroute));

        GraphNodeDesc literal_bool;
        literal_bool._type_id = "Literal.Bool";
        literal_bool._display_name = "Bool";
        literal_bool._category = "Value";
        literal_bool._tooltip = "Boolean literal value.";
        literal_bool._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        literal_bool._pins.push_back({"Value", "bool", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "false"});
        RegisterNodeOnce(std::move(literal_bool));

        GraphNodeDesc literal_int;
        literal_int._type_id = "Literal.Int";
        literal_int._display_name = "Int";
        literal_int._category = "Value";
        literal_int._tooltip = "Integer literal value.";
        literal_int._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        literal_int._pins.push_back({"Value", "int", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "0"});
        RegisterNodeOnce(std::move(literal_int));

        GraphNodeDesc literal_float;
        literal_float._type_id = "Literal.Float";
        literal_float._display_name = "Float";
        literal_float._category = "Value";
        literal_float._tooltip = "Float literal value.";
        literal_float._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        literal_float._pins.push_back({"Value", "float", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "0.0"});
        RegisterNodeOnce(std::move(literal_float));

        GraphNodeDesc literal_string;
        literal_string._type_id = "Literal.String";
        literal_string._display_name = "String";
        literal_string._category = "Value";
        literal_string._tooltip = "String literal value.";
        literal_string._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        literal_string._pins.push_back({"Value", "string", EGraphPinDirection::kOutput, EGraphPinKind::kValue, ""});
        RegisterNodeOnce(std::move(literal_string));

        GraphNodeDesc add_float;
        add_float._type_id = "Math.AddFloat";
        add_float._display_name = "Add Float";
        add_float._category = "Math";
        add_float._tooltip = "Adds two float values.";
        add_float._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        add_float._pins.push_back({"A", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        add_float._pins.push_back({"B", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        add_float._pins.push_back({"Result", "float", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "0.0"});
        RegisterNodeOnce(std::move(add_float));

        GraphNodeDesc multiply_float;
        multiply_float._type_id = "Math.MultiplyFloat";
        multiply_float._display_name = "Multiply Float";
        multiply_float._category = "Math";
        multiply_float._tooltip = "Multiplies two float values.";
        multiply_float._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        multiply_float._pins.push_back({"A", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        multiply_float._pins.push_back({"B", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        multiply_float._pins.push_back({"Result", "float", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "0.0"});
        RegisterNodeOnce(std::move(multiply_float));

        GraphNodeDesc greater_float;
        greater_float._type_id = "Math.GreaterFloat";
        greater_float._display_name = "Greater Float";
        greater_float._category = "Math";
        greater_float._tooltip = "Compares two float values.";
        greater_float._flags |= GraphFlag(EGraphNodeFlag::kPureNode);
        greater_float._pins.push_back({"A", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        greater_float._pins.push_back({"B", "float", EGraphPinDirection::kInput, EGraphPinKind::kValue, "0.0"});
        greater_float._pins.push_back({"Result", "bool", EGraphPinDirection::kOutput, EGraphPinKind::kValue, "false"});
        RegisterNodeOnce(std::move(greater_float));
    }
} // namespace Ailu
