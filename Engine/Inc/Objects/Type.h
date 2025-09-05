//
// Created by 22292 on 2024/11/1.
//

#ifndef AILU_TYPE_H
#define AILU_TYPE_H
#include "Object.h"
#include "ReflectTemplate.h"
#include "generated/Type.gen.h"
#include <any>
#include <functional>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

namespace Ailu
{
    enum class AILU_API EMemberType
    {
        kConstructor = 1,
        kProperty = 2,
        kFunction = 4,
        kField = 8,
        kTypeInfo = 16,
    };
    AENUM()
    enum class EDataType
    {
        kInt8,
        kInt16,
        kInt32,
        kInt64,
        kUInt8,
        kUInt16,
        kUInt32,
        kUInt64,
        kFloat,
        kDouble,
        kVec2,
        kVec3,
        kVec4,
        kVec2Int,
        kVec3Int,
        kVec4Int,
        kVec2UInt,
        kVec3UInt,
        kVec4UInt,
        kChar,
        kBool,
        kString,
        kObject,
        kVoid,
        kPtr,
        kRef,
        kEnum,
        kNone,
    };

    //for editor
    struct AILU_API Meta
    {
        HashMap<String, std::variant<String, i32, f32, bool>> _values;

        void Set(const String &key, String &value)
        {
            if (_values.contains(key) && std::holds_alternative<String>(_values[key]))
            {
                _values[key] = value;
                return;
            }
            _values[key] = value;
        }
        void Set(const String &key, i32 value)
        {
            if (_values.contains(key) && std::holds_alternative<i32>(_values[key]))
            {
                _values[key] = value;
                return;
            }
            _values[key] = value;
        }
        void Set(const String &key, f32 value)
        {
            if (_values.contains(key) && std::holds_alternative<f32>(_values[key]))
            {
                _values[key] = value;
                return;
            }
            _values[key] = value;
        }
        void Set(const String &key, bool value)
        {
            if (_values.contains(key) && std::holds_alternative<bool>(_values[key]))
            {
                _values[key] = value;
                return;
            }
            _values[key] = value;
        }

        String GetString(const String &key, const String &default_value = "") const
        {
            auto it = _values.find(key);
            return it != _values.end() && std::holds_alternative<String>(it->second) ? std::get<String>(it->second) : default_value;
        }
        int GetInt(const String &key, i32 default_value = 0) const
        {
            auto it = _values.find(key);
            return it != _values.end() && std::holds_alternative<i32>(it->second) ? std::get<i32>(it->second) : default_value;
        }
        float GetFloat(const String &key, f32 default_value = 0.f) const
        {
            auto it = _values.find(key);
            return it != _values.end() && std::holds_alternative<f32>(it->second) ? std::get<f32>(it->second) : default_value;
        }
        bool GetBool(const String &key, bool default_value = false) const
        {
            auto it = _values.find(key);
            return it != _values.end() && std::holds_alternative<bool>(it->second) ? std::get<bool>(it->second) : default_value;
        }
    };
    class Type;

    struct AILU_API TemplateParamInfo
    {
        String _param_type_name;              // 完整名字，例如 "SlotItemData" 或 "Map<int, string>"
        const Type *_param_type = nullptr;    // 反射 Type*（如果能找到）
        Vector<TemplateParamInfo> _sub_params;// 如果这个参数本身是模板，就有子参数
        static TemplateParamInfo Parse(const String &full_type);

    private:
        static TemplateParamInfo ParseTemplate(const String &s, size_t &pos);
    };

    // bit 辅助
    enum EMemberFlags : u32
    {
        kStatic = 1u << 0,
        kPublic = 1u << 1,
        kConst = 1u << 2,
        kPointer = 1u << 3,
        kRef = 1u << 4,
        kFunc = 1u << 5,
        kTemplate = 1u << 6,
    };

    class FArchive;
    using SerializeFunc = void (*)(void *field_ptr, FArchive &ar, const String *path);
    using DeserializeFunc = void (*)(void *field_ptr, FArchive &ar, const String *path);

