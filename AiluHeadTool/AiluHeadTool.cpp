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
        info._is_struct = false;
    }
    else { aht.Log(std::format("ParserClassInfo failed with line: {}", line)); }
}

static void ParserStructInfo(const std::string &line, AiluHeadTool::ClassInfo &info, AiluHeadTool &aht)
{
    std::regex pattern(R"(struct\s+((\w*_API)\s+)?(\w+)(?:\s*:\s*public\s+(\w+))?)");
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
        info._is_struct = true;
    }
    else { aht.Log(std::format("ParserClassInfo failed with line: {}", line)); }
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
        else { id = (uint32_t) info._members.size(); }
        info._members.emplace_back(value);
        begin = matches.suffix().first;
        ++parser_num;
    }
    if (parser_num == 0) { aht.Log(std::format("ParserEnumValues failed with line: {}", line)); }
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
    while (std::getline(stream, param, ',')) { result.push_back(param.substr(0, param.find_first_of(' '))); }
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
        if (line.find("final") != std::string::npos || line.find("override") != std::string::npos) { info._is_virtual = true; }
        info._return_type = matches[3].str();
        info._name = matches[4].str();
        info._params = SplitParams(matches[5].str());
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
        }
    }
}

#define BOOL_STR(x) x ? "true" : "false"

