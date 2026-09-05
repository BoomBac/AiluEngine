#ifndef __SERIALIZE_H__
#define __SERIALIZE_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/ReflectionMacros.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/Path.h"
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include "Type.h"
#include "generated/Serialize.gen.h"
namespace Ailu
{
    class Archive;
    class AILU_API Archive
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
    protected:
        std::ostream *_os;
        std::istream *_is;
        u32 _version = 0;
    };
    struct AILU_API IndentBlock
    {
        IndentBlock(Archive &arch) : _arch(arch) 
        { 
            arch.IncreaseIndent();
        }
        ~IndentBlock() { _arch.DecreaseIndent(); }
        Archive& _arch;
    };
    class AILU_API TextOArchive : public Archive
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

    class AILU_API TextIArchive : public Archive
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

        virtual bool Save(const Path & sys_path) = 0;
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
        // Structured archives can omit fields when loading older documents.  The default keeps
        // non-JSON archive implementations strict while allowing compatible optional-field checks.
        virtual bool HasField(const String &name) { return true; }
    };

    ACLASS()
    class AILU_API SerializeObject : public Object
    {
        GENERATED_BODY()
    public:
        virtual ~SerializeObject() = default;
        virtual void Serialize(FArchive &ar);
        virtual void Deserialize(FArchive &ar);
        virtual void PostDeserialize() { /*optional override*/ }
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
                    if (sar && name)
                        sar->EndObject();
                    LOG_ERROR(" SerializerWrapper::Serialize(object: {}) : enum_type is nullptr", name ? *name : "noname");
                    return;
                }
                using UnderlyingType = std::underlying_type_t<T>;
                const u32 id = static_cast<u32>(static_cast<UnderlyingType>(*reinterpret_cast<T *>(data)));
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
                const Type *class_type = StaticClass<T>();
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
            if (sar && name && sar->IsLoading() && !sar->HasField(*name))
                return;
            if (sar && name) sar->BeginObject(*name);

            if constexpr (std::is_enum_v<T>)
            {
                const Enum *enum_type = StaticEnum<T>();
                if (enum_type == nullptr)
                {
                    if (sar && name)
                        sar->EndObject();
                    LOG_ERROR(" SerializerWrapper::Serialize(object: {}) : enum_type is nullptr", name ? *name : "noname");
                    return;
                }
                String enum_name;
                ar >> enum_name;
                i32 idx = enum_type->GetIndexByName(enum_name);
                if (idx == -1)
                {
                    LOG_ERROR("Enum {} not found in {}", enum_name, enum_type->Name());
                    if (sar && name) sar->EndObject();
                    return;
                }
                *reinterpret_cast<T *>(data) = static_cast<T>(idx);
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
                const Type *class_type = StaticClass<T>();
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
    struct SerializerWrapper<Ref<T>>
    {
        static_assert(std::is_base_of_v<SerializeObject, T>, "SerializerWrapper<Ref<T>> only supports SerializeObject-derived types");

        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper<Ref<T>>::Serialize: data is null");
                return;
            }

            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar && name) sar->BeginObject(*name);

            auto *ref = reinterpret_cast<Ref<T> *>(data);
            bool has_value = ref->get() != nullptr;
            const String has_value_name = "_has_value";
            SerializerWrapper<bool>::Serialize(&has_value, ar, &has_value_name);
            if (has_value)
            {
                static_cast<SerializeObject *>(ref->get())->Serialize(ar);
            }

            if (sar && name) sar->EndObject();
        }

        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper<Ref<T>>::Deserialize: data is null");
                return;
            }

            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar && name) sar->BeginObject(*name);

            auto *ref = reinterpret_cast<Ref<T> *>(data);
            bool has_value = false;
            const String has_value_name = "_has_value";
            SerializerWrapper<bool>::Deserialize(&has_value, ar, &has_value_name);
            if (!has_value)
            {
                ref->reset();
                if (sar && name) sar->EndObject();
                return;
            }

            if (sar == nullptr)
            {
                if constexpr (std::is_abstract_v<T>)
                {
                    LOG_ERROR("SerializerWrapper<Ref<T>>::Deserialize requires FStructedArchive for abstract SerializeObject refs");
                    ref->reset();
                }
                else
                {
                    auto value = MakeRef<T>();
                    value->Deserialize(ar);
                    *ref = std::move(value);
                }

                if (sar && name) sar->EndObject();
                return;
            }

            const String type_name_field = "_type_name";
            String type_name;
            sar->BeginObject(type_name_field);
            ar >> type_name;
            sar->EndObject();

            const Type *object_type = Type::Find(type_name);
            if (object_type == nullptr)
            {
                LOG_ERROR("SerializerWrapper<Ref<T>>::Deserialize: type {} not found", type_name);
                ref->reset();
                if (sar && name) sar->EndObject();
                return;
            }

            Object *raw_instance = object_type->CreateInstance<Object>();
            if (raw_instance == nullptr)
            {
                LOG_ERROR("SerializerWrapper<Ref<T>>::Deserialize: failed to construct type {}", type_name);
                ref->reset();
                if (sar && name) sar->EndObject();
                return;
            }

            T *instance = dynamic_cast<T *>(raw_instance);
            if (instance == nullptr)
            {
                LOG_ERROR("SerializerWrapper<Ref<T>>::Deserialize: type {} is incompatible with requested ref", type_name);
                delete raw_instance;
                ref->reset();
                if (sar && name) sar->EndObject();
                return;
            }

            instance->Deserialize(ar);
            *ref = Ref<T>(instance);

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
