#ifndef __JSON_ARCHIVE_H
#define __JSON_ARCHIVE_H
#include <variant>
#include <stack>
#include "Serialize.h"
namespace Ailu
{
    class AILU_API JsonArchive : public FStructedArchive
    {
    public:
        struct JsonValue;

        using JsonObject = HashMap<String, JsonValue>;
        using JsonArray = Vector<JsonValue>;

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

        void Save(const Path &sys_path) final;
        void Load(const Path &sys_path) final;
    private:
        JsonValue *FindNode();
        void Reset()
        {
            _cur_key.clear();
            _cur_sub_name.clear();
            while (!_cur_obj_nodes.empty())
                _cur_obj_nodes.pop();
            _root = JsonObject{};
        }
    private:
        class Impl;
        Impl *_impl = nullptr;
        bool _is_loading = false;
        bool _is_saving = false;
        bool _is_loaded = false;
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