    class AILU_API MemberInfo
    {
    public:
        friend struct MemberBuilder;
        virtual ~MemberInfo() = default;
        const String &Name() const { return _name; }
        const Meta &MetaInfo() const { return _meta; }
        u32 Flags() const { return _flags; }
        bool IsConst() const { return _flags & EMemberFlags::kConst; }
        bool IsStatic() const { return _flags & EMemberFlags::kStatic; }
        bool IsPublic() const { return _flags & EMemberFlags::kPublic; }

    protected:
        EMemberType _member_type = EMemberType::kTypeInfo;
        std::string _name;// 成员名
        u64 _offset = 0;  // 字段用
        u32 _flags = 0;   // bit flags：static/public/const/pointer/ref/func等
        Meta _meta;       // 附加元信息
    };

    class AILU_API PropertyInfo : public MemberInfo
    {
    public:
        friend struct MemberBuilder;
        const Type *GetType();
        const String &TypeName() const { return _type_name; }
        void Serialize(void *instance, FArchive &ar) const;
        void Deserialize(void *instance, FArchive &ar) const;
        template<typename T, typename ClassType>
        T &Get(ClassType &instance)
        {
            return *reinterpret_cast<T *>(reinterpret_cast<u8 *>(&instance) + _offset);
        }
        template<typename T, typename ClassType>
        void Set(ClassType &instance, const T &value) const
        {
            T *value_ptr = reinterpret_cast<T *>(reinterpret_cast<u8 *>(&instance) + _offset);
            *value_ptr = value;
        }

    public:
        SerializeFunc _serialize_fn = nullptr;
        DeserializeFunc _deserialize_fn = nullptr;

    private:
        String _type_name;               // 原始类型名（可含模板）：std::vector<int>
        TemplateParamInfo _template_info;// 模板结构树（非模板则 name=基名，subParams空）
        const Type *_type = nullptr;
    };

    class AILU_API FunctionInfo : public MemberInfo
    {
        friend class Type;
        friend struct MemberBuilder;

    public:
        // 调用成员函数
        template<typename ClassType, typename ReturnType, typename... Args>
        ReturnType Invoke(ClassType &instance, Args... args) const
        {
            if (!_mf_ptr.has_value())
            {
                throw std::runtime_error("FunctionInfo not initialized with a member function pointer.");
            }
            // 从 std::any 中提取成员函数指针
            auto funcPtr = std::any_cast<ReturnType (ClassType::*)(Args...)>(_mf_ptr);
            return (instance.*funcPtr)(args...);// 调用成员函数
        }
        const Type *GetRetType();

    public:
        String _ret_type_name;

    private:
        TemplateParamInfo _ret_template_info;
        std::any _mf_ptr;
        const Type *_ret_type;
    };

    struct AILU_API MemberBuilder
    {
        //basic
        EMemberType _mem_type = EMemberType::kTypeInfo;
        String _name = "MemberInfo";
        String _type_name = "MemberInfo";
        //flag
        bool _is_static = false;
        bool _is_public = false;
        bool _is_const = false;
        bool _is_pointer = false;
        bool _is_ref = false;
        bool _is_template = false;
        //field
        u32 _offset = 0;
        std::any _member_ptr = nullptr;
        //function
        String _ret_type_name;
        //property only
        SerializeFunc _serialize_fn = nullptr;
        DeserializeFunc _deserialize_fn = nullptr;
        Meta _meta;
        static PropertyInfo BuildProperty(MemberBuilder &b)
        {
            PropertyInfo p;
            p._member_type = EMemberType::kProperty;
            p._name = b._name;
            p._offset = b._offset;
            p._flags |= b._is_const ? EMemberFlags::kConst : 0u;
            p._flags |= b._is_static ? EMemberFlags::kStatic : 0u;
            p._flags |= b._is_public ? EMemberFlags::kPublic : 0u;
            p._flags |= b._is_pointer ? EMemberFlags::kPointer : 0u;
            p._flags |= b._is_ref ? EMemberFlags::kRef : 0u;
            p._flags |= b._is_template ? EMemberFlags::kTemplate : 0u;
            p._meta = b._meta;

            p._serialize_fn = b._serialize_fn;
            p._deserialize_fn = b._deserialize_fn;
            p._type_name = b._type_name;
            p._template_info = TemplateParamInfo::Parse(b._type_name);
            return p;
        }
        static FunctionInfo BuildFunction(MemberBuilder &b)
        {
            FunctionInfo f;
            f._member_type = EMemberType::kFunction;
            f._name = b._name;
            f._offset = b._offset;
            f._flags |= b._is_const ? EMemberFlags::kConst : 0u;
            f._flags |= b._is_static ? EMemberFlags::kStatic : 0u;
            f._flags |= b._is_public ? EMemberFlags::kPublic : 0u;
            f._meta = b._meta;
            f._ret_type_name = b._ret_type_name;
            f._ret_template_info = TemplateParamInfo::Parse(b._ret_type_name);
            f._mf_ptr = b._member_ptr;
            return f;
        }
    };

