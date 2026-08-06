// Unit tests for the automation adapter layer: adapter resolution, Describe and
// ReadProperty through the registry.

#include "Automation/AutomationAdapter.h"

using namespace Ailu;

namespace Ailu::Editor::AutomationAdapterTests
{
    bool TestAdapterResolve()
    {
        AutomationAdapterRegistry::Get().Initialize();

        // Component descriptor-backed type resolves.
        if (AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.TransformComponent") == nullptr)
            return false;
        // Unknown type does not resolve.
        if (AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.NoSuchComponent") != nullptr)
            return false;
        return true;
    }

    bool TestAdapterDescribeComponent()
    {
        const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.TransformComponent");
        if (adapter == nullptr)
            return false;
        AutomationResult schema = adapter->Describe("Ailu.ECS.TransformComponent");
        if (!schema._success || !schema._data.IsObject())
            return false;
        const AutomationObject &object = schema._data.AsObject();
        if (object.at("kind").AsString() != "component")
            return false;
        if (object.at("properties").AsArray().empty())
            return false;

        bool found_position = false;
        for (const AutomationValue &entry : object.at("properties").AsArray())
        {
            if (entry.AsObject().at("path").AsString() == "_local_transform._position")
            {
                found_position = true;
                break;
            }
        }
        return found_position;
    }

    bool TestAdapterReadComponent()
    {
        const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.TransformComponent");
        if (adapter == nullptr)
            return false;

        ECS::TransformComponent transform;
        transform._local_transform._position = Vector3f(7.0f, 8.0f, 9.0f);

        AutomationResult value = adapter->ReadProperty("Ailu.ECS.TransformComponent", &transform, "_local_transform._position");
        if (!value._success || !value._data.IsArray() || value._data.AsArray().size() != 3u)
            return false;
        if (value._data.AsArray()[2].AsFloat() != 9.0)
            return false;

        // Unknown path fails with the fixed error code.
        AutomationResult missing = adapter->ReadProperty("Ailu.ECS.TransformComponent", &transform, "_no_such");
        if (missing._success || missing._error._code != AutomationErrors::kPropertyNotFound)
            return false;
        return true;
    }
}// namespace Ailu::Editor::AutomationAdapterTests
