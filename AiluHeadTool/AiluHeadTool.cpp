//// AiluHeadTool.cpp : Source file for your target.
////
#include "AiluHeadTool.h"
#include "Timer.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>


static Timer g_Timer;

static void ParserClassInfo(const std::string &line, AiluHeadTool::ClassInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"(class\s+((\w*_API)\s+)?(\w+)(?:\s*:\s*public\s+(\w+))?)");
    std::smatch matches;
    if (std::regex_search(line, matches, pattern))
    {
        if (matches[2].matched)
        {
            info._export_id = matches[2].str();// 导出符号（如 EXPORT_API）
        }
        else
        {
            info._export_id = "";// 如果没有导出符号，设置为空字符串
        }
        info._name = matches[3].str();// 类名
        if (matches[4].matched)
        {
            info._parent = matches[4].str();// 父类名
        }
        else
        {
            info._parent = "";// 如果没有父类，设置为空字符串
        }
        info.is_export = !info._export_id.empty();
    }
    else
    {
        aht.Log(std::format("ParserClassInfo failed with line: {}", line));
    }
}
static void ParserEnumInfo(const std::string &line, AiluHeadTool::EnumInfo &info, AiluHeadTool &aht)
{
    // 正则表达式解释：
    // - `enum\s+`：匹配 "enum " 或 "enum class "
    // - `(?:class\s+)?`：可选的 "class" 关键字
    // - `(\w+)`：匹配枚举的名称
    // - `(?:\s*:\s*(\w+))?`：匹配可选的继承类型，如 `: uint32_t`
    std::regex pattern(R"(enum\s+(class\s+)?(\w+)(?:\s*:\s*(\w+))?)");
    std::smatch matches;

    if (std::regex_search(line, matches, pattern))
    {
        info._is_enum_class = matches[1].matched;// 是否为 enum class
        info._name = matches[2].str();           // 枚举名称

        if (matches[3].matched)
        {
            info._underlying_type = matches[3].str();// 枚举的底层类型
        }
        else
        {
            info._underlying_type = "";// 如果没有底层类型，设置为空字符串
        }
    }
    else
    {
        aht.Log(std::format("ParserEnumInfo failed with line: {}", line));
    }
}
static void ParserEnumClass(const std::string &line, AiluHeadTool::EnumInfo &info, AiluHeadTool &aht)
{
    std::regex enumRegex(
            R"((?:(\b_API\(.*?\)\b)\s*)?enum\s+(?:class|struct)?\s*(\w+)?(?:\s*:\s*\w+)?)");

    std::smatch match;
    std::string::const_iterator searchStart(line.cbegin());

    while (std::regex_search(searchStart, line.cend(), match, enumRegex))
    {
        if (match[2].matched)
            info._name = match[2].str();
        info._is_enum_class = line.find("class") != std::string::npos;
        searchStart = match.suffix().first;
    }
}

static void ParserEnumValues(const std::string &line, AiluHeadTool::EnumInfo &info, AiluHeadTool &aht)
{
    // 正则表达式解释：
    // - `(\w+)`：匹配枚举项的名称
    // - `(?:\s*=\s*(-?\d+))?`：可选的赋值操作，如 `= 1`，支持负数
    // - `(?:\s*,\s*)?`：可选的逗号和空格，用于分隔多个枚举项
    std::regex pattern(R"((\w+)(?:\s*=\s*(-?\d+))?(?:\s*,\s*)?)");
    std::smatch matches;

    auto begin = line.cbegin();
    auto end = line.cend();
    int parser_num = 0;
    // 逐个解析枚举项
    while (std::regex_search(begin, end, matches, pattern))
    {
        std::pair<std::string, uint32_t> value;
        auto &[name, id] = value;
        name = matches[1].str();// 获取枚举项名称

        if (matches[2].matched)
        {
            id = std::stoi(matches[2].str());// 解析赋值
        }
        else
        {
            id = (uint32_t)info._members.size();
        }
        info._members.emplace_back(value);
        begin = matches.suffix().first;
        ++parser_num;
    }
    if (parser_num == 0)
    {
        aht.Log(std::format("ParserEnumValues failed with line: {}", line));
    }
}


