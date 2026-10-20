#include "Common/ConsoleCommandRegistry.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <format>
#include <map>
#include <sstream>

namespace Ailu::Editor
{
    namespace
    {
        bool IsAsciiSpace(char character)
        {
            return character == ' ' || character == '\t';
        }

        String LowercaseAscii(String value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool ParseBoolean(StringView value)
        {
            const String lowered = LowercaseAscii(String(value));
            return lowered == "true" || lowered == "false" || lowered == "1" || lowered == "0" ||
                   lowered == "yes" || lowered == "no";
        }

        bool ParseInteger(StringView value)
        {
            if (value.empty())
                return false;
            i64 parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size();
        }

        bool ParseFloat(StringView value)
        {
            if (value.empty())
                return false;
            String copy(value);
            char *end = nullptr;
            std::strtod(copy.c_str(), &end);
            return end == copy.c_str() + copy.size();
        }
    }

    ConsoleParseResult ConsoleCommandParser::Parse(StringView line)
    {
        ConsoleParseResult result;
        result._success = true;
        if (line.empty())
            return result;
        if (line.size() > kMaxInputLength)
        {
            result._success = false;
            result._error = std::format("input exceeds {} bytes", kMaxInputLength);
            result._error_position = kMaxInputLength;
            return result;
        }

        String token;
        bool in_quotes = false;
        bool token_started = false;
        for (u32 index = 0u; index < line.size(); ++index)
        {
            const char character = line[index];
            if (in_quotes)
            {
                if (character == '"')
                {
                    in_quotes = false;
                    token_started = true;
                }
                else if (character == '\\')
                {
                    if (index + 1u >= line.size() || (line[index + 1u] != '"' && line[index + 1u] != '\\'))
                    {
                        result._success = false;
                        result._error = "inside quotes, backslash must escape a quote or backslash";
                        result._error_position = index;
                        return result;
                    }
                    token.push_back(line[++index]);
                    token_started = true;
                }
                else
                {
                    token.push_back(character);
                    token_started = true;
                }
                continue;
            }

            if (character == '"')
            {
                in_quotes = true;
                token_started = true;
            }
            else if (character == '\\' && index + 1u < line.size() && line[index + 1u] == '"')
            {
                token.push_back(line[++index]);
                token_started = true;
            }
            else if (IsAsciiSpace(character))
            {
                if (token_started)
                {
                    if (result._tokens.size() >= kMaxArguments)
                    {
                        result._success = false;
                        result._error = std::format("too many arguments; maximum is {}", kMaxArguments);
                        result._error_position = index;
                        return result;
                    }
                    result._tokens.emplace_back(std::move(token));
                    token.clear();
                    token_started = false;
                }
            }
            else
            {
                token.push_back(character);
                token_started = true;
            }
        }

        if (in_quotes)
        {
            result._success = false;
            result._error = "unclosed double quote";
            result._error_position = static_cast<u32>(line.size());
            return result;
        }
        if (token_started)
        {
            if (result._tokens.size() >= kMaxArguments)
            {
                result._success = false;
                result._error = std::format("too many arguments; maximum is {}", kMaxArguments);
                result._error_position = static_cast<u32>(line.size());
                return result;
            }
            result._tokens.emplace_back(std::move(token));
        }
        return result;
    }

    ConsoleCommandRegistry::ConsoleCommandRegistry() : _main_thread_id(std::this_thread::get_id())
    {
        RegisterBuiltins();
    }

