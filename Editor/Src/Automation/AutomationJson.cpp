#include "Automation/AutomationJson.h"

#include <cctype>
#include <cstdio>
#include <format>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            void WriteStringEscaped(const String &value, String &out)
            {
                out += '"';
                for (const char c : value)
                {
                    switch (c)
                    {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        case '\b': out += "\\b"; break;
                        case '\f': out += "\\f"; break;
                        default: out += c; break;
                    }
                }
                out += '"';
            }

            void WriteValue(const AutomationValue &value, String &out)
            {
                if (value.IsNull())
                {
                    out += "null";
                    return;
                }
                if (value.IsBool())
                {
                    out += value.AsBool() ? "true" : "false";
                    return;
                }
                if (value.IsInt())
                {
                    if (std::holds_alternative<u64>(value.StorageRef()))
                        out += std::to_string(value.AsUInt());
                    else
                        out += std::to_string(value.AsInt());
                    return;
                }
                if (value.IsFloat())
                {
                    out += std::format("{}", value.AsFloat());
                    return;
                }
                if (value.IsString())
                {
                    WriteStringEscaped(value.AsString(), out);
                    return;
                }
                if (value.IsArray())
                {
                    out += '[';
                    bool first = true;
                    for (const AutomationValue &item : value.AsArray())
                    {
                        if (!first)
                            out += ',';
                        first = false;
                        WriteValue(item, out);
                    }
                    out += ']';
                    return;
                }
                if (value.IsObject())
                {
                    out += '{';
                    bool first = true;
                    for (const auto &[key, item] : value.AsObject())
                    {
                        if (!first)
                            out += ',';
                        first = false;
                        WriteStringEscaped(key, out);
                        out += ':';
                        WriteValue(item, out);
                    }
                    out += '}';
                    return;
                }
                out += "null";
            }

            struct JsonParser
            {
                StringView _text;
                size_t _pos = 0;

                bool Parse(AutomationValue &out)
                {
                    SkipWhitespace();
                    return ParseValue(out);
                }

                void SkipWhitespace()
                {
                    while (_pos < _text.size() && std::isspace(static_cast<unsigned char>(_text[_pos])))
                        ++_pos;
                }

                bool ParseValue(AutomationValue &out)
                {
                    SkipWhitespace();
                    if (_pos >= _text.size())
                        return false;
                    const char c = _text[_pos];
                    if (c == '{')
                        return ParseObject(out);
                    if (c == '[')
                        return ParseArray(out);
                    if (c == '"')
                        return ParseString(out);
                    if (c == 't' || c == 'f')
                        return ParseBool(out);
                    if (c == 'n')
                        return ParseNull(out);
                    return ParseNumber(out);
                }

                bool ParseString(AutomationValue &out)
                {
                    if (_pos >= _text.size() || _text[_pos] != '"')
                        return false;
                    ++_pos;
                    String result;
                    while (_pos < _text.size())
                    {
                        const char c = _text[_pos];
                        if (c == '"')
                        {
                            ++_pos;
                            out = AutomationValue(std::move(result));
                            return true;
                        }
                        if (c == '\\')
                        {
                            ++_pos;
                            if (_pos >= _text.size())
                                return false;
                            const char escaped = _text[_pos];
                            switch (escaped)
                            {
                                case '"': result += '"'; break;
                                case '\\': result += '\\'; break;
                                case '/': result += '/'; break;
                                case 'n': result += '\n'; break;
                                case 'r': result += '\r'; break;
                                case 't': result += '\t'; break;
                                case 'b': result += '\b'; break;
                                case 'f': result += '\f'; break;
                                case 'u':
                                    // Best effort: consume the 4 hex digits; raw UTF-8 is carried through.
                                    _pos += 4;
                                    break;
                                default: result += escaped; break;
                            }
                            ++_pos;
                        }
                        else
                        {
                            result += c;
                            ++_pos;
                        }
                    }
                    return false;
                }

                bool ParseNumber(AutomationValue &out)
                {
                    const size_t start = _pos;
                    if (_pos < _text.size() && _text[_pos] == '-')
                        ++_pos;
                    while (_pos < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_pos])))
                        ++_pos;
                    bool is_float = false;
                    if (_pos < _text.size() && _text[_pos] == '.')
                    {
                        is_float = true;
                        ++_pos;
                        while (_pos < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_pos])))
                            ++_pos;
                    }
                    if (_pos < _text.size() && (_text[_pos] == 'e' || _text[_pos] == 'E'))
                    {
                        is_float = true;
                        ++_pos;
                        if (_pos < _text.size() && (_text[_pos] == '+' || _text[_pos] == '-'))
                            ++_pos;
                        while (_pos < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_pos])))
                            ++_pos;
                    }
                    const StringView token = _text.substr(start, _pos - start);
                    if (token.empty())
                        return false;
                    try
                    {
                        if (is_float)
                        {
                            out = AutomationValue(std::stod(String(token)));
                        }
                        else if (token[0] == '-')
                        {
                            out = AutomationValue(std::stoll(String(token)));
                        }
                        else
                        {
                            out = AutomationValue(std::stoull(String(token)));
                        }
                    }
                    catch (...)
                    {
                        return false;
                    }
                    return true;
                }

                bool ParseBool(AutomationValue &out)
                {
                    if (_text.substr(_pos, 4) == "true")
                    {
                        _pos += 4;
                        out = AutomationValue(true);
                        return true;
                    }
                    if (_text.substr(_pos, 5) == "false")
                    {
                        _pos += 5;
                        out = AutomationValue(false);
                        return true;
                    }
                    return false;
                }

                bool ParseNull(AutomationValue &out)
                {
                    if (_text.substr(_pos, 4) == "null")
                    {
                        _pos += 4;
                        out = AutomationValue{};
                        return true;
                    }
                    return false;
                }

                bool ParseArray(AutomationValue &out)
                {
                    if (_pos >= _text.size() || _text[_pos] != '[')
                        return false;
                    ++_pos;
                    AutomationArray array;
                    SkipWhitespace();
                    if (_pos < _text.size() && _text[_pos] == ']')
                    {
                        ++_pos;
                        out = AutomationValue(std::move(array));
                        return true;
                    }
                    while (_pos < _text.size())
                    {
                        AutomationValue item;
                        if (!ParseValue(item))
                            return false;
                        array.emplace_back(std::move(item));
                        SkipWhitespace();
                        if (_pos < _text.size() && _text[_pos] == ',')
                        {
                            ++_pos;
                            continue;
                        }
                        if (_pos < _text.size() && _text[_pos] == ']')
                        {
                            ++_pos;
                            out = AutomationValue(std::move(array));
                            return true;
                        }
                        return false;
                    }
                    return false;
                }

                bool ParseObject(AutomationValue &out)
                {
                    if (_pos >= _text.size() || _text[_pos] != '{')
                        return false;
                    ++_pos;
                    AutomationObject object;
                    SkipWhitespace();
                    if (_pos < _text.size() && _text[_pos] == '}')
                    {
                        ++_pos;
                        out = AutomationValue(std::move(object));
                        return true;
                    }
                    while (_pos < _text.size())
                    {
                        SkipWhitespace();
                        AutomationValue key_value;
                        if (!ParseString(key_value))
                            return false;
                        const String key = key_value.AsString();
                        SkipWhitespace();
                        if (_pos >= _text.size() || _text[_pos] != ':')
                            return false;
                        ++_pos;
                        AutomationValue value;
                        if (!ParseValue(value))
                            return false;
                        object.emplace(key, std::move(value));
                        SkipWhitespace();
                        if (_pos < _text.size() && _text[_pos] == ',')
                        {
                            ++_pos;
                            continue;
                        }
                        if (_pos < _text.size() && _text[_pos] == '}')
                        {
                            ++_pos;
                            out = AutomationValue(std::move(object));
                            return true;
                        }
                        return false;
                    }
                    return false;
                }
            };
        }// namespace

        String AutomationJson::Write(const AutomationValue &value)
        {
            String out;
            WriteValue(value, out);
            return out;
        }

        bool AutomationJson::Read(StringView text, AutomationValue &out)
        {
            JsonParser parser{text};
            return parser.Parse(out);
        }

        String AutomationJson::WriteRequest(const AutomationRequest &request)
        {
            AutomationObject root;
            root.emplace("request_id", AutomationValue(request._request_id));
            root.emplace("method", AutomationValue(request._method));
            root.emplace("arguments", AutomationValue(request._arguments));
            AutomationObject context;
            context.emplace("caller", AutomationValue(request._context._caller));
            context.emplace("allow_write", AutomationValue(request._context._allow_write));
            context.emplace("allow_destructive", AutomationValue(request._context._allow_destructive));
            context.emplace("interactive", AutomationValue(request._context._interactive));
            root.emplace("context", AutomationValue(std::move(context)));
            return Write(AutomationValue(std::move(root)));
        }

        bool AutomationJson::ReadRequest(StringView text, AutomationRequest &out)
        {
            AutomationValue root;
            if (!Read(text, root) || !root.IsObject())
                return false;
            const AutomationObject &object = root.AsObject();
            const auto method_it = object.find("method");
            if (method_it == object.end())
                return false;
            out._method = method_it->second.AsString();
            const auto id_it = object.find("request_id");
            out._request_id = id_it != object.end() ? id_it->second.AsUInt() : 0u;
            const auto args_it = object.find("arguments");
            if (args_it != object.end() && args_it->second.IsObject())
                out._arguments = args_it->second.AsObject();
            const auto ctx_it = object.find("context");
            if (ctx_it != object.end() && ctx_it->second.IsObject())
            {
                const AutomationObject &context = ctx_it->second.AsObject();
                const auto caller = context.find("caller");
                if (caller != context.end())
                    out._context._caller = caller->second.AsString();
                const auto write = context.find("allow_write");
                if (write != context.end())
                    out._context._allow_write = write->second.AsBool();
                const auto destructive = context.find("allow_destructive");
                if (destructive != context.end())
                    out._context._allow_destructive = destructive->second.AsBool();
                const auto interactive = context.find("interactive");
                if (interactive != context.end())
                    out._context._interactive = interactive->second.AsBool();
            }
            return true;
        }

        String AutomationJson::WriteResult(u64 request_id, const AutomationResult &result)
        {
            AutomationObject root;
            root.emplace("request_id", AutomationValue(request_id));
            root.emplace("success", AutomationValue(result._success));
            if (result._success)
            {
                root.emplace("data", result._data);
            }
            else
            {
                AutomationObject error;
                error.emplace("code", AutomationValue(result._error._code));
                error.emplace("message", AutomationValue(result._error._message));
                if (!result._error._details.empty())
                    error.emplace("details", AutomationValue(result._error._details));
                root.emplace("error", AutomationValue(std::move(error)));
            }
            return Write(AutomationValue(std::move(root)));
        }

        bool AutomationJson::ReadResult(StringView text, u64 &request_id, AutomationResult &out)
        {
            AutomationValue root;
            if (!Read(text, root) || !root.IsObject())
                return false;
            const AutomationObject &object = root.AsObject();
            const auto id_it = object.find("request_id");
            request_id = id_it != object.end() ? id_it->second.AsUInt() : 0u;
            const auto success_it = object.find("success");
            out._success = success_it != object.end() && success_it->second.AsBool();
            if (out._success)
            {
                const auto data_it = object.find("data");
                if (data_it != object.end())
                    out._data = data_it->second;
            }
            else
            {
                const auto error_it = object.find("error");
                if (error_it != object.end() && error_it->second.IsObject())
                {
                    const AutomationObject &error = error_it->second.AsObject();
                    const auto code = error.find("code");
                    if (code != error.end())
                        out._error._code = code->second.AsString();
                    const auto message = error.find("message");
                    if (message != error.end())
                        out._error._message = message->second.AsString();
                    const auto details = error.find("details");
                    if (details != error.end() && details->second.IsObject())
                        out._error._details = details->second.AsObject();
                }
            }
            return true;
        }
    }// namespace Editor
}// namespace Ailu
