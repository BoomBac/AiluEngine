//// AiluHeadTool.cpp : Source file for your target.
////
#include "AiluHeadTool.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include "Timer.h"


static Timer g_Timer;

static void ParserClassInfo(const std::string &line, AiluHeadTool::ClassInfo &info,AiluHeadTool& aht)
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


static void ParserPropertyInfo(const std::string &line, AiluHeadTool::MemberInfo &info,AiluHeadTool& aht)
{
    std::regex pattern(R"((static\s+)?([\w\*]+(?:\s*::\s*\w+)?)\s+(\w+)\s*(=\s*[^;]+)?;)");
    std::smatch matches;
    if (std::regex_search(line, matches, pattern))
    {
        info._is_static = matches[1].matched;
        info._type = matches[2].str();
        info._name = matches[3].str();
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

static void ParserFunctionInfo(const std::string &line, AiluHeadTool::MemberInfo &info,AiluHeadTool& aht)
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


void AiluHeadTool::Parser(const Path &path, const Path &out_dir,std::string work_namespace)
{
    g_Timer.start();
    _classes.clear();
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
            bool has_class_marked = false, has_property_marked = false, has_function_marked = false;
            bool is_cur_access_scope_public = false;
            int line_count = 0;
            while (std::getline(file, line))
            {
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
                //提取信息
                if (has_class_marked)
                {
                    if (line.find("class") != std::string::npos)
                    {
                        ClassInfo class_info;
                        ParserClassInfo(line, class_info,*this);
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
                        ParserPropertyInfo(line, member_info,*this);
                        member_info._is_public = is_cur_access_scope_public;
                        has_property_marked = false;
                        _classes.back()._members.emplace_back(member_info);
                        continue;
                    }
                    if (has_function_marked)
                    {
                        MemberInfo member_info;
                        ParserFunctionInfo(line, member_info,*this);
                        member_info._is_public = is_cur_access_scope_public;
                        member_info._is_function = true;
                        has_function_marked = false;
                        _classes.back()._members.emplace_back(member_info);
                        continue;
                    }
                    if (line.find(kPropertyMacro) != std::string::npos)
                    {
                        has_property_marked = true;
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
            if (_classes.empty())
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
                out_file << "#endif "<< std::endl;
                out_file << "#define " << unique_def << std::endl;
                for (auto &class_info: _classes)
                {
                    out_file <<"//Class " << class_info._name <<" begin..........................."<< std::endl;
                    out_file << std::format("#define {} \\", class_info._gen_macro_body);
                    // Replace "Person" with the specified className
                    std::string search = "TClass";
                    size_t pos = 0;
//                    /** Private move- and copy-constructors, should never be used */ \
//                    TClass(TClass&&) = delete;                                     \
//                    TClass(const TClass &) = delete;
                    std::string generate_body = R"(
private: \
    friend Ailu::Type* Z_Construct_TClass_Type();\
    static Ailu::Type* GetPrivateStaticClass();\
public:\
    static class Ailu::Type *StaticType() {return GetPrivateStaticClass();};
)";
                    while ((pos = generate_body.find(search, pos)) != std::string::npos)
                    {
                        generate_body.replace(pos, search.length(), class_info._name);
                        pos += class_info._name.length();
                    }
                    out_file << generate_body;
                    out_file <<"//Class " << class_info._name <<" end..........................."<< std::endl;
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
                for(auto& inc : s_common_src_dep_file)
                    cpp_file << "#include "<<inc << std::endl;
                cpp_file <<"using namespace " << work_namespace <<";"<< std::endl;
                for (auto &class_info: _classes)
                {
                    cpp_file << std::format("Ailu::Type* Ailu::Z_Construct_{}_Type()", class_info._name) << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << std::format("static std::unique_ptr<Ailu::Type> cur_type = nullptr;") << std::endl;
                    cpp_file << std::format("if(cur_type == nullptr)") << std::endl;
                    cpp_file << "{" << std::endl;
                    cpp_file << "TypeInitializer initializer;" << endl;
                    cpp_file << std::format("initializer._name = \"{}\"",class_info._name) << ";"<<endl;
                    cpp_file << std::format("initializer._size = sizeof({});", class_info._name)<<endl;
                    cpp_file << std::format("initializer._full_name = \"{}\"",work_namespace+"::"+ class_info._name) << ";"<<endl;
                    cpp_file << std::format("initializer._is_class = true;") << endl;
                    cpp_file << std::format("initializer._is_abstract = {}",BOOL_STR(class_info._is_abstract)) << ";"<<endl;
                    cpp_file << std::format("initializer._namespace = \"{}\"",work_namespace) << ";"<<endl;
                    for(auto& mem : class_info._members)
                    {
                        if(mem._is_function)
                        {
                            auto s = std::format(R"(initializer._functions.emplace_back(MemberInfoInitializer(EMemberType::kFunction, "{}", "{}", {}, {}, {}, &{}::{}));)",
                                                 mem._name, ConstructFuncType(mem._return_type,mem._params,mem._is_const), BOOL_STR(mem._is_static), BOOL_STR(mem._is_public), mem._offset, class_info._name, mem._name);
                            cpp_file << s << std::endl;
                        }
                        else
                        {
                            auto s = std::format(R"(initializer._properties.emplace_back(MemberInfoInitializer(EMemberType::kProperty,"{}","{}", {}, {},{},&{}::{}));)",
                                                 mem._name,mem._type, BOOL_STR(mem._is_static), BOOL_STR(mem._is_public), mem._offset, class_info._name, mem._name);
                            cpp_file << s << std::endl;
                        }
                    }
                    cpp_file << "cur_type = std::make_unique<Ailu::Type>(initializer);" << std::endl;
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
                }
                cpp_file.close();
                Log(std::format("AiluHeadTool::Parser create file {} succeed!", cpp_path.string()));
                g_Timer.stop();
                Log(std::format("AiluHeadTool::Parser file {} success with {} class after {}ms", path.string(),_classes.size(), g_Timer.ElapsedMilliseconds()));
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