    String ConsoleCommandRegistry::NormalizeName(StringView name)
    {
        if (name.empty())
            return {};
        String normalized;
        normalized.reserve(name.size());
        for (const unsigned char character : name)
        {
            if (!(std::isalnum(character) || character == '_' || character == '-' || character == '.'))
                return {};
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
        return normalized;
    }

    bool ConsoleCommandRegistry::RegisterCommand(ConsoleCommandDesc command, String *error)
    {
        const String normalized_name = NormalizeName(command._name);
        if (normalized_name.empty())
        {
            if (error != nullptr)
                *error = "command name must contain only ASCII letters, digits, '.', '_' or '-'";
            return false;
        }
        if (!command._execute)
        {
            if (error != nullptr)
                *error = "command callback is empty";
            return false;
        }
        command._name = normalized_name;
        std::lock_guard lock(_mutex);
        if (_commands.contains(normalized_name))
        {
            if (error != nullptr)
                *error = std::format("command already registered: {}", normalized_name);
            return false;
        }
        _commands.emplace(normalized_name, std::move(command));
        return true;
    }

    bool ConsoleCommandRegistry::UnregisterCommand(StringView name, String *error)
    {
        const String normalized_name = NormalizeName(name);
        if (normalized_name.empty())
        {
            if (error != nullptr)
                *error = "invalid command name";
            return false;
        }
        std::lock_guard lock(_mutex);
        if (_commands.erase(normalized_name) == 0u)
        {
            if (error != nullptr)
                *error = std::format("command is not registered: {}", normalized_name);
            return false;
        }
        return true;
    }

    ConsoleCommandResult ConsoleCommandRegistry::Execute(StringView line, const ConsoleCommandContext &context)
    {
        const ConsoleParseResult parsed = ConsoleCommandParser::Parse(line);
        if (!parsed._success)
            return MakeError(std::format("Parse error at {}: {}", parsed._error_position, parsed._error));
        if (parsed._tokens.empty())
            return {true, {}, false, true};

        ConsoleCommandDesc command;
        const String command_name = NormalizeName(parsed._tokens.front());
        {
            std::lock_guard lock(_mutex);
            const auto command_it = _commands.find(command_name);
            if (command_it == _commands.end())
            {
                String message = std::format("Unknown command: {}", parsed._tokens.front());
                Vector<String> suggestions;
                for (const auto &[name, registered_command] : _commands)
                {
                    (void) registered_command;
                    if (name.starts_with(command_name) && suggestions.size() < 3u)
                        suggestions.push_back(name);
                }
                if (!suggestions.empty())
                {
                    message += ". Suggestions: ";
                    for (u32 index = 0u; index < suggestions.size(); ++index)
                    {
                        if (index != 0u)
                            message += ", ";
                        message += suggestions[index];
                    }
                }
                return MakeError(std::move(message));
            }
            command = command_it->second;
        }

        const u32 argument_count = static_cast<u32>(parsed._tokens.size() - 1u);
        u32 required_count = command._min_argument_count;
        for (const auto &argument : command._arguments)
            required_count += command._min_argument_count == 0u && !argument._optional ? 1u : 0u;
        const u32 max_count = command._max_argument_count != 0u
                                  ? command._max_argument_count
                                  : (command._is_variadic ? ConsoleCommandParser::kMaxArguments :
                                     static_cast<u32>(command._arguments.size()));
        if (argument_count < required_count || argument_count > max_count)
        {
            return MakeError(std::format("{} expects {}-{} argument(s), got {}", command._name, required_count, max_count,
                                         argument_count));
        }
        for (u32 index = 0u; index < argument_count; ++index)
        {
            if (command._arguments.empty())
                break;
            const u32 schema_index = command._is_variadic ? std::min<u32>(index, command._arguments.size() - 1u) : index;
            const auto &argument = command._arguments[schema_index];
            if (!ValidateArgument(argument, parsed._tokens[index + 1u]))
            {
                return MakeError(std::format("{} argument {} ({}) has invalid {} value: {}", command._name, index + 1u,
                                             argument._name, ArgumentTypeName(argument._type), parsed._tokens[index + 1u]));
            }
        }
        if (command._requires_main_thread && std::this_thread::get_id() != _main_thread_id)
            return MakeError(std::format("{} must run on the editor main thread", command._name));

        try
        {
            return command._execute(Vector<String>(parsed._tokens.begin() + 1, parsed._tokens.end()), context);
        }
        catch (const std::exception &exception)
        {
            return MakeError(std::format("{} failed: {}", command._name, exception.what()));
        }
        catch (...)
        {
            return MakeError(std::format("{} failed with an unknown exception", command._name));
        }
    }

    Vector<String> ConsoleCommandRegistry::Complete(StringView prefix) const
    {
        String command_prefix(prefix);
        const size_t separator = command_prefix.find_first_of(" \t");
        if (separator != String::npos)
            command_prefix.resize(separator);
        command_prefix = NormalizeName(command_prefix);
        Vector<String> result;
        std::lock_guard lock(_mutex);
        for (const auto &[name, command] : _commands)
        {
            (void) command;
            if (name.starts_with(command_prefix))
                result.push_back(name);
        }
        return result;
    }

    Vector<ConsoleCommandDesc> ConsoleCommandRegistry::GetCommands() const
    {
        Vector<ConsoleCommandDesc> result;
        std::lock_guard lock(_mutex);
        result.reserve(_commands.size());
        for (const auto &[name, command] : _commands)
        {
            (void) name;
            result.push_back(command);
        }
        return result;
    }

    String ConsoleCommandRegistry::FormatArgumentSchema(const ConsoleCommandDesc &command)
    {
        String schema;
        for (const auto &argument : command._arguments)
        {
            if (!schema.empty())
                schema += ' ';
            schema += argument._optional ? '[' : '<';
            schema += argument._name;
            schema += argument._optional ? ']' : '>';
        }
        if (command._is_variadic)
            schema += " ...";
        return schema;
    }

    String ConsoleCommandRegistry::ArgumentTypeName(EConsoleArgumentType type)
    {
        switch (type)
        {
            case EConsoleArgumentType::kInteger: return "integer";
            case EConsoleArgumentType::kFloat: return "float";
            case EConsoleArgumentType::kBoolean: return "boolean";
            default: return "string";
        }
    }

    bool ConsoleCommandRegistry::ValidateArgument(const ConsoleArgumentDesc &argument, StringView value)
    {
        switch (argument._type)
        {
            case EConsoleArgumentType::kInteger: return ParseInteger(value);
            case EConsoleArgumentType::kFloat: return ParseFloat(value);
            case EConsoleArgumentType::kBoolean: return ParseBoolean(value);
            default: return true;
        }
    }

    String ConsoleCommandRegistry::BuildHelpText() const
    {
        String result = "Commands:";
        for (const auto &command : GetCommands())
        {
            result += std::format("\n  {} {} - {}", command._name, FormatArgumentSchema(command), command._help);
        }
        return result;
    }

    ConsoleCommandResult ConsoleCommandRegistry::MakeError(String message) const
    {
        return {false, std::move(message), false, false};
    }

    void ConsoleCommandRegistry::RegisterBuiltins()
    {
        RegisterCommand({"help", "List registered engine commands", {}, 0u, 0u, false, false,
                         [this](const Vector<String> &, const ConsoleCommandContext &) {
                             return ConsoleCommandResult{true, BuildHelpText(), false, false};
                         }});
        RegisterCommand({"echo", "Print arguments separated by spaces", {{"text", EConsoleArgumentType::kString, false}},
                         1u, ConsoleCommandParser::kMaxArguments, true, false,
                         [](const Vector<String> &arguments, const ConsoleCommandContext &) {
                             String message;
                             for (const auto &argument : arguments)
                             {
                                 if (!message.empty())
                                     message += ' ';
                                 message += argument;
                             }
                             return ConsoleCommandResult{true, std::move(message), false, false};
                         }});
        RegisterCommand({"clear", "Clear the local Terminal transcript", {}, 0u, 0u, false, false,
                         [](const Vector<String> &, const ConsoleCommandContext &) {
                             return ConsoleCommandResult{true, {}, true, false};
                         }});
    }
}