static void ParserPropertyInfo(const std::string &line, AiluHeadTool::MemberInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"((static\s+)?([\w\*]+(?:\s*::\s*\w+)?)\s+(\w+)\s*(=\s*[^;]+)?;)");
    std::smatch matches;
    if (std::regex_search(line, matches, pattern))
    {
        info._is_static = matches[1].matched;
        info._type = matches[2].str();
        info._name = matches[3].str();
        info._is_enum = info._type[0] == 'E' && std::isupper(info._type[1]);
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
        result.push_back(param.substr(0, param.find_first_of(' ')));
    }
    return result;
}

static void ParserFunctionInfo(const std::string &line, AiluHeadTool::MemberInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"((static\s+)?(virtual\s+)?([\w:]+(?:\s*\*|\s*&|\s*::\s*\w+)*)\s+(\w+)\(([^)]*)\)\s*(const)?)");
    std::smatch matches;

    if (std::regex_search(line, matches, pattern))
    {
        info._is_static = matches[1].matched;
        info._is_virtual = matches[2].matched;
        if (line.find("final") != std::string::npos || line.find("override") != std::string::npos)
        {
            info._is_virtual = true;
        }
        info._return_type = matches[3].str();
        info._name = matches[4].str();
        info._params = SplitParams(matches[5].str());
        info._is_const = matches[6].matched;
        info._is_function = true;
    }
    else
    {
        aht.Log(std::format("ParserFunctionInfo failed with line: {}", line));
    }
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
        if (i != params.size() - 1)
        {
            func_type += ",";
        }
    }
    func_type += ")";
    if (is_const)
    {
        func_type += " const";
    }
    return func_type;
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
                else
                    rgx = std::regex(R"(Range\((-?\d+),\s*(-?\d+)\))");
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
        }
    }
}

