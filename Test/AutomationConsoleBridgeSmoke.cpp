#include "Automation/AutomationConsoleBridge.h"
#include "Automation/AutomationJson.h"
#include "Automation/AutomationRegistry.h"
#include "Common/ConsoleCommandRegistry.h"

#include <iostream>

namespace
{
    int s_failures = 0;

    void Expect(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++s_failures;
        }
    }
}

int main()
{
    using namespace Ailu;
    using namespace Ailu::Editor;

    EditorAutomationRegistry automation_registry;
    AutomationMethodDesc echo_desc;
    echo_desc._name = "test.echo";
    echo_desc._description = "Echo a string and integer.";
    echo_desc._input_schema.AddParam("text", "Text", EAutomationValueType::kString, true);
    echo_desc._input_schema.AddParam("count", "Count", EAutomationValueType::kInteger);
    echo_desc._input_schema.AddParam("labels", "Labels", EAutomationValueType::kString, false, true);
    automation_registry.Register("test.echo", std::move(echo_desc),
                                  [](EditorAutomationContext &, const AutomationObject &arguments)
                                  {
                                      AutomationObject result;
                                      result.emplace("text", arguments.at("text"));
                                      result.emplace("count", arguments.contains("count")
                                                                  ? arguments.at("count")
                                                                  : AutomationValue(u32(1u)));
                                      if (arguments.contains("labels"))
                                          result.emplace("labels", arguments.at("labels"));
                                      return AutomationResult::Ok(AutomationValue(std::move(result)));
                                  });

    AutomationMethodDesc object_desc;
    object_desc._name = "test.object";
    object_desc._description = "Accept a JSON object.";
    object_desc._input_schema.AddParam("value", "Value", EAutomationValueType::kObject, true);
    automation_registry.Register("test.object", std::move(object_desc),
                                  [](EditorAutomationContext &, const AutomationObject &arguments)
                                  {
                                      return AutomationResult::Ok(arguments.at("value"));
                                  });

    ConsoleCommandRegistry console_registry;
    const u32 registered = RegisterAutomationConsoleCommands(console_registry, automation_registry);
    Expect(registered == 2u, "all automation methods are registered in the console");

    const ConsoleCommandResult echo = console_registry.Execute(R"(test.echo text="hello world" count=3 labels=[1,2])");
    Expect(echo._success && echo._message.find("hello world") != String::npos &&
               echo._message.find("3") != String::npos && echo._message.find("1") != String::npos,
           "named scalar automation arguments execute through the console");

    const ConsoleCommandResult object = console_registry.Execute(R"(test.object value="{\"name\":\"cube\"}")");
    Expect(object._success && object._message.find("cube") != String::npos,
           "quoted JSON object automation arguments execute through the console");

    const ConsoleCommandResult missing = console_registry.Execute("test.echo count=3");
    Expect(!missing._success && missing._message.find("text") != String::npos,
           "required automation arguments are validated");

    const ConsoleCommandResult help = console_registry.Execute("help");
    Expect(help._success && help._message.find("test.echo") != String::npos &&
               help._message.find("test.object") != String::npos,
           "automation methods are discoverable through help");

    if (s_failures == 0)
    {
        std::cout << "AutomationConsoleBridgeSmoke: PASS\n";
        return 0;
    }
    std::cerr << "AutomationConsoleBridgeSmoke: " << s_failures << " failure(s)\n";
    return 1;
}
