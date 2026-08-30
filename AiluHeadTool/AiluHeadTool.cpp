//// AiluHeadTool.cpp : Source file for your target.
////
#include "AiluHeadTool.h"
#include "Timer.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <optional>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_set>
#include <vector>


static Timer g_Timer;

static std::string ParentNamespace(const std::string &ns)
{
    const auto pos = ns.rfind("::");
    return pos == std::string::npos ? "" : ns.substr(0, pos);
}

static std::string ResolveReflectedTypeName(const std::string &type_name,
                                            const std::string &current_namespace,
                                            const std::unordered_map<std::string, std::set<std::string>> &type_namespace_map)
{
    if (type_name.find("::") != std::string::npos)
        return type_name;

    auto it = type_namespace_map.find(type_name);
    if (it == type_namespace_map.end())
        return type_name;

    const auto &candidates = it->second;
    const std::string current_full_name = current_namespace.empty() ? type_name : current_namespace + "::" + type_name;
    if (candidates.contains(current_full_name))
        return current_full_name;

    std::string ns = current_namespace;
    while (!ns.empty())
    {
        ns = ParentNamespace(ns);
        if (!ns.empty())
        {
            const std::string parent_full_name = ns + "::" + type_name;
            if (candidates.contains(parent_full_name))
                return parent_full_name;
        }
    }

    if (candidates.size() == 1)
        return *candidates.begin();

    return type_name;
}

void AiluHeadTool::SetFilteredBaseClasses(std::vector<std::string> filtered_base_classes)
{
    _filtered_base_classes = std::move(filtered_base_classes);
}

bool AiluHeadTool::IsFilteredBaseClass(const std::string &line) const
{
    for (const std::string &base_class : _filtered_base_classes)
    {
        if (line.find(base_class) != std::string::npos)
            return true;
    }
    return false;
}

static const std::regex kClassDeclarationRegex(
    "(class|struct)[ \\t]+"
    "([A-Za-z_][A-Za-z0-9_]*_API[ \\t]+)?"
    "([A-Za-z_][A-Za-z0-9_]*)([ \\t]*:[ \\t]*public[ \\t]+"
    "([A-Za-z_][A-Za-z0-9_:]*))?");
static thread_local std::smatch s_class_matches;

static void ParserClassOrStructInfo(const std::string& line,AiluHeadTool::ClassInfo& info,AiluHeadTool& aht)
{
    // Supports class/struct declarations with an optional export macro and public base type.
    if (!std::regex_search(line, s_class_matches, kClassDeclarationRegex))
    {
        aht.Log(std::format("ParserClassInfo failed with line: {}", line));
        return;
    }
    info._is_struct = s_class_matches[1].str() == "struct";
    info._export_id = s_class_matches[2].matched ? s_class_matches[2].str() : std::string{};
    if (!info._export_id.empty())
        info._export_id.resize(info._export_id.find_last_not_of(" \t") + 1u);
    info._name = s_class_matches[3].str();
    info._parent = s_class_matches[5].matched ? s_class_matches[5].str() : std::string{};

    // These are implementation-only bases, not reflected engine types.
    if (aht.IsFilteredBaseClass(line))
        info._parent.clear();

    info.is_export = !info._export_id.empty();
}

static void ParserScriptTypeAnnotation(const std::string &line, bool &is_script_api,
                                       std::string *script_global_name = nullptr)
{
    size_t macro_begin = line.find("ASTRUCT(");
    if (macro_begin == std::string::npos)
        macro_begin = line.find("ACLASS(");
    if (macro_begin == std::string::npos)
        return;

    const size_t content_begin = line.find('(', macro_begin);
    const size_t content_search_begin = content_begin == std::string::npos ? macro_begin : content_begin + 1u;
    const size_t content_end = line.find(')', content_search_begin);
    if (content_begin == std::string::npos || content_end == std::string::npos)
        return;

    std::string content = line.substr(content_begin + 1u, content_end - content_begin - 1u);
    content = std::regex_replace(content, std::regex("\\s+"), "");
    size_t token_begin = 0u;
    while (token_begin <= content.size())
    {
        const size_t token_end = content.find_first_of(",;", token_begin);
        const size_t token_length = token_end == std::string::npos ? std::string::npos : token_end - token_begin;
        const std::string token = content.substr(token_begin, token_length);
        if (token == "Script")
            is_script_api = true;
        else if (token.rfind("Global=", 0u) == 0u && script_global_name != nullptr)
        {
            std::string global_name = token.substr(7u);
            if (global_name.size() >= 2u &&
                ((global_name.front() == '"' && global_name.back() == '"') ||
                 (global_name.front() == '\'' && global_name.back() == '\'')))
                global_name = global_name.substr(1u, global_name.size() - 2u);
            *script_global_name = std::move(global_name);
        }
        if (token_end == std::string::npos)
            break;
        token_begin = token_end + 1u;
    }
}

static void ParserScriptFunctionAnnotation(const std::string &line, bool &is_script, bool &is_script_property)
{
    const size_t macro_begin = line.find("AFUNCTION(");
    if (macro_begin == std::string::npos)
        return;

    const size_t content_begin = line.find('(', macro_begin);
    const size_t content_search_begin = content_begin == std::string::npos ? macro_begin : content_begin + 1u;
    const size_t content_end = line.find(')', content_search_begin);
    if (content_begin == std::string::npos || content_end == std::string::npos)
        return;

    std::string content = line.substr(content_begin + 1u, content_end - content_begin - 1u);
    content = std::regex_replace(content, std::regex("\\s+"), "");
    size_t token_begin = 0u;
    while (token_begin <= content.size())
    {
        const size_t token_end = content.find_first_of(",;", token_begin);
        const size_t token_length = token_end == std::string::npos ? std::string::npos : token_end - token_begin;
        const std::string token = content.substr(token_begin, token_length);
        if (token == "Script" || token == "ScriptProperty")
            is_script = true;
        if (token == "ScriptProperty")
            is_script_property = true;
        if (token_end == std::string::npos)
            break;
        token_begin = token_end + 1u;
    }
}

static void ParserEnumAnnotation(const std::string &line, bool &is_script)
{
    const size_t macro_begin = line.find("AENUM(");
    if (macro_begin == std::string::npos)
        return;

    const size_t content_begin = line.find('(', macro_begin);
    const size_t content_end = line.find(')', content_begin == std::string::npos ? macro_begin : content_begin + 1u);
    if (content_begin == std::string::npos || content_end == std::string::npos)
        return;

    std::string content = line.substr(content_begin + 1u, content_end - content_begin - 1u);
    content = std::regex_replace(content, std::regex("\\s+"), "");
    is_script = content == "Script" || content.starts_with("Script,") || content.find(",Script,") != std::string::npos ||
                content.ends_with(",Script");
}

static bool ParseReflectedDeclarationName(const std::string &line, std::string &name)
{
    std::smatch matches;
    if (!std::regex_search(line, matches, kClassDeclarationRegex))
        return false;
    name = matches[3].str();
    return !name.empty();
}


static void ParserEnumClass(const std::string &line, AiluHeadTool::EnumInfo &info, AiluHeadTool &aht)
{
    // 捕获顺序：
    // [1] 可选的 "class "
    // [2] 枚举名
    // [3] 可选的底层类型
    std::regex pattern(R"(enum\s+(class\s+)?(\w+)(?:\s*:\s*(\w+))?)");
    std::smatch matches;

    if (std::regex_search(line, matches, pattern))
    {
        // 是 enum class 吗？
        info._is_enum_class = matches[1].matched;

        // 保存名称
        info._name = matches[2].str();

        // 保存底层类型
        if (matches[3].matched)
            info._underlying_type = matches[3].str();
        else
            info._underlying_type = "i32";// 默认底层类型

        // 保存声明方式
        if (info._is_enum_class)
            info._decl_type = "enum class";
        else
            info._decl_type = "enum";
    }
    else
    {
        aht.Log(std::format("ParserEnumClass failed with line: {}", line));
    }
}

