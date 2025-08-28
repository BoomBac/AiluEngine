#ifndef __SERIALIZE_H__
#define __SERIALIZE_H__
#include "GlobalMarco.h"
#include "Framework/Common/Log.h"
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include "Type.h"
#include "generated/Serialize.gen.h"
namespace Ailu
{
    class Archive;
    class AILU_API IPersistentable
    {
    public:
        virtual ~IPersistentable() = default;
        virtual void Serialize(Archive &arch) = 0;
        virtual void Deserialize(Archive &arch) = 0;
    };
    class Archive
    {
    public:
        Archive(std::ostream *os) : _os(os) {};
        Archive(std::istream *is) : _is(is) {};

        std::ostream &GetOStream() { return *_os; }
        std::istream &GetIStream() { return *_is; }

        virtual void InsertIndent(){};
        virtual void NewLine(){};
        virtual const String &GetIndent() const
        { 
            static String ret = "";
            return ret;
        };
        virtual void IncreaseIndent() {};
        virtual void DecreaseIndent() {};
        template<typename T>
        std::ostream &operator<<(const T &obj)
        {
            (*_os) << obj;
            return (*_os);
        }
        template<typename T>
        std::ostream &operator<<(T *obj)
        {
            (*_os) << (*obj);
            return (*_os);
        }
        template<>
        std::ostream &operator<<(const char *obj)
        {
            (*_os) << obj;
            return (*_os);
        }
        template<typename T>
        std::istream &operator>>(T &obj)
        {
            (*_is) >> obj;
            return (*_is);
        }
        // std::ostream &operator<<(IPersistentable &obj)
        // {
        //     obj.Serialize(*this);
        //     return GetOStream();
        // }
        // std::istream &operator>>(IPersistentable &obj)
        // {
        //     obj.Deserialize(*this);
        //     return GetIStream();
        // }

    protected:
        std::ostream *_os;
        std::istream *_is;
        u32 _version = 0;
    };
    struct IndentBlock
    {
        IndentBlock(Archive &arch) : _arch(arch) 
        { 
            arch.IncreaseIndent();
        }
        ~IndentBlock() { _arch.DecreaseIndent(); }
        Archive& _arch;
    };
    class TextOArchive : public Archive
    {
    public:
        TextOArchive(std::ostream *os) : Archive(os), _indentLevel(0) {}
        void InsertIndent() final
        {
            GetOStream() << indents[_indentLevel];
        }

        void NewLine() final
        {
            GetOStream() << "\n";
        }
        const String &GetIndent() const final { return indents[_indentLevel]; }
        void IncreaseIndent() final { ++_indentLevel; }
        void DecreaseIndent() final
        {
            if (_indentLevel > 0) --_indentLevel;
        }

    private:
        int _indentLevel;
        inline static String indents[] = {"", "    ", "        ", "            ", "                ", "                    "};
    };

    class TextIArchive : public Archive
    {
    public:
        TextIArchive(std::istream *is) : Archive(is) {}
        void InsertIndent() override {}
        void NewLine() override {}
        void IncreaseIndent() override {}
        void DecreaseIndent() override {}

    private:
    };

#define INTERFACE_LEFT_OP(t) virtual FArchive &operator<<(const t &obj) = 0;
#define INTERFACE_RIGHT_OP(t) virtual FArchive &operator>>(t &obj) = 0;

#define IMPL_LEFT_OP(t) FArchive &operator<<(const t &obj) final;
#define IMPL_RIGHT_OP(t) FArchive &operator>>(t &obj) final;


    class AILU_API FArchive
    {
    public:
        virtual ~FArchive() = default;
        virtual bool IsSaving() const = 0;
        virtual bool IsLoading() const = 0;
        virtual bool IsLoaded() const = 0;

        INTERFACE_LEFT_OP(u8)
        INTERFACE_LEFT_OP(i8)
        INTERFACE_LEFT_OP(u16)
        INTERFACE_LEFT_OP(i16)
        INTERFACE_LEFT_OP(u32)
        INTERFACE_LEFT_OP(i32)
        INTERFACE_LEFT_OP(u64)
        INTERFACE_LEFT_OP(i64)
        INTERFACE_LEFT_OP(f32)
        INTERFACE_LEFT_OP(f64)
        INTERFACE_LEFT_OP(bool)
        INTERFACE_LEFT_OP(String)
        virtual FArchive &operator<<(Object &obj) = 0;

