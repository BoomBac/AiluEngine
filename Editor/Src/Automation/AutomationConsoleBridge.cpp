#include "Automation/AutomationConsoleBridge.h"

#include "Automation/AutomationJson.h"
#include "Automation/AutomationRegistry.h"
#include "Common/ConsoleCommandRegistry.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            const char *PermissionToString(EAutomationPermission permission)
            {
                switch (permission)
                {
                    case EAutomationPermission::kReadOnly: return "read_only";
                    case EAutomationPermission::kSafeWrite: return "safe_write";
                    case EAutomationPermission::kDestructiveWrite: return "destructive_write";
                    case EAutomationPermission::kExternalEffect: return "external_effect";
                }
                return "read_only";
            }

            const char *ValueTypeToString(EAutomationValueType type)
            {
                switch (type)
                {
                    case EAutomationValueType::kBool: return "bool";
                    case EAutomationValueType::kInteger: return "integer";
                    case EAutomationValueType::kFloat: return "float";
                    case EAutomationValueType::kGuid: return "guid";
                    case EAutomationValueType::kEnum: return "enum";
                    case EAutomationValueType::kVector: return "vector";
                    case EAutomationValueType::kQuaternion: return "quaternion";
                    case EAutomationValueType::kColor: return "color";
                    case EAutomationValueType::kArray: return "array";
                    case EAutomationValueType::kObject: return "object";
                    case EAutomationValueType::kEntityReference: return "entity_ref";
                    case EAutomationValueType::kAssetReference: return "asset_ref";
                    case EAutomationValueType::kObjectReference: return "object_ref";
                    default: return "string";
                }
            }

            String FormatSchema(const AutomationSchema &schema)
            {
                String result;
                for (const AutomationParamDesc &param : schema._params)
                {
                    if (!result.empty())
                        result += ' ';
                    result += param._required ? '<' : '[';
                    result += param._name;
                    result += ':';
                    result += ValueTypeToString(param._type);
                    if (param._is_array)
                        result += "[]";
                    result += param._required ? '>' : ']';
                }
                return result;
            }

            bool ValidateValueShape(const AutomationParamDesc &param, const AutomationValue &value, String &error)
            {
                if (param._is_array && !value.IsArray())
                {
                    error = std::format("argument '{}' must be a JSON array", param._name);
                    return false;
                }
                if (param._is_array)
                    return true;

                switch (param._type)
                {
                    case EAutomationValueType::kBool:
                        if (!value.IsBool()) error = std::format("argument '{}' must be boolean", param._name);
                        break;
                    case EAutomationValueType::kInteger:
                        if (!value.IsInt()) error = std::format("argument '{}' must be integer", param._name);
                        break;
                    case EAutomationValueType::kFloat:
                        if (!value.IsNumber()) error = std::format("argument '{}' must be number", param._name);
                        break;
                    case EAutomationValueType::kObject:
                    case EAutomationValueType::kEntityReference:
                    case EAutomationValueType::kAssetReference:
                    case EAutomationValueType::kObjectReference:
                        if (!value.IsObject()) error = std::format("argument '{}' must be a JSON object", param._name);
                        break;
                    case EAutomationValueType::kArray:
                    case EAutomationValueType::kVector:
                    case EAutomationValueType::kQuaternion:
                    case EAutomationValueType::kColor:
                        if (!value.IsArray()) error = std::format("argument '{}' must be a JSON array", param._name);
                        break;
                    case EAutomationValueType::kGuid:
                    case EAutomationValueType::kEnum:
                    case EAutomationValueType::kString:
                        if (!value.IsString()) error = std::format("argument '{}' must be a string", param._name);
                        break;
                    default:
                        break;
                }
                return error.empty();
            }

            bool ParseValue(const AutomationParamDesc &param, StringView text, AutomationValue &value, String &error)
            {
                if (!param._is_array &&
                    (param._type == EAutomationValueType::kString || param._type == EAutomationValueType::kGuid ||
                     param._type == EAutomationValueType::kEnum))
                {
                    value = AutomationValue(String(text));
                    return true;
                }
                if (!AutomationJson::Read(text, value))
                {
                    error = std::format("argument '{}' expects {} JSON", param._name, ValueTypeToString(param._type));
                    return false;
                }
                return ValidateValueShape(param, value, error);
            }

            bool BuildArguments(const AutomationMethodDesc &method, const Vector<String> &tokens,
                                AutomationObject &arguments, String &error)
            {
                if (tokens.size() == 1u && !tokens[0].empty() && tokens[0].front() == '{')
                {
                    AutomationValue object_value;
                    if (!AutomationJson::Read(tokens[0], object_value) || !object_value.IsObject())
                    {
                        error = "single automation argument must be a JSON object";
                        return false;
                    }
                    arguments = object_value.AsObject();
                }
                else
                {
                    for (const String &token : tokens)
                    {
                        const size_t separator = token.find('=');
                        if (separator == String::npos || separator == 0u)
                        {
                            error = "automation arguments must use name=value or one JSON object";
                            return false;
                        }
                        const String name = token.substr(0u, separator);
                        const AutomationParamDesc *param = method._input_schema.Find(name);
                        if (param == nullptr)
                        {
                            error = std::format("unknown argument '{}'", name);
                            return false;
                        }
                        if (arguments.contains(name))
                        {
                            error = std::format("argument '{}' was specified more than once", name);
                            return false;
                        }
                        AutomationValue value;
                        if (!ParseValue(*param, StringView(token).substr(separator + 1u), value, error))
                            return false;
                        arguments.emplace(name, std::move(value));
                    }
                }

                for (const auto &[name, value] : arguments)
                {
                    const AutomationParamDesc *param = method._input_schema.Find(name);
                    if (param == nullptr)
                    {
                        error = std::format("unknown argument '{}'", name);
                        return false;
                    }
                    if (!ValidateValueShape(*param, value, error))
                        return false;
                }
                for (const AutomationParamDesc &param : method._input_schema._params)
                {
                    if (param._required && !arguments.contains(param._name))
                    {
                        error = std::format("missing required argument '{}'", param._name);
                        return false;
                    }
                }
                return true;
            }

            ConsoleCommandResult ExecuteAutomationCommand(EditorAutomationRegistry &registry,
                                                           const AutomationMethodDesc &method,
                                                           const Vector<String> &tokens)
            {
                AutomationObject arguments;
                String error;
                if (!BuildArguments(method, tokens, arguments, error))
                    return {false, std::move(error), false, false};

                AutomationRequestContext request;
                request._caller = "terminal";
                request._allow_write = true;
                request._allow_destructive = true;
                request._interactive = true;
                EditorAutomationContext context;
                context._request = &request;
                context._registry = &registry;
                const AutomationResult result = registry.Invoke(method._name, context, arguments);
                if (!result._success)
                    return {false, std::format("[{}] {}", result._error._code, result._error._message), false, false};
                return {true, AutomationJson::Write(result._data), false, false};
            }
        }// namespace

        u32 RegisterAutomationConsoleCommands(ConsoleCommandRegistry &console_registry,
                                              EditorAutomationRegistry &automation_registry)
        {
            u32 registered_count = 0u;
            automation_registry.ForEachMethod([&](const AutomationMethodDesc &method)
            {
                ConsoleCommandDesc command;
                command._name = method._name;
                command._help = std::format("{} [permission={}]{}; use name=value, complex values as JSON",
                                             method._description, PermissionToString(method._permission),
                                             FormatSchema(method._input_schema).empty()
                                                 ? String{}
                                                 : " args: " + FormatSchema(method._input_schema));
                command._max_argument_count = ConsoleCommandParser::kMaxArguments;
                command._is_variadic = true;
                command._requires_main_thread = true;
                command._execute = [&automation_registry, method](const Vector<String> &tokens,
                                                                   const ConsoleCommandContext &)
                {
                    return ExecuteAutomationCommand(automation_registry, method, tokens);
                };
                if (console_registry.RegisterCommand(std::move(command)))
                    ++registered_count;
            });
            return registered_count;
        }
    }// namespace Editor
}// namespace Ailu