static std::string Trim(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
    {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

static bool TryParseIncludeTarget(const std::string &line, std::string &target)
{
    static const std::regex kIncludeRegex(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"].*)");
    std::smatch match;
    if (!std::regex_match(line, match, kIncludeRegex))
    {
        return false;
    }
    target = match[2].str();
    return true;
}

static bool IsGeneratedIncludeTarget(std::string target)
{
    for (char &ch: target)
    {
        if (ch == '\\')
            ch = '/';
    }

    const std::string kGeneratedPrefix = "generated/";
    const std::string kGeneratedSuffix = ".gen.h";
    return target.rfind(kGeneratedPrefix, 0) == 0 && target.size() >= kGeneratedSuffix.size() &&
           target.compare(target.size() - kGeneratedSuffix.size(), kGeneratedSuffix.size(), kGeneratedSuffix) == 0;
}

class EnumExprParser
{
public:
    EnumExprParser(std::string_view expression, const std::unordered_map<std::string, uint32_t> &known_values)
        : _expression(expression), _known_values(known_values)
    {
    }

    std::optional<uint32_t> Parse()
    {
        auto value = ParseBitwiseOr();
        SkipWhitespace();
        if (!value.has_value() || _cursor != _expression.size())
        {
            return std::nullopt;
        }
        return static_cast<uint32_t>(*value);
    }

private:
    std::optional<uint64_t> ParsePrimary()
    {
        SkipWhitespace();
        if (_cursor >= _expression.size())
        {
            return std::nullopt;
        }

        if (_expression[_cursor] == '(')
        {
            ++_cursor;
            auto value = ParseBitwiseOr();
            SkipWhitespace();
            if (!value.has_value() || _cursor >= _expression.size() || _expression[_cursor] != ')')
            {
                return std::nullopt;
            }
            ++_cursor;
            return value;
        }

        if (std::isdigit(static_cast<unsigned char>(_expression[_cursor])))
        {
            return ParseNumber();
        }

        if (_expression[_cursor] == '_'
            || std::isalpha(static_cast<unsigned char>(_expression[_cursor])))
        {
            return ParseIdentifier();
        }

        return std::nullopt;
    }

    std::optional<uint64_t> ParseUnary()
    {
        SkipWhitespace();
        if (_cursor >= _expression.size())
        {
            return std::nullopt;
        }

        const char token = _expression[_cursor];
        if (token == '+' || token == '-' || token == '~')
        {
            ++_cursor;
            auto value = ParseUnary();
            if (!value.has_value())
            {
                return std::nullopt;
            }
            if (token == '+')
            {
                return value;
            }
            if (token == '-')
            {
                return static_cast<uint64_t>(-static_cast<int64_t>(*value));
            }
            return ~(*value);
        }
        return ParsePrimary();
    }

    std::optional<uint64_t> ParseAdditive()
    {
        auto lhs = ParseUnary();
        while (lhs.has_value())
        {
            SkipWhitespace();
            if (_cursor >= _expression.size() || (_expression[_cursor] != '+' && _expression[_cursor] != '-'))
            {
                break;
            }
            const char op = _expression[_cursor++];
            auto rhs = ParseUnary();
            if (!rhs.has_value())
            {
                return std::nullopt;
            }
            lhs = (op == '+') ? *lhs + *rhs : *lhs - *rhs;
        }
        return lhs;
    }

    std::optional<uint64_t> ParseShift()
    {
        auto lhs = ParseAdditive();
        while (lhs.has_value())
        {
            SkipWhitespace();
            if (_cursor + 1 >= _expression.size())
            {
                break;
            }

            std::string_view op = _expression.substr(_cursor, 2);
            if (op != "<<" && op != ">>")
            {
                break;
            }
            _cursor += 2;
            auto rhs = ParseAdditive();
            if (!rhs.has_value())
            {
                return std::nullopt;
            }
            lhs = (op == "<<") ? (*lhs << *rhs) : (*lhs >> *rhs);
        }
        return lhs;
    }

    std::optional<uint64_t> ParseBitwiseAnd()
    {
        auto lhs = ParseShift();
        while (lhs.has_value())
        {
            SkipWhitespace();
            if (_cursor >= _expression.size() || _expression[_cursor] != '&')
            {
                break;
            }
            if (_cursor + 1 < _expression.size() && _expression[_cursor + 1] == '&')
            {
                return std::nullopt;
            }
            ++_cursor;
            auto rhs = ParseShift();
            if (!rhs.has_value())
            {
                return std::nullopt;
            }
            lhs = *lhs & *rhs;
        }
        return lhs;
    }

    std::optional<uint64_t> ParseBitwiseXor()
    {
        auto lhs = ParseBitwiseAnd();
        while (lhs.has_value())
        {
            SkipWhitespace();
            if (_cursor >= _expression.size() || _expression[_cursor] != '^')
            {
                break;
            }
            ++_cursor;
            auto rhs = ParseBitwiseAnd();
            if (!rhs.has_value())
            {
                return std::nullopt;
            }
            lhs = *lhs ^ *rhs;
        }
        return lhs;
    }

    std::optional<uint64_t> ParseBitwiseOr()
    {
        auto lhs = ParseBitwiseXor();
        while (lhs.has_value())
        {
            SkipWhitespace();
            if (_cursor >= _expression.size() || _expression[_cursor] != '|')
            {
                break;
            }
            if (_cursor + 1 < _expression.size() && _expression[_cursor + 1] == '|')
            {
                return std::nullopt;
            }
            ++_cursor;
            auto rhs = ParseBitwiseXor();
            if (!rhs.has_value())
            {
                return std::nullopt;
            }
            lhs = *lhs | *rhs;
        }
        return lhs;
    }

    std::optional<uint64_t> ParseNumber()
    {
        const auto begin = _cursor;
        while (_cursor < _expression.size())
        {
            const char ch = _expression[_cursor];
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '\'' || ch == 'x' || ch == 'X'))
            {
                break;
            }
            ++_cursor;
        }

        std::string token = std::string(_expression.substr(begin, _cursor - begin));
        token.erase(std::remove(token.begin(), token.end(), '\''), token.end());
        while (!token.empty())
        {
            const char suffix = token.back();
            if (suffix == 'u' || suffix == 'U' || suffix == 'l' || suffix == 'L')
            {
                token.pop_back();
                continue;
            }
            break;
        }
        if (token.empty())
        {
            return std::nullopt;
        }

        try
        {
            size_t consumed = 0;
            const auto value = std::stoull(token, &consumed, 0);
            if (consumed != token.size())
            {
                return std::nullopt;
            }
            return value;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<uint64_t> ParseIdentifier()
    {
        const auto begin = _cursor;
        while (_cursor < _expression.size())
        {
            const char ch = _expression[_cursor];
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'))
            {
                break;
            }
            ++_cursor;
        }
        const std::string name = std::string(_expression.substr(begin, _cursor - begin));
        if (_known_values.contains(name))
        {
            return _known_values.at(name);
        }
        return std::nullopt;
    }

    void SkipWhitespace()
    {
        while (_cursor < _expression.size() && std::isspace(static_cast<unsigned char>(_expression[_cursor])))
        {
            ++_cursor;
        }
    }

private:
    std::string_view _expression;
    const std::unordered_map<std::string, uint32_t> &_known_values;
    size_t _cursor = 0;
};

static std::optional<uint32_t> EvaluateEnumValue(std::string_view expression, const std::unordered_map<std::string, uint32_t> &known_values)
{
    EnumExprParser parser(expression, known_values);
    return parser.Parse();
}

static void ParserEnumValues(const std::string &line, AiluHeadTool::EnumInfo &info, AiluHeadTool &aht)
{
    std::string line_no_comment = line;
    if (const auto pos = line_no_comment.find("//"); pos != std::string::npos)
    {
        line_no_comment = line_no_comment.substr(0, pos);
    }

    line_no_comment = Trim(line_no_comment);
    if (line_no_comment.empty())
    {
        return;
    }
    if (line_no_comment.back() == ',')
    {
        line_no_comment.pop_back();
        line_no_comment = Trim(line_no_comment);
    }
    if (line_no_comment.empty())
    {
        return;
    }

    static const std::regex pattern(R"(^([A-Za-z_]\w*)(?:\s*=\s*(.+))?$)");
    std::smatch matches;
    if (!std::regex_match(line_no_comment, matches, pattern))
    {
        aht.Log(std::format("ParserEnumValues failed with line: {}", line));
        return;
    }

    const std::string name = matches[1].str();
    uint32_t value = info._members.empty() ? 0u : (std::get<1>(info._members.back()) + 1u);

    if (matches[2].matched)
    {
        std::unordered_map<std::string, uint32_t> known_values;
        known_values.reserve(info._members.size());
        for (const auto &[member_name, member_value]: info._members)
        {
            known_values.emplace(member_name, member_value);
        }

        const std::string expression = Trim(matches[2].str());
        const auto evaluated = EvaluateEnumValue(expression, known_values);
        if (!evaluated.has_value())
        {
            aht.Log(std::format("ParserEnumValues failed to evaluate expression '{}' in line: {}", expression, line));
            return;
        }
        value = *evaluated;
    }

    info._members.emplace_back(name, value);
}

static void ParserPropertyInfo(const std::string &line, AiluHeadTool::MemberInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"((static\s+)?([\w:]+(?:<[^<>]*>)?)\s*([\*\&]*)\s+(\w+)\s*(=\s*[^;]+)?;)");
    std::smatch matches;
    if (std::regex_search(line, matches, pattern))
    {
        info._is_static = matches[1].matched;
        info._type = matches[2].str();
        std::string ptrref = matches[3].str();
        info._is_pointer = ptrref.find('*') != std::string::npos;
        info._is_reference = ptrref.find('&') != std::string::npos;
        info._name = matches[4].str();
        info._is_template = info._type.find('<') != std::string::npos;
        info._is_enum = info._type.size() > 1 && info._type[0] == 'E' && std::isupper(info._type[1]);
    }
    else
    {
        aht.Log(std::format("ParserPropertyInfo failed with line: {}", line));
    }
}


static std::vector<std::string> SplitParams(const std::string &params)
{
    std::vector<std::string> result;
    std::istringstream stream(params);
    std::string param;
    while (std::getline(stream, param, ','))
    {
        param = Trim(param);
        if (param.empty() || param == "void")
            continue;
        const size_t default_pos = param.find('=');
        if (default_pos != std::string::npos)
            param = Trim(param.substr(0u, default_pos));
        const size_t name_pos = param.find_last_of(" \t");
        result.push_back(name_pos == std::string::npos ? param : Trim(param.substr(0u, name_pos)));
    }
    return result;
}

static std::vector<std::string> SplitParamNames(const std::string &params)
{
    std::vector<std::string> result;
    std::istringstream stream(params);
    std::string param;
    while (std::getline(stream, param, ','))
    {
        param = Trim(param);
        if (param.empty() || param == "void")
            continue;
        const size_t default_pos = param.find('=');
        if (default_pos != std::string::npos)
            param = Trim(param.substr(0u, default_pos));
        const size_t name_pos = param.find_last_of(" \t");
        if (name_pos == std::string::npos)
            result.emplace_back();
        else
        {
            std::string name = Trim(param.substr(name_pos + 1u));
            name.erase(0u, name.find_first_not_of("*&"));
            result.emplace_back(std::move(name));
        }
    }
    return result;
}

static std::vector<std::string> SplitFunctionPointerParams(const std::string &params)
{
    std::vector<std::string> result;
    std::istringstream stream(params);
    std::string param;
    while (std::getline(stream, param, ','))
    {
        param = Trim(param);
        if (param.empty() || param == "void")
            continue;
        const size_t default_pos = param.find('=');
        if (default_pos != std::string::npos)
            param = Trim(param.substr(0u, default_pos));
        const size_t name_pos = param.find_last_of(" \t");
        if (name_pos == std::string::npos)
        {
            result.emplace_back(std::move(param));
            continue;
        }

        std::string type = Trim(param.substr(0u, name_pos));
        const std::string name = Trim(param.substr(name_pos + 1u));
        const size_t name_begin = name.find_first_not_of("*&");
        if (name_begin != 0u && name_begin != std::string::npos)
            type += " " + name.substr(0u, name_begin);
        result.emplace_back(std::move(type));
    }
    return result;
}

static void ParserFunctionInfo(const std::string &line, AiluHeadTool::MemberInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"((static\s+)?(virtual\s+)?([\w:]+(?:<[^<>]*>)?(?:\s*\*|\s*&|\s*::\s*\w+)*)\s+(\w+)\(([^)]*)\)\s*(const)?)");
    std::smatch matches;

    if (std::regex_search(line, matches, pattern))
    {
        info._is_static = matches[1].matched;
        info._is_virtual = matches[2].matched;
        if (line.find("final") != std::string::npos || line.find("override") != std::string::npos) { info._is_virtual = true; }
        info._return_type = matches[3].str();
        info._name = matches[4].str();
        info._params = SplitParams(matches[5].str());
        info._function_pointer_params = SplitFunctionPointerParams(matches[5].str());
        info._param_names = SplitParamNames(matches[5].str());
        info._is_const = matches[6].matched;
        info._is_function = true;
    }
    else { aht.Log(std::format("ParserFunctionInfo failed with line: {}", line)); }
}

static void Replace(std::string &str, const std::string &old_str, const std::string &new_str)
{
    size_t pos = str.find(old_str);
    while (pos != std::string::npos)
    {
        str.replace(pos, old_str.length(), new_str);
        pos = str.find(old_str, pos + new_str.length());
    }
}