        INTERFACE_RIGHT_OP(u8)
        INTERFACE_RIGHT_OP(i8)
        INTERFACE_RIGHT_OP(u16)
        INTERFACE_RIGHT_OP(i16)
        INTERFACE_RIGHT_OP(u32)
        INTERFACE_RIGHT_OP(i32)
        INTERFACE_RIGHT_OP(u64)
        INTERFACE_RIGHT_OP(i64)
        INTERFACE_RIGHT_OP(f32)
        INTERFACE_RIGHT_OP(f64)
        INTERFACE_RIGHT_OP(bool)
        INTERFACE_RIGHT_OP(String)
        virtual FArchive &operator>>(Object &obj) = 0;

        virtual void Serialize(void *data, u64 size) = 0;
        virtual void Deserialize(void *data, u64 size) = 0;

        virtual void Save(const Path & sys_path) = 0;
        virtual void Load(const Path &sys_path) = 0;
    };

    class AILU_API FStructedArchive : public FArchive
    {
    public:
        enum class EStructedDataType
        {
            kInt,
            kUInt,
            kFloat,
            kBool,
            kString,
            kStruct,
        };
        template<typename T>
        constexpr static EStructedDataType GetStructedType()
        {
            if constexpr (
                    std::is_same_v<T, i8> || std::is_same_v<T, i16> ||
                    std::is_same_v<T, i32> || std::is_same_v<T, i64>)
            {
                return EStructedDataType::kInt;
            }
            else if constexpr (
                    std::is_same_v<T, u8> || std::is_same_v<T, u16> ||
                    std::is_same_v<T, u32> || std::is_same_v<T, u64>)
            {
                return EStructedDataType::kUInt;
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return EStructedDataType::kBool;
            }
            else if constexpr (std::is_same_v<T, String>)
            {
                return EStructedDataType::kString;
            }
            else if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>)
            {
                return EStructedDataType::kFloat;
            }
            else
            {
                return EStructedDataType::kStruct;
            }
        }

        constexpr static u64 GetStructedTypeSize(EStructedDataType type)
        {
            switch (type)
            {
            case EStructedDataType::kInt:
                return sizeof(i64);
            case EStructedDataType::kUInt:
                return sizeof(u64);
            case EStructedDataType::kFloat:
                return sizeof(f64);
            case EStructedDataType::kBool:
                return sizeof(bool);
            case EStructedDataType::kString:
                return sizeof(String);
            case EStructedDataType::kStruct:
                return 16;
            default:
                return 0;
            }
        }
        virtual ~FStructedArchive() = default;
        virtual bool IsSaving() const = 0;
        virtual bool IsLoading() const = 0;
        
        virtual void BeginObject(const String &name) = 0;
        virtual void EndObject() = 0;

        virtual void BeginArray(u64 size,EStructedDataType type) = 0;
        //begin read
        virtual u32  BeginArray(EStructedDataType& type) = 0;
        virtual void EndArray() = 0;

        virtual void WriteKey(const String &key) = 0;

