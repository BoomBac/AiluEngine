#include "Framework/Common/Log.h"
#include "Framework/Common/Allocator.hpp"
#include "Objects/JsonArchive.h"

#include <Ext/rapidjson/inc/document.h>
#include <Ext/rapidjson/inc/filereadstream.h>
#include <Ext/rapidjson/inc/filewritestream.h>
#include <Ext/rapidjson/inc/prettywriter.h>
#include <Ext/rapidjson/inc/stringbuffer.h>
#include <cstdio>

namespace Ailu
{
    // Save-side compatibility helpers. Load-side navigation uses the RapidJSON node stack below.
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

    static JsonArchive::JsonValuePtr CloneJsonValuePtr(const JsonArchive::JsonValuePtr &value)
    {
        return value ? std::make_shared<JsonArchive::JsonValue>(*value) : nullptr;
    }

    JsonArchive::JsonObject::JsonObject(const JsonObject &other)
    {
        *this = other;
    }

    JsonArchive::JsonObject &JsonArchive::JsonObject::operator=(const JsonObject &other)
    {
        if (this == &other)
            return *this;

        _entries.clear();
        _entry_lookup.clear();
        _entries.reserve(other._entries.size());
        _entry_lookup.reserve(other._entries.size());

        for (const auto &entry: other._entries)
        {
            JsonObjectEntry cloned_entry;
            cloned_entry._key = entry._key;
            cloned_entry._value = CloneJsonValuePtr(entry._value);
            _entry_lookup.emplace(cloned_entry._key, _entries.size());
            _entries.push_back(std::move(cloned_entry));
        }
        return *this;
    }

    void JsonArchive::JsonObject::reserve(size_t size)
    {
        _entries.reserve(size);
        _entry_lookup.reserve(size);
    }

    JsonArchive::JsonValue *JsonArchive::JsonObject::Find(const String &key)
    {
        auto it = _entry_lookup.find(key);
        if (it == _entry_lookup.end())
            return nullptr;
        return _entries[it->second]._value.get();
    }

    const JsonArchive::JsonValue *JsonArchive::JsonObject::Find(const String &key) const
    {
        auto it = _entry_lookup.find(key);
        if (it == _entry_lookup.end())
            return nullptr;
        return _entries[it->second]._value.get();
    }

    JsonArchive::JsonValue &JsonArchive::JsonObject::InsertOrAssign(String key, JsonValue value)
    {
        auto it = _entry_lookup.find(key);
        if (it != _entry_lookup.end())
        {
            auto &entry_value = _entries[it->second]._value;
            if (!entry_value)
                entry_value = std::make_shared<JsonValue>(std::move(value));
            else
                *entry_value = std::move(value);
            return *entry_value;
        }

        JsonObjectEntry entry;
        entry._key = std::move(key);
        entry._value = std::make_shared<JsonValue>(std::move(value));
        _entry_lookup.emplace(entry._key, _entries.size());
        _entries.push_back(std::move(entry));
        return *_entries.back()._value;
    }

    JsonArchive::JsonArray::JsonArray(const JsonArray &other)
    {
        *this = other;
    }

    JsonArchive::JsonArray &JsonArchive::JsonArray::operator=(const JsonArray &other)
    {
        if (this == &other)
            return *this;

        _items.clear();
        _items.reserve(other._items.size());
        for (const auto &item: other._items)
        {
            _items.push_back(CloneJsonValuePtr(item));
        }
        return *this;
    }

    void JsonArchive::JsonArray::reserve(size_t size)
    {
        _items.reserve(size);
    }

    size_t JsonArchive::JsonArray::size() const
    {
        return _items.size();
    }

    bool JsonArchive::JsonArray::empty() const
    {
        return _items.empty();
    }

    void JsonArchive::JsonArray::push_back(JsonValue value)
    {
        _items.push_back(std::make_shared<JsonValue>(std::move(value)));
    }

    JsonArchive::JsonValue &JsonArchive::JsonArray::operator[](size_t index)
    {
        return *_items[index];
    }

