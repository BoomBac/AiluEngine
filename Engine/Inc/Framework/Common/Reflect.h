#pragma once
#ifndef __REFLECT_H__
#define __REFLECT_H__
#include "GlobalMarco.h"
#include <cstddef>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <optional>


//https://github.com/jsoysouvanh/Refureku

namespace Ailu
{
    enum class ESerializablePropertyType : u8
    {
        kUndefined = 0,
        kString,
        kFloat,
        kBool,
        kVector3f,
        kVector4f,
        kColor,
        kTransform,
        kTexture2D,
        kCubeMap,
        kStaticMesh,
        kRange,
        kEnum
    };
    static String GetSerializablePropertyTypeStr(const ESerializablePropertyType &type)
    {
        switch (type)
        {
            case Ailu::ESerializablePropertyType::kString:
                return "String";
            case Ailu::ESerializablePropertyType::kFloat:
                return "Float";
            case Ailu::ESerializablePropertyType::kBool:
                return "Bool";
            case Ailu::ESerializablePropertyType::kVector3f:
                return "Vector3f";
            case Ailu::ESerializablePropertyType::kVector4f:
                return "Vector4f";
            case Ailu::ESerializablePropertyType::kColor:
                return "Color";
            case Ailu::ESerializablePropertyType::kTransform:
                return "Transform";
            case Ailu::ESerializablePropertyType::kTexture2D:
                return "Texture2D";
            case Ailu::ESerializablePropertyType::kStaticMesh:
                return "StaticMesh";
            case Ailu::ESerializablePropertyType::kRange:
                return "Range";
            case Ailu::ESerializablePropertyType::kUndefined:
                return "Undefined";
            case Ailu::ESerializablePropertyType::kCubeMap:
                return "CubeMap";
            case Ailu::ESerializablePropertyType::kEnum:
                return "Enum";
            default:
                return "undefined";
        }
        return "undefined";
    }

    template<class T>
    class Property
    {
    public:
        using ValueChangeEvent = std::function<void(T)>;
        Property()
        {
            event = [](T value) {};
        }
        const T &Get() const { return _value; };
        void Set(T value)
        {
            _value = value;
            event(value);
        }
        ValueChangeEvent event;

    private:
        T _value;
    };

    struct AILU_API SerializableProperty
    {
        void *_value_ptr;
        String _name;
        String _value_name;
        ESerializablePropertyType _type;
        float _param[4];
        SerializableProperty(void *value_ptr, const String &name, const ESerializablePropertyType &type)
            : _value_ptr(value_ptr), _name(name), _type(type)
        {
            memset(_param, 0, sizeof(float) * 4);
        }

        SerializableProperty(void *value_ptr, const String &name, const String &value_name, const ESerializablePropertyType &type) : SerializableProperty(value_ptr, name, type)
        {
            memset(_param, 0, sizeof(float) * 4);
            _value_name = value_name;
        }

        SerializableProperty() : _value_ptr(nullptr), _name(""), _type(ESerializablePropertyType::kUndefined)
        {
        }

