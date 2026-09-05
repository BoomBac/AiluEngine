#ifndef __JSON_ARCHIVE_H
#define __JSON_ARCHIVE_H
#include <memory>
#include <stack>
#include <variant>
#include "Serialize.h"
namespace Ailu
{
    class AILU_API JsonArchive : public FStructedArchive
    {
    public:
        struct JsonValue;
        using JsonValuePtr = std::shared_ptr<JsonValue>;

        struct JsonObjectEntry
        {
            String _key;
            JsonValuePtr _value;
        };

        struct JsonObject
        {
            JsonObject() = default;
            JsonObject(const JsonObject &other);
            JsonObject(JsonObject &&other) noexcept = default;
            JsonObject &operator=(const JsonObject &other);
            JsonObject &operator=(JsonObject &&other) noexcept = default;

            void reserve(size_t size);
            JsonValue *Find(const String &key);
            const JsonValue *Find(const String &key) const;
            JsonValue &InsertOrAssign(String key, JsonValue value);

            Vector<JsonObjectEntry>::iterator begin() { return _entries.begin(); }
            Vector<JsonObjectEntry>::iterator end() { return _entries.end(); }
            Vector<JsonObjectEntry>::const_iterator begin() const { return _entries.begin(); }
            Vector<JsonObjectEntry>::const_iterator end() const { return _entries.end(); }

            Vector<JsonObjectEntry> _entries;
            HashMap<String, size_t> _entry_lookup;
        };

        struct JsonArray
        {
            JsonArray() = default;
            JsonArray(const JsonArray &other);
            JsonArray(JsonArray &&other) noexcept = default;
            JsonArray &operator=(const JsonArray &other);
            JsonArray &operator=(JsonArray &&other) noexcept = default;

            void reserve(size_t size);
            size_t size() const;
            bool empty() const;
            void push_back(JsonValue value);
            JsonValue &operator[](size_t index);
            const JsonValue &operator[](size_t index) const;

            Vector<JsonValuePtr> _items;
        };

        struct JsonValue
        {
            using VariantType = std::variant<
                    std::monostate,
                    i64,
                    u64,
                    f64,
                    bool,
                    String,
                    JsonObject,
                    JsonArray>;

            VariantType value;

            JsonValue() = default;

            // 基础类型构造
            JsonValue(i64 v) : value(v) {}
            JsonValue(u64 v) : value(v) {}
            JsonValue(f64 v) : value(v) {}
            JsonValue(bool v) : value(v) {}
            JsonValue(const String &v) : value(v) {}
            JsonValue(String &&v) : value(std::move(v)) {}

            // 对象和数组构造
            JsonValue(const JsonObject &obj) : value(obj) {}
            JsonValue(JsonObject &&obj) : value(std::move(obj)) {}
            JsonValue(const JsonArray &arr) : value(arr) {}
            JsonValue(JsonArray &&arr) : value(std::move(arr)) {}

            // 允许从 monostate 构造
            JsonValue(std::monostate v) : value(v) {}
        };


    public:
        JsonArchive();
        ~JsonArchive();
        bool IsSaving() const { return _is_saving; };
        bool IsLoading() const { return _is_loading; };
        bool IsLoaded() const { return _is_loaded; };

        template<typename T>
        FArchive &operator<<(const T &obj)
        {
            using ValueType = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<ValueType, u8> || std::is_same_v<ValueType, u16> ||
                          std::is_same_v<ValueType, u32> || std::is_same_v<ValueType, u64>)
            {
                return *this << static_cast<u64>(obj);
            }
            else if constexpr (std::is_same_v<ValueType, i8> || std::is_same_v<ValueType, i16> ||
                               std::is_same_v<ValueType, i32> || std::is_same_v<ValueType, i64>)
            {
                return *this << static_cast<i64>(obj);
            }
            else if constexpr (std::is_same_v<ValueType, f32> || std::is_same_v<ValueType, f64>)
            {
                return *this << static_cast<f64>(obj);
            }
            else if constexpr (std::is_same_v<ValueType, bool> || std::is_same_v<ValueType, String>)
            {
                return *this << obj;
            }
            else if constexpr (std::is_base_of_v<Object, ValueType>)
            {
                return *this << static_cast<Object &>(const_cast<ValueType &>(obj));
            }
            else if constexpr (requires(ValueType &value) { value.GetType(); })
            {
                ValueType &value = const_cast<ValueType &>(obj);
                for (const auto &property: value.GetType()->GetProperties())
                    property.Serialize(&value, *this);
                return *this;
            }
            else
            {
                static_assert(std::is_base_of_v<Object, ValueType> || requires(ValueType &value) { value.GetType(); },
                              "JsonArchive only supports reflected types and primitive types");
            }
        }

