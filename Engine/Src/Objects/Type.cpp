//
// Created by 22292 on 2024/11/1.
//

#include "Inc/Objects/Type.h"
#include <Framework/Common/Utils.h>
namespace Ailu
{
    static bool IsEnumType(const String &type_name)
    {
        bool is_enum = su::BeginWith(type_name, "E");
        is_enum &= isupper(type_name[1]);
        return is_enum;
    }
    static EDataType GetDataTypeFromName(const String &name)
    {
        if (name == "bool")
            return EDataType::kBool;
        else if (name == "char")
            return EDataType::kChar;
        else if (name == "i8")
            return EDataType::kInt8;
        else if (name == "i16")
            return EDataType::kInt16;
        else if (name == "i32" || name == "int")
            return EDataType::kInt32;
        else if (name == "i64")
            return EDataType::kInt64;
        else if (name == "u8")
            return EDataType::kUInt8;
        else if (name == "u16")
            return EDataType::kUInt16;
        else if (name == "u32")
            return EDataType::kUInt32;
        else if (name == "u64")
            return EDataType::kUInt64;
        else if (name == "float" || name == "f32")
            return EDataType::kFloat;
        else if (name == "double" || name == "f64")
            return EDataType::kDouble;
        else if (name == "Vector2f")
            return EDataType::kVec2;
        else if (name == "Vector3f")
            return EDataType::kVec3;
        else if (name == "Vector4f" || name == "Color")
            return EDataType::kVec4;
        else if (name == "Vector2Int")
            return EDataType::kVec2Int;
        else if (name == "Vector3Int")
            return EDataType::kVec3Int;
        else if (name == "Vector4Int")
            return EDataType::kVec4Int;
        else if (name == "String" || name == "string")
            return EDataType::kString;
        else if (su::EndWith(name, "*"))
            return EDataType::kPtr;
        else if (IsEnumType(name))
            return EDataType::kEnum;
        else
            return EDataType::kNone;
    }

    MemberInfo::MemberInfo(const MemberInfoInitializer &initializer)
    {
        _name = initializer._name;
        _type = initializer._type;
        _type_name = initializer._type_name;
        _is_public = initializer._is_public;
        _is_static = initializer._is_static;
        _offset = initializer._offset;
        _member_ptr = initializer._member_ptr;
        _meta = initializer._meta;
        _is_const = initializer._is_const;
    }
    MemberInfo::MemberInfo() : MemberInfo(MemberInfoInitializer())
    {
    }
    PropertyInfo::PropertyInfo(const MemberInfoInitializer &initializer) : MemberInfo(initializer)
    {
        _data_type = GetDataTypeFromName(initializer._type_name);
    }
    //------------------------------------------------------------------------------------------------------------
    void Type::RegisterType(Type *type)
    {
        std::once_flag once;
        std::call_once(once, [&]()
                       {
            if (type->Name() == "Object")
                {
                    type->_base = type;
                    type->_base_name = type->FullName();
                } });
        if (!s_global_types.contains(type->FullName()))
        {
            for (auto &it: s_global_types)
            {
                auto &[full_name, cur_type] = it;
                if (cur_type->_base_name == type->FullName())
                {
                    cur_type->_base = type;
                    type->_derived_types.insert(cur_type);
                }
            }
            s_global_types[type->FullName()] = type;
            if (!type->_base_name.empty() && s_global_types.contains(type->_base_name))
            {
                Type *base_type = s_global_types[type->_base_name];
                if (base_type != type->_base)
                {
                    type->_base = base_type;
                    base_type->_derived_types.insert(type);
                }
            }
        }
    }


    Type::Type()
    {
        _base = nullptr;
        _name = "Type";
        _full_name = "Ailu::Type";
        _namespace = "Ailu";
        _is_class = true;
        _is_abstract = false;
        _size = 0u;
        _base_name = "";
    }
    Type *Type::BaseType() const
    {
        return _base;
    }
    bool Type::IsClass() const
    {
        return _is_class;
    }
    bool Type::IsAbstract() const
    {
        return _is_abstract;
    }
    String Type::FullName() const
    {
        return _full_name;
    }
    String Type::Namespace() const
    {
        return _namespace;
    }
    const Vector<PropertyInfo> &Type::GetProperties() const
    {
        return _properties;
    }
    const Vector<FunctionInfo> &Type::GetFunctions() const
    {
        return _functions;
    }
    const Vector<MemberInfo *> &Type::GetMembers() const
    {
        return _members;
    }

    Type::Type(TypeInitializer initializer)
    {
        _name = initializer._name;
        _full_name = initializer._full_name;
        _namespace = initializer._namespace;
        _is_class = initializer._is_class;
        _is_abstract = initializer._is_abstract;
        _properties = initializer._properties;
        _functions = initializer._functions;
        _size = initializer._size;
        _base_name = initializer._base_name;
        for (auto &prop: _properties)
            _members.emplace_back(&prop);
        for (auto &func: _functions)
            _members.emplace_back(&func);
        for (auto &member: _members)
            _members_lut[member->_name] = member;
    }
    u32 Type::Size() const
    {
        return _size;
    }
    PropertyInfo *Type::FindPropertyByName(const String &name)
    {
        if (_members_lut.contains(name))
            return dynamic_cast<PropertyInfo *>(_members_lut[name]);
        return nullptr;
    }
    FunctionInfo *Type::FindFunctionByName(const String &name)
    {
        if (_members_lut.contains(name))
            return dynamic_cast<FunctionInfo *>(_members_lut[name]);
        return nullptr;
    }
    const std::set<Type *> &Type::DerivedTypes() const
    {
        return _derived_types;
    }
    //---------------------------------------------------------------------------------------Enum------------------------------------------------------------------------------
    Enum::Enum(const EnumInitializer &initializer) : Object(initializer._name)
    {
        _str_to_enum_lut = initializer._str_to_enum_lut;
        for (auto &pair: _str_to_enum_lut)
        {
            _enum_to_str_lut[pair.second] = _str_to_enum_lut.find(pair.first);
            _enum_names.push_back(&pair.first);
        }
    }
    Enum *Enum::GetEnumByName(const String &name)
    {
        if (s_global_enums.contains(name))
        {
            return s_global_enums[name];
        }
        return nullptr;
    }
    void Enum::InitTypeInfo()
    {
        while (!s_registers.empty())
        {
            s_registers.front()();
            s_registers.pop();
        }
    }
    void Enum::RegisterEnum(Enum *enum_ptr)
    {
        s_global_enums[enum_ptr->Name()] = enum_ptr;
    }
    i32 Enum::GetIndexByName(const std::string &name) const
    {
        if (_str_to_enum_lut.contains(name))
        {
            return (i32) _str_to_enum_lut.at(name);
        }
        return -1;
    }
}// namespace Ailu
