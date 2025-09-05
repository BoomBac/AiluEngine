#pragma once
#include <filesystem>
#include <mutex>
#include <vector>
#include <set>
#include <unordered_map>

#include "NamespaceTracker.h"

namespace fs = std::filesystem;
using Path = std::filesystem::path;
class AiluHeadTool
{
public:
    void SaveLog(const Path &out_dir);
    void Log(const std::string &msg);
    void ColloctClassNamespace(std::set<fs::path> inc_files,Path p);
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
        bool _is_pointer = false;
        bool _is_reference = false;
        bool _is_template = false;
        bool _is_primitive = false;
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
        bool _is_struct = false;
        std::vector<MemberInfo> _members;
    };
    struct EnumInfo
    {
        std::string _name;
        std::string _underlying_type;
        std::string _decl_type;
        std::string _namespace;
        std::vector<std::tuple<std::string, int>> _members;
        bool _is_enum_class;
    };
private:
    void SaveClassNamespaceMap(const std::unordered_map<std::string, std::set<std::string>> &map, const std::string &filename);
    std::unordered_map<std::string, std::set<std::string>> LoadClassNamespaceMap(const std::string &filename);
    void FindBaseClass(ClassInfo &info);

private:
    inline static std::string kClassMacro = "ACLASS";
    inline static std::string kStructMacro = "ASTRUCT";
    inline static std::string kEnumMacro = "AENUM";
    inline static std::string kBodyMacro = "GENERATED_BODY";
    inline static std::string kStructBodyMacro = "GENERATED_BODY";
    inline static std::string kPropertyMacro = "APROPERTY";
    inline static std::string kFunctionMacro = "AFUNCTION";
    inline static std::string kClassID = "class";
    inline static std::string kClassNsCachePath = "AHT/class_ns_map.txt";
    inline static std::vector<std::string> s_common_src_dep_file;
    std::mutex _log_mutex;
    std::stringstream _log_ss;
    std::vector<ClassInfo> _classes;
    std::vector<ClassInfo> _structs;
    std::vector<EnumInfo> _enums;
    NamespaceTracker _tracker;
    std::unordered_map<std::string, std::set<std::string>> _class_ns_map;
    bool _is_cur_file_engine_lib = true;
};