        template<typename T>
        FArchive &operator>>(T &obj)
        {
            using ValueType = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<ValueType, u8> || std::is_same_v<ValueType, u16> ||
                          std::is_same_v<ValueType, u32> || std::is_same_v<ValueType, u64>)
            {
                u64 value;
                *this >> value;
                obj = static_cast<ValueType>(value);
                return *this;
            }
            else if constexpr (std::is_same_v<ValueType, i8> || std::is_same_v<ValueType, i16> ||
                               std::is_same_v<ValueType, i32> || std::is_same_v<ValueType, i64>)
            {
                i64 value;
                *this >> value;
                obj = static_cast<ValueType>(value);
                return *this;
            }
            else if constexpr (std::is_same_v<ValueType, f32> || std::is_same_v<ValueType, f64>)
            {
                f64 value;
                *this >> value;
                obj = static_cast<ValueType>(value);
                return *this;
            }
            else if constexpr (std::is_same_v<ValueType, bool> || std::is_same_v<ValueType, String>)
            {
                return *this >> obj;
            }
            else if constexpr (std::is_base_of_v<Object, ValueType>)
            {
                return *this >> static_cast<Object &>(obj);
            }
            else if constexpr (requires(ValueType &value) { value.GetType(); })
            {
                for (const auto &property: obj.GetType()->GetProperties())
                    property.Deserialize(&obj, *this);
                return *this;
            }
            else
            {
                static_assert(std::is_base_of_v<Object, ValueType> || requires(ValueType &value) { value.GetType(); },
                              "JsonArchive only supports reflected types and primitive types");
            }
        }

        FArchive &operator<<(const u8 &obj) final { return *this << (u64) obj; }
        FArchive &operator<<(const u16 &obj) final { return *this << (u64) obj; }
        FArchive &operator<<(const u32 &obj) final { return *this << (u64) obj; }
        FArchive &operator<<(const i8 &obj) final { return *this << (i64) obj; }
        FArchive &operator<<(const i16 &obj) final { return *this << (i64) obj; }
        FArchive &operator<<(const i32 &obj) final { return *this << (i64) obj; }
        FArchive &operator<<(const f32 &obj) final { return *this << (f64) obj; }
        FArchive &operator<<(Object &obj) final 
        {
            if (auto sob = dynamic_cast<SerializeObject *>(&obj); sob != nullptr)
            {
                sob->Serialize(*this);
            }
            else
            {
                for (const auto &p: obj.GetType()->GetProperties())
                {
                    p.Serialize(&obj, *this);
                }
            }
            return *this;
        }
        IMPL_LEFT_OP(u64)
        IMPL_LEFT_OP(i64)
        IMPL_LEFT_OP(f64)
        IMPL_LEFT_OP(bool)
        IMPL_LEFT_OP(String)

        FArchive &operator>>(u8 &obj) final
        {
            u64 tmp;
            *this >> tmp;
            obj = static_cast<u8>(tmp);
            return *this;
        }
        FArchive &operator>>(u16 &obj) final
        {
            u64 tmp;
            *this >> tmp;
            obj = static_cast<u16>(tmp);
            return *this;
        }
        FArchive &operator>>(u32 &obj) final
        {
            u64 tmp;
            *this >> tmp;
            obj = static_cast<u32>(tmp);
            return *this;
        }
        FArchive &operator>>(i8 &obj) final
        {
            i64 tmp;
            *this >> tmp;
            obj = static_cast<i8>(tmp);
            return *this;
        }
        FArchive &operator>>(i16 &obj) final
        {
            i64 tmp;
            *this >> tmp;
            obj = static_cast<i16>(tmp);
            return *this;
        }
        FArchive &operator>>(i32 &obj) final
        {
            i64 tmp;
            *this >> tmp;
            obj = static_cast<i32>(tmp);
            return *this;
        }
        FArchive &operator>>(f32 &obj) final
        {
            f64 tmp;
            *this >> tmp;
            obj = static_cast<f32>(tmp);
            return *this;
        }
        FArchive &operator>>(Object &obj) final
        {
            if (auto sob = dynamic_cast<SerializeObject *>(&obj); sob != nullptr)
            {
                sob->Deserialize(*this);
                sob->PostDeserialize();
            }
            else
            {
                for (const auto &p: obj.GetType()->GetProperties())
                {
                    p.Deserialize(&obj, *this);
                }
            }
            return *this;
        }
        IMPL_RIGHT_OP(u64)
        IMPL_RIGHT_OP(i64)
        IMPL_RIGHT_OP(f64)
        IMPL_RIGHT_OP(bool)
        IMPL_RIGHT_OP(String)
        void Serialize(void *data, u64 size) final;
        void Deserialize(void *data, u64 size) final;

        void BeginObject(const String &name) final;
        void BeginArrayElement(u32 index);
        void EndArrayElement();
        void EndObject() final;

        void BeginArray(u64 size, EStructedDataType type) final;
        u32 BeginArray(EStructedDataType &type) final;
        void EndArray() final;

        void WriteKey(const String &key) final;

        void WriteBool(bool v) final;
        void WriteInt(i64 v) final;
        void WriteUInt(u64 v) final;
        void WriteFloat(f64 v) final;
        void WriteString(const String &v) final;
        void ReadBool(bool &v) final;
        void ReadInt(i64 &v) final;
        void ReadUInt(u64 &v) final;
        void ReadFloat(f64 &v) final;
        void ReadString(String &v) final;

        bool Save(const Path &sys_path) final;
        void Load(const Path &sys_path) final;
        String SaveToString();
        bool LoadFromString(const String &json_text);
        bool HasField(const String &name) override;
        bool IsCurrentNodeObject();
        Vector<String> GetCurrentObjectKeys();
    private:
        JsonValue *FindNode();
        const void *FindReadNode() const;
        void Reset()
        {
            _cur_key.clear();
            _cur_sub_name.clear();
            while (!_cur_obj_nodes.empty())
                _cur_obj_nodes.pop();
            _read_node_stack.clear();
            _root = JsonObject{};
            _is_loading = false;
            _is_saving = false;
            _is_loaded = false;
        }
    private:
        class Impl;
        Impl *_impl = nullptr;
        bool _is_loading = false;
        bool _is_saving = false;
        bool _is_loaded = false;
        Vector<const void *> _read_node_stack;
        String _cur_key, _cur_sub_name;
        struct JsonNode
        {
            String _path;
            JsonValue _value;
            union 
            {
                struct {
                    u32 _is_array    : 1;
                    u32 _is_arr_item : 1;
                    u32 _is_struct   : 1;
                };
                u32 _flags;
            };
        };
        std::stack<JsonNode> _cur_obj_nodes;
        JsonValue _root;
    };
}

#endif// !__JSON_ARCHIVE_H

