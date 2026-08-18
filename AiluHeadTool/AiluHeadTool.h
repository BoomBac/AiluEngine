#pragma once
#include <filesystem>
#include <mutex>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "NamespaceTracker.h"

namespace fs = std::filesystem;
using Path = std::filesystem::path;
class AiluHeadTool
{
public:
    void SaveLog(const Path &out_dir);
    void Log(const std::string &msg);
    void ColloctClassNamespace(std::set<fs::path> inc_files,Path p);
    void CollectScriptApiTypes(const std::set<fs::path> &inc_files);
    void Parser(const Path &path, const Path &out_dir, std::string work_namespace = "Ailu");
    void SetFilteredBaseClasses(std::vector<std::string> filtered_base_classes);
    bool IsFilteredBaseClass(const std::string &line) const;
    static void AddDependencyInc(std::string file) { s_common_src_dep_file.push_back(std::move(file)); };
    struct PropertyMeta
    {
        std::string _category = "";
        float _min = 0.f, _max = 1.0f;
        bool _is_range = false;
        bool _is_float_range = true;
        bool _is_color = false;
        bool _is_script = false;
        void Reset()
        {
            _category = "";
            _min = 0.f;
            _max = 1.0f;
            _is_range = false;
            _is_float_range = true;
            _is_color = false;
            _is_script = false;
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
        bool _is_event = false;
        bool _is_script = false;
        bool _is_script_property = false;
        std::string _return_type;
        std::vector<std::string> _params;
        std::vector<std::string> _function_pointer_params;
        std::vector<std::string> _param_names;
        int _event_key_index = -1;
        std::string _event_key_name;
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
        bool _is_script_api = false;
        std::string _script_global_name;
        std::vector<MemberInfo> _members;
    };
    struct EnumInfo
    {
        std::string _name;
        std::string _underlying_type;
        std::string _decl_type;
        std::string _namespace;
        std::vector<std::tuple<std::string, uint32_t>> _members;
        bool _is_enum_class = false;
        bool _is_script = false;
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
    inline static std::string kEventMacro = "AEVENT";
    inline static std::string kClassID = "class";
    inline static std::string kClassNsCachePath = "AHT/class_ns_map.txt";
    inline static std::vector<std::string> s_common_src_dep_file;
    std::mutex _log_mutex;
    std::stringstream _log_ss;
    std::vector<ClassInfo> _classes;
    std::vector<ClassInfo> _structs;
    std::vector<EnumInfo> _enums;
    std::vector<std::string> _filtered_base_classes;
    NamespaceTracker _tracker;
    std::unordered_map<std::string, std::set<std::string>> _class_ns_map;
    std::unordered_set<std::string> _script_api_types;
    bool _is_cur_file_engine_lib = true;
};