static std::string ConstructFuncType(std::string return_type, const std::vector<std::string> &params, bool is_const)
{
    std::string func_type = return_type + "(";
    for (size_t i = 0; i < params.size(); ++i)
    {
        func_type += params[i];
        if (i != params.size() - 1) { func_type += ","; }
    }
    func_type += ")";
    if (is_const) { func_type += " const"; }
    return func_type;
}

static std::string FunctionPointerCast(const std::string &class_name, const AiluHeadTool::MemberInfo &member)
{
    std::string pointer_type = member._return_type;
    pointer_type += member._is_static ? " (*)" : " (" + class_name + "::*)";
    pointer_type += "(";
    const auto &params = member._function_pointer_params.empty() ? member._params : member._function_pointer_params;
    for (size_t index = 0u; index < params.size(); ++index)
    {
        if (index > 0u)
            pointer_type += ",";
        pointer_type += params[index];
    }
    pointer_type += ")";
    if (!member._is_static && member._is_const)
        pointer_type += " const";
    return std::format("static_cast<{}>(&{}::{})", pointer_type, class_name, member._name);
}

static void ParserMeta(std::string line, AiluHeadTool::PropertyMeta &meta)
{
    // 去除空白字符后的字符串
    line = std::regex_replace(line, std::regex("\\s+"), "");
    // 正则：提取 APROPERTY() 中的内容
    std::regex rgx1("APROPERTY\\((.*)\\)");
    std::smatch match1;

    if (std::regex_search(line, match1, rgx1))
    {
        // match[1] 是 {} 之间的内容
        std::string content = match1[1].str();
        // 正则：按分号分隔字符串
        std::regex rgx2("(\\s*[^;]+\\s*)");// 匹配每个分号分隔的片段
        auto words_begin = std::sregex_iterator(content.begin(), content.end(), rgx2);
        auto words_end = std::sregex_iterator();
        std::vector<std::string> meta_item{};
        for (std::sregex_iterator i = words_begin; i != words_end; ++i)
        {
            auto item = i->str();
            meta_item.emplace_back(i->str());
            if (item.find("Range") != std::string::npos)
            {
                meta._is_range = true;
                meta._is_float_range = item.find(".") != std::string::npos;
                std::smatch match;
                std::regex rgx;
                if (meta._is_float_range)
                {
                    // 正则表达式：匹配 "Range(x,y)" 中的浮点数 x 和 y
                    rgx = std::regex("Range\\((-?\\d+\\.\\d+)f?,\\s*(-?\\d+\\.\\d+)f?\\)");
                }
                else rgx = std::regex(R"(Range\((-?\d+),\s*(-?\d+)\))");
                if (std::regex_search(item, match, rgx))
                {
                    meta._min = meta._is_float_range ? std::stof(match[1].str()) : std::stoi(match[1].str());
                    meta._max = meta._is_float_range ? std::stof(match[2].str()) : std::stoi(match[2].str());
                }
            }
            else if (item.find("Category") != std::string::npos)// Category="*"
            {
                meta._category = item.substr(item.find("=") + 2, item.find_last_of("\"") - item.find("=") - 2);
            }
            else if (item == "Script")
            {
                meta._is_script = true;
            }
        }
    }
}

#define BOOL_STR(x) x ? "true" : "false"

static void GenerateClassTypeInfo(const AiluHeadTool::ClassInfo &class_info,
                                  std::ofstream &file,
                                  const std::unordered_map<std::string, std::set<std::string>> &type_namespace_map)
{
    using std::endl;
    std::string full_name = class_info._namespace + "::" + class_info._name;
    file << std::format("const Ailu::Type* {}::Z_Construct_{}_Type()", class_info._namespace, class_info._name) << std::endl;
    file << "{" << std::endl;
    if (!class_info._parent.empty())
        file << std::format("{}::StaticType();",class_info._parent) << std::endl;
    file << std::format("static std::unique_ptr<Ailu::Type> cur_type = nullptr;") << std::endl;
    file << std::format("if(cur_type == nullptr)") << std::endl;
    file << "{" << std::endl;
    file << "TypeInitializer initializer;" << endl;
    file << std::format("initializer._name = \"{}\"", class_info._name) << ";" << endl;
    file << std::format("initializer._size = sizeof({});", full_name) << endl;
    file << std::format("initializer._full_name = \"{}\"", full_name) << ";" << endl;
    file << std::format("initializer._is_class = true;") << endl;
    file << std::format("initializer._is_abstract = {}", BOOL_STR(class_info._is_abstract)) << ";" << endl;
    file << std::format("initializer._namespace = \"{}\"", class_info._namespace) << ";" << endl;
    file << std::format("initializer._base_name = \"{}\"", class_info._parent) << ";" << endl;
    file << std::format("initializer._constructor = []()->{}* {{return new {};}}",full_name, full_name)<< ";" << endl;

    std::unordered_map<std::string, size_t> function_counts;
    for (const auto &member : class_info._members)
    {
        if (member._is_function)
            ++function_counts[member._name];
    }
    std::unordered_map<std::string, size_t> function_indices;
    for (auto &mem: class_info._members)
    {
        if (mem._is_function)
        {
            const bool is_overloaded = function_counts[mem._name] > 1u;
            const std::string suffix = is_overloaded ? std::format("_{}", function_indices[mem._name]++) : std::string{};
            std::string cur_meta = "meta" + mem._name + suffix;
            file << "Meta " << cur_meta << ";" << std::endl;
            file << std::format("{}.Set(\"Script\",{});", cur_meta, BOOL_STR(mem._is_script)) << std::endl;
            auto bd_name = "builder" + mem._name + suffix;
            file << std::format("MemberBuilder {};", bd_name) << std::endl;
            file << std::format("{}._name = \"{}\";", bd_name, mem._name) << std::endl;
            file << std::format("{}._type_name = \"{}\";", bd_name, ConstructFuncType(mem._return_type, mem._params, mem._is_const)) << std::endl;
            file << std::format("{}._offset = 0u;", bd_name) << std::endl;
            file << std::format("{}._is_const = {};", bd_name, BOOL_STR(mem._is_const)) << std::endl;
            file << std::format("{}._is_static = {};", bd_name, BOOL_STR(mem._is_static)) << std::endl;
            file << std::format("{}._is_public = {};", bd_name, BOOL_STR(mem._is_public)) << std::endl;
            file << std::format("{}._ret_type_name = \"{}\";", bd_name, mem._return_type) << std::endl;
            file << std::format("{}._meta = {};", bd_name, cur_meta) << std::endl;
            file << std::format("{}._member_ptr = {};", bd_name,
                                is_overloaded ? FunctionPointerCast(full_name, mem) : "&" + full_name + "::" + mem._name)
                 << std::endl;
            //file << std::format("{}._accessor = MakeScope<OffsetPropertyAccessor>({}._offset);", bd_name) << std::endl;
            file << std::format("initializer._functions.emplace_back(MemberBuilder::BuildFunction({}));", bd_name) << std::endl;
        }
        else if (!mem._is_event)
        {
            std::string cur_meta = "meta" + mem._name;
            file << "Meta " << cur_meta << ";" << std::endl;
            file << std::format("{}.Set(\"Category\",\"{}\");", cur_meta, mem._meta._category) << std::endl;
            file << std::format("{}.Set(\"IsColor\",{});", cur_meta, BOOL_STR(mem._meta._is_color)) << std::endl;
            file << std::format("{}.Set(\"IsRange\",{});", cur_meta, BOOL_STR(mem._meta._is_range)) << std::endl;
            file << std::format("{}.Set(\"IsFloatRange\",{});", cur_meta, BOOL_STR(mem._meta._is_float_range)) << std::endl;
            if (mem._meta._is_float_range)
            {
                file << std::format("{}.Set(\"RangeMin\",(f32){});", cur_meta, mem._meta._min) << std::endl;
                file << std::format("{}.Set(\"RangeMax\",(f32){});", cur_meta, mem._meta._max) << std::endl;
            }
            else
            {
                file << std::format("{}.Set(\"RangeMin\",(i32){});", cur_meta, mem._meta._min)<< std::endl;
                file << std::format("{}.Set(\"RangeMax\",(i32){});", cur_meta, mem._meta._max)<< std::endl;
            }
            auto bd_name = "builder" + mem._name;
            const std::string reflected_type_name = ResolveReflectedTypeName(mem._type, class_info._namespace, type_namespace_map);
            file << std::format("MemberBuilder {};", bd_name) << std::endl;
            file << std::format("{}._name = \"{}\";", bd_name,mem._name) << std::endl;
            file << std::format("{}._type_name = \"{}\";", bd_name, reflected_type_name) << std::endl;
            file << std::format("{}._offset = offsetof({},{});", bd_name, class_info._name, mem._name) << std::endl;
            file << std::format("{}._is_const = {};", bd_name,BOOL_STR(mem._is_const)) << std::endl;
            file << std::format("{}._is_static = {};", bd_name, BOOL_STR(mem._is_static)) << std::endl;
            file << std::format("{}._is_public = {};", bd_name, BOOL_STR(mem._is_public)) << std::endl;
            file << std::format("{}._is_pointer = {};", bd_name, BOOL_STR(mem._is_pointer)) << std::endl;
            file << std::format("{}._is_ref = {};", bd_name, BOOL_STR(mem._is_reference)) << std::endl;
            file << std::format("{}._is_template = {};", bd_name, BOOL_STR(mem._is_template)) << std::endl;
            file << std::format("{}._meta = {};", bd_name, cur_meta) << std::endl;
            //file << std::format("{}._accessor = MakeScope<OffsetPropertyAccessor>({}._offset);", bd_name, bd_name) << std::endl;
            file << std::format("{}._serialize_fn = static_cast<SerializeFunc>(&SerializePrimitive<{}>);",bd_name,mem._type) << std::endl;
            file << std::format("{}._deserialize_fn = static_cast<DeserializeFunc>(&DeserializePrimitive<{}>);",bd_name,mem._type) << std::endl;
            file << std::format("initializer._properties.emplace_back(MemberBuilder::BuildProperty({}));", bd_name) << std::endl;
        }
    }
    file << "cur_type = std::make_unique<Ailu::Type>(initializer);" << std::endl;
    file << "Ailu::Type::RegisterType(cur_type.get());" << std::endl;
    file << "}" << std::endl;
    file << "return cur_type.get();" << std::endl;
    file << "}" << std::endl;
    file << std::endl;
    file << std::format("const Ailu::Type* {}::GetPrivateStaticClass()", full_name) << std::endl;
    file << "{" << std::endl;
    file << std::format("\tstatic const Ailu::Type* type = Z_Construct_{}_Type();", class_info._name) << std::endl;
    file << std::format("\treturn type;") << std::endl;
    file << "}" << std::endl;
    file << std::endl;
    file << std::format("template<> const Ailu::Type* Ailu::StaticClass<{}::{}>()", class_info._namespace, class_info._name) << std::endl;
    file << "{" << std::endl;
    file << "return " << std::format("{}::StaticType();", full_name) << std::endl;
    file << "}" << std::endl;
    file << std::format("    const Type *{}::GetType()", full_name) << std::endl;
    file << "{" << std::endl;
    file << "return " << std::format("{}::GetPrivateStaticClass();", full_name) << std::endl;
    file << "}" << std::endl;
    //ClassTypeRegister s_register_object(&Ailu::Object::StaticType, "Ailu::Object");
    file << std::format("ClassTypeRegister s_register_{}(&{}::StaticType, \"{}\");", class_info._name, full_name, full_name) << std::endl;
}

