#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"

#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace Ailu::Editor
{
    struct ConsoleParseResult
    {
        bool _success = false;
        Vector<String> _tokens;
        String _error;
        u32 _error_position = 0u;
    };

    class ConsoleCommandParser
    {
    public:
        static constexpr u32 kMaxInputLength = 4096u;
        static constexpr u32 kMaxArguments = 64u;

        static ConsoleParseResult Parse(StringView line);
    };

    struct ConsoleCommandContext
    {
        bool _is_editor = true;
        bool _allow_mutation = false;
    };

    struct ConsoleCommandResult
    {
        bool _success = false;
        String _message;
        bool _clear_transcript = false;
        bool _was_noop = false;
    };

    enum class EConsoleArgumentType : u8
    {
        kString,
        kInteger,
        kFloat,
        kBoolean
    };

    struct ConsoleArgumentDesc
    {
        String _name;
        EConsoleArgumentType _type = EConsoleArgumentType::kString;
        bool _optional = false;
    };

    struct ConsoleCommandDesc
    {
        String _name;
        String _help;
        Vector<ConsoleArgumentDesc> _arguments;
        u32 _min_argument_count = 0u;
        u32 _max_argument_count = 0u;
        bool _is_variadic = false;
        bool _requires_main_thread = true;
        std::function<ConsoleCommandResult(const Vector<String> &, const ConsoleCommandContext &)> _execute;
    };

    class ConsoleCommandRegistry
    {
    public:
        ConsoleCommandRegistry();
        ~ConsoleCommandRegistry() = default;

        ConsoleCommandRegistry(const ConsoleCommandRegistry &) = delete;
        ConsoleCommandRegistry &operator=(const ConsoleCommandRegistry &) = delete;

        bool RegisterCommand(ConsoleCommandDesc command, String *error = nullptr);
        bool UnregisterCommand(StringView name, String *error = nullptr);
        ConsoleCommandResult Execute(StringView line, const ConsoleCommandContext &context = {});
        Vector<String> Complete(StringView prefix) const;
        Vector<ConsoleCommandDesc> GetCommands() const;

    private:
        static String NormalizeName(StringView name);
        static String FormatArgumentSchema(const ConsoleCommandDesc &command);
        static bool ValidateArgument(const ConsoleArgumentDesc &argument, StringView value);
        static String ArgumentTypeName(EConsoleArgumentType type);

        String BuildHelpText() const;
        ConsoleCommandResult MakeError(String message) const;
        void RegisterBuiltins();

        mutable std::mutex _mutex;
        std::map<String, ConsoleCommandDesc> _commands;
        std::thread::id _main_thread_id;
    };
}