        SerializableProperty(const SerializableProperty &other)
        {
            _value_ptr = other._value_ptr;
            _value_name = other._value_name;
            _name = other._name;
            _type = other._type;
            std::copy(std::begin(other._param), std::end(other._param), std::begin(_param));
        }
        SerializableProperty &operator=(const SerializableProperty &other)
        {
            _value_ptr = other._value_ptr;
            _value_name = other._value_name;
            _name = other._name;
            _type = other._type;
            std::copy(std::begin(other._param), std::end(other._param), std::begin(_param));
            return *this;
        }
        SerializableProperty &operator=(SerializableProperty &&other) noexcept
        {
            _value_ptr = other._value_ptr;
            _value_name = other._value_name;
            _name = other._name;
            _type = other._type;
            std::copy(std::begin(other._param), std::end(other._param), std::begin(_param));
            other._value_ptr = nullptr;
            other._name = "null";
            other._value_name = "null";
            other._type = ESerializablePropertyType::kUndefined;
            memset(other._param, 0, sizeof(other._param));
            return *this;
        }
        template<typename T>
        static String ToString(const T &value);
        template<typename T>
        static std::optional<T> GetProppertyValue(const SerializableProperty &prop);
        template<typename T>
        void SetProppertyValue(const T &value)
        {
            *reinterpret_cast<T *>(_value_ptr) = value;
        }
        template<typename T>
        std::optional<T> GetProppertyValue() const
        {
            if (_value_ptr != nullptr)
                return *reinterpret_cast<T *>(_value_ptr);
            else
                return {};
        }
        inline static bool SameType(const SerializableProperty &a, const SerializableProperty &b)
        {
            return a._name == b._name && a._type == b._type;
        }
        static u16 GetValueSize(ESerializablePropertyType type)
        {
            switch (type)
            {
                case Ailu::ESerializablePropertyType::kUndefined:
                    return 0;
                case Ailu::ESerializablePropertyType::kString:
                    break;
                case Ailu::ESerializablePropertyType::kFloat:
                    return 4;
                case Ailu::ESerializablePropertyType::kEnum:
                    return 4;
                case Ailu::ESerializablePropertyType::kBool:
                    return 1;
                case Ailu::ESerializablePropertyType::kVector3f:
                    return 12;
                case Ailu::ESerializablePropertyType::kVector4f:
                    return 16;
                case Ailu::ESerializablePropertyType::kColor:
                    return 16;
                case Ailu::ESerializablePropertyType::kTransform:
                    break;
                case Ailu::ESerializablePropertyType::kTexture2D:
                    break;
                case Ailu::ESerializablePropertyType::kCubeMap:
                    break;
                case Ailu::ESerializablePropertyType::kStaticMesh:
                    break;
                case Ailu::ESerializablePropertyType::kRange:
                    return 4;
                default:
                    break;
            }
            return 0;
        }
    };


    template<typename T>
    String SerializableProperty::ToString(const T &value)
    {
        if (std::is_same<T, float>()) return std::format("{}", value);
    }

    template<typename T>
    inline std::optional<T> SerializableProperty::GetProppertyValue(const SerializableProperty &prop)
    {
        if (prop._value_ptr != nullptr)
            return *reinterpret_cast<T *>(prop._value_ptr);
        else
            return {};
    }

#define DECLARE_REFLECT_FIELD(class_name) friend void class_name##InitReflectProperties(class_name *obj);
    //#define DECLARE_REFLECT_FIELD protected: std::unordered_map<String,SerializableProperty> _properties{};\
//virtual void InitReflectProperties();\
//public:\
//    template<typename T>\
//    T GetProperty(const String& name){return *reinterpret_cast<T*>(_properties.find(name)->second._value_ptr);}\
//     SerializableProperty& GetProperty(const String& name){return _properties.find(name)->second;}\
//    std::unordered_map<String,SerializableProperty>& GetAllProperties(){return _properties;}

#define IMPLEMENT_REFLECT_FIELD(class_name) class_name##InitReflectProperties(this);

#define REFLECT_FILED_BEGIN(class_name)                            \
    static void class_name##InitReflectProperties(class_name *obj) \
    {                                                              \
        obj->_properties.clear();

#define REFLECT_FILED_END }

    //#define DECLARE_REFLECT_PROPERTY(type_name,name,value) _properties.insert(std::make_pair(#name,SerializableProperty{&value,#name,#type_name}));

#define DECLARE_REFLECT_PROPERTY(prop_type, name, value) obj->_properties.insert(std::make_pair(#name, SerializableProperty{&(obj->value), #name, prop_type}));

    namespace reflect
    {
        //--------------------------------------------------------
        // Base class of all type descriptors
        //--------------------------------------------------------

        struct TypeDescriptor
        {
            const char *name;
            size_t size;

            TypeDescriptor(const char *name, size_t size) : name{name}, size{size} {}
            virtual ~TypeDescriptor() {}
            virtual String getFullName() const { return name; }
            virtual void dump(const void *obj, int indentLevel = 0) const = 0;
        };

        //--------------------------------------------------------
        // Finding type descriptors
        //--------------------------------------------------------

        // Declare the function template that handles primitive types such as int, String, etc.:
        template<typename T>
        TypeDescriptor *getPrimitiveDescriptor();

        // A helper class to find TypeDescriptors in different ways:
        struct DefaultResolver
        {
            template<typename T>
            static char func(decltype(&T::Reflection));
            template<typename T>
            static int func(...);
            template<typename T>
            struct IsReflected
            {
                enum
                {
                    value = (sizeof(func<T>(nullptr)) == sizeof(char))
                };
            };

            // This version is called if T has a static member named "Reflection":
            template<typename T, typename std::enable_if<IsReflected<T>::value, int>::type = 0>
            static TypeDescriptor *get()
            {
                return &T::Reflection;
            }

            // This version is called otherwise:
            template<typename T, typename std::enable_if<!IsReflected<T>::value, int>::type = 0>
            static TypeDescriptor *get()
            {
                return getPrimitiveDescriptor<T>();
            }
        };

        // This is the primary class template for finding all TypeDescriptors:
        template<typename T>
        struct TypeResolver
        {
            static TypeDescriptor *get()
            {
                return DefaultResolver::get<T>();
            }
        };

        //--------------------------------------------------------
        // Type descriptors for user-defined structs/classes
        //--------------------------------------------------------

        struct TypeDescriptor_Struct : TypeDescriptor
        {
            struct Member
            {
                const char *name;
                size_t offset;
                TypeDescriptor *type;
            };

            std::vector<Member> members;

            TypeDescriptor_Struct(void (*init)(TypeDescriptor_Struct *)) : TypeDescriptor{nullptr, 0}
            {
                init(this);
            }
            TypeDescriptor_Struct(const char *name, size_t size, const std::initializer_list<Member> &init) : TypeDescriptor{nullptr, 0}, members{init}
            {
            }
            virtual void dump(const void *obj, int indentLevel) const override
            {
                std::cout << name << " {" << std::endl;
                for (const Member &member: members)
                {
                    std::cout << String(4 * (indentLevel + 1), ' ') << member.name << " = ";
                    member.type->dump((char *) obj + member.offset, indentLevel + 1);
                    std::cout << std::endl;
                }
                std::cout << String(4 * indentLevel, ' ') << "}";
            }
        };

#define REFLECT()                                     \
    friend struct reflect::DefaultResolver;           \
    static reflect::TypeDescriptor_Struct Reflection; \
    static void initReflection(reflect::TypeDescriptor_Struct *);

#define REFLECT_STRUCT_BEGIN(type)                                         \
    reflect::TypeDescriptor_Struct type::Reflection{type::initReflection}; \
    void type::initReflection(reflect::TypeDescriptor_Struct *typeDesc)    \
    {                                                                      \
        using T = type;                                                    \
        typeDesc->name = #type;                                            \
        typeDesc->size = sizeof(T);                                        \
        typeDesc->members = {

#define REFLECT_STRUCT_MEMBER(name) \
    {#name, offsetof(T, name), reflect::TypeResolver<decltype(T::name)>::get()},

#define REFLECT_STRUCT_END() \
    }                        \
    ;                        \
    }

        //--------------------------------------------------------
        // Type descriptors for std::vector
        //--------------------------------------------------------

        struct TypeDescriptor_StdVector : TypeDescriptor
        {
            TypeDescriptor *itemType;
            size_t (*getSize)(const void *);
            const void *(*getItem)(const void *, size_t);

            template<typename ItemType>
            TypeDescriptor_StdVector(ItemType *)
                : TypeDescriptor{"std::vector<>", sizeof(std::vector<ItemType>)},
                  itemType{TypeResolver<ItemType>::get()}
            {
                getSize = [](const void *vecPtr) -> size_t
                {
                    const auto &vec = *(const std::vector<ItemType> *) vecPtr;
                    return vec.size();
                };
                getItem = [](const void *vecPtr, size_t index) -> const void *
                {
                    const auto &vec = *(const std::vector<ItemType> *) vecPtr;
                    return &vec[index];
                };
            }
            virtual String getFullName() const override
            {
                return String("std::vector<") + itemType->getFullName() + ">";
            }
            virtual void dump(const void *obj, int indentLevel) const override
            {
                size_t numItems = getSize(obj);
                std::cout << getFullName();
                if (numItems == 0)
                {
                    std::cout << "{}";
                }
                else
                {
                    std::cout << "{" << std::endl;
                    for (size_t index = 0; index < numItems; index++)
                    {
                        std::cout << String(4 * (indentLevel + 1), ' ') << "[" << index << "] ";
                        itemType->dump(getItem(obj, index), indentLevel + 1);
                        std::cout << std::endl;
                    }
                    std::cout << String(4 * indentLevel, ' ') << "}";
                }
            }
        };

        // Partially specialize TypeResolver<> for std::vectors:
        template<typename T>
        class TypeResolver<std::vector<T>>
        {
        public:
            static TypeDescriptor *get()
            {
                static TypeDescriptor_StdVector typeDesc{(T *) nullptr};
                return &typeDesc;
            }
        };

    }// namespace reflect
}// namespace Ailu


#endif// !REFLECT_H__