static std::string EnumConstructorName(const AiluHeadTool::EnumInfo &enum_info)
{
    std::string namespace_name = enum_info._namespace;
    std::string::size_type pos = 0;
    while ((pos = namespace_name.find("::", pos)) != std::string::npos)
    {
        namespace_name.replace(pos, 2, "_");
        ++pos;
    }

    if (namespace_name.empty())
        return std::format("Z_Construct_Enum_{}_Type", enum_info._name);
    return std::format("Z_Construct_Enum_{}_{}_Type", namespace_name, enum_info._name);
}

static void GenerateEnumTypeInfo(const AiluHeadTool::EnumInfo &enum_info, std::ofstream &file)
{
    const std::string full_name = enum_info._namespace.empty() ? enum_info._name : enum_info._namespace + "::" + enum_info._name;
    std::string type_ins_name = std::format("s_enum_type_{}", enum_info._name);
    file << std::format("static std::unique_ptr<Ailu::Enum> {} = nullptr;", type_ins_name) << std::endl;
    const std::string construct_enum_func = EnumConstructorName(enum_info);
    file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
    file << std::format("const Ailu::Enum* {}()", construct_enum_func) << std::endl;
    file << "{" << std::endl;
    file << std::format("if({} == nullptr)", type_ins_name) << std::endl;
    file << "{" << std::endl;
    file << "EnumInitializer initializer;" << std::endl;
    file << std::format("initializer._name = \"{}\"", enum_info._name) << ";" << std::endl;
    file << std::format("initializer._namespace = \"{}\"", enum_info._namespace) << ";" << std::endl;
    file << std::format("initializer._full_name = \"{}\"", full_name) << ";" << std::endl;
    for (auto &mem: enum_info._members)
    {
        auto &[name, id] = mem;
        file << std::format("initializer._str_to_enum_lut[\"{}\"] = {};", name, id) << std::endl;
    }
    file << std::format("{} = std::make_unique<Ailu::Enum>(initializer);", type_ins_name) << std::endl;
    file << std::format("Ailu::Enum::RegisterEnum({}.get());", type_ins_name) << std::endl;
    file << "}" << std::endl;
    file << "return "<<type_ins_name << ".get();" << std::endl;
    file << "}" << std::endl;
    file << std::format("static Ailu::EnumTypeRegister g_register_{}({});", enum_info._name, construct_enum_func) << std::endl;
    file << std::format("template<> const Ailu::Enum* Ailu::StaticEnum<{}::{}>()", enum_info._namespace, enum_info._name) << std::endl;
    file << "{" << std::endl;
    file << "return " << type_ins_name << ".get();" << std::endl;
    file << "}" << std::endl;
    file << "//Enum " << enum_info._name << " end..........................." << std::endl;
    file << std::endl;
}

static void ParserEventAnnotation(const std::string &line, bool &is_script, int &key_index, std::string &key_name)
{
    std::regex pattern(R"(AEVENT\s*\(\s*([^,)]*)\s*(?:,\s*KeyIndex\s*=\s*(\d+))?\s*(?:,\s*KeyName\s*=\s*(\w+))?\s*\))");
    std::smatch matches;
    if (!std::regex_search(line, matches, pattern))
        return;

    is_script = Trim(matches[1].str()) == "Script";
    if (matches[2].matched)
        key_index = std::stoi(matches[2].str());
    if (matches[3].matched)
        key_name = matches[3].str();
}

static void ParserEventInfo(const std::string &line, AiluHeadTool::MemberInfo &info, AiluHeadTool &aht, int key_index,
                            const std::string &key_name)
{
    std::regex function_pattern(
        R"((static\s+)?(virtual\s+)?([\w:]+(?:<[^<>]*>)?(?:\s*\*|\s*&|\s*::\s*\w+)*)\s+)"
        R"((\w+)\(([^)]*)\)\s*(const)?)");
    std::smatch matches;
    if (std::regex_search(line, matches, function_pattern))
    {
        info._is_static = matches[1].matched;
        info._is_virtual = matches[2].matched;
        info._return_type = matches[3].str();
        info._name = matches[4].str();
        info._params = SplitParams(matches[5].str());
        info._param_names = SplitParamNames(matches[5].str());
        info._is_const = matches[6].matched;
        info._event_key_index = key_index;
        info._event_key_name = key_name;
        if (!key_name.empty() && info._event_key_index < 0)
        {
            for (size_t index = 0u; index < info._param_names.size(); ++index)
            {
                if (info._param_names[index] == key_name)
                {
                    info._event_key_index = static_cast<int>(index);
                    break;
                }
            }
        }
        info._is_function = false;
        info._is_event = true;
        return;
    }

    std::regex legacy_pattern(R"(DECLARE_DELEGATE(?:_VIEW)?\s*\(\s*(\w+)\s*(?:,\s*(.*?))?\s*\))");
    if (!std::regex_search(line, matches, legacy_pattern))
    {
        aht.Log(std::format("ParserEventInfo failed with line: {}", line));
        return;
    }

    info._name = matches[1].str();
    info._params = matches[2].matched ? SplitParams(matches[2].str()) : std::vector<std::string>{};
    info._param_names = matches[2].matched ? SplitParamNames(matches[2].str()) : std::vector<std::string>{};
    info._event_key_index = key_index;
    info._event_key_name = key_name;
    info._return_type = "void";
    info._is_function = false;
    info._is_event = true;
}

static std::string ToLuaFunctionName(const std::string &name)
{
    std::string result;
    for (size_t index = 0u; index < name.size(); ++index)
    {
        const char ch = name[index];
        if (std::isupper(static_cast<unsigned char>(ch)) && index > 0u &&
            !std::isdigit(static_cast<unsigned char>(name[index - 1u])))
            result += '_';
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return result;
}

static std::string NormalizeLuaType(std::string type)
{
    type = Trim(type);
    Replace(type, "const ", "");
    Replace(type, "&", "");
    Replace(type, "*", "");
    return Trim(type);
}

static bool IsSupportedLuaType(const std::string &type, const std::unordered_set<std::string> &script_api_types)
{
    static const std::set<std::string> kSupportedTypes = {
        "void", "bool", "f32", "f64", "float", "double", "i32", "u32", "i64", "u64", "String", "Vector2f",
        "Vector3f", "Math::Quaternion", "Color"};
    const std::string normalized_type = NormalizeLuaType(type);
    if (kSupportedTypes.contains(normalized_type) || script_api_types.contains(normalized_type))
        return true;
    if (normalized_type.starts_with("std::optional<") && normalized_type.ends_with('>'))
    {
        const std::string value_type = normalized_type.substr(14u, normalized_type.size() - 15u);
        return IsSupportedLuaType(value_type, script_api_types);
    }
    if (normalized_type.starts_with("Vector<") && normalized_type.ends_with('>'))
    {
        const std::string element_type = normalized_type.substr(7u, normalized_type.size() - 8u);
        return IsSupportedLuaType(element_type, script_api_types);
    }
    const size_t namespace_separator = normalized_type.rfind("::");
    return namespace_separator != std::string::npos &&
           script_api_types.contains(normalized_type.substr(namespace_separator + 2u));
}

struct LuaPropertyBinding
{
    std::string _name;
    const AiluHeadTool::MemberInfo *_getter = nullptr;
    const AiluHeadTool::MemberInfo *_setter = nullptr;
};

static std::string LuaPropertyName(const std::string &function_name)
{
    const size_t prefix_size = function_name.starts_with("Is") ? 2u : 3u;
    return ToLuaFunctionName(function_name.substr(prefix_size));
}

static std::vector<LuaPropertyBinding> CollectLuaProperties(const AiluHeadTool::ClassInfo &type)
{
    std::unordered_map<std::string, LuaPropertyBinding> properties;
    for (const auto &member : type._members)
    {
        if (!member._is_function || !member._is_script_property)
            continue;

        const bool is_getter = member._name.starts_with("Get") || member._name.starts_with("Is");
        const bool is_setter = member._name.starts_with("Set");
        if (!is_getter && !is_setter)
            throw std::runtime_error(std::format("Lua property method {}::{} must start with Get, Is, or Set",
                                                 type._name, member._name));

        const bool valid_getter = is_getter && member._params.empty() &&
                                  NormalizeLuaType(member._return_type) != "void";
        const bool valid_setter = is_setter && member._params.size() == 1u &&
                                  NormalizeLuaType(member._return_type) == "void";
        if (!valid_getter && !valid_setter)
            throw std::runtime_error(std::format("Invalid Lua property signature {}::{}", type._name, member._name));
        if (member._is_static)
            throw std::runtime_error(std::format("Lua property {}::{} cannot be static", type._name, member._name));

        const std::string property_name = LuaPropertyName(member._name);
        auto &property = properties[property_name];
        property._name = property_name;
        if (valid_getter)
        {
            if (property._getter != nullptr)
                throw std::runtime_error(std::format("Duplicate Lua property getter {}::{}",
                                                     type._name, property_name));
            property._getter = &member;
        }
        else
        {
            if (property._setter != nullptr)
                throw std::runtime_error(std::format("Duplicate Lua property setter {}::{}",
                                                     type._name, property_name));
            property._setter = &member;
        }
    }

    std::vector<LuaPropertyBinding> result;
    result.reserve(properties.size());
    for (auto &[name, property] : properties)
    {
        if (property._setter != nullptr && property._getter == nullptr)
            throw std::runtime_error(std::format("Lua property setter {}::{} has no getter", type._name, name));
        result.emplace_back(property);
    }
    std::sort(result.begin(), result.end(), [](const LuaPropertyBinding &lhs, const LuaPropertyBinding &rhs)
    {
        return lhs._name < rhs._name;
    });
    return result;
}

static std::string EventFieldName(const std::string &name)
{
    return "_" + ToLuaFunctionName(name);
}

static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return std::tolower(character); });
    return value;
}

static std::string ToLuaEnumName(const std::string &enum_name)
{
    return enum_name.size() > 1u && enum_name[0] == 'E' && std::isupper(static_cast<unsigned char>(enum_name[1])) ?
               enum_name.substr(1u) : enum_name;
}

