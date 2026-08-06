// Unit tests for the automation read model: value conversions, type name
// normalization and the component descriptor registry.

#include "Automation/AutomationReadModel.h"

using namespace Ailu;

namespace Ailu::Editor::AutomationReadModelTests
{
    bool TestTypeNameNormalization()
    {
        if (NormalizeTypeName("Ailu::Render::Material") != "Ailu.Render.Material")
            return false;
        if (DenormalizeTypeName("Ailu.Render.Material") != "Ailu::Render::Material")
            return false;
        if (NormalizeTypeName("Ailu.ECS.TransformComponent") != "Ailu.ECS.TransformComponent")
            return false;
        return true;
    }

    bool TestValueConversions()
    {
        if (ToAutomationValue(true).AsBool() != true)
            return false;
        if (ToAutomationValue(42).AsInt() != 42)
            return false;
        if (ToAutomationValue(3.5f).AsFloat() != 3.5f)
            return false;
        if (ToAutomationValue(String("hello")).AsString() != "hello")
            return false;

        const Vector3f vector(1.0f, 2.0f, 3.0f);
        const AutomationValue vector_value = ToAutomationValue(vector);
        if (!vector_value.IsArray() || vector_value.AsArray().size() != 3u)
            return false;
        if (vector_value.AsArray()[0].AsFloat() != 1.0 || vector_value.AsArray()[2].AsFloat() != 3.0)
            return false;

        const Color color(0.5f, 0.25f, 0.0f, 1.0f);
        const AutomationValue color_value = ToAutomationValue(color);
        if (!color_value.IsObject())
            return false;
        if (color_value.AsObject().at("type").AsString() != "color")
            return false;
        if (color_value.AsObject().at("value").AsArray().size() != 4u)
            return false;

        const Quaternion quaternion(1.0f, 0.0f, 0.0f, 0.0f);
        const AutomationValue quat_value = ToAutomationValue(quaternion);
        if (!quat_value.IsArray() || quat_value.AsArray()[0].AsFloat() != 1.0)
            return false;

        const Guid guid("01234567-89ab-cdef-0123-456789abcdef");
        if (ToAutomationValue(guid).AsString() != guid.ToString())
            return false;
        return true;
    }

    bool TestComponentStableName()
    {
        const ECS::ComponentTypeId id = ECS::TransformComponent::StaticComponentTypeId();
        if (ComponentStableTypeName(id) != "Ailu.ECS.TransformComponent")
            return false;
        return true;
    }

    bool TestComponentDescriptorRead()
    {
        RegisterDefaultComponentDescriptors();

        ECS::TransformComponent transform;
        transform._local_transform._position = Vector3f(4.0f, 5.0f, 6.0f);
        transform._local_transform._scale = Vector3f(2.0f, 2.0f, 2.0f);

        const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find("Ailu.ECS.TransformComponent");
        if (desc == nullptr)
            return false;
        if (desc->_fields.size() != 3u)
            return false;

        const AutomationValue position = ReadComponentField(&transform, "Ailu.ECS.TransformComponent", "_local_transform._position");
        if (!position.IsArray() || position.AsArray()[1].AsFloat() != 5.0)
            return false;

        // Unknown field yields Null.
        const AutomationValue missing = ReadComponentField(&transform, "Ailu.ECS.TransformComponent", "_no_such_field");
        if (!missing.IsNull())
            return false;
        return true;
    }
}// namespace Ailu::Editor::AutomationReadModelTests
