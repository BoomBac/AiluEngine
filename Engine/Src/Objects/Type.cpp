//
// Created by 22292 on 2024/11/1.
//

#include "Inc/Objects/Type.h"
#include <Framework/Common/Utils.h>
namespace Ailu
{
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
        else if (name == "String" || name == "string")
            return EDataType::kString;
        else if (su::EndWith(name,"*"))
            return EDataType::kPtr;
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
    }
    MemberInfo::MemberInfo() : MemberInfo(MemberInfoInitializer())
    {
    }
    PropertyInfo::PropertyInfo(const MemberInfoInitializer &initializer) : MemberInfo(initializer)
    {
        _data_type = GetDataTypeFromName(initializer._type_name);
    }
//------------------------------------------------------------------------------------------------------------
    Type::Type()
    {
        _base = nullptr;
        _name = "Type";
        _full_name = "Ailu::Type";
        _namespace = "Ailu";
        _is_class = true;
        _is_abstract = false;
        _size = 0u;
    }
    Type* Type::BaseType() const
    {
        return nullptr;
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
    const Vector<MemberInfo*> &Type::GetMembers() const
    {
        return _members;
    }

    Type::Type(TypeInitializer initializer)
    {
        _base = initializer._base;
        _name = initializer._name;
        _full_name = initializer._full_name;
        _namespace = initializer._namespace;
        _is_class = initializer._is_class;
        _is_abstract = initializer._is_abstract;
        _properties = initializer._properties;
        _functions = initializer._functions;
        _size = initializer._size;
        for(auto& prop : _properties)
            _members.emplace_back(&prop);
        for(auto& func : _functions)
            _members.emplace_back(&func);
        for (auto& member : _members)
            _members_lut[member->_name] = member;
    }
    u32 Type::Size() const
    {
        return _size;
    }
    PropertyInfo* Type::FindPropertyByName(const String &name)
    {
        if (_members_lut.contains(name))
            return dynamic_cast<PropertyInfo*>(_members_lut[name]);
        return nullptr;
    }
    FunctionInfo *Type::FindFunctionByName(const String &name)
    {
        if (_members_lut.contains(name))
            return dynamic_cast<FunctionInfo*>(_members_lut[name]);
        return nullptr;
    }
}