static std::string ToLuaEnumMemberName(const std::string &member_name)
{
    return member_name.size() > 1u && member_name[0] == 'k' && std::isupper(static_cast<unsigned char>(member_name[1])) ?
               member_name.substr(1u) : member_name;
}

static void GenerateLuaBindings(const std::vector<AiluHeadTool::ClassInfo> &types,
                                const std::vector<AiluHeadTool::EnumInfo> &enums,
                                const std::string &binding_name, std::ofstream &file,
                                const std::unordered_set<std::string> &script_api_types)
{
    file << "#if AILU_ENABLE_LUA_SCRIPTING" << std::endl;
    file << "#include <sol/sol.hpp>" << std::endl;
    file << "#include <Framework/Script/ScriptLuaBindingRegistry.h>" << std::endl;
    file << "#include <Framework/Script/ScriptSystem.h>" << std::endl;
    file << std::format("namespace Ailu {{ void {}(sol::state &lua); }}", binding_name) << std::endl;
    file << std::format("void Ailu::{}(sol::state &lua)", binding_name) << std::endl;
    file << "{" << std::endl;
    for (const auto &enum_info : enums)
    {
        if (!enum_info._is_script)
            continue;

        const std::string full_name = enum_info._namespace.empty() ? enum_info._name : enum_info._namespace + "::" + enum_info._name;
        file << std::format("auto enum_{} = lua.create_table();", enum_info._name) << std::endl;
        for (const auto &[member_name, member_value] : enum_info._members)
        {
            file << std::format("enum_{}[\"{}\"] = static_cast<u32>({}::{});", enum_info._name,
                                ToLuaEnumMemberName(member_name), full_name, member_name) << std::endl;
        }
        file << std::format("lua[\"{}\"] = enum_{};", ToLuaEnumName(enum_info._name), enum_info._name) << std::endl;
    }
    for (const auto &type : types)
    {
        if (!type._is_script_api)
            continue;

        bool has_script_function = false;
        for (const auto &member : type._members)
            has_script_function |= (member._is_function || member._is_event) && member._is_script;
        if (!has_script_function)
            continue;

        const std::string full_name = type._namespace.empty() ? type._name : type._namespace + "::" + type._name;
        file << std::format("auto type_{} = lua.new_usertype<{}>(\"{}\");", type._name, full_name, type._name) << std::endl;
        for (const auto &property : CollectLuaProperties(type))
        {
            if (!IsSupportedLuaType(property._getter->_return_type, script_api_types))
                throw std::runtime_error(std::format("Lua property generation rejected {}::{} return type '{}'",
                                                     full_name, property._name, property._getter->_return_type));
            file << std::format("type_{}[\"{}\"] = sol::property(", type._name, property._name);
            file << "&" << full_name << "::" << property._getter->_name;
            if (property._setter != nullptr)
                file << ", &" << full_name << "::" << property._setter->_name;
            file << ");" << std::endl;
        }
        std::unordered_set<std::string> emitted_function_names;
        for (const auto &member : type._members)
        {
            if (member._is_function && member._is_script)
            {
                if (!emitted_function_names.emplace(member._name).second)
                    continue;

                std::vector<const AiluHeadTool::MemberInfo *> overloads;
                for (const auto &candidate : type._members)
                {
                    if (candidate._is_function && candidate._is_script && candidate._name == member._name)
                        overloads.emplace_back(&candidate);
                }
                if (!IsSupportedLuaType(member._return_type, script_api_types))
                    throw std::runtime_error(std::format("Lua binding generation rejected {}::{} return type '{}'", full_name,
                                                         member._name, member._return_type));
                for (const auto *overload : overloads)
                {
                    if (!IsSupportedLuaType(overload->_return_type, script_api_types))
                        throw std::runtime_error(std::format("Lua binding generation rejected {}::{} return type '{}'", full_name,
                                                             overload->_name, overload->_return_type));
                    for (const auto &param : overload->_params)
                    {
                        if (!IsSupportedLuaType(param, script_api_types))
                            throw std::runtime_error(std::format("Lua binding generation rejected {}::{} parameter type '{}'", full_name,
                                                                 overload->_name, param));
                    }
                }
                file << std::format("type_{}.set_function(\"{}\", ", type._name, ToLuaFunctionName(member._name));
                if (overloads.size() > 1u)
                {
                    file << "sol::overload(";
                    for (size_t index = 0u; index < overloads.size(); ++index)
                    {
                        if (index > 0u)
                            file << ", ";
                        file << FunctionPointerCast(full_name, *overloads[index]);
                    }
                    file << ")";
                }
                else
                    file << "&" << full_name << "::" << member._name;
                file << ");" << std::endl;
                if (type._name == "ScriptAssetValue" && member._is_static && overloads.size() == 1u)
                    file << std::format("lua[\"{}\"] = &{}::{};", member._name, full_name, member._name) << std::endl;
            }
            else if (member._is_event && member._is_script)
            {
                for (const auto &param : member._params)
                {
                    if (!IsSupportedLuaType(param, script_api_types))
                        throw std::runtime_error(std::format("Lua binding generation rejected {}::{} event parameter type '{}'", full_name,
                                                             member._name, param));
                }
                if (!member._event_key_name.empty())
                {
                    if (member._event_key_index < 0 ||
                        member._event_key_index >= static_cast<int>(member._params.size()))
                        throw std::runtime_error(std::format("Lua binding generation rejected {}::{} key index {}", full_name,
                                                             member._name, member._event_key_index));
                    const std::string key_type = NormalizeLuaType(member._params[member._event_key_index]);
                    file << std::format("type_{}.set_function(\"{}\", []( {} &self, const {} &{}, "
                                        "sol::protected_function callback) {{ return ScriptSystem::Get().BindLuaEventRouter(self.{}, "
                                        "{}, std::move(callback)); }});",
                                        type._name, ToLuaFunctionName(member._name), full_name, key_type, member._event_key_name,
                                        EventFieldName(member._name), member._event_key_name) << std::endl;
                }
                else
                {
                    file << std::format("type_{}.set_function(\"{}\", []( {} &self, sol::protected_function callback) {{ return "
                                        "ScriptSystem::Get().BindLuaDelegate(self.{}, std::move(callback)); }});",
                                        type._name, ToLuaFunctionName(member._name), full_name, EventFieldName(member._name)) << std::endl;
                }
            }
        }
        if (type._script_global_name == "engine")
        {
            file << "auto global_engine = lua[\"engine\"].get_or_create<sol::table>();" << std::endl;
            for (const auto &member : type._members)
            {
                if (member._is_function && member._is_script && member._is_static)
                    file << std::format("global_engine.set_function(\"{}\", &{}::{});", ToLuaFunctionName(member._name), full_name,
                                        member._name) << std::endl;
            }
        }
        else if (!type._script_global_name.empty())
        {
            if (type._script_global_name == "input")
                file << "lua[\"input\"] = std::ref(ScriptSystem::Get().GetInput());" << std::endl;
            else
                file << std::format("lua[\"{}\"] = {}{{}};", type._script_global_name, full_name) << std::endl;
        }
    }
    file << "}" << std::endl;
    file << std::format("ScriptLuaBindingRegister s_register_lua_bindings_{}(&Ailu::{});", binding_name, binding_name) << std::endl;
    file << "#endif" << std::endl;
}

static std::string ToLuaTypeName(const std::string &type, const std::unordered_set<std::string> *script_enum_types = nullptr)
{
    const std::string normalized_type = NormalizeLuaType(type);
    if (normalized_type.starts_with("std::optional<") && normalized_type.ends_with('>'))
    {
        const std::string value_type = normalized_type.substr(14u, normalized_type.size() - 15u);
        return ToLuaTypeName(value_type, script_enum_types) + "|nil";
    }
    if (normalized_type.starts_with("Vector<") && normalized_type.ends_with('>'))
    {
        const std::string element_type = normalized_type.substr(7u, normalized_type.size() - 8u);
        return ToLuaTypeName(element_type, script_enum_types) + "[]";
    }
    if (normalized_type == "bool")
        return "boolean";
    if (normalized_type == "f32" || normalized_type == "f64" || normalized_type == "float" || normalized_type == "double" ||
        normalized_type == "i32" || normalized_type == "u32" || normalized_type == "i64" || normalized_type == "u64")
        return "number";
    if (normalized_type == "String")
        return "string";
    if (normalized_type == "Vector2f")
        return "Vec2";
    if (normalized_type == "Vector3f")
        return "Vec3";
    if (normalized_type == "Math::Quaternion")
        return "Quaternion";
    if (normalized_type == "Color")
        return "Color";
    if (script_enum_types != nullptr && script_enum_types->contains(normalized_type))
        return ToLuaEnumName(normalized_type);
    return normalized_type;
}

static bool HasScriptMember(const AiluHeadTool::ClassInfo &type)
{
    for (const auto &member : type._members)
    {
        if (member._is_script)
            return true;
    }
    return false;
}

