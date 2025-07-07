//
// Created by 22292 on 2024/11/1.
//

#include "Inc/Objects/Type.h"
#include <Framework/Common/Utils.h>
#include "Framework/Math/ALMath.hpp"
#include "Framework/Common/Log.h"

namespace Ailu
{
    static bool IsEnumType(const String &type_name)
    {
        bool is_enum = su::BeginWith(type_name, "E");
        is_enum &= static_cast<bool>(isupper(type_name[1]));
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
        else if (name == "Vector2UInt")
            return EDataType::kVec2UInt;
        else if (name == "Vector3UInt")
            return EDataType::kVec3UInt;
        else if (name == "Vector4UInt")
            return EDataType::kVec4UInt;
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

String PropertyInfo::StringValue(void *instance) const
    {
        const char *ptr = reinterpret_cast<char *>(instance) + _offset;

        auto static formatVec = [](auto &v, int dim) -> String
        {
            switch (dim)
            {
                case 2:
                    return std::format("{},{}", v.data[0], v.data[1]);
                case 3:
                    return std::format("{},{},{}", v.data[0], v.data[1], v.data[2]);
                case 4:
                    return std::format("{},{},{},{}", v.data[0], v.data[1],v.data[2], v.data[3]);
                default:
                    return "[Invalid Vector]";
            }
        };

        switch (_data_type)
        {
            case Ailu::EDataType::kInt8:
                return std::format("{}", *reinterpret_cast<const i8 *>(ptr));
            case Ailu::EDataType::kInt16:
                return std::format("{}", *reinterpret_cast<const i16 *>(ptr));
            case Ailu::EDataType::kInt32:
                return std::format("{}", *reinterpret_cast<const i32 *>(ptr));
            case Ailu::EDataType::kInt64:
                return std::format("{}", *reinterpret_cast<const i64 *>(ptr));
            case Ailu::EDataType::kUInt8:
                return std::format("{}", *reinterpret_cast<const u8 *>(ptr));
            case Ailu::EDataType::kUInt16:
                return std::format("{}", *reinterpret_cast<const u16 *>(ptr));
            case Ailu::EDataType::kUInt32:
                return std::format("{}", *reinterpret_cast<const u32 *>(ptr));
            case Ailu::EDataType::kUInt64:
                return std::format("{}", *reinterpret_cast<const u64 *>(ptr));
            case Ailu::EDataType::kFloat:
                return std::format("{:.6f}", *reinterpret_cast<const float *>(ptr));
            case Ailu::EDataType::kDouble:
                return std::format("{:.6f}", *reinterpret_cast<const double *>(ptr));
            case Ailu::EDataType::kChar:
                return std::format("'{}'", *reinterpret_cast<const char *>(ptr));
            case Ailu::EDataType::kBool:
                return *reinterpret_cast<const bool *>(ptr) ? "true" : "false";
            case Ailu::EDataType::kString:
                return *reinterpret_cast<const std::string *>(ptr);
            case Ailu::EDataType::kVec2:
                return formatVec(*reinterpret_cast<const Vector2f *>(ptr), 2);
            case Ailu::EDataType::kVec3:
                return formatVec(*reinterpret_cast<const Vector3f *>(ptr), 3);
            case Ailu::EDataType::kVec4:
                return formatVec(*reinterpret_cast<const Vector4f *>(ptr), 4);
            case Ailu::EDataType::kVec2Int:
                return formatVec(*reinterpret_cast<const Vector2Int *>(ptr), 2);
            case Ailu::EDataType::kVec3Int:
                return formatVec(*reinterpret_cast<const Vector3Int *>(ptr), 3);
            case Ailu::EDataType::kVec4Int:
                return formatVec(*reinterpret_cast<const Vector4Int *>(ptr), 4);
            case Ailu::EDataType::kVec2UInt:
                return formatVec(*reinterpret_cast<const Vector2UInt *>(ptr), 2);
            case Ailu::EDataType::kVec3UInt:
                return formatVec(*reinterpret_cast<const Vector3UInt *>(ptr), 3);
            case Ailu::EDataType::kVec4UInt:
                return formatVec(*reinterpret_cast<const Vector4UInt *>(ptr), 4);
            case Ailu::EDataType::kEnum:
                return std::format("{}", static_cast<int>(*reinterpret_cast<const i32 *>(ptr)));
            case Ailu::EDataType::kPtr:
                return std::format("ptr: {}", *reinterpret_cast<void *const *>(ptr));
            case Ailu::EDataType::kRef:
                return std::format("ref: {}", *reinterpret_cast<void *const *>(ptr));
            case Ailu::EDataType::kObject:
                return "[Object]";
            case Ailu::EDataType::kVoid:
                return "[Void]";
            case Ailu::EDataType::kNone:
            default:
                return "[Unknown]";
        }
    }



bool PropertyInfo::SetValueFromString(void *instance, const String &str) const
    {
        char *ptr = reinterpret_cast<char *>(instance) + _offset;

        auto parseVec = [](const String &s, auto &v, int dim) -> bool
        {
            int matched = 0;
            switch (dim)
            {
                case 2:
                    matched = sscanf_s(s.c_str(), "%f,%f", &v.data[0], &v.data[1]);
                    return matched == 2;
                case 3:
                    matched = sscanf_s(s.c_str(), "%f, %f, %f", &v.data[0], &v.data[1], &v.data[2]);
                    return matched == 3;
                case 4:
                    matched = sscanf_s(s.c_str(), "%f, %f, %f, %f", &v.data[0], &v.data[1], &v.data[2], &v.data[3]);
                    return matched == 4;
                default:
                    return false;
            }
        };

        try
        {
            switch (_data_type)
            {
                case Ailu::EDataType::kInt8:
                    *reinterpret_cast<i8 *>(ptr) = static_cast<i8>(std::stoi(str));
                    return true;
                case Ailu::EDataType::kInt16:
                    *reinterpret_cast<i16 *>(ptr) = static_cast<i16>(std::stoi(str));
                    return true;
                case Ailu::EDataType::kInt32:
                    *reinterpret_cast<i32 *>(ptr) = std::stoi(str);
                    return true;
                case Ailu::EDataType::kInt64:
                    *reinterpret_cast<i64 *>(ptr) = std::stoll(str);
                    return true;
                case Ailu::EDataType::kUInt8:
                    *reinterpret_cast<u8 *>(ptr) = static_cast<u8>(std::stoul(str));
                    return true;
                case Ailu::EDataType::kUInt16:
                    *reinterpret_cast<u16 *>(ptr) = static_cast<u16>(std::stoul(str));
                    return true;
                case Ailu::EDataType::kUInt32:
                    *reinterpret_cast<u32 *>(ptr) = std::stoul(str);
                    return true;
                case Ailu::EDataType::kUInt64:
                    *reinterpret_cast<u64 *>(ptr) = std::stoull(str);
                    return true;
                case Ailu::EDataType::kFloat:
                    *reinterpret_cast<float *>(ptr) = std::stof(str);
                    return true;
                case Ailu::EDataType::kDouble:
                    *reinterpret_cast<double *>(ptr) = std::stod(str);
                    return true;
                case Ailu::EDataType::kBool:
                    *reinterpret_cast<bool *>(ptr) = (str == "true" || str == "1" || str == "True");
                    return true;
                case Ailu::EDataType::kChar:
                    if (!str.empty())
                    {
                        *reinterpret_cast<char *>(ptr) = str[0];
                        return true;
                    }
                    return false;
                case Ailu::EDataType::kString:
                    *reinterpret_cast<std::string *>(ptr) = str;
                    return true;
                case Ailu::EDataType::kVec2:
                    return parseVec(str, *reinterpret_cast<Vector2f *>(ptr), 2);
                case Ailu::EDataType::kVec3:
                    return parseVec(str, *reinterpret_cast<Vector3f *>(ptr), 3);
                case Ailu::EDataType::kVec4:
                    return parseVec(str, *reinterpret_cast<Vector4f *>(ptr), 4);
                case Ailu::EDataType::kVec2Int:
                    return sscanf_s(str.c_str(), "%d,%d", &reinterpret_cast<Vector2Int *>(ptr)->x, &reinterpret_cast<Vector2Int *>(ptr)->y) == 2;
                case Ailu::EDataType::kVec3Int:
                    return sscanf_s(str.c_str(), "%d,%d,%d", &reinterpret_cast<Vector3Int *>(ptr)->x, &reinterpret_cast<Vector3Int *>(ptr)->y, &reinterpret_cast<Vector3Int *>(ptr)->z) == 3;
                case Ailu::EDataType::kVec4Int:
                    return sscanf_s(str.c_str(), "%d,%d,%d,%d", &reinterpret_cast<Vector4Int *>(ptr)->x, &reinterpret_cast<Vector4Int *>(ptr)->y, &reinterpret_cast<Vector4Int *>(ptr)->z, &reinterpret_cast<Vector4Int *>(ptr)->w) == 4;
                case Ailu::EDataType::kVec2UInt:
                    return sscanf_s(str.c_str(), "%u,%u", &reinterpret_cast<Vector2UInt *>(ptr)->x, &reinterpret_cast<Vector2UInt *>(ptr)->y) == 2;
                case Ailu::EDataType::kVec3UInt:
                    return sscanf_s(str.c_str(), "%u,%u,%u", &reinterpret_cast<Vector3UInt *>(ptr)->x, &reinterpret_cast<Vector3UInt *>(ptr)->y, &reinterpret_cast<Vector3UInt *>(ptr)->z) == 3;
                case Ailu::EDataType::kVec4UInt:
                    return sscanf_s(str.c_str(), "%u,%u,%u,%u", &reinterpret_cast<Vector4UInt *>(ptr)->x, &reinterpret_cast<Vector4UInt *>(ptr)->y, &reinterpret_cast<Vector4UInt *>(ptr)->z, &reinterpret_cast<Vector4UInt *>(ptr)->w) == 4;
                case Ailu::EDataType::kEnum:
                    *reinterpret_cast<i32 *>(ptr) = std::stoi(str);
                    return true;
                case Ailu::EDataType::kPtr:
                case Ailu::EDataType::kRef:
                case Ailu::EDataType::kObject:
                case Ailu::EDataType::kVoid:
                case Ailu::EDataType::kNone:
                default:
                    AL_ASSERT_MSG(false, "PropertyInfo::SetValueFromString: Unsupported type");
                    return false;
            }
        }
        catch (const std::exception &e)
        {
            LOG_WARNING("PropertyInfo::SetValueFromString parse error: {}", e.what());
            return false;
        }
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