void AiluHeadTool::Parser(const Path &path, const Path &out_dir, std::string work_namespace)
{
    g_Timer.start();
    _classes.clear();
    _enums.clear();
#define BOOL_STR(x) x ? "true" : "false"
    using std::endl;
    if (fs::exists(path))
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            Log(std::format("AiluHeadTool::Parser {} with result open file failed", path.string()));
        }
        else
        {
            std::string cur_file_id = path.stem().string();
            cur_file_id.append("_GEN_H");
            std::transform(cur_file_id.begin(), cur_file_id.end(), cur_file_id.begin(), ::toupper);
            std::string line, last_line;
            bool is_include_start = false, is_include_end = false;
            bool has_class_marked = false, has_property_marked = false, has_function_marked = false, has_enum_marked = false;
            bool is_cur_access_scope_public = false;
            int line_count = 0;
            PropertyMeta pre_prop_meta;
            while (std::getline(file, line))
            {
                if (line.empty())
                {
                    ++line_count;
                    continue;
                }
                if (line.find("#include") != std::string::npos)
                {
                    is_include_start = true;
                }
                else if (is_include_start)
                {
                    if (!is_include_end)
                    {
                        is_include_end = true;
                        if (last_line.find("gen.h") == std::string::npos)
                        {
                            Log(std::format("input file {} is not mark as generated", path.string()));
                            return;
                        }
                    }
                }
                last_line = line;
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
                    if (line.find("enum") != std::string ::npos)
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
                            if (line.find('{') != std::string::npos)
                            {
                                std::getline(file, line);
                                while (line.find('}') == std::string::npos)
                                {
                                    ParserEnumValues(line, enum_info, *this);
                                    std::getline(file, line);
                                }
                            }
                            else
                            {
                                Log("[Error]: the next line of enum define must only have {");
                            }
                        }
                        if (!enum_info._members.empty())
                            _enums.emplace_back(enum_info);
                        has_enum_marked = false;
                    }
                }
                //提取信息
                if (has_class_marked)
                {
                    if (line.find("class") != std::string::npos)
                    {
                        ClassInfo class_info;
                        ParserClassInfo(line, class_info, *this);
                        has_class_marked = false;
                        _classes.emplace_back(class_info);
                        continue;
                    }
                }
                //检查标记
                if (line.find(kClassMacro) != std::string::npos)
                {
                    has_class_marked = true;
                    continue;
                }
                if (line.find(kEnumMacro) != std::string::npos)
                {
                    has_enum_marked = true;
                    continue;
                }
                if (!_classes.empty())
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
                        has_function_marked = false;
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
                        continue;
                    }
                }
            }
            file.close();
            if (_classes.empty() && _enums.empty())
                return;
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
                out_file << "#define " << unique_def << std::endl;
                for (auto &class_info: _classes)
                {
                    out_file << "//Class " << class_info._name << " begin..........................." << std::endl;
                    out_file << std::format("#define {} \\", class_info._gen_macro_body);
                    // Replace "Person" with the specified className
                    std::string search = "TClass";
                    bool is_base_object = class_info._name == "Object";
                    size_t pos = 0;
                    //                    /** Private move- and copy-constructors, should never be used */ \
//                    TClass(TClass&&) = delete;                                     \
//                    TClass(const TClass &) = delete;
                    std::string generate_body;
                    if (is_base_object)
                    {
                        generate_body = R"(
                            private: \
                                friend Ailu::Type* Z_Construct_TClass_Type();\
                                static Ailu::Type* GetPrivateStaticClass();\
                            public:\
                                static class Ailu::Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual const Ailu::Type  *GetType() const;
                            )";
                    }
                    else
                    {
                        generate_body = R"(
                            private: \
                                friend Ailu::Type* Z_Construct_TClass_Type();\
                                static Ailu::Type* GetPrivateStaticClass();\
                            public:\
                                static class Ailu::Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual const Ailu::Type  *GetType() const override;
                            )";
                    }

                    while ((pos = generate_body.find(search, pos)) != std::string::npos)
                    {
                        generate_body.replace(pos, search.length(), class_info._name);
                        pos += class_info._name.length();
                    }
                    out_file << generate_body;
                    out_file << "//Class " << class_info._name << " end..........................." << std::endl;
                    out_file << std::endl;
                }
                for (auto &enum_info: _enums)
                {
                    out_file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
                    out_file << std::format("void Z_Construct_Enum_{}_Type();", enum_info._name) << std::endl;
                    out_file << "//Enum " << enum_info._name << " end..........................." << std::endl;
                    out_file << std::endl;
                }
                out_file << "#undef CURRENT_FILE_ID" << std::endl;
                out_file << "#define CURRENT_FILE_ID " << cur_file_id << std::endl;
                out_file.close();
                Log(std::format("AiluHeadTool::Parser {} with create gen.h file success", out_path.string()));
            }
            else
            {
                Log(std::format("AiluHeadTool::Parser {} with create gen.h file failed", out_path.string()));
            }
            //write gen cpp file
            Path cpp_path = out_dir / path.filename().replace_extension(".gen.cpp");
            std::ofstream cpp_file(cpp_path);
            if (cpp_file.is_open())
            {
                cpp_file << "//Generated by ahl" << std::endl;
                //cpp_file << "#include \"" << out_path.filename().string() <<"\""<< std::endl;
                cpp_file << "#include \"../" << path.filename().string() << "\"" << std::endl;
                for (auto &inc: s_common_src_dep_file)
                    cpp_file << "#include " << inc << std::endl;
                cpp_file << "using namespace " << work_namespace << ";" << std::endl;
                for (auto &class_info: _classes)
                {
                    cpp_file << std::format("Ailu::Type* Ailu::Z_Construct_{}_Type()", class_info._name) << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << std::format("static std::unique_ptr<Ailu::Type> cur_type = nullptr;") << std::endl;
                    cpp_file << std::format("if(cur_type == nullptr)") << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << "TypeInitializer initializer;" << endl;
                    cpp_file << std::format("initializer._name = \"{}\"", class_info._name) << ";" << endl;
                    cpp_file << std::format("initializer._size = sizeof({});", class_info._name) << endl;
                    cpp_file << std::format("initializer._full_name = \"{}\"", work_namespace + "::" + class_info._name) << ";" << endl;
                    cpp_file << std::format("initializer._is_class = true;") << endl;
                    cpp_file << std::format("initializer._is_abstract = {}", BOOL_STR(class_info._is_abstract)) << ";" << endl;
                    cpp_file << std::format("initializer._namespace = \"{}\"", work_namespace) << ";" << endl;
                    cpp_file << std::format("initializer._base_name = \"{}\"", !class_info._parent.empty() ? work_namespace + "::" + class_info._parent : "") << ";" << endl;
                    for (auto &mem: class_info._members)
                    {
                        if (mem._is_function)
                        {
                            auto s = std::format(R"(initializer._functions.emplace_back(MemberInfoInitializer(EMemberType::kFunction, "{}", "{}", {}, {}, {}, &{}::{}));)",
                                                 mem._name, ConstructFuncType(mem._return_type, mem._params, mem._is_const), BOOL_STR(mem._is_static), BOOL_STR(mem._is_public), mem._offset, class_info._name, mem._name);
                            cpp_file << s << std::endl;
                        }
                        else
                        {
                            //cpp_file << "Meta " <<mem._name << "_meta{"<< std::format("{}")<<"};"<<std::endl;
                            std::string cur_meta = "meta" + mem._name;
                            cpp_file << "Meta " << cur_meta << ";" << std::endl;
                            cpp_file << cur_meta << "._category=" << std::format("\"{}\"", mem._meta._category) << ";" << std::endl;
                            cpp_file << cur_meta << "._min=(float)" << mem._meta._min << ";" << std::endl;
                            cpp_file << cur_meta << "._max=(float)" << mem._meta._max << ";" << std::endl;
                            cpp_file << cur_meta << "._is_range=" << (BOOL_STR(mem._meta._is_range)) << ";" << std::endl;
                            cpp_file << cur_meta << "._is_float_range=" << (BOOL_STR(mem._meta._is_float_range)) << ";" << std::endl;
                            cpp_file << cur_meta << "._is_color=" << (BOOL_STR(mem._meta._is_color)) << ";" << std::endl;
                            auto s = std::format(R"(initializer._properties.emplace_back(MemberInfoInitializer(EMemberType::kProperty,"{}","{}", {}, {},{},&{}::{},{},{}));)",
                                                 mem._name, mem._type, BOOL_STR(mem._is_static), BOOL_STR(mem._is_public), std::format("offsetof({},{})", class_info._name, mem._name), class_info._name, mem._name, cur_meta, BOOL_STR(mem._is_const));
                            cpp_file << s << std::endl;
                        }
                    }
                    cpp_file << "cur_type = std::make_unique<Ailu::Type>(initializer);" << std::endl;
                    cpp_file << "Ailu::Type::RegisterType(cur_type.get());" << std::endl;
                    cpp_file << "}" << std::endl;
                    cpp_file << "return cur_type.get();" << std::endl;
                    cpp_file << "}" << std::endl;

                    cpp_file << std::endl;
                    cpp_file << std::format("Ailu::Type* {}::GetPrivateStaticClass()", class_info._name) << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << std::format("\tstatic Ailu::Type* type = Z_Construct_{}_Type();", class_info._name) << std::endl;
                    cpp_file << std::format("\treturn type;") << std::endl;
                    cpp_file << "}" << std::endl;
                    cpp_file << std::endl;
                    cpp_file << std::format("    const Type *{}::GetType() const", class_info._name) << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << "return " << std::format("{}::GetPrivateStaticClass();", class_info._name) << std::endl;
                    cpp_file << "}" << std::endl;
                }
                for (auto &enum_info: _enums)
                {
                    std::string construct_enum_func = std::format("Z_Construct_Enum_{}_Type", enum_info._name);
                    cpp_file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
                    //cpp_file << std::format("static Ailu::EnumTypeRegister g_register_{}({});", enum_info._name, construct_enum_func) << std::endl;
                    cpp_file << std::format("void Z_Construct_Enum_{}_Type()", enum_info._name) << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << std::format("static std::unique_ptr<Ailu::Enum> cur_type = nullptr;") << std::endl;
                    cpp_file << std::format("if(cur_type == nullptr)") << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << "EnumInitializer initializer;" << endl;
                    cpp_file << std::format("initializer._name = \"{}\"", enum_info._name) << ";" << endl;
                    for (auto &mem: enum_info._members)
                    {
                        auto &[name, id] = mem;
                        cpp_file << std::format("initializer._str_to_enum_lut[\"{}\"] = {};", name, id) << std::endl;
                    }
                    cpp_file << "cur_type = std::make_unique<Ailu::Enum>(initializer);" << std::endl;
                    cpp_file << "Ailu::Enum::RegisterEnum(cur_type.get());" << std::endl;
                    cpp_file << "}" << std::endl;
                    cpp_file << "}" << std::endl;
                    cpp_file << "//Enum " << enum_info._name << " end..........................." << std::endl;
                    cpp_file << std::endl;
                }
                cpp_file.close();
                Log(std::format("AiluHeadTool::Parser create file {} succeed!", cpp_path.string()));
                g_Timer.stop();
                Log(std::format("AiluHeadTool::Parser file {} success with {} class and {} enum after {}ms", path.string(), _classes.size(), _enums.size(), g_Timer.ElapsedMilliseconds()));
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

void AiluHeadTool::Log(const std::string &msg)
{
    std::lock_guard<std::mutex> l(_log_mutex);
    _log_ss << msg << std::endl;
    std::cout << msg << std::endl;
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
