//
// Created by 22292 on 2024/11/1.
//

#ifndef AILU_TYPE_H
#define AILU_TYPE_H
#include "Object.h"
#include <any>
#include <set>
#include <tuple>
#include <utility>

#include "generated/Type.gen.h"

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
        String _category = "";
        f32 _min = 0.0f, _max = 1.0f;
        bool _is_range = false;
        bool _is_float_range = true;
        bool _is_color = false;
    };

    struct AILU_API MemberInfoInitializer
    {
        EMemberType _type = EMemberType::kTypeInfo;
        String _name = "MemberInfo";
        String _type_name = "MemberInfo";
        bool _is_static = false;
        bool _is_public = false;
        u32 _offset = 0;
        std::any _member_ptr = nullptr;
        Meta _meta;
        bool _is_const = false;
    };
    class AILU_API MemberInfo
    {
        friend class Type;

    public:
        MemberInfo();
        explicit MemberInfo(const MemberInfoInitializer &initializer);
        virtual ~MemberInfo() = default;
        [[nodiscard]] EMemberType Type() const { return _type; }
        [[nodiscard]] const String &Name() const { return _name; }
        [[nodiscard]] const String &TypeName() const { return _type_name; }
        [[nodiscard]] const Meta &MetaInfo() const { return _meta; }
        [[nodiscard]] const bool &IsConst() const { return _is_const; }

    protected:
        EMemberType _type;
        String _name;
        String _type_name;
        bool _is_static;
        bool _is_public;
        bool _is_const;
        std::any _member_ptr;
        u64 _offset;
        Meta _meta;
    };
    //    class AILU_API FieldInfo : public MemberInfo
    //    {
    //        friend class Type;
    //    public:
    //        FieldInfo() : MemberInfo() { _type = EMemberType::kField; }
    //        explicit FieldInfo(const MemberInfoInitializer& initializer);
    //        ~FieldInfo() override = default;
    //        template<typename T, typename ClassType>
    //        T GetValue(const ClassType& instance) const
    //        {
    //            if (!_member_ptr.has_value()) {
    //                throw std::runtime_error("FieldInfo not initialized with a member pointer.");
    //            }
    //            auto fieldPtr = std::any_cast<T ClassType::*>(_member_ptr);
    //            return instance.*fieldPtr;
    //        }
    //
    //        template<typename T, typename ClassType>
    //        void SetValue(ClassType& instance, const T& value) const
    //        {
    //            if (!_member_ptr.has_value()) {
    //                throw std::runtime_error("FieldInfo not initialized with a member pointer.");
    //            }
    //            auto fieldPtr = std::any_cast<T ClassType::*>(_member_ptr);
    //            instance.*fieldPtr = value;
    //        }
    //        [[nodiscard]] const EDataType & DataType() const { return _data_type; }
    //    private:
    //        EDataType _data_type = EDataType::kNone;
    //    };

    class AILU_API PropertyInfo : public MemberInfo
    {
        friend class Type;

    public:
        PropertyInfo() : MemberInfo() { _type = EMemberType::kField; }
        explicit PropertyInfo(const MemberInfoInitializer &initializer);
        ~PropertyInfo() override = default;
        template<typename T, typename ClassType>
        T GetValue(const ClassType &instance) const
        {
            if (!_member_ptr.has_value())
            {
                throw std::runtime_error("FieldInfo not initialized with a member pointer.");
            }
            const T *value_ptr = reinterpret_cast<const T *>(reinterpret_cast<const char *>(&instance) + _offset);
            return *value_ptr;
            // auto fieldPtr = std::any_cast<T ClassType::*>(_member_ptr);
            // return instance.*fieldPtr;
        }

        template<typename T, typename ClassType>
        void SetValue(ClassType &instance, const T &value) const
        {
            if (!_member_ptr.has_value())
            {
                throw std::runtime_error("FieldInfo not initialized with a member pointer.");
            }
            T *value_ptr = reinterpret_cast<T *>(reinterpret_cast<char *>(&instance) + _offset);
            *value_ptr = value;
            // auto fieldPtr = std::any_cast<T ClassType::*>(_member_ptr);
            // instance.*fieldPtr = value;
        }
        [[nodiscard]] const EDataType &DataType() const { return _data_type; }

    private:
        EDataType _data_type = EDataType::kNone;
    };

    class AILU_API FunctionInfo : public MemberInfo
    {
        friend class Type;

    public:
        FunctionInfo() : MemberInfo() { _type = EMemberType::kFunction; }
        explicit FunctionInfo(const MemberInfoInitializer &initializer) : MemberInfo(initializer) {}
        ~FunctionInfo() override = default;
        // 调用成员函数
        template<typename ClassType, typename ReturnType, typename... Args>
        ReturnType Invoke(ClassType &instance, Args... args) const
        {
            if (!_member_ptr.has_value())
            {
                throw std::runtime_error("FunctionInfo not initialized with a member function pointer.");
            }
            // 从 std::any 中提取成员函数指针
            auto funcPtr = std::any_cast<ReturnType (ClassType::*)(Args...)>(_member_ptr);
            return (instance.*funcPtr)(args...);// 调用成员函数
        }
    };
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
    };

    class AILU_API Type : public Object
    {
    public:
        static void RegisterType(Type *type);

    public:
        Type();
        explicit Type(TypeInitializer initializer);
        [[nodiscard]] Type *BaseType() const;
        [[nodiscard]] bool IsClass() const;
        [[nodiscard]] bool IsAbstract() const;
        //name without namespace
        [[nodiscard]] String FullName() const;
        [[nodiscard]] String Namespace() const;
        [[nodiscard]] const Vector<PropertyInfo> &GetProperties() const;
        [[nodiscard]] const Vector<FunctionInfo> &GetFunctions() const;
        [[nodiscard]] const Vector<MemberInfo *> &GetMembers() const;
        [[nodiscard]] u32 Size() const;
        [[nodiscard]] PropertyInfo *FindPropertyByName(const String &name);
        [[nodiscard]] FunctionInfo *FindFunctionByName(const String &name);
        [[nodiscard]] const std::set<Type *> &DerivedTypes() const;


    private:
        inline static Map<String, Type *> s_global_types;

    private:
        Type *_base;
        bool _is_class;
        bool _is_abstract;
        u32 _size;
        String _full_name;
        String _namespace;
        String _base_name;
        Vector<PropertyInfo> _properties;
        Vector<FunctionInfo> _functions;
        Vector<MemberInfo *> _members;
        Map<String, MemberInfo *> _members_lut;
        std::set<Type *> _derived_types;
    };

    struct AILU_API EnumInitializer
    {
        String _name;
        Map<String, u32> _str_to_enum_lut;
    };


    class AILU_API Enum : public Object
    {
        friend class EnumTypeRegister;

    public:
        static Enum *GetEnumByName(const String &name);
        static void InitTypeInfo();
        static void RegisterEnum(Enum *enum_ptr);
        Enum() = default;
        explicit Enum(const EnumInitializer &initializer);
        u32 EnumCount() const { return static_cast<u32>(_str_to_enum_lut.size()); }
        i32 GetIndexByName(const std::string &name) const;
        const Vector<const String *> &GetEnumNames() const { return _enum_names; };
        /// @brief 根据枚举值返回其名称
        /// @tparam E 可转化为u32的枚举值
        /// @param enum_value 枚举值
        /// @return 枚举名称
        template<class E>
        const String& GetNameByEnum(E enum_value) const
        {
            u32 index = static_cast<u32>(enum_value);
            if (_enum_to_str_lut.contains(index))
            {
                return _enum_to_str_lut[index]->first;
            }
            return kErrorEnumValue;
        }
        const String& GetNameByIndex(u32 index) const
        {
            if (_enum_to_str_lut.contains(index))
            {
                return _enum_to_str_lut.at(index)->first;
            }
            return kErrorEnumValue;
        }

    private:
        inline static Map<String, Enum *> s_global_enums;
        inline static Queue<std::function<void()>> s_registers;
        inline static String kErrorEnumValue = "ErrorEnumValue";
    private:
        Map<std::string, u32> _str_to_enum_lut;
        Map<u32, Map<std::string, u32>::iterator> _enum_to_str_lut;
        Vector<const String *> _enum_names;
    };

    class AILU_API EnumTypeRegister{
        public:
                EnumTypeRegister(std::function<void()> fn){
                        Enum::s_registers.push(fn);
}// namespace Ailu
}
;
}// namespace Ailu


#endif//AILU_TYPE_H
