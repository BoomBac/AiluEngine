//
// Created by 22292 on 2024/11/1.
//

#ifndef AILU_TYPE_H
#define AILU_TYPE_H
#include "Object.h"
#include <any>
#include <tuple>
#include <utility>


namespace Ailu
{
    enum class EMemberType
    {
        kConstructor = 1,
        kProperty = 2,
        kFunction = 4,
        kField = 8,
        kTypeInfo = 16,
    };

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
        kChar,
        kBool,
        kString,
        kObject,
        kVoid,
        kPtr,
        kRef,
        kNone,
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

    protected:
        EMemberType _type;
        String _name;
        String _type_name;
        bool _is_static;
        bool _is_public;
        std::any _member_ptr;
        u64 _offset;
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
            auto fieldPtr = std::any_cast<T ClassType:: *>(_member_ptr);
            return instance.*fieldPtr;
        }

        template<typename T, typename ClassType>
        void SetValue(ClassType &instance, const T &value) const
        {
            if (!_member_ptr.has_value())
            {
                throw std::runtime_error("FieldInfo not initialized with a member pointer.");
            }
            auto fieldPtr = std::any_cast<T ClassType:: *>(_member_ptr);
            instance.*fieldPtr = value;
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
            auto funcPtr = std::any_cast<ReturnType (ClassType:: *)(Args...)>(_member_ptr);
            return (instance.*funcPtr)(args...);// 调用成员函数
        }
    };
    struct AILU_API TypeInitializer
    {
        String _name;
        String _namespace;
        String _full_name;
        Type *_base;
        bool _is_class;
        bool _is_abstract;
        u32 _size;
        Vector<PropertyInfo> _properties;
        Vector<FunctionInfo> _functions;
    };

    class AILU_API Type : public Object
    {
    public:
        Type();
        explicit Type(TypeInitializer initializer);
        [[nodiscard]] Type* BaseType() const;
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

    private:
        Type *_base;
        bool _is_class;
        bool _is_abstract;
        u32 _size;
        String _full_name;
        String _namespace;
        Vector<PropertyInfo> _properties;
        Vector<FunctionInfo> _functions;
        Vector<MemberInfo *> _members;
        Map<String, MemberInfo *> _members_lut;
    };

    class AILU_API AEnum : public Type
    {
    public:
        AEnum() = default;
        AEnum(const std::string& name,std::map<std::string, u32> enums) : _name(name), _str_to_enum_lut(std::move(enums)) {};
        template<typename E>
        static AEnum *StaticType()
        {
            return nullptr;
        }
        i32 GetIndexByName(const std::string& name)
        {
            if (_str_to_enum_lut.contains(name))
            {
                return (i32)_str_to_enum_lut[name];
            }
            return -1;
        }
        template<class E>
        static std::string GetNameByEnum(E enum_value)
        {
            AEnum* enum_type = StaticType<E>();
            u32 index = static_cast<u32>(enum_value);
            for (const auto& pair : enum_type->_str_to_enum_lut)
            {
                if (pair.second == index)
                {
                    return pair.first;
                }
            }
            return "";
        }
    template<typename E>
    static std::vector<std::string> Names()
    {
        static std::vector<std::string> v;
        return v;
    }
    template<typename E>
    static std::vector<u32> Values()
    {
        static std::vector<u32> v;
        return v;
    }
    private:
        std::string _name;
        std::map<std::string, u32> _str_to_enum_lut;
    };
}// namespace Ailu


#endif//AILU_TYPE_H
