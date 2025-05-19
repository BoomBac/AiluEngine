#pragma once
#include <filesystem>
#include <mutex>
#include <vector>

#include "NamespaceTracker.h"

namespace fs = std::filesystem;
using Path = std::filesystem::path;
class AiluHeadTool
{
public:
    void SaveLog(const Path &out_dir);
    void Log(const std::string &msg);
    void Parser(const Path &path, const Path &out_dir, std::string work_namespace = "Ailu");
    static void AddDependencyInc(std::string file) { s_common_src_dep_file.push_back(std::move(file)); };
    struct PropertyMeta
    {
        std::string _category = "";
        float _min = 0.f, _max = 1.0f;
        bool _is_range = false;
        bool _is_float_range = true;
        bool _is_color = false;
        void Reset()
        {
            _category = "";
            _min = 0.f;
            _max = 1.0f;
            _is_range = false;
            _is_float_range = true;
            _is_color = false;
        }
    };

    struct MemberInfo
    {
        std::string _type;
        std::string _name;
        bool _is_static = false;
        bool _is_public = false;
        bool _is_enum = false;
        //function
        bool _is_const = false;
        bool _is_virtual = false;
        bool _is_function = false;
        std::string _return_type;
        std::vector<std::string> _params;
        int _offset = 0u;
        PropertyMeta _meta;
    };
    struct ClassInfo
    {
        std::string _name;
        std::string _parent;
        std::string _export_id;
        std::string _gen_macro_body;
        std::string _namespace;
        bool _is_abstract = false;
        bool is_export;
        std::vector<MemberInfo> _members;
    };
    struct EnumInfo
    {
        std::string _name;
        std::string _underlying_type;
        std::vector<std::tuple<std::string, uint32_t>> _members;
        bool _is_enum_class;
    };

private:
    std::mutex _log_mutex;
    std::stringstream _log_ss;

private:
    inline static std::string kClassMacro = "ACLASS";
    inline static std::string kStructMacro = "ASTRUCT";
    inline static std::string kEnumMacro = "AENUM";
    inline static std::string kBodyMacro = "GENERATED_BODY";
    inline static std::string kPropertyMacro = "APROPERTY";
    inline static std::string kFunctionMacro = "AFUNCTION";
    inline static std::string kClassID = "class";
    inline static std::vector<std::string> s_common_src_dep_file;

private:
    std::vector<ClassInfo> _classes;
    std::vector<EnumInfo> _enums;
    NamespaceTracker _tracker;
};