    //class AILU_API MemberInfo
    //{
    //    friend class Type;

    //public:
    //    MemberInfo();
    //    explicit MemberInfo(const MemberInfoInitializer &initializer);
    //    virtual ~MemberInfo() = default;
    //    [[nodiscard]] EMemberType MemberType() const { return _mem_type; }
    //    [[nodiscard]] const Type *SrcType() const { return _type; }
    //    [[nodiscard]] const String &Name() const { return _name; }
    //    [[nodiscard]] const String &TypeName() const { return _type_name; }
    //    [[nodiscard]] const Meta &MetaInfo() const { return _meta; }
    //    [[nodiscard]] bool IsConst() const { return _is_const; }
    //    [[nodiscard]] bool IsPointer() const { return _is_pointer; }
    //    [[nodiscard]] bool IsRef() const { return _is_ref; }
    //    [[nodiscard]] bool IsFunc() const { return _is_func; }
    //    [[nodiscard]] bool IsStatic() const { return _is_static; }
    //protected:
    //    EMemberType _mem_type;
    //    String _name;
    //    String _type_name;
    //    const Type *_type;
    //    union
    //    {
    //        struct
    //        {
    //            u32 _is_static : 1;
    //            u32 _is_public : 1;
    //            u32 _is_const : 1;
    //            u32 _is_pointer : 1;
    //            u32 _is_ref : 1;
    //            u32 _is_func : 1;
    //        };
    //        u32 _flag;
    //    };

    //    std::any _member_ptr;
    //    u64 _offset;
    //    Meta _meta;
    //};


    //class AILU_API PropertyInfo : public MemberInfo
    //{
    //    friend class Type;
    //public:
    //    PropertyInfo() : MemberInfo() { _mem_type = EMemberType::kField; }
    //    explicit PropertyInfo(const MemberInfoInitializer &initializer);
    //    ~PropertyInfo() override = default;
    //    template<typename T, typename ClassType>
    //    T GetValue(const ClassType &instance) const
    //    {
    //        if (!_member_ptr.has_value())
    //        {
    //            throw std::runtime_error("FieldInfo not initialized with a member pointer.");
    //        }
    //        const T *value_ptr = reinterpret_cast<const T *>(reinterpret_cast<const char *>(&instance) + _offset);
    //        return *value_ptr;
    //    }

    //    template<typename T, typename ClassType>
    //    void SetValue(ClassType &instance, const T &value) const
    //    {
    //        if (!_member_ptr.has_value())
    //        {
    //            throw std::runtime_error("FieldInfo not initialized with a member pointer.");
    //        }
    //        T *value_ptr = reinterpret_cast<T *>(reinterpret_cast<char *>(&instance) + _offset);
    //        *value_ptr = value;
    //    }
    //    [[nodiscard]] const EDataType &DataType() const { return _data_type; }
    //    /// <summary>
    //    /// 获取属性的字符串值,eg: uint8_t -> "255", bool -> "true", String -> "Hello"
    //    /// </summary>
    //    /// <returns></returns>
    //    [[nodiscard]] String StringValue(void* instance) const;
    //    /// <summary>
    //    /// Sets the value of an instance from a string representation.
    //    /// </summary>
    //    /// <param name="instance">A pointer to the instance whose value will be set.</param>
    //    /// <param name="str">The string containing the value to assign.</param>
    //    /// <returns>True if the value was successfully set from the string; otherwise, false.</returns>
    //    bool SetValueFromString(void *instance, const String &str) const;

