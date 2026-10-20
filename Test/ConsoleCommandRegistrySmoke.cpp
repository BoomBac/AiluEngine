#include "Common/ConsoleCommandRegistry.h"
#include "Common/TerminalTranscript.h"

#include <format>
#include <iostream>
#include <string>

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

    const ConsoleParseResult quoted = ConsoleCommandParser::Parse(R"(echo "a\"b\\c")");
    Expect(quoted._success && quoted._tokens.size() == 2u, "quoted input produces two tokens");
    Expect(quoted._tokens.size() == 2u && quoted._tokens[1] == "a\"b\\c", "quoted escapes are decoded");

    const ConsoleParseResult windows_path = ConsoleCommandParser::Parse(R"(echo C:\temp\scene.almap)");
    Expect(windows_path._success && windows_path._tokens[1] == R"(C:\temp\scene.almap)",
           "backslashes outside quotes are preserved");
    const ConsoleParseResult invalid_escape = ConsoleCommandParser::Parse(R"(echo "bad\q")");
    Expect(!invalid_escape._success && invalid_escape._error_position == 9u, "invalid escape reports its position");
    const ConsoleParseResult unclosed = ConsoleCommandParser::Parse(R"(echo "unclosed)");
    Expect(!unclosed._success && unclosed._error.find("unclosed") != String::npos, "unclosed quote is rejected");

    const ConsoleParseResult too_long = ConsoleCommandParser::Parse(String(ConsoleCommandParser::kMaxInputLength + 1u, 'x'));
    Expect(!too_long._success, "overlong input is rejected");

    ConsoleCommandRegistry registry;
    const ConsoleCommandResult echo = registry.Execute(R"(echo "hello world")");
    Expect(echo._success && echo._message == "hello world", "echo preserves quoted spaces");
    const ConsoleCommandResult help = registry.Execute("help");
    Expect(help._success && help._message.find("echo") != String::npos && help._message.find("clear") != String::npos,
           "help lists builtins deterministically");
    const ConsoleCommandResult clear = registry.Execute("clear");
    Expect(clear._success && clear._clear_transcript, "clear requests local transcript clearing");
    const ConsoleCommandResult unknown = registry.Execute("ech");
    Expect(!unknown._success && unknown._message.find("echo") != String::npos, "unknown command suggests a prefix match");

    String error;
    const bool first_registration = registry.RegisterCommand({
        "typed", "Validate typed arguments", {{"count", EConsoleArgumentType::kInteger, false},
                                                {"enabled", EConsoleArgumentType::kBoolean, false}}, 0u, 0u, false, true,
        [](const Vector<String> &arguments, const ConsoleCommandContext &) {
            return ConsoleCommandResult{true, arguments[0] + ":" + arguments[1], false, false};
        }}, &error);
    Expect(first_registration, "typed command registers");
    Expect(!registry.RegisterCommand({"typed", "duplicate", {}, 0u, 0u, false, true,
                                      [](const Vector<String> &, const ConsoleCommandContext &) {
                                          return ConsoleCommandResult{true, {}, false, false};
                                      }}, &error), "duplicate registration is rejected");
    Expect(registry.Execute("typed 42 true")._success, "typed command accepts valid values");
    Expect(!registry.Execute("typed not-a-number true")._success, "typed command rejects invalid integer");
    Expect(registry.UnregisterCommand("typed", &error), "registered command unregisters");
    Expect(!registry.Execute("typed 42 true")._success, "unregistered command cannot execute");

    const Vector<String> completions = registry.Complete("e");
    Expect(completions.size() == 1u && completions[0] == "echo", "completion is sorted and bounded by registry names");

    TerminalTranscript transcript;
    for (u32 index = 0u; index < 20000u; ++index)
        transcript.Append(ETerminalLineChannel::kOutput, std::format("line {}", index));
    Expect(transcript.Lines().size() <= TerminalTranscript::kMaxLines, "transcript line count is bounded");
    Expect(transcript.Bytes() <= TerminalTranscript::kMaxBytes, "transcript bytes are bounded");
    Expect(!transcript.Lines().empty() && transcript.Lines().back()._text == "line 19999",
           "transcript retains the newest line");
    transcript.Clear();
    Expect(transcript.Lines().empty() && transcript.Bytes() == 0u, "transcript clear removes all lines");

    if (s_failures == 0)
    {
        std::cout << "ConsoleCommandRegistrySmoke: PASS\n";
        return 0;
    }
    std::cerr << "ConsoleCommandRegistrySmoke: " << s_failures << " failure(s)\n";
    return 1;
}