static void GenerateLuaDeclarations(const std::vector<AiluHeadTool::ClassInfo> &types,
                                    const std::vector<AiluHeadTool::EnumInfo> &enums, const Path &output_path)
{
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream file(output_path);
    if (!file.is_open())
        throw std::runtime_error(std::format("Failed to create Lua declaration file {}", output_path.string()));

    file << "---@meta\n";
    file << "-- Generated by AiluHeadTool from Script reflection metadata. Do not edit.\n\n";

    std::unordered_set<std::string> script_enum_types;
    for (const auto &enum_info : enums)
    {
        if (!enum_info._is_script)
            continue;

        script_enum_types.emplace(enum_info._name);
        file << std::format("---@enum {}\n{} = {{\n", ToLuaEnumName(enum_info._name), ToLuaEnumName(enum_info._name));
        for (const auto &[member_name, member_value] : enum_info._members)
            file << std::format("    {} = {},\n", ToLuaEnumMemberName(member_name), member_value);
        file << "}\n\n";
    }

    for (const auto &type : types)
    {
        if (!type._is_script_api)
            continue;
        if (!HasScriptMember(type))
            continue;

        file << std::format("---@class {}\n", type._name);
        for (const auto &member : type._members)
        {
            if (!member._is_function && !member._is_event && member._is_script)
                file << std::format("---@field {} {}\n", ToLuaFunctionName(member._name),
                                    ToLuaTypeName(member._type, &script_enum_types));
        }
        for (const auto &property : CollectLuaProperties(type))
            file << std::format("---@field {} {}\n", property._name,
                                ToLuaTypeName(property._getter->_return_type, &script_enum_types));
        // These declarations describe runtime globals registered in ScriptSystem.
        // Keeping them local makes LuaLS report the API as undefined in user scripts.
        file << std::format("{} = {{}}\n\n", type._name);
        if (!type._script_global_name.empty())
        {
            file << std::format("---@type {}\n{} = {{}}\n\n", type._name, type._script_global_name);
        }
        std::unordered_set<std::string> emitted_function_names;
        for (const auto &member : type._members)
        {
            if ((!member._is_function && !member._is_event) || !member._is_script)
                continue;
            if (member._is_function && !emitted_function_names.emplace(member._name).second)
                continue;
            if (member._is_event)
            {
                if (!member._event_key_name.empty())
                    file << std::format("---@param {} {}\n", member._event_key_name,
                                        ToLuaTypeName(member._params[member._event_key_index], &script_enum_types));
                file << "---@param callback fun(";
                bool has_callback_parameter = false;
                for (size_t index = 0u; index < member._params.size(); ++index)
                {
                    if (static_cast<int>(index) == member._event_key_index)
                        continue;
                    if (has_callback_parameter)
                        file << ", ";
                    has_callback_parameter = true;
                    const std::string parameter_name = index < member._param_names.size() && !member._param_names[index].empty() ?
                                                           member._param_names[index] : std::format("arg{}", index);
                    file << parameter_name << ": " << ToLuaTypeName(member._params[index], &script_enum_types);
                }
                file << ")\n---@return integer subscription_id\n";
                file << std::format("function {}:{}({}) end\n\n", type._name, ToLuaFunctionName(member._name),
                                    !member._event_key_name.empty() ? member._event_key_name + ", callback" : "callback");
                continue;
            }
            for (const auto &overload : type._members)
            {
                if (!overload._is_function || !overload._is_script || overload._name != member._name || &overload == &member)
                    continue;
                file << "---@overload fun(";
                if (!member._is_static)
                    file << std::format("self: {}, ", type._name);
                for (size_t index = 0u; index < overload._params.size(); ++index)
                {
                    if (index > 0u)
                        file << ", ";
                    const std::string parameter_name = index < overload._param_names.size() && !overload._param_names[index].empty()
                                                           ? overload._param_names[index] : std::format("arg{}", index);
                    file << parameter_name << ": " << ToLuaTypeName(overload._params[index], &script_enum_types);
                }
                file << ")";
                if (NormalizeLuaType(overload._return_type) != "void")
                    file << ": " << ToLuaTypeName(overload._return_type, &script_enum_types);
                file << "\n";
            }
            for (size_t index = 0u; index < member._params.size(); ++index)
            {
                const std::string parameter_name = index < member._param_names.size() && !member._param_names[index].empty() ?
                                                   member._param_names[index] : std::format("arg{}", index);
                file << std::format("---@param {} {}\n", parameter_name,
                                    ToLuaTypeName(member._params[index], &script_enum_types));
            }
            if (NormalizeLuaType(member._return_type) != "void")
                file << std::format("---@return {}\n", ToLuaTypeName(member._return_type, &script_enum_types));
            const char *separator = member._is_static ? "." : ":";
            file << std::format("function {}{}{}(", type._name, separator, ToLuaFunctionName(member._name));
            for (size_t index = 0u; index < member._params.size(); ++index)
            {
                if (index > 0u)
                    file << ", ";
                const std::string parameter_name = index < member._param_names.size() && !member._param_names[index].empty() ?
                                                   member._param_names[index] : std::format("arg{}", index);
                file << parameter_name;
            }
            file << ") end\n\n";
            if (type._name == "ScriptAssetValue" && member._is_static)
                file << std::format("---@return {}\nfunction {}() end\n\n", type._name, member._name);
        }
    }
}

static void GenerateLuaDeclarationIndex(const Path &output_path)
{
    std::ofstream file(output_path);
    if (!file.is_open())
        throw std::runtime_error(std::format("Failed to create Lua declaration index {}", output_path.string()));

    file << "---@meta\n";
    file << "-- Generated by AiluHeadTool. Individual Script API declarations are split by module.\n\n";
    file << "---@class Vec2\n---@field x number\n---@field y number\n"
            "---@overload fun(): Vec2\n---@overload fun(x: number, y: number): Vec2\nVec2 = nil\n\n";
    file << "---@class Vec3\n---@field x number\n---@field y number\n---@field z number\n"
            "---@overload fun(): Vec3\n---@overload fun(x: number, y: number, z: number): Vec3\nVec3 = nil\n\n";
    file << "---@class Quaternion\n---@field x number\n---@field y number\n---@field z number\n---@field w number\n"
            "---@overload fun(): Quaternion\n---@overload fun(x: number, y: number, z: number, w: number): Quaternion\nQuaternion = nil\n\n";
    file << "---@class Color\n---@field r number\n---@field g number\n---@field b number\n---@field a number\n"
            "---@overload fun(): Color\n---@overload fun(r: number, g: number, b: number, a: number): Color\nColor = nil\n\n";
    file << "require(\"scriptengine\")\nrequire(\"scriptassetvalue\")\nrequire(\"scriptentity\")\n"
            "require(\"scriptcamera\")\nrequire(\"scriptscene\")\n";
    file << "require(\"scriptinput\")\nrequire(\"scripttime\")\nrequire(\"scriptphysics2d\")\n"
            "require(\"scriptrigidbody2d\")\nrequire(\"scriptspriterenderer\")\n"
            "require(\"scriptanimator\")\nrequire(\"scriptaudiosource\")\nrequire(\"scriptaudio\")\n\n";
    file << "---@class AiluScript\n---@field entity ScriptEntity\n---@field scene ScriptScene\nlocal AiluScript = {}\n\n";
    file << "function AiluScript:on_create() end\n\nfunction AiluScript:on_enable() end\n\n";
    file << "---@param dt number\nfunction AiluScript:on_fixed_update(dt) end\n\n---@param dt number\n"
            "function AiluScript:on_update(dt) end\n\n";
    file << "---@param dt number\n---@param render_alpha number\n"
            "function AiluScript:on_late_update(dt, render_alpha) end\n\n"
            "function AiluScript:on_disable() end\n\n";
    file << "function AiluScript:on_destroy() end\n\nfunction AiluScript:on_reload() end\n";
}

static void GenerateLuaWorkspaceConfig(const Path &output_path)
{
    std::ofstream file(output_path);
    if (!file.is_open())
        throw std::runtime_error(std::format("Failed to create Lua workspace config {}", output_path.string()));

    file << "{\n";
    file << "    \"runtime.version\": \"Lua 5.4\",\n";
    file << "    \"workspace.library\": [\".ailu/lua\"],\n";
    file << "    \"diagnostics.globals\": [\"engine\", \"input\", \"time\", \"physics2d\", \"camera\", \"audio\"]\n";
    file << "}\n";
}

void AiluHeadTool::CollectScriptApiTypes(const std::set<fs::path> &inc_files)
{
    _script_api_types.clear();
    for (const fs::path &path : inc_files)
    {
        std::ifstream file(path);
        if (!file.is_open())
            continue;

        std::string line;
        bool has_script_type_marked = false;
        bool has_script_enum_marked = false;
        while (std::getline(file, line))
        {
            if (line.find("ASTRUCT(") != std::string::npos || line.find("ACLASS(") != std::string::npos)
            {
                has_script_type_marked = false;
                ParserScriptTypeAnnotation(line, has_script_type_marked);
                continue;
            }
            if (line.find("AENUM(") != std::string::npos)
            {
                has_script_enum_marked = false;
                ParserEnumAnnotation(line, has_script_enum_marked);
                continue;
            }
            if (has_script_enum_marked)
            {
                std::smatch matches;
                if (std::regex_search(line, matches, std::regex(R"(enum\s+(?:class\s+)?(\w+))")))
                {
                    _script_api_types.emplace(matches[1].str());
                    has_script_enum_marked = false;
                }
            }
            if (!has_script_type_marked)
                continue;

            std::string type_name;
            if (ParseReflectedDeclarationName(line, type_name))
            {
                _script_api_types.emplace(type_name);
                has_script_type_marked = false;
            }
        }
    }
}