    //    void Serialize(void *instance, FArchive &ar) const;
    //    void Deserialize(void *instance, FArchive &ar) const;
    //private:
    //    void *GetFieldPtr(void *instance) const
    //    {
    //        return reinterpret_cast<u8 *>(instance) + _offset;
    //    }
    //private:
    //    SerializeFunc _serialize_fn = nullptr;
    //    DeserializeFunc _deserialize_fn = nullptr;
    //    EDataType _data_type = EDataType::kNone;
    //    TemplateParamInfo _template_params;
    //};

    //class AILU_API FunctionInfo : public MemberInfo
    //{
    //    friend class Type;

    //public:
    //    FunctionInfo() : MemberInfo() { _mem_type = EMemberType::kFunction; }
    //    explicit FunctionInfo(const MemberInfoInitializer &initializer) : MemberInfo(initializer) {}
    //    ~FunctionInfo() override = default;
    //    // 调用成员函数
    //    template<typename ClassType, typename ReturnType, typename... Args>
    //    ReturnType Invoke(ClassType &instance, Args... args) const
    //    {
    //        if (!_member_ptr.has_value())
    //        {
    //            throw std::runtime_error("FunctionInfo not initialized with a member function pointer.");
    //        }
    //        // 从 std::any 中提取成员函数指针
    //        auto funcPtr = std::any_cast<ReturnType (ClassType::*)(Args...)>(_member_ptr);
    //        return (instance.*funcPtr)(args...);// 调用成员函数
    //    }
    //};
    class Type;
    using TypeRegisterFunc = std::function<Type *()>;
    using ObjectConstructor = std::function<void *()>;
    struct AILU_API TypeInitializer
    {
        String _name;
        String _namespace;
        String _full_name;
        String _base_name;
        bool _is_class;
        bool _is_abstract;
        u32 _size;
        Vector<PropertyInfo> _properties;
        Vector<FunctionInfo> _functions;
        ObjectConstructor _constructor = nullptr;// 用于创建对象的构造函数
    };

    class AILU_API Type : public Object
    {
        friend class ClassTypeRegister;
        using RegisterFunc = std::function<Type *()>;
    public:
        static void RegisterType(Type *type);
        static Type *Find(const String &name);

    public:
        Type();
        explicit Type(TypeInitializer initializer);
        [[nodiscard]] Type *BaseType() const;
        [[nodiscard]] bool IsClass() const;
        [[nodiscard]] bool IsAbstract() const;
        [[nodiscard]] bool IsEnum() const { return _is_enum; };
        template<typename T>
        [[nodiscard]] T *CreateInstance()
        {
            if (_constructor)
            {
                return reinterpret_cast<T*>(_constructor());
            }
            return nullptr;
        };
        //name without namespace
        [[nodiscard]] String FullName() const;
        [[nodiscard]] String Namespace() const;
        [[nodiscard]] Vector<PropertyInfo> &GetProperties();
        [[nodiscard]] const Vector<FunctionInfo> &GetFunctions() const;
        [[nodiscard]] const Vector<MemberInfo *> &GetMembers() const;
        [[nodiscard]] u32 Size() const;
        [[nodiscard]] PropertyInfo *FindPropertyByName(const String &name) const;
        [[nodiscard]] FunctionInfo *FindFunctionByName(const String &name) const;
        [[nodiscard]] const std::set<Type *> &DerivedTypes() const;
        // Iterator support
        using PropertyIterator = Vector<PropertyInfo>::const_iterator;
        using FunctionIterator = Vector<FunctionInfo>::const_iterator;
        using MemberIterator = Vector<MemberInfo *>::const_iterator;