        // 写基本类型
        virtual void WriteBool(bool v) = 0;
        virtual void WriteInt(i64 v) = 0;
        virtual void WriteUInt(u64 v) = 0;
        virtual void WriteFloat(f64 v) = 0;
        virtual void WriteString(const String &v) = 0;
        virtual void ReadBool(bool& v) = 0;
        virtual void ReadInt(i64 &v) = 0;
        virtual void ReadUInt(u64 &v) = 0;
        virtual void ReadFloat(f64 &v) = 0;
        virtual void ReadString(String &v) = 0;
    };

    ACLASS()
    class AILU_API SerializeObject : public Object
    {
        GENERATED_BODY()
    public:
        virtual ~SerializeObject() = default;
        virtual void Serialize(FArchive &ar) 
        {
            Type *class_type = GetType();
            auto sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar != nullptr)
            {
                sar->BeginObject("_type_name");
                *sar << class_type->FullName();
                sar->EndObject();
            }
            while (class_type != nullptr)
            {
                for (auto &p: class_type->GetProperties())
                {
                    p.Serialize(this, ar);
                }
                class_type = class_type->BaseType();
            }
        }
        virtual void Deserialize(FArchive &ar)
        {
            Type *class_type = GetType();
            while (class_type != nullptr)
            {
                for (auto &p: class_type->GetProperties())
                {
                    p.Deserialize(this, ar);
                }
                class_type = class_type->BaseType();
            }
        }
    };

    template<typename T>
    concept Serializable = requires(T a, FArchive &ar) {
        { a.Serialize(ar) } -> std::same_as<void>;
    };

    template<typename T>
    concept Deserializable = requires(T a, FArchive &ar) {
        { a.Deserialize(ar) } -> std::same_as<void>;
    };

    template<typename T>
    struct SerializerWrapper
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar && name) sar->BeginObject(*name);

            if constexpr (std::is_enum_v<T>)
            {
                const Enum *enum_type = StaticEnum<T>();
                if (enum_type == nullptr)
                {
                    LOG_ERROR(" SerializerWrapper::Serialize(object: {}) : enum_type is nullptr", name ? *name : "noname");
                    return;
                }
                u32 id = *reinterpret_cast<u32 *>(data);
                ar << enum_type->GetNameByIndex(id);
            }
            else if constexpr (std::is_fundamental_v<T>)
            {
                ar << *reinterpret_cast<T *>(data);
            }
            else if constexpr (std::is_base_of_v<SerializeObject, T>)
            {
                T *obj = reinterpret_cast<T *>(data);
                obj->Serialize(ar);
            }
            else if constexpr (Serializable<T>)
            {
                T *obj = reinterpret_cast<T *>(data);
                obj->Serialize(ar);
            }
            else
            {
                Type *class_type = StaticClass<T>();
                if (class_type == nullptr)
                {
                    if (sar && name) 
                        sar->EndObject();
                    LOG_ERROR(" SerializerWrapper::Serialize(object: {}) : class_type is nullptr", name ? *name : "noname");
                    return;
                }
                T *obj = reinterpret_cast<T *>(data);
                for (const auto &p: class_type->GetProperties())
                {
                    p.Serialize(obj, ar);
                }
            }

            if (sar && name) sar->EndObject();
        }

        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar && name) sar->BeginObject(*name);

            if constexpr (std::is_enum_v<T>)
            {
                const Enum *enum_type = StaticEnum<T>();
                if (enum_type == nullptr)
                {
                    LOG_ERROR(" SerializerWrapper::Serialize(object: {}) : enum_type is nullptr", name ? *name : "noname");
                    return;
                }
                String enum_name;
                ar >> enum_name;
                i32 idx = enum_type->GetIndexByName(enum_name);
                if (idx == -1)
                {
                    LOG_ERROR("Enum {} not found in {}", enum_name, enum_type->Name());
                }
                reinterpret_cast<u32 *>(data)[0] = idx;
            }
            else if constexpr (std::is_fundamental_v<T>)
            {
                T tmp{};
                ar >> tmp;
                *reinterpret_cast<T *>(data) = tmp;
            }
            else if constexpr (std::is_base_of_v<SerializeObject, T>)
            {
                T *obj = reinterpret_cast<T *>(data);
                obj->Deserialize(ar);
            }
            else if constexpr (Deserializable<T>)
            {
                T *obj = reinterpret_cast<T *>(data);
                obj->Deserialize(ar);
            }
            else
            {
                Type *class_type = StaticClass<T>();
                if (class_type == nullptr)
                {
                    if (sar && name)
                        sar->EndObject();
                    LOG_ERROR(" SerializerWrapper::Deserialize(object: {}) : class_type is nullptr", name ? *name : "noname");
                    return;
                }
                T *obj = reinterpret_cast<T *>(data);
                for (const auto &p: class_type->GetProperties())
                {
                    p.Deserialize(obj, ar);
                }
            }

            if (sar && name) sar->EndObject();
        }
    };

    template<typename T>
    void SerializePrimitive(void *data, FArchive &ar, const String *name = nullptr)
    {
        return SerializerWrapper<T>::Serialize(data, ar, name);
    }

    template<typename T>
    void DeserializePrimitive(void *data, FArchive &ar, const String *name = nullptr)
    {
        return SerializerWrapper<T>::Deserialize(data, ar, name);
    }
    //--------------------------------------------------------------basic save/load------------------------------------------------------------
}// namespace Ailu
#endif//