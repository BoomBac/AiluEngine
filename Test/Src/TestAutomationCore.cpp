// Unit tests for the editor automation core (AutomationValue / Registry / Service).
// Compiled into the Test target together with Editor/Src/Automation/*.cpp.

#include "Automation/AutomationRegistry.h"
#include "Automation/AutomationService.h"
#include "Automation/AutomationTypes.h"

using namespace Ailu;

namespace Ailu::Editor::AutomationCoreTests
{
    bool TestAutomationValuePrimitives()
    {
        if (!AutomationValue{}.IsNull())
            return false;
        if (!AutomationValue(true).IsBool() || AutomationValue(true).AsBool() != true)
            return false;
        if (!AutomationValue(42).IsInt() || AutomationValue(42).AsInt() != 42)
            return false;
        if (AutomationValue(42u).AsUInt() != 42u)
            return false;
        if (AutomationValue(1.5f).AsFloat() != 1.5f)
            return false;
        if (AutomationValue(2.5).AsFloat() != 2.5)
            return false;
        if (!AutomationValue("hello").IsString() || AutomationValue("hello").AsString() != "hello")
            return false;
        // Defaults for missing / mismatched types.
        if (AutomationValue("x").AsBool(true) != true)
            return false;
        if (!AutomationValue(7).AsString().empty())
            return false;
        if (AutomationValue(3.14).AsInt() != 3)
            return false;
        if (AutomationValue{}.AsFloat(9.0) != 9.0)
            return false;
        return true;
    }

    bool TestAutomationValueArrayObject()
    {
        AutomationValue array = AutomationValue::MakeArray();
        if (!array.IsArray())
            return false;
        array.Array().push_back(AutomationValue(1));
        array.Array().push_back(AutomationValue("two"));
        if (array.AsArray().size() != 2u)
            return false;
        if (array.AsArray()[0].AsInt() != 1)
            return false;
        if (array.AsArray()[1].AsString() != "two")
            return false;

        AutomationValue object = AutomationValue::MakeObject();
        object.SetMember("name", AutomationValue("Bob"));
        object.SetMember("hp", AutomationValue(100));
        if (!object.IsObject())
            return false;
        if (object.AsObject().at("hp").AsInt() != 100)
            return false;
        if (object.AsObject().at("name").AsString() != "Bob")
            return false;
        // Object/array variants are mutually exclusive storage slots.
        array.Object().emplace("x", AutomationValue(1));
        if (array.AsObject().at("x").AsInt() != 1)
            return false;
        return true;
    }

    bool TestAutomationValueEquality()
    {
        if (!(AutomationValue(1) == AutomationValue(1)))
            return false;
        if (!(AutomationValue(1) != AutomationValue(2)))
            return false;
        if (!(AutomationValue("a") == AutomationValue("a")))
            return false;
        if (!(AutomationValue("a") != AutomationValue("b")))
            return false;
        if (!(AutomationValue(true) != AutomationValue(false)))
            return false;
        if (!(AutomationValue(1) == AutomationValue(1.0)))
            return false;
        if (AutomationValue(1) == AutomationValue("1"))
            return false;
        // Array deep equality.
        AutomationValue left = AutomationValue::MakeArray();
        left.Array().push_back(AutomationValue(5));
        AutomationValue right = AutomationValue::MakeArray();
        right.Array().push_back(AutomationValue(5));
        if (!(left == right))
            return false;
        right.Array()[0] = AutomationValue(6);
        if (!(left != right))
            return false;
        return true;
    }

    bool TestAutomationRegistry()
    {
        EditorAutomationRegistry registry;
        AutomationMethodDesc desc;
        desc._name = "test.add";
        desc._description = "Add two integers";
        AutomationHandler handler = [](EditorAutomationContext &, const AutomationObject &args) -> AutomationResult
        {
            i64 a = 0;
            i64 b = 0;
            if (const auto it = args.find("a"); it != args.end())
                a = it->second.AsInt();
            if (const auto it = args.find("b"); it != args.end())
                b = it->second.AsInt();
            return AutomationResult::Ok(AutomationValue(a + b));
        };
        if (!registry.Register("test.add", std::move(desc), std::move(handler)))
            return false;
        if (!registry.Contains("test.add"))
            return false;
        if (registry.Find("test.add") == nullptr)
            return false;

        AutomationObject args;
        args.emplace("a", AutomationValue(3));
        args.emplace("b", AutomationValue(4));
        EditorAutomationContext context;
        AutomationResult result = registry.Invoke("test.add", context, args);
        if (!result._success || result._data.AsInt() != 7)
            return false;

        // Duplicate registration is rejected.
        AutomationMethodDesc duplicate;
        duplicate._name = "test.add";
        if (registry.Register("test.add", std::move(duplicate),
                              [](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                              { return AutomationResult::Ok(AutomationValue{}); }))
            return false;

        // Unknown method reports the fixed error code.
        AutomationResult missing = registry.Invoke("test.missing", context, args);
        if (missing._success)
            return false;
        if (missing._error._code != AutomationErrors::kActionNotFound)
            return false;
        return true;
    }

    bool TestAutomationServiceSubmitTick()
    {
        EditorAutomationService service;
        service.Initialize();
        if (!service.IsReady())
            return false;

        AutomationRequest request;
        request._request_id = 1u;
        request._method = "automation.ping";
        request._arguments.emplace("token", AutomationValue(1234));

        std::future<AutomationResult> future = service.Submit(std::move(request));
        if (!future.valid())
            return false;
        service.Tick();
        future.wait();
        AutomationResult result = future.get();
        if (!result._success)
            return false;
        if (result._data.AsObject().at("token").AsInt() != 1234)
            return false;
        if (result._data.AsObject().at("pong").AsBool() != true)
            return false;

        service.Finalize();
        return true;
    }
}// namespace Ailu::Editor::AutomationCoreTests