        [[nodiscard]] PropertyIterator PropertyBegin() const { return _properties.begin(); }
        [[nodiscard]] PropertyIterator PropertyEnd() const { return _properties.end(); }
        [[nodiscard]] FunctionIterator FunctionBegin() const { return _functions.begin(); }
        [[nodiscard]] FunctionIterator FunctionEnd() const { return _functions.end(); }

    private:
        static void InitBaseTypeInfo();

    private:
        inline static Map<String, Type *> s_global_types;
        inline static HashMap<String, RegisterFunc> s_global_register;
        inline static bool s_is_base_type_init = false;
        SerializeFunc _serialize_fn;
        DeserializeFunc _deserialize_fn;

    protected:
        Type *_base;
        bool _is_class;
        bool _is_abstract;
        bool _is_enum;
        u32 _size;
        String _full_name;
        String _namespace;
        String _base_name;
        Vector<PropertyInfo> _properties;
        Vector<FunctionInfo> _functions;
        Vector<MemberInfo *> _members;
        Map<String, MemberInfo *> _members_lut;
        std::set<Type *> _derived_types;
        ObjectConstructor _constructor;
    };

    DECLARE_STATIC_TYPE(i8)
    DECLARE_STATIC_TYPE(u8)
    DECLARE_STATIC_TYPE(i16)
    DECLARE_STATIC_TYPE(u16)
    DECLARE_STATIC_TYPE(i32)
    DECLARE_STATIC_TYPE(u32)
    DECLARE_STATIC_TYPE(i64)
    DECLARE_STATIC_TYPE(u64)
    DECLARE_STATIC_TYPE(f32)
    DECLARE_STATIC_TYPE(f64)
    DECLARE_STATIC_TYPE(bool)
    DECLARE_STATIC_TYPE(String)


    class AILU_API ClassTypeRegister{
        public:
        ClassTypeRegister(Type::RegisterFunc func, const String &name)
        {
                Type::s_global_register[name] = func;
        }
    };


    struct AILU_API EnumInitializer
    {
        String _name;
        String _full_name;
        Map<String, u32> _str_to_enum_lut;
    };


    class AILU_API Enum : public Type
    {
        friend class EnumTypeRegister;

    public:
        using RegisterFunc = std::function<const Enum *()>;
        static Enum *GetEnumByName(const String &name);
        static void InitTypeInfo();
        static void RegisterEnum(Enum *enum_ptr);
        Enum() = default;
        explicit Enum(const EnumInitializer &initializer);
        u32 EnumCount() const { return static_cast<u32>(_str_to_enum_lut.size()); }
        i32 GetIndexByName(const String &name) const;
        template<typename E>
        Enum *StaticEnum()
        {
            return nullptr;
        };
        const Vector<const String *> &GetEnumNames() const { return _enum_names; };
        /// @brief 根据枚举值返回其名称
        /// @tparam E 可转化为u32的枚举值
        /// @param enum_value 枚举值
        /// @return 枚举名称
        template<class E>
        const String &GetNameByEnum(E enum_value) const
        {
            u32 index = static_cast<u32>(enum_value);
            if (_enum_to_str_lut.contains(index))
            {
                return _enum_to_str_lut.at(index)->first;
            }
            return kErrorEnumValue;
        }
        const String &GetNameByIndex(u32 index) const
        {
            if (_enum_to_str_lut.contains(index))
            {
                return _enum_to_str_lut.at(index)->first;
            }
            return kErrorEnumValue;
        }

    private:
        inline static Map<String, Enum *> s_global_enums;
        inline static Queue<RegisterFunc> s_registers;
        inline static String kErrorEnumValue = "ErrorEnumValue";

    private:
        Map<String, u32> _str_to_enum_lut;
        Map<u32, Map<String, u32>::iterator> _enum_to_str_lut;
        Vector<const String *> _enum_names;
    };

    template<typename T>
    struct TEnumReflection
    {
        static const Enum *StaticType() { return nullptr; }
    };

    class AILU_API EnumTypeRegister{
        public:
                EnumTypeRegister(Enum::RegisterFunc fn){
                        Enum::s_registers.push(fn);
}// namespace Ailu
}
;
}// namespace Ailu


#endif//AILU_TYPE_H