void AiluHeadTool::Parser(const Path &path, const Path &out_dir, std::string work_namespace)
{
    g_Timer.start();
    _classes.clear();
    _enums.clear();
    _structs.clear();

    try
    {
        using std::endl;
        if (fs::exists(path))
        {
            _tracker.Clean();
            std::ifstream file(path);
            if (!file.is_open()) { Log(std::format("AiluHeadTool::Parser {} with result open file failed", path.string())); }
            else
            {
                _is_cur_file_engine_lib = path.string().find("Editor\\Inc") == std::string::npos;
                std::string cur_file_id = path.stem().string();
                cur_file_id.append("_GEN_H");
                std::transform(cur_file_id.begin(), cur_file_id.end(), cur_file_id.begin(), ::toupper);
                std::string line;
                std::string last_include_target;
                int last_include_line = 0;
                bool is_last_include_generated = false;
                bool has_class_marked = false, has_property_marked = false, has_function_marked = false, has_event_marked = false, has_enum_marked = false, has_struct_marked = false;
                bool is_script_function = false, is_script_property_function = false;
        bool is_script_event = false;
        bool is_script_enum = false;
        bool is_script_api = false;
        std::string script_global_name;
                int event_key_index = -1;
                std::string event_key_name;
                bool is_cur_access_scope_public = false;
                bool is_process_class = false;
                int line_count = 0;
                std::string cur_namespace;
                PropertyMeta pre_prop_meta;
                while (std::getline(file, line))
                {
                    cur_namespace = _tracker.ProcessLine(line);
                    if (line.empty())
                    {
                        ++line_count;
                        continue;
                    }
                    std::string include_target;
                    if (TryParseIncludeTarget(line, include_target))
                    {
                        last_include_target = include_target;
                        last_include_line = line_count + 1;
                        is_last_include_generated = IsGeneratedIncludeTarget(include_target);
                    }
                    ++line_count;
                    //标记scope
                    if (line.find("public:") != std::string::npos)
                    {
                        is_cur_access_scope_public = true;
                        continue;
                    }
                    else if (line.find("private:") != std::string::npos || line.find("protected:") != std::string::npos)
                    {
                        is_cur_access_scope_public = false;
                        continue;
                    }
                    else {};
                    if (has_enum_marked)
                    {
                        if (line.find("enum") != std::string::npos)
                        {
                            EnumInfo enum_info;
                            if (line.find('}') != std::string::npos)
                            {
                                Log("[Error]: enum can't be define in one line");
                                //ParserEnumValues(line, enum_info, *this);
                            }
                            else//多行枚举
                            {
                                ParserEnumClass(line, enum_info, *this);
                                std::getline(file, line);
                                ++line_count;
                                if (line.find('{') != std::string::npos)
                                {
                                    std::getline(file, line);
                                    ++line_count;
                                    while (line.find('}') == std::string::npos)
                                    {
                                        ParserEnumValues(line, enum_info, *this);
                                        std::getline(file, line);
                                        ++line_count;
                                    }
                                }
                                else { Log("[Error]: the next line of enum define must only have {"); }
                                enum_info._namespace = cur_namespace;
                                enum_info._is_script = is_script_enum;
                            }
                            if (!enum_info._members.empty()) _enums.emplace_back(enum_info);
                            is_script_enum = false;
                            has_enum_marked = false;
                        }
                    }
                    //提取信息
                    if (has_class_marked)
                    {
                        if (line.find("class") != std::string::npos)
                        {
                            ClassInfo class_info;
                            ParserClassOrStructInfo(line, class_info, *this);
                            class_info._namespace = cur_namespace;
                            class_info._is_script_api = is_script_api;
                            class_info._script_global_name = script_global_name;
                            has_class_marked = false;
                            is_script_api = false;
                            script_global_name.clear();
                            is_process_class = true;
                            FindBaseClass(class_info);
                            _classes.emplace_back(class_info);
                            continue;
                        }
                    }
                    if (has_struct_marked)
                    {
                        if (line.find("struct") != std::string::npos)
                        {
                            ClassInfo struct_info;
                            ParserClassOrStructInfo(line, struct_info, *this);
                            struct_info._namespace = cur_namespace;
                            struct_info._is_script_api = is_script_api;
                            struct_info._script_global_name = script_global_name;
                            has_struct_marked = false;
                            is_script_api = false;
                            script_global_name.clear();
                            is_process_class = false;
                            _structs.emplace_back(struct_info);
                            continue;
                        }
                    }
                    //检查标记
                    if (line.find(kClassMacro) != std::string::npos)
                    {
                        has_class_marked = true;
                        is_script_api = false;
                        script_global_name.clear();
                        ParserScriptTypeAnnotation(line, is_script_api, &script_global_name);
                        continue;
                    }
                    if (line.find(kStructMacro) != std::string::npos)
                    {
                        has_struct_marked = true;
                        is_script_api = false;
                        script_global_name.clear();
                        ParserScriptTypeAnnotation(line, is_script_api, &script_global_name);
                        continue;
                    }
                    if (line.find(kEnumMacro) != std::string::npos)
                    {
                        has_enum_marked = true;
                        ParserEnumAnnotation(line, is_script_enum);
                        continue;
                    }
                    if (!_classes.empty() && is_process_class)
                    {
                        if (_classes.back()._gen_macro_body.empty())
                        {
                            if (line.find(kBodyMacro) != std::string::npos)
                            {
                                _classes.back()._gen_macro_body = std::format("{}_{}_GENERATED_BODY", cur_file_id, line_count);
                            }
                        }
                if (has_property_marked)
                {
                            MemberInfo member_info;
                            ParserPropertyInfo(line, member_info, *this);
                            member_info._is_public = is_cur_access_scope_public;
                    member_info._meta = pre_prop_meta;
                    member_info._is_script = pre_prop_meta._is_script;
                            member_info._meta._is_color = member_info._type == "Color" || member_info._type == "Color32";
                            pre_prop_meta.Reset();
                            has_property_marked = false;
                            _classes.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (has_function_marked)
                        {
                            MemberInfo member_info;
                            ParserFunctionInfo(line, member_info, *this);
                            member_info._is_public = is_cur_access_scope_public;
                            member_info._is_function = true;
                            member_info._is_script = is_script_function;
                            member_info._is_script_property = is_script_property_function;
                            is_script_function = false;
                            is_script_property_function = false;
                            has_function_marked = false;
                            _classes.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (has_event_marked)
                        {
                            MemberInfo member_info;
                            ParserEventInfo(line, member_info, *this, event_key_index, event_key_name);
                            member_info._is_public = is_cur_access_scope_public;
                            member_info._is_script = is_script_event;
                            is_script_event = false;
                            event_key_index = -1;
                            event_key_name.clear();
                            has_event_marked = false;
                            _classes.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (line.find(kPropertyMacro) != std::string::npos)
                        {
                            has_property_marked = true;
                            ParserMeta(line, pre_prop_meta);
                            continue;
                        }
                        if (line.find(kFunctionMacro) != std::string::npos)
                        {
                            has_function_marked = true;
                            is_script_function = false;
                            is_script_property_function = false;
                            ParserScriptFunctionAnnotation(line, is_script_function, is_script_property_function);
                            continue;
                        }
                        if (line.find(kEventMacro) != std::string::npos)
                        {
                            has_event_marked = true;
                            ParserEventAnnotation(line, is_script_event, event_key_index, event_key_name);
                            continue;
                        }
                    }
                    if (!_structs.empty() && !is_process_class)
                    {
                        if (_structs.back()._gen_macro_body.empty())
                        {
                            if (line.find(kBodyMacro) != std::string::npos)
                            {
                                _structs.back()._gen_macro_body = std::format("{}_{}_GENERATED_BODY", cur_file_id, line_count);
                            }
                        }
                if (has_property_marked)
                {
                            MemberInfo member_info;
                            ParserPropertyInfo(line, member_info, *this);
                            member_info._is_public = is_cur_access_scope_public;
                    member_info._meta = pre_prop_meta;
                    member_info._is_script = pre_prop_meta._is_script;
                            member_info._meta._is_color = member_info._type == "Color" || member_info._type == "Color32";
                            pre_prop_meta.Reset();
                            has_property_marked = false;
                            _structs.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (has_function_marked)
                        {
                            MemberInfo member_info;
                            ParserFunctionInfo(line, member_info, *this);
                            member_info._is_public = is_cur_access_scope_public;
                            member_info._is_function = true;
                            member_info._is_script = is_script_function;
                            member_info._is_script_property = is_script_property_function;
                            is_script_function = false;
                            is_script_property_function = false;
                            has_function_marked = false;
                            _structs.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (has_event_marked)
                        {
                            MemberInfo member_info;
                            ParserEventInfo(line, member_info, *this, event_key_index, event_key_name);
                            member_info._is_public = is_cur_access_scope_public;
                            member_info._is_script = is_script_event;
                            is_script_event = false;
                            event_key_index = -1;
                            event_key_name.clear();
                            has_event_marked = false;
                            _structs.back()._members.emplace_back(member_info);
                            continue;
                        }
                        if (line.find(kPropertyMacro) != std::string::npos)
                        {
                            has_property_marked = true;
                            ParserMeta(line, pre_prop_meta);
                            continue;
                        }
                        if (line.find(kFunctionMacro) != std::string::npos)
                        {
                            has_function_marked = true;
                            is_script_function = false;
                            is_script_property_function = false;
                            ParserScriptFunctionAnnotation(line, is_script_function, is_script_property_function);
                            continue;
                        }
                        if (line.find(kEventMacro) != std::string::npos)
                        {
                            has_event_marked = true;
                            ParserEventAnnotation(line, is_script_event, event_key_index, event_key_name);
                            continue;
                        }
                    }
                }
                file.close();
                if (_classes.empty() && _enums.empty() && _structs.empty()) return;
                if (!is_last_include_generated)
                {
                    Log(std::format("input file {} is not mark as generated, last include line {}: {}",
                                    path.string(),
                                    last_include_line,
                                    last_include_target));
                    return;
                }
                //write gen head file
                Path out_path = out_dir / path.filename().replace_extension(".gen.h");
                std::string unique_def = std::format("__{}__", cur_file_id);
                std::ofstream out_file(out_path);
                if (out_file.is_open())
                {
                    out_file << "//Generated by ahl" << std::endl;
                    out_file << "#ifdef " << unique_def << std::endl;
                    out_file << "#error " << out_path.filename().string() << " already included, missing '#pragma once' in " << path.filename().string() << std::endl;
                    out_file << "#endif " << std::endl;
                    out_file << "#include \"Objects/ReflectTemplate.h\"" << std::endl;
                    out_file << "#define " << unique_def << std::endl;
                    for (const auto &class_info: _classes)
                    {
                        out_file << "//Class " << class_info._name << " begin..........................." << std::endl;
                        out_file << std::format("#define {} \\", class_info._gen_macro_body);
                        // Replace "Person" with the specified className
                        std::string search = "TClass";
                        const std::string get_type_decl_prefix = _is_cur_file_engine_lib && class_info._export_id.empty() ? "AILU_API " : "";
                        size_t pos = 0;
                        std::string generate_body;
                        if (class_info._name == "Object")
                        {
                            generate_body = R"(
                            private: \
                                friend const Type* Z_Construct_TClass_Type();\
                                static const Type* GetPrivateStaticClass();\
                            public:\
                                static const Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual const TGetTypeApiType  *GetType();
                            )";
                        }
                        else if (!class_info._parent.empty())
                        {
                            generate_body = R"(
                            private: \
                                friend const Type* Z_Construct_TClass_Type();\
                                static const Type* GetPrivateStaticClass();\
                            public:\
                                static const Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual const TGetTypeApiType  *GetType() override;
                            )";
                        }
                        else
                        {
                            generate_body = R"(
                            private: \
                                friend const Type* Z_Construct_TClass_Type();\
                                static const Type* GetPrivateStaticClass();\
                            public:\
                                static const Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual const TGetTypeApiType  *GetType();
                            )";
                        }

                        while ((pos = generate_body.find(search, pos)) != std::string::npos)
                        {
                            generate_body.replace(pos, search.length(), class_info._name);
                            pos += class_info._name.length();
                        }
                        Replace(generate_body, "TGetTypeApi", get_type_decl_prefix);
                        out_file << generate_body;
                        out_file << "namespace Ailu {class Type;}" << std::endl;
                        out_file << "namespace " << class_info._namespace << "{" << std::endl;
                        out_file << "class " << class_info._name << " ;" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = class_info._namespace + "::" + class_info._name;
                        if (_is_cur_file_engine_lib)
                            out_file << std::format("AILU_API const class Ailu::Type* Ailu::StaticClass<class {}>();", full_name) << std::endl;
                        else
                            out_file << std::format("const class Ailu::Type* Ailu::StaticClass<class {}>();", full_name) << std::endl;
                            
                        out_file << "//Class " << class_info._name << " end..........................." << std::endl;
                        out_file << std::endl;
                    }
                    for (const auto &struct_info: _structs)
                    {
                        out_file << "//Struct " << struct_info._name << " begin..........................." << std::endl;
                        out_file << std::format("#define {} \\", struct_info._gen_macro_body);
                        std::string search = "TClass";
                        const std::string get_type_decl_prefix = _is_cur_file_engine_lib && struct_info._export_id.empty() ? "AILU_API " : "";
                        size_t pos = 0;
                        std::string generate_body = R"(
                            private: \
                                friend const Type* Z_Construct_TClass_Type();\
                                static const Type* GetPrivateStaticClass();\
                            public:\
                                static const Type *StaticType() {return GetPrivateStaticClass();};\
                                const TGetTypeApiType  *GetType();
                            )";

                        while ((pos = generate_body.find(search, pos)) != std::string::npos)
                        {
                            generate_body.replace(pos, search.length(), struct_info._name);
                            pos += struct_info._name.length();
                        }
                        Replace(generate_body, "TGetTypeApi", get_type_decl_prefix);
                        out_file << generate_body;
                        out_file << "namespace Ailu {class Type;}" << std::endl;
                        out_file << "namespace " << struct_info._namespace << "{" << std::endl;
                        out_file << "struct " << struct_info._name << " ;" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = struct_info._namespace + "::" + struct_info._name;
                        if (_is_cur_file_engine_lib)
                            out_file << std::format("AILU_API const class Ailu::Type* Ailu::StaticClass<struct {}>();", full_name) << std::endl;
                        else
                            out_file << std::format("const class Ailu::Type* Ailu::StaticClass<struct {}>();", full_name) << std::endl;
                        out_file << "//Struct " << struct_info._name << " end..........................." << std::endl;
                        out_file << std::endl;
                    }
                    for (auto &enum_info: _enums)
                    {
                        out_file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
                        std::string func_name = std::format("const Ailu::Enum* {}()", EnumConstructorName(enum_info));
                        out_file << func_name << ";" << std::endl;
                        out_file << "namespace " << enum_info._namespace << " {" << std::endl;
                        out_file << enum_info._decl_type << " " << enum_info._name << " : " << enum_info._underlying_type << ";" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = enum_info._namespace + "::" + enum_info._name;
                        if (_is_cur_file_engine_lib)
                            out_file << std::format("AILU_API const Ailu::Enum* Ailu::StaticEnum<{}>();", full_name) << std::endl;
                        else
                            out_file << std::format("const Ailu::Enum* Ailu::StaticEnum<{}>();", full_name) << std::endl;
                        //out_file << " return " << std::format("Z_Construct_Enum_{}_Type()", enum_info._name) << ";" << std::endl;
                        //out_file << "}" << std::endl;
                        out_file << "//Enum " << enum_info._name << " end..........................." << std::endl;
                        out_file << std::endl;
                    }
                    out_file << "#undef CURRENT_FILE_ID" << std::endl;
                    out_file << "#define CURRENT_FILE_ID " << cur_file_id << std::endl;
                    out_file.close();
                    Log(std::format("AiluHeadTool::Parser {} with create gen.h file success", out_path.string()));
                }
                else { Log(std::format("AiluHeadTool::Parser {} with create gen.h file failed", out_path.string())); }
                //write gen cpp file
                Path cpp_path = out_dir / path.filename().replace_extension(".gen.cpp");
                std::ofstream cpp_file(cpp_path);
                if (cpp_file.is_open())
                {
                    cpp_file << "//Generated by ahl" << std::endl;
                    //cpp_file << "#include \"" << out_path.filename().string() <<"\""<< std::endl;
                    cpp_file << "#include \"../" << path.filename().string() << "\"" << std::endl;
                    for (auto &inc: s_common_src_dep_file) cpp_file << "#include " << inc << std::endl;
                    cpp_file << "using namespace " << work_namespace << ";" << std::endl;
                    for (const auto &class_info: _classes)
                    {
                        GenerateClassTypeInfo(class_info, cpp_file, _class_ns_map);
                    }
                    for (const auto &struct_info: _structs)
                        GenerateClassTypeInfo(struct_info, cpp_file, _class_ns_map);
                    for (auto &enum_info: _enums)
                        GenerateEnumTypeInfo(enum_info, cpp_file);
                    if (path.filename().string().starts_with("Script") && path.filename() != "ScriptSystem.h")
                    {
                        std::vector<ClassInfo> script_types = _classes;
                        script_types.insert(script_types.end(), _structs.begin(), _structs.end());
                        const std::string file_stem = path.stem().string();
                        GenerateLuaBindings(script_types, _enums, "RegisterGeneratedLuaBindings_" + file_stem, cpp_file,
                                            _script_api_types);
                        const Path project_dir = out_dir.parent_path().parent_path().parent_path().parent_path().parent_path();
                        GenerateLuaDeclarations(script_types, _enums,
                                                project_dir / ".ailu" / "lua" / (ToLower(file_stem) + ".lua"));
                        GenerateLuaDeclarationIndex(project_dir / ".ailu" / "lua" / "ailu_engine.lua");
                        GenerateLuaWorkspaceConfig(project_dir / ".luarc.json");
                    }
                    cpp_file.close();
                    Log(std::format("AiluHeadTool::Parser create file {} succeed!", cpp_path.string()));
                    std::string all_classes, all_structs, all_enums;
                    for (const auto &class_info: _classes)
                    {
                        all_classes += class_info._name + ",";
                    }
                    for (const auto &struct_info: _structs)
                    {
                        all_structs += struct_info._name + ",";
                    }
                    for (const auto &enum_info: _enums)
                    {
                        all_enums += enum_info._name + ",";
                    }
                    Log(std::format("AiluHeadTool::Parser file {}(is engine lib: {}) success({}ms) with result:"
                                    "       class({}):{}"
                                    "       struct({}):{}"
                                    "       enum({}):{}",
                                    path.string(), BOOL_STR(_is_cur_file_engine_lib), g_Timer.ElapsedMilliseconds(), _classes.size(), all_classes, _structs.size(), all_structs, _enums.size(), all_enums));
                    g_Timer.stop();
                    g_Timer.reset();
                    return;
                }
                else
                {
                    Log(std::format("AiluHeadTool::Parser {} with create gen.cpp file failed", cpp_path.string()));
                    g_Timer.reset();
                }
            }
        }
    }
    catch (const std::exception & e) 
    {
        Log(std::format("AiluHeadTool::Parser {} with exception: {}", path.string(), e.what()));
    }
}

void AiluHeadTool::SaveClassNamespaceMap(const std::unordered_map<std::string, std::set<std::string>> &map, const std::string &filename)
{
    std::ofstream ofs(filename);
    if (!ofs.is_open())
    {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }

    for (const auto &[className, namespaces]: map)
    {
        for (const auto &ns: namespaces)
        {
            ofs << className << "|" << ns << "\n";
        }
    }
}

std::unordered_map<std::string, std::set<std::string>> AiluHeadTool::LoadClassNamespaceMap(const std::string &filename)
{
    std::unordered_map<std::string, std::set<std::string>> result;
    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        throw std::runtime_error("Failed to open file for reading: " + filename);
    }

    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.empty()) continue;

        size_t sep = line.find('|');
        if (sep == std::string::npos) continue;// 跳过无效行

        std::string className = line.substr(0, sep);
        std::string ns = line.substr(sep + 1);
        result[className].insert(ns);
    }

    return result;
}