    const JsonArchive::JsonValue &JsonArchive::JsonArray::operator[](size_t index) const
    {
        return *_items[index];
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
                for (const auto &entry: obj._entries)
                {
                    rapidjson::Value key(entry._key.c_str(), static_cast<rapidjson::SizeType>(entry._key.size()), alloc);
                    rapidjson::Value val;
                    if (entry._value)
                        ToRapidJsonValue(*entry._value, val, alloc);
                    else
                        val.SetNull();
                    dst.AddMember(key, val, alloc);
                }
            }
            void operator()(const JsonArchive::JsonArray &arr) const
            {
                dst.SetArray();
                dst.Reserve(static_cast<rapidjson::SizeType>(arr.size()), alloc);
                for (const auto &value: arr._items)
                {
                    rapidjson::Value val;
                    if (value)
                        ToRapidJsonValue(*value, val, alloc);
                    else
                        val.SetNull();
                    dst.PushBack(val, alloc);
                }
            }
        };

        std::visit(Visitor{dst, alloc}, src.value);
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

            char *buffer = AL_ALLOC_TAG(EMemoryTag::kTemporary, char, 65536);
            rapidjson::FileReadStream is(fp, buffer, sizeof(buffer));
            doc.ParseStream(is);
            fclose(fp);
            AL_FREE(buffer);
            return !doc.HasParseError();
        }

        bool LoadFromString(const String &json_text)
        {
            doc.Parse(json_text.c_str());
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

            char *buffer = AL_ALLOC_TAG(EMemoryTag::kTemporary, char, 65536);
            rapidjson::FileWriteStream os(fp, buffer, sizeof(buffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
            AL_FREE(buffer);
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
            char *buffer = AL_ALLOC_TAG(EMemoryTag::kTemporary, char, 65536);
            rapidjson::FileWriteStream os(fp, buffer, sizeof(buffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            writer.SetMaxDecimalPlaces(4);
            writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
            doc.Accept(writer);
            fclose(fp);
            AL_FREE(buffer);
            return true;
        }

        String SaveToString(const JsonArchive::JsonObject &root)
        {
            rapidjson::Document doc;
            doc.SetObject();
            ToRapidJsonValue(root, doc, doc.GetAllocator());
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            writer.SetMaxDecimalPlaces(4);
            writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);
            doc.Accept(writer);
            return String(buffer.GetString(), buffer.GetSize());
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
    JsonArchive::JsonArchive() : _impl(AL_NEW_TAG(EMemoryTag::kAsset, Impl))
    {
        _root = JsonObject{};
    }
    JsonArchive::~JsonArchive() { AL_DELETE(_impl); }

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
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node != nullptr && node->IsBool())
                value = node->GetBool();
            else
                LOG_ERROR("Read bool from current JSON node failed");
        }
        else
        {
            if (auto node = FindNode(); node != nullptr && std::holds_alternative<bool>(node->value))
                value = std::get<bool>(node->value);
        }
        return *this;
    }

    FArchive &JsonArchive::operator>>(String &value)
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node != nullptr && node->IsString())
                value.assign(node->GetString(), node->GetStringLength());
            else
                LOG_ERROR("Read String from current JSON node failed");
        }
        else
        {
            if (auto node = FindNode(); node != nullptr && std::holds_alternative<String>(node->value))
                value = std::get<String>(node->value);
        }
        return *this;
    }
    FArchive &JsonArchive::operator>>(u64 &value)
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node != nullptr && node->IsUint64())
                value = node->GetUint64();
            else if (node != nullptr && node->IsInt64() && node->GetInt64() >= 0)
                value = static_cast<u64>(node->GetInt64());
            else
                LOG_ERROR("Read u64 from current JSON node failed");
        }
        else
        {
            if (auto node = FindNode(); node != nullptr)
            {
                if (std::holds_alternative<i64>(node->value))
                    value = static_cast<u64>(std::get<i64>(node->value));
                else if (std::holds_alternative<u64>(node->value))
                    value = std::get<u64>(node->value);
            }
        }
        return *this;
    }
    FArchive &JsonArchive::operator>>(i64 &value)
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node != nullptr && node->IsInt64())
                value = node->GetInt64();
            else if (node != nullptr && node->IsUint64() && node->GetUint64() <= static_cast<u64>(INT64_MAX))
                value = static_cast<i64>(node->GetUint64());
            else
                LOG_ERROR("Read i64 from current JSON node failed");
        }
        else
        {
            if (auto node = FindNode(); node != nullptr && std::holds_alternative<i64>(node->value))
                value = std::get<i64>(node->value);
        }
        return *this;
    }
    FArchive &JsonArchive::operator>>(f64 &value)
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node != nullptr && node->IsNumber())
                value = node->GetDouble();
            else
                LOG_ERROR("Read f64 from current JSON node failed");
        }
        else
        {
            if (auto node = FindNode(); node != nullptr)
            {
                if (std::holds_alternative<f64>(node->value))
                    value = std::get<f64>(node->value);
                else if (std::holds_alternative<i64>(node->value))
                    value = static_cast<f64>(std::get<i64>(node->value));
            }
        }
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
        if (_is_loading)
        {
            const auto *parent = static_cast<const rapidjson::Value *>(FindReadNode());
            const rapidjson::Value *child = nullptr;
            if (parent != nullptr && parent->IsObject())
            {
                const auto member = parent->FindMember(name.c_str());
                if (member != parent->MemberEnd())
                    child = &member->value;
            }
            else if (parent != nullptr && parent->IsArray())
            {
                u64 index = 0u;
                const auto result = std::from_chars(name.data(), name.data() + name.size(), index);
                if (result.ec == std::errc{} && index < parent->Size())
                    child = &(*parent)[static_cast<rapidjson::SizeType>(index)];
            }
            if (child == nullptr)
                LOG_ERROR("JsonArchive::BeginObject: JSON member {} not found", name);
            _read_node_stack.push_back(child);
            return;
        }
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
        if (_is_loading)
        {
            if (_read_node_stack.size() > 1u)
                _read_node_stack.pop_back();
            return;
        }
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
                std::get<JsonObject>(parent).InsertOrAssign(node._path, std::move(node._value));
            }
        }
        else
        {
            std::get<JsonObject>(_root.value).InsertOrAssign(old_key, std::move(node._value));
        }
    }


    void JsonArchive::BeginArrayElement(u32 index)
    {
        if (!_is_loading)
            return;
        const auto *parent = static_cast<const rapidjson::Value *>(FindReadNode());
        const rapidjson::Value *child = nullptr;
        if (parent != nullptr && parent->IsArray() && index < parent->Size())
            child = &(*parent)[index];
        if (child == nullptr)
            LOG_ERROR("JsonArchive::BeginArrayElement: index {} is out of range", index);
        _read_node_stack.push_back(child);
    }

    void JsonArchive::EndArrayElement()
    {
        if (_is_loading && _read_node_stack.size() > 1u)
            _read_node_stack.pop_back();
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
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node == nullptr || !node->IsArray())
            {
                LOG_ERROR("JsonArchive::BeginArray: current value is not an array");
                return 0u;
            }
            type = Impl::GetArrayType(node);
            return node->Size();
        }
        if (auto node = FindNode(); node != nullptr)
        {
            type = EStructedDataType::kStruct;
            if (!std::holds_alternative<JsonArray>(node->value))
            {
                LOG_ERROR("JsonArchive::BeginArray: current value is not an array");
                return 0u;
            }
            auto &arr = std::get<JsonArray>(node->value);
            if (!arr.empty())
            {
                if (std::holds_alternative<String>(arr[0].value)) type = EStructedDataType::kString;
                else if (std::holds_alternative<bool>(arr[0].value)) type = EStructedDataType::kBool;
                else if (std::holds_alternative<f64>(arr[0].value)) type = EStructedDataType::kFloat;
                else if (std::holds_alternative<u64>(arr[0].value)) type = EStructedDataType::kUInt;
                else if (std::holds_alternative<i64>(arr[0].value)) type = EStructedDataType::kInt;
            }
            return static_cast<u32>(arr.size());
        }
        return 0u;
    }

    void JsonArchive::EndArray()
    {
        // Array elements are pushed and popped by BeginObject/EndObject.
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
    bool JsonArchive::Save(const Path &sys_path)
    {
        const bool saved = _impl->Save(sys_path.wstring(), std::get<JsonObject>(_root.value));
        if (!saved)
        {
            LOG_ERROR("Failed to save JSON archive to {}", sys_path);
        }
        Reset();
        return saved;
    }
    void JsonArchive::Load(const Path &sys_path)
    {
        Reset();
        if (!_impl->Load(sys_path.wstring()))
        {
            LOG_ERROR("Failed to load JSON archive from {}", sys_path);
        }
        else
        {
            _is_loading = true;
            _read_node_stack.push_back(&_impl->doc);
            _is_loaded = true;
        }
    }
    String JsonArchive::SaveToString()
    {
        String json_text;
        if (std::holds_alternative<JsonObject>(_root.value))
            json_text = _impl->SaveToString(std::get<JsonObject>(_root.value));
        Reset();
        return json_text;
    }
    bool JsonArchive::LoadFromString(const String &json_text)
    {
        Reset();
        if (!_impl->LoadFromString(json_text))
        {
            LOG_ERROR("Failed to load JSON archive from string");
            return false;
        }
        _is_loading = true;
        _read_node_stack.push_back(&_impl->doc);
        _is_loaded = true;
        return true;
    }
    bool JsonArchive::HasField(const String &name)
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            return node != nullptr && node->IsObject() && node->HasMember(name.c_str());
        }
        String key = _cur_key.empty() ? name : std::format("{}.{}", _cur_key, name);
        JsonValue *node = &_root;

        auto &&keys = SplitPath(key);
        for (const auto &p: keys)
        {
            if (std::holds_alternative<JsonObject>(node->value))
            {
                auto *child = std::get<JsonObject>(node->value).Find(p);
                if (child == nullptr)
                    return false;
                node = child;
            }
            else if (std::holds_alternative<JsonArray>(node->value))
            {
                u64 idx;
                auto res = std::from_chars(p.data(), p.data() + p.size(), idx);
                if (res.ec != std::errc{})
                    return false;
                auto &arr = std::get<JsonArray>(node->value);
                if (idx >= arr.size())
                    return false;
                node = &arr[(size_t) idx];
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    bool JsonArchive::IsCurrentNodeObject()
    {
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            return node != nullptr && node->IsObject();
        }
        JsonValue *node = FindNode();
        return node != nullptr && std::holds_alternative<JsonObject>(node->value);
    }

    Vector<String> JsonArchive::GetCurrentObjectKeys()
    {
        Vector<String> keys;
        if (_is_loading)
        {
            const auto *node = static_cast<const rapidjson::Value *>(FindReadNode());
            if (node == nullptr || !node->IsObject())
                return keys;
            keys.reserve(node->MemberCount());
            for (auto it = node->MemberBegin(); it != node->MemberEnd(); ++it)
                keys.emplace_back(it->name.GetString(), it->name.GetStringLength());
            return keys;
        }
        JsonValue *node = FindNode();
        if (node == nullptr || !std::holds_alternative<JsonObject>(node->value))
            return keys;

        const JsonObject &obj = std::get<JsonObject>(node->value);
        keys.reserve(obj._entries.size());
        for (const auto &entry: obj._entries)
            keys.push_back(entry._key);
        return keys;
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
                auto *child = std::get<JsonObject>(node->value).Find(p);
                if (child == nullptr)
                    return nullptr;
                node = child;
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
                node = &arr[(size_t) idx];
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

    const void *JsonArchive::FindReadNode() const
    {
        return _read_node_stack.empty() ? nullptr : _read_node_stack.back();
    }


#pragma endregion JsonSerializer


}// namespace Ailu
