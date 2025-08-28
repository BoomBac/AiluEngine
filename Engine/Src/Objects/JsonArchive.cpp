#include "Framework/Common/Log.h"
#include "Objects/JsonArchive.h"

#include <Ext/rapidjson/inc/document.h>
#include <Ext/rapidjson/inc/filereadstream.h>
#include <Ext/rapidjson/inc/filewritestream.h>
#include <Ext/rapidjson/inc/prettywriter.h>
#include <Ext/rapidjson/inc/stringbuffer.h>
#include <cstdio>

namespace Ailu
{
    // 工具函数：拆分路径 "a.b.c" -> {"a", "b", "c"}
    static Vector<String> SplitPath(const String &path)
    {
        Vector<String> result;
        size_t start = 0, end;
        while ((end = path.find('.', start)) != String::npos)
        {
            result.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        result.push_back(path.substr(start));
        return result;
    }

    // ========== Impl ==========
    static void ToRapidJsonValue(const JsonArchive::JsonValue &src, rapidjson::Value &dst, rapidjson::Document::AllocatorType &alloc)
    {
        struct Visitor
        {
            rapidjson::Value &dst;
            rapidjson::Document::AllocatorType &alloc;

            void operator()(std::monostate) const
            {
                dst.SetNull();
            }
            void operator()(i64 v) const
            {
                dst.SetInt64(v);
            }
            void operator()(u64 v) const
            {
                dst.SetUint64(v);
            }
            void operator()(f64 v) const
            {
                dst.SetDouble(v);
            }
            void operator()(bool v) const
            {
                dst.SetBool(v);
            }
            void operator()(const String &v) const
            {
                dst.SetString(v.c_str(), static_cast<rapidjson::SizeType>(v.size()), alloc);
            }
            void operator()(const JsonArchive::JsonObject &obj) const
            {
                dst.SetObject();
                for (const auto &kv: obj)
                {
                    rapidjson::Value key(kv.first.c_str(), static_cast<rapidjson::SizeType>(kv.first.size()), alloc);
                    rapidjson::Value val;
                    ToRapidJsonValue(kv.second, val, alloc);
                    dst.AddMember(key, val, alloc);
                }
            }
            void operator()(const JsonArchive::JsonArray &arr) const
            {
                dst.SetArray();
                dst.Reserve(static_cast<rapidjson::SizeType>(arr.size()), alloc);
                for (const auto &v: arr)
                {
                    rapidjson::Value val;
                    ToRapidJsonValue(v, val, alloc);
                    dst.PushBack(val, alloc);
                }
            }
        };

        std::visit(Visitor{dst, alloc}, src.value);
    }

    static void FromRapidJsonValue(const rapidjson::Value &src, JsonArchive::JsonValue &dst)
    {
        if (src.IsNull())
        {
            dst = JsonArchive::JsonValue(std::monostate{});
        }
        else if (src.IsBool())
        {
            dst = JsonArchive::JsonValue(src.GetBool());
        }
        else if (src.IsInt64())
        {
            dst = JsonArchive::JsonValue(static_cast<i64>(src.GetInt64()));
        }
        else if (src.IsUint64())
        {
            dst = JsonArchive::JsonValue(static_cast<u64>(src.GetUint64()));
        }
        else if (src.IsDouble())
        {
            dst = JsonArchive::JsonValue(static_cast<f64>(src.GetDouble()));
        }
        else if (src.IsString())
        {
            dst = JsonArchive::JsonValue(String(src.GetString(), src.GetStringLength()));
        }
        else if (src.IsObject())
        {
            JsonArchive::JsonObject obj;
            for (auto it = src.MemberBegin(); it != src.MemberEnd(); ++it)
            {
                String key(it->name.GetString(), it->name.GetStringLength());
                JsonArchive::JsonValue val;
                FromRapidJsonValue(it->value, val);
                obj.emplace(std::move(key), std::move(val));
            }
            dst = JsonArchive::JsonValue(std::move(obj));
        }
        else if (src.IsArray())
        {
            JsonArchive::JsonArray arr;
            arr.reserve(src.Size());
            for (auto &v: src.GetArray())
            {
                JsonArchive::JsonValue val;
                FromRapidJsonValue(v, val);
                arr.push_back(std::move(val));
            }
            dst = JsonArchive::JsonValue(std::move(arr));
        }
    }


    class JsonArchive::Impl
    {
    public:
        static FStructedArchive::EStructedDataType GetArrayType(const rapidjson::Value *node)
        {
            if (!node || !node->IsArray() || node->Empty()) return FStructedArchive::EStructedDataType::kStruct;
            auto &first = (*node)[0];
            if (first.IsInt64()) return FStructedArchive::EStructedDataType::kInt;
            if (first.IsUint64()) return FStructedArchive::EStructedDataType::kUInt;
            if (first.IsDouble()) return FStructedArchive::EStructedDataType::kFloat;
            if (first.IsBool()) return FStructedArchive::EStructedDataType::kBool;
            if (first.IsString()) return FStructedArchive::EStructedDataType::kString;
            return FStructedArchive::EStructedDataType::kStruct;
        }

    public:
        rapidjson::Document doc;

        Impl() { doc.SetObject(); }

        bool Load(const WString &path)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"rb") != 0)
            {
                LOG_ERROR(L"JsonArchive::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;

            char *buffer = new char[65536];
            rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));
            doc.ParseStream(is);
            fclose(fp);
            delete[] buffer;
            return !doc.HasParseError();
        }

        bool Load(const WString &path, JsonArchive::JsonValue& dst)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"rb") != 0)
            {
                LOG_ERROR(L"JsonArchive::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;

            char *buffer = new char[65536];
            rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));
            doc.ParseStream(is);
            fclose(fp);
            delete[] buffer;
            FromRapidJsonValue(doc, dst);
            return !doc.HasParseError();
        }

        bool Save(const WString &path)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"wb") != 0)
            {
                LOG_ERROR(L"JsonArchive::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;

            char *buffer = new char[65536];
            rapidjson::FileWriteStream os(fp, buffer, sizeof(buffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
            delete[] buffer;
            return true;
        }

        bool Save(const WString &path, const JsonArchive::JsonObject& root)
        {
            FILE *fp;
            if (_wfopen_s(&fp, path.c_str(), L"wb") != 0)
            {
                LOG_ERROR(L"JsonArchive::Impl open file({}) failed", path)
                return false;
            }
            if (!fp) return false;
            rapidjson::Document doc;
            doc.SetObject();
            ToRapidJsonValue(root, doc, doc.GetAllocator());
            char *buffer = new char[65536];
            rapidjson::FileWriteStream os(fp, buffer, sizeof(buffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
            doc.Accept(writer);
            fclose(fp);
            delete[] buffer;
            return true;
        }

        const rapidjson::Value *GetNode(const String &path) const
        {
            const rapidjson::Value *node = &doc;
            for (const auto &key: SplitPath(path))
            {
                if (!node->IsObject() || !node->HasMember(key.c_str()))
                    return nullptr;
                node = &(*node)[key.c_str()];
            }
            return node;
        }

        rapidjson::Value *EnsureNode(const String &path)
        {
            rapidjson::Value *node = &doc;
            for (const auto &key: SplitPath(path))
            {
                if (!node->IsObject()) node->SetObject();
                if (!node->HasMember(key.c_str()))
                {
                    rapidjson::Value k(key.c_str(), doc.GetAllocator());
                    rapidjson::Value v;
                    v.SetObject();// 默认空对象
                    node->AddMember(k, v, doc.GetAllocator());
                }
                node = &(*node)[key.c_str()];
            }
            return node;
        }

        // 读取
        String GetString(const String &path, const String &def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsString()) ? node->GetString() : def;
        }

        i64 GetInt(const String &path, i64 def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsInt()) ? node->GetInt64() : def;
        }

        u64 GetUInt(const String &path, u64 def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsUint64()) ? node->GetUint64() : def;
        }

        bool GetBool(const String &path, bool def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsBool()) ? node->GetBool() : def;
        }

        f64 GetFloat(const String &path, f64 def) const
        {
            auto *node = GetNode(path);
            return (node && node->IsNumber()) ? node->GetDouble() : def;
        }

        // 写入
        void SetString(const String &path, const String &val)
        {
            auto *node = EnsureNode(path);
            node->SetString(val.c_str(), doc.GetAllocator());
        }

        void SetInt(const String &path, i64 val)
        {
            auto *node = EnsureNode(path);
            node->SetInt64(val);
        }

        void SetUInt(const String &path, u64 val)
        {
            auto *node = EnsureNode(path);
            node->SetUint64(val);
        }

        void SetBool(const String &path, bool val)
        {
            auto *node = EnsureNode(path);
            node->SetBool(val);
        }

        void SetFloat(const String &path, f64 val)
        {
            auto *node = EnsureNode(path);
            node->SetDouble(val);
        }

        template<typename T, typename Func>
        std::vector<T> GetArray(const String &path, Func convert, const std::vector<T> &def = {}) const
        {
            auto *node = GetNode(path);
            if (!node || !node->IsArray()) return def;

            std::vector<T> result;
            for (auto &v: node->GetArray())
            {
                if (convert(v, result.emplace_back())) continue;
                result.pop_back();// 转换失败就移除
            }
            return result;
        }

        std::vector<i64> GetIntArray(const String &path, const std::vector<i64> &def = {}) const
        {
            return GetArray<i64>(path, [](const rapidjson::Value &v, i64 &out)
                                 {
        if (v.IsInt64()) { out = v.GetInt64(); return true; }
        return false; }, def);
        }

        std::vector<u64> GetUIntArray(const String &path, const std::vector<u64> &def = {}) const
        {
            return GetArray<u64>(path, [](const rapidjson::Value &v, u64 &out)
                                 {
        if (v.IsUint64()) { out = v.GetUint64(); return true; }
        return false; }, def);
        }

        std::vector<f64> GetFloatArray(const String &path, const std::vector<f64> &def = {}) const
        {
            return GetArray<f64>(path, [](const rapidjson::Value &v, f64 &out)
                                 {
        if (v.IsNumber()) { out = v.GetDouble(); return true; }
        return false; }, def);
        }

        std::vector<String> GetStringArray(const String &path, const std::vector<String> &def = {}) const
        {
            return GetArray<String>(path, [](const rapidjson::Value &v, String &out)
                                    {
        if (v.IsString()) { out = v.GetString(); return true; }
        return false; }, def);
        }

        std::vector<bool> GetArrayBool(const String &path, const std::vector<bool> &def = {}) const
        {
            auto *node = GetNode(path);
            if (!node || !node->IsArray()) return def;

            std::vector<bool> result;
            for (auto &v: node->GetArray())
            {
                if (v.IsBool())
                    result.emplace_back(v.GetBool());
            }
            return result;
        }


        template<typename T, typename Func>
        void SetArray(const String &path, const std::vector<T> &vec, Func fill)
        {
            auto *node = EnsureNode(path);
            node->SetArray();
            for (const auto &item: vec)
            {
                rapidjson::Value v;
                fill(item, v, doc.GetAllocator());
                node->PushBack(v, doc.GetAllocator());
            }
        }

        void SetIntArray(const String &path, const std::vector<i64> &vec)
        {
            SetArray<i64>(path, vec, [](i64 val, rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc)
                          { v.SetInt64(val); });
        }

        void SetUIntArray(const String &path, const std::vector<u64> &vec)
        {
            SetArray<u64>(path, vec, [](u64 val, rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc)
                          { v.SetUint64(val); });
        }

        void SetFloatArray(const String &path, const std::vector<f64> &vec)
        {
            SetArray<f64>(path, vec, [](f64 val, rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc)
                          { v.SetDouble(val); });
        }

        void SetStringArray(const String &path, const std::vector<String> &vec)
        {
            SetArray<String>(path, vec, [](const String &val, rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc)
                             { v.SetString(val.c_str(), static_cast<rapidjson::SizeType>(val.size()), alloc); });
        }

        void SetBoolArray(const String &path, const std::vector<bool> &vec)
        {
            SetArray<bool>(path, vec, [](bool val, rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc)
                           { v.SetBool(val); });
        }
    };

    // ========== JSONParser 实现 ==========