static void GenerateClassTypeInfo(const AiluHeadTool::ClassInfo &class_info, std::ofstream &file)
{
    using std::endl;
    std::string full_name = class_info._namespace + "::" + class_info._name;
    file << std::format("Ailu::Type* {}::Z_Construct_{}_Type()", class_info._namespace, class_info._name) << std::endl;
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

    for (auto &mem: class_info._members)
    {
        if (mem._is_function)
        {
            auto s = std::format(R"(initializer._functions.emplace_back(MemberInfoInitializer(EMemberType::kFunction, "{}", "{}", {}, {}, {}, &{}::{}));)",
                                 mem._name, ConstructFuncType(mem._return_type, mem._params, mem._is_const), BOOL_STR(mem._is_static), BOOL_STR(mem._is_public), mem._offset, class_info._name, mem._name);

            std::string cur_meta = "meta" + mem._name;
            file << "Meta " << cur_meta << ";" << std::endl;
            auto bd_name = "builder" + mem._name;
            file << std::format("MemberBuilder {};", bd_name) << std::endl;
            file << std::format("{}._name = {};", bd_name, mem._name) << std::endl;
            file << std::format("{}._type_name = {};", bd_name, ConstructFuncType(mem._return_type, mem._params, mem._is_const)) << std::endl;
            file << std::format("{}._offset = offsetof({},{});", bd_name, class_info._name, mem._name) << std::endl;
            file << std::format("{}._is_const = {};", bd_name, BOOL_STR(mem._is_const)) << std::endl;
            file << std::format("{}._is_static = {};", bd_name, BOOL_STR(mem._is_static)) << std::endl;
            file << std::format("{}._is_public = {};", bd_name, BOOL_STR(mem._is_public)) << std::endl;
            file << std::format("{}._ret_type_name = {};", bd_name, mem._return_type) << std::endl;
            file << std::format("{}._meta = {};", bd_name, cur_meta) << std::endl;
            file << std::format("{}._member_ptr = &{}::{};", bd_name, class_info._name, mem._name) << std::endl;
            //file << std::format("{}._accessor = MakeScope<OffsetPropertyAccessor>({}._offset);", bd_name) << std::endl;
            file << std::format("initializer._functions.emplace_back(MemberBuilder::BuildFunction({}));", bd_name) << std::endl;
            file << s << std::endl;
        }
        else
        {
            std::string cur_meta = "meta" + mem._name;
            file << "Meta " << cur_meta << ";" << std::endl;
            file << std::format("{}.Set(\"Category\",\"{}\");", cur_meta, mem._meta._category) << std::endl;
            file << std::format("{}.Set(\"IsColor\",{});", cur_meta, BOOL_STR(mem._meta._is_color)) << std::endl;
            file << std::format("{}.Set(\"IsRange\",{});", cur_meta, BOOL_STR(mem._meta._is_range)) << std::endl;
            file << std::format("{}.Set(\"IsFloatRange\",{});", cur_meta, BOOL_STR(mem._meta._is_float_range)) << std::endl;
            if (mem._meta._is_float_range)
            {
                file << std::format("{}.Set(\"RangeMin\",{});", cur_meta, mem._meta._min) << std::endl;
                file << std::format("{}.Set(\"RangeMax\",{});", cur_meta, mem._meta._max) << std::endl;
            }
            else
            {
                file << std::format("{}.Set(\"RangeMin\",(i32){});", cur_meta, mem._meta._min)<< std::endl;
                file << std::format("{}.Set(\"RangeMax\",(i32){});", cur_meta, mem._meta._max)<< std::endl;
            }
            auto bd_name = "builder" + mem._name;
            file << std::format("MemberBuilder {};", bd_name) << std::endl;
            file << std::format("{}._name = \"{}\";", bd_name,mem._name) << std::endl;
            file << std::format("{}._type_name = \"{}\";", bd_name,mem._type) << std::endl;
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
    file << std::format("Ailu::Type* {}::GetPrivateStaticClass()", full_name) << std::endl;
    file << "{" << std::endl;
    file << std::format("\tstatic Ailu::Type* type = Z_Construct_{}_Type();", class_info._name) << std::endl;
    file << std::format("\treturn type;") << std::endl;
    file << "}" << std::endl;
    file << std::endl;
    file << std::format("template<> Ailu::Type* Ailu::StaticClass<{}::{}>()", class_info._namespace, class_info._name) << std::endl;
    file << "{" << std::endl;
    file << "return " << std::format("{}::StaticType();", full_name) << std::endl;
    file << "}" << std::endl;
    if (!class_info._is_struct)
    {
        file << std::format("    Type *{}::GetType()", full_name) << std::endl;
        file << "{" << std::endl;
        file << "return " << std::format("{}::GetPrivateStaticClass();", full_name) << std::endl;
        file << "}" << std::endl;
    }
    //ClassTypeRegister s_register_object(&Ailu::Object::StaticType, "Ailu::Object");
    file << std::format("ClassTypeRegister s_register_{}(&{}::StaticType, \"{}\");", class_info._name, full_name, full_name) << std::endl;
}

static void GenerateEnumTypeInfo(const AiluHeadTool::EnumInfo &enum_info, std::ofstream &file)
{
    std::string type_ins_name = std::format("s_enum_type_{}", enum_info._name);
    file << std::format("static std::unique_ptr<Ailu::Enum> {} = nullptr;", type_ins_name) << std::endl;
    std::string construct_enum_func = std::format("Z_Construct_Enum_{}_Type", enum_info._name);
    file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
    file << std::format("const Ailu::Enum* Z_Construct_Enum_{}_Type()", enum_info._name) << std::endl;
    file << "{" << std::endl;
    file << std::format("if({} == nullptr)", type_ins_name) << std::endl;
    file << "{" << std::endl;
    file << "EnumInitializer initializer;" << std::endl;
    file << std::format("initializer._name = \"{}\"", enum_info._name) << ";" << std::endl;
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
                std::string cur_file_id = path.stem().string();
                cur_file_id.append("_GEN_H");
                std::transform(cur_file_id.begin(), cur_file_id.end(), cur_file_id.begin(), ::toupper);
                std::string line, last_line;
                bool is_include_start = false, is_include_end = false;
                bool has_class_marked = false, has_property_marked = false, has_function_marked = false, has_enum_marked = false, has_struct_marked = false;
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
                    if (line.find("#include") != std::string::npos) { is_include_start = true; }
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
                            }
                            if (!enum_info._members.empty()) _enums.emplace_back(enum_info);
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
                            class_info._namespace = cur_namespace;
                            has_class_marked = false;
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
                            ParserStructInfo(line, struct_info, *this);
                            struct_info._namespace = cur_namespace;
                            has_struct_marked = false;
                            is_process_class = false;
                            _structs.emplace_back(struct_info);
                            continue;
                        }
                    }
                    //检查标记
                    if (line.find(kClassMacro) != std::string::npos)
                    {
                        has_class_marked = true;
                        continue;
                    }
                    if (line.find(kStructMacro) != std::string::npos)
                    {
                        has_struct_marked = true;
                        continue;
                    }
                    if (line.find(kEnumMacro) != std::string::npos)
                    {
                        has_enum_marked = true;
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
                            has_function_marked = false;
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
                            continue;
                        }
                    }
                }
                file.close();
                if (_classes.empty() && _enums.empty() && _structs.empty()) return;
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
                        bool is_base_object = class_info._name == "Object";
                        size_t pos = 0;
                        std::string generate_body;
                        if (is_base_object)
                        {
                            generate_body = R"(
                            private: \
                                friend Type* Z_Construct_TClass_Type();\
                                static Type* GetPrivateStaticClass();\
                            public:\
                                static Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual Type  *GetType();
                            )";
                        }
                        else
                        {
                            generate_body = R"(
                            private: \
                                friend Type* Z_Construct_TClass_Type();\
                                static Type* GetPrivateStaticClass();\
                            public:\
                                static Type *StaticType() {return GetPrivateStaticClass();};\
                                virtual Type  *GetType() override;
                            )";
                        }

                        while ((pos = generate_body.find(search, pos)) != std::string::npos)
                        {
                            generate_body.replace(pos, search.length(), class_info._name);
                            pos += class_info._name.length();
                        }
                        out_file << generate_body;
                        out_file << "namespace Ailu {class Type;}" << std::endl;
                        out_file << "namespace " << class_info._namespace << "{" << std::endl;
                        out_file << "class " << class_info._name << " ;" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = class_info._namespace + "::" + class_info._name;
                        out_file << std::format("AILU_API class Ailu::Type* Ailu::StaticClass<class {}>();", full_name) << std::endl;
                        out_file << "//Class " << class_info._name << " end..........................." << std::endl;
                        out_file << std::endl;
                    }
                    for (const auto &struct_info: _structs)
                    {
                        out_file << "//Struct " << struct_info._name << " begin..........................." << std::endl;
                        out_file << std::format("#define {} \\", struct_info._gen_macro_body);
                        std::string search = "TClass";
                        size_t pos = 0;
                        std::string generate_body = R"(
                            private: \
                                friend Type* Z_Construct_TClass_Type();\
                                static Type* GetPrivateStaticClass();\
                            public:\
                                static Type *StaticType() {return GetPrivateStaticClass();};
                            )";

                        while ((pos = generate_body.find(search, pos)) != std::string::npos)
                        {
                            generate_body.replace(pos, search.length(), struct_info._name);
                            pos += struct_info._name.length();
                        }
                        out_file << generate_body;
                        out_file << "namespace Ailu {class Type;}" << std::endl;
                        out_file << "namespace " << struct_info._namespace << "{" << std::endl;
                        out_file << "struct " << struct_info._name << " ;" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = struct_info._namespace + "::" + struct_info._name;
                        out_file << std::format("AILU_API class Ailu::Type* Ailu::StaticClass<struct {}>();", full_name) << std::endl;
                        out_file << "//Struct " << struct_info._name << " end..........................." << std::endl;
                        out_file << std::endl;
                    }
                    for (auto &enum_info: _enums)
                    {
                        out_file << "//Enum " << enum_info._name << " begin..........................." << std::endl;
                        std::string func_name = std::format("const Ailu::Enum* Z_Construct_Enum_{}_Type()", enum_info._name);
                        out_file << func_name << ";" << std::endl;
                        out_file << "namespace " << enum_info._namespace << " { " << std::endl;
                        out_file << enum_info._decl_type << " " << enum_info._name << " : " << enum_info._underlying_type << ";" << std::endl;
                        out_file << "}" << std::endl;
                        out_file << "template<>" << std::endl;
                        std::string full_name = enum_info._namespace + "::" + enum_info._name;
                        out_file << std::format("AILU_API const Ailu::Enum* Ailu::StaticEnum<{}>();", full_name) << std::endl;
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
                        GenerateClassTypeInfo(class_info, cpp_file);
                    }
                    for (const auto &struct_info: _structs)
                        GenerateClassTypeInfo(struct_info, cpp_file);
                    for (auto &enum_info: _enums)
                        GenerateEnumTypeInfo(enum_info, cpp_file);
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
                    Log(std::format("AiluHeadTool::Parser file {} success({}ms) with result:"
                                    "       class({}):{}"
                                    "       struct({}):{}"
                                    "       enum({}):{}",
                                    path.string(), g_Timer.ElapsedMilliseconds(), _classes.size(), all_classes, _structs.size(), all_structs, _enums.size(), all_enums));
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
                    _class_ns_map[p.string()] = {};
                }
                _class_ns_map[class_name].insert(full_name);
                Log(std::format("Found class {} in namespace {}", class_name, cur_namespace));
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