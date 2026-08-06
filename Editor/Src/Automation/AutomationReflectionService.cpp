#include "Automation/AutomationReflectionService.h"

#include "Automation/AutomationAdapter.h"

namespace Ailu
{
    namespace Editor
    {
        void AutomationReflectionService::Register(EditorAutomationRegistry &registry)
        {
            AutomationMethodDesc desc;
            desc._name = "reflection.get_type_schema";
            desc._description = "Return the property schema for a component type or reflected object type.";
            desc._permission = EAutomationPermission::kReadOnly;
            desc._input_schema.AddParam("type", "Stable type name (e.g. Ailu.ECS.TransformComponent).", EAutomationValueType::kString, true);
            registry.Register("reflection.get_type_schema", std::move(desc), HandleGetTypeSchema);
        }

        AutomationResult AutomationReflectionService::HandleGetTypeSchema(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const String type_name = arguments.contains("type") ? arguments.at("type").AsString() : String{};
            if (type_name.empty())
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "type is required");

            const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(type_name);
            if (adapter == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown type");
            return adapter->Describe(type_name);
        }
    }// namespace Editor
}// namespace Ailu