#pragma region JsonSerializer
#define RJ_NODE(n) reinterpret_cast<rapidjson::Value *>(n)
    JsonArchive::JsonArchive() : _impl(new Impl()) 
    {
        _root = JsonObject{};
    }
    JsonArchive::~JsonArchive() { delete _impl; }

    #define ZERO_NODES_CHECH(fn) if (_cur_obj_nodes.empty())\
    {\
        LOG_ERROR("JsonArchive::{},_cur_obj_nodes is empty,forget to BeginObject?", #fn)\
        return *this;\
    }

    FArchive &JsonArchive::operator<<(const bool &value)
    {
        ZERO_NODES_CHECH(<<)
        _cur_obj_nodes.top()._value = value;
        return *this;
    }
    FArchive &JsonArchive::operator<<(const String &value)
    {
        ZERO_NODES_CHECH(<<)
        _cur_obj_nodes.top()._value = value;
        return *this;
    }
    FArchive &JsonArchive::operator<<(const u64 &value)
    {
        ZERO_NODES_CHECH(<<)
        _cur_obj_nodes.top()._value = value;
        return *this;
    }
    FArchive &JsonArchive::operator<<(const i64 &value)
    {
        ZERO_NODES_CHECH(<<)
        _cur_obj_nodes.top()._value = value;
        return *this;
    }
    FArchive &JsonArchive::operator<<(const f64 &value)
    {
        ZERO_NODES_CHECH(<<)
        _cur_obj_nodes.top()._value = value;
        return *this;
    }

    FArchive &JsonArchive::operator>>(bool &value)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            AL_ASSERT(std::holds_alternative<bool>(node->value));
            value = std::get<bool>(node->value);
        }
        else
            LOG_ERROR("Read bool form key {},name {} failed", _cur_key, _cur_sub_name);
        return *this;
    }

    FArchive &JsonArchive::operator>>(String &value)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            AL_ASSERT(std::holds_alternative<String>(node->value));
            value = std::get<String>(node->value);
        }
        else
            LOG_ERROR("Read String form key {},name {} failed", _cur_key, _cur_sub_name);
        return *this;
    }
    FArchive &JsonArchive::operator>>(u64 &value)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            //rj默认会使用int存储，超过范围才会使用uint
            if (std::holds_alternative<i64>(node->value))
                value = (u64) std::get<i64>(node->value);
            else if (std::holds_alternative<i64>(node->value))
                value = std::get<u64>(node->value);
            else
                AL_ASSERT_MSG(false, "not int type");
        }
        else
            LOG_ERROR("Read u64 form key {},name {} failed", _cur_key, _cur_sub_name);
        return *this;
    }
    FArchive &JsonArchive::operator>>(i64 &value)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            AL_ASSERT(std::holds_alternative<i64>(node->value));
            value = std::get<i64>(node->value);
        }
        else
            LOG_ERROR("Read i64 form key {},name {} failed", _cur_key, _cur_sub_name);
        return *this;
    }
    FArchive &JsonArchive::operator>>(f64 &value)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            if (std::holds_alternative<f64>(node->value))
                value = std::get<f64>(node->value);
            else if (std::holds_alternative<i64>(node->value))
            {
                value = static_cast<f64>(std::get<i64>(node->value));
                LOG_WARNING("Read f64 from i64 type, key {},name {}", _cur_key, _cur_sub_name);
            }
            else {}
        }
        else
            LOG_ERROR("Read f64 form key {},name {} failed", _cur_key, _cur_sub_name);
        return *this;
    }
    void JsonArchive::Deserialize(void *data, u64 size)
    {
        LOG_WARNING("JsonArchive not support Deserialize yet")
    }
    void JsonArchive::Serialize(void *data, u64 size)
    {
        LOG_WARNING("JsonArchive not support Serialize yet")
    }
    void JsonArchive::BeginObject(const String &name)
    {
        if (name.size() == 0)
            return;
        if (_cur_key.empty())
            _cur_key = name;
        else
            _cur_key = std::format("{}.{}", _cur_key, name);
        bool is_arr_item = false;
        if (_cur_obj_nodes.size() > 0 && _cur_obj_nodes.top()._is_array)
            is_arr_item = true;
        _cur_obj_nodes.push({name, JsonObject{}, 0u});
        _cur_obj_nodes.top()._is_arr_item = is_arr_item;
        _cur_sub_name = name;
    }

    void JsonArchive::EndObject()
    {
        auto old_key = _cur_key;
        auto pos = _cur_key.rfind('.');
        if (pos == String::npos)
        {
            // 没有 '.' 说明在最外层
            _cur_key.clear();
        }
        else
        {
            _cur_key.erase(pos);
        }
        auto node = std::move(_cur_obj_nodes.top());
        _cur_obj_nodes.pop();

        if (!_cur_obj_nodes.empty())
        {
            auto &parent = _cur_obj_nodes.top()._value.value;
            if (std::holds_alternative<JsonArray>(parent))
            {
                std::get<JsonArray>(parent).push_back(std::move(node._value));
            }
            else
            {
                std::get<JsonObject>(parent)[node._path] = std::move(node._value);
            }
        }
        else
        {
            std::get<JsonObject>(_root.value)[old_key] = std::move(node._value);
        }
    }


    void JsonArchive::BeginArray(u64 size, EStructedDataType type)
    {
        JsonArray arr;
        arr.reserve(size);
        _cur_obj_nodes.top()._value = arr;
        _cur_obj_nodes.top()._is_array = true;
        _cur_obj_nodes.top()._is_struct = type == EStructedDataType::kStruct;
    }

    u32 JsonArchive::BeginArray(EStructedDataType &type)
    {
        if (auto node = FindNode(); node != nullptr)
        {
            type = EStructedDataType::kStruct;
            AL_ASSERT(std::holds_alternative<JsonArray>(node->value));
            auto &arr = std::get<JsonArray>(node->value);
            if (arr.empty())
                return 0u;
            if (std::holds_alternative<String>(arr[0].value))
                type = EStructedDataType::kString;
            else if (std::holds_alternative<bool>(arr[0].value))
                type = EStructedDataType::kBool;
            else if (std::holds_alternative<f64>(arr[0].value))
                type = EStructedDataType::kFloat;
            else if (std::holds_alternative<u64>(arr[0].value))
                type = EStructedDataType::kUInt;
            else if (std::holds_alternative<i64>(arr[0].value))
                type = EStructedDataType::kInt;
            else {}
            return (u32)arr.size();
        }
        return 0u;
    }

    void JsonArchive::EndArray()
    {

    }

    void JsonArchive::WriteKey(const String &key)
    {
        _cur_key = key;
    }

    void JsonArchive::WriteBool(bool v)
    {
        (*this) << v;
    }
    void JsonArchive::WriteInt(i64 v)
    {
        (*this) << v;
    }

    void JsonArchive::WriteUInt(u64 v)
    {
        (*this) << v;
    }

    void JsonArchive::WriteFloat(f64 v)
    {
        (*this) << v;
    }

    void JsonArchive::WriteString(const String &v)
    {
        (*this) << v;
    }
    void JsonArchive::ReadBool(bool &v)
    {
        (*this) >> v;
    }
    void JsonArchive::ReadInt(i64 &v)
    {
        (*this) >> v;
    }
    void JsonArchive::ReadUInt(u64 &v)
    {
        (*this) >> v;
    }
    void JsonArchive::ReadFloat(f64 &v)
    {
        (*this) >> v;
    }
    void JsonArchive::ReadString(String &v)
    {
        (*this) >> v;
    }
    void JsonArchive::Save(const Path &sys_path)
    {
        if (!_impl->Save(sys_path.wstring(), std::get<JsonObject>(_root.value)))
        {
            LOG_ERROR("Failed to save JSON archive to {}", sys_path);
        }
        Reset();
    }
    void JsonArchive::Load(const Path &sys_path)
    {
        Reset();
        if (!_impl->Load(sys_path.wstring(),_root))
        {
            LOG_ERROR("Failed to load JSON archive from {}", sys_path);
        }
        else
            _is_loaded = true;
    }
    JsonArchive::JsonValue *JsonArchive::FindNode()
    {
        JsonValue *node = &_root;
        String pre_key{};
        auto &&keys = SplitPath(_cur_key);

        for (const auto &p: keys)
        {
            if (std::holds_alternative<JsonObject>(node->value))
            {
                auto &obj_map = std::get<JsonObject>(node->value);
                auto it = obj_map.find(p);
                if (it == obj_map.end())
                    return nullptr;
                node = &it->second;
            }
            else if (std::holds_alternative<JsonArray>(node->value))
            {
                u64 idx;
                auto res = std::from_chars(p.data(), p.data() + p.size(), idx);
                if (res.ec != std::errc{})
                    return nullptr;// 非数字
                auto &arr = std::get<JsonArray>(node->value);
                if (idx >= arr.size())
                    return nullptr;
                node = &arr[idx];
            }
            else
            {
                LOG_WARNING("JsonArchive::FindNode: json node: {} is not an object/array", p);
                return nullptr;
            }
            pre_key = p;
        }
        return node;
    }


#pragma endregion JsonSerializer


}// namespace Ailu