void AiluHeadTool::FindBaseClass(ClassInfo &info)
{
    if (info._parent.empty()) 
        return;
    static auto GetParentNamespace = [](const std::string &ns) -> std::string
    {
        size_t pos = ns.rfind("::");
        if (pos == std::string::npos) return "";
        return ns.substr(0, pos);
    };
    auto it = _class_ns_map.find(info._parent);
    if (it == _class_ns_map.end())
    {
        info._parent = "";// 没找到
        Log(std::format("Error: class: {} not found in class_ns map", info._parent));
        return;
    }
    const auto &candidates = it->second;// std::set<std::string>（候选命名空间集合）
    // 1. 当前 namespace 完全匹配
    if (!info._namespace.empty())
    {
        std::string full = info._namespace + "::" + info._parent;
        if (candidates.count(info._namespace))
        {
            info._parent = full;
            return;
        }
    }
    // 2. 父 namespace 逐层回退
    std::string ns = info._namespace;
    while (!ns.empty())
    {
        ns = GetParentNamespace(ns);
        if (!ns.empty())
        {
            std::string full = ns + "::" + info._parent;
            if (candidates.count(ns))
            {
                info._parent = full;
                return;
            }
        }
    }
    // 3. 全局唯一
    if (candidates.size() == 1)
    {
        info._parent = *candidates.begin();
        return;
    }
    Log(std::format("Warning: class: {} in multi namespace!", info._parent));
    // 4. 无法确定
    info._parent.clear();
}

void AiluHeadTool::Log(const std::string &msg)
{
    std::lock_guard<std::mutex> l(_log_mutex);
    _log_ss << msg << std::endl;
    std::cout << msg << std::endl;
}

void AiluHeadTool::ColloctClassNamespace(std::set<fs::path> inc_files, Path p)
{
    _class_ns_map = std::move(LoadClassNamespaceMap(p.string()));
    for (auto &p: inc_files)
    {
        std::ifstream file(p);
        if (!file.is_open())
        {
            Log(std::format("AiluHeadTool::ColloctClassNamespace {} with result open file failed,skip it", p.string()));
            continue;
        }
        std::string line;
        _tracker.Clean();
        std::regex classRegex(R"(^\s*(class|struct)\s+([_A-Za-z]\w*)(?:\s*[:{]|$))");
        std::regex classWithMacroRegex(R"(^\s*(class|struct)\s+[A-Z0-9_]+\s+([_A-Za-z]\w*)(?:\s*[:{]|$))");
        std::regex enumRegex(R"(^\s*enum\s+(?:class\s+)?([_A-Za-z]\w*)(?:\s*[:{]|$))");
        while (std::getline(file, line))
        {
            auto &&cur_namespace = _tracker.ProcessLine(line);
            std::smatch match;
            if (std::regex_search(line, match, classRegex) ||
                std::regex_search(line, match, classWithMacroRegex))
            {
                std::string class_name = match[2].str();
                std::string full_name = cur_namespace.empty()
                                               ? class_name
                                               : cur_namespace + "::" + class_name;
                if (!_class_ns_map.contains(class_name))
                {
                    _class_ns_map[class_name] = {};
                }
                _class_ns_map[class_name].insert(full_name);
                Log(std::format("Found class {} in namespace {}", class_name, cur_namespace));
            }
            else if (std::regex_search(line, match, enumRegex))
            {
                std::string enum_name = match[1].str();
                std::string full_name = cur_namespace.empty()
                                               ? enum_name
                                               : cur_namespace + "::" + enum_name;
                if (!_class_ns_map.contains(enum_name))
                {
                    _class_ns_map[enum_name] = {};
                }
                _class_ns_map[enum_name].insert(full_name);
                Log(std::format("Found enum {} in namespace {}", enum_name, cur_namespace));
            }
        }
    }
    SaveClassNamespaceMap(_class_ns_map, p.string());
}

void AiluHeadTool::SaveLog(const Path &out_dir)
{
    std::lock_guard<std::mutex> l(_log_mutex);
    std::ofstream log_file(out_dir / "aht_log.txt");
    if (log_file.is_open())
    {
        log_file << _log_ss.str();
        log_file.close();
    }
}
