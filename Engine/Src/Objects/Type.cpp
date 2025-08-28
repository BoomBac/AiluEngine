//
// Created by 22292 on 2024/11/1.
//

#include "Inc/Objects/Type.h"
#include "Objects/Serialize.h"
#include "Framework/Common/Log.h"
#include "Framework/Math/ALMath.hpp"
#include <Framework/Common/Utils.h>

namespace Ailu
{
    static bool IsEnumType(const String &type_name)
    {
        bool is_enum = su::BeginWith(type_name, "E");
        is_enum &= static_cast<bool>(isupper(type_name[1]));
        return is_enum;
    }

    // 递归解析模板
    TemplateParamInfo Ailu::TemplateParamInfo::ParseTemplate(const std::string &s, size_t &pos)
    {
        // 跳过空格
        static auto SkipSpaces = [](const std::string &s, size_t &pos)
        {
            while (pos < s.size() && std::isspace((unsigned char) s[pos])) ++pos;
        };

        // 读取一个标识符（允许 :: 和模板参数名）
        static auto ReadIdentifier = [](const std::string &s, size_t &pos) -> std::string
        {
            size_t start = pos;
            while (pos < s.size() && (std::isalnum((unsigned char) s[pos]) || s[pos] == '_' || s[pos] == ':'))
            {
                ++pos;
            }
            return s.substr(start, pos - start);
        };
        TemplateParamInfo info;
        SkipSpaces(s, pos);
        info._param_type_name = ReadIdentifier(s, pos);

        SkipSpaces(s, pos);
        if (pos < s.size() && s[pos] == '<')
        {
            // 进入模板参数
            ++pos;// 跳过 '<'
            while (pos < s.size())
            {
                SkipSpaces(s, pos);
                TemplateParamInfo child = ParseTemplate(s, pos);
                info._sub_params.push_back(std::move(child));

                SkipSpaces(s, pos);
                if (pos < s.size() && s[pos] == ',')
                {
                    ++pos;// 跳过逗号
                    continue;
                }
                else if (pos < s.size() && s[pos] == '>')
                {
                    ++pos;// 跳过 '>'
                    break;
                }
                else
                {
                    // 格式错误或结束
                    break;
                }
            }
        }
        return info;
    }

    TemplateParamInfo TemplateParamInfo::Parse(const String &full_type)
    {
        size_t pos = 0;
        return ParseTemplate(full_type, pos);
    }

    void PropertyInfo::Serialize(void *instance, FArchive &ar) const
    {
        AL_ASSERT(_serialize_fn != nullptr);
        const String &name = _name;
        _serialize_fn(reinterpret_cast<u8 *>(instance) + _offset, ar, &name);
    }

    void PropertyInfo::Deserialize(void *instance, FArchive &ar) const
    {
        AL_ASSERT(_deserialize_fn != nullptr);
        const String &name = _name;
        _deserialize_fn(reinterpret_cast<u8 *>(instance) + _offset, ar, &name);
    }

    const Type *Ailu::PropertyInfo::GetType()
    {
        if (_type == nullptr)
            _type = Type::Find(_type_name);
        return _type;
    }

    const Type *Ailu::FunctionInfo::GetRetType()
    {
        if (_ret_type == nullptr)
            _ret_type = Type::Find(_ret_type_name);
        return _ret_type;
    }
    //------------------------------------------------------------------------------------------------------------
    void Type::RegisterType(Type *type)
    {
        std::once_flag once;
        std::call_once(once, [&]()
                       {
            if (type->Name() == "Object")
                {
                    type->_base = nullptr;
                    type->_base_name = "";
                } });
        if (!s_is_base_type_init)
        {
            InitBaseTypeInfo();
            s_is_base_type_init = true;
        }
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

    Type *Type::Find(const String &name)
    {
        auto it = s_global_types.find(name);
        if (it == s_global_types.end())
        {
            LOG_WARNING("Type::Find: Type not found: {},try register it...", name);
            if (auto itt = s_global_register.find(name); itt != s_global_register.end())
            {
                s_global_types[name] = s_global_register[name]();
                s_global_register.erase(name);
            }
            else
            {
                LOG_ERROR("Type::Find: Type not found: {}", name);
                return nullptr;
            }
        }
        return s_global_types[name];
    };


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
    Vector<PropertyInfo> &Type::GetProperties()
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
        _is_enum = false;
        _properties = initializer._properties;
        _functions = initializer._functions;
        _size = initializer._size;
        _base_name = initializer._base_name;
        _base = nullptr;
        _constructor = initializer._constructor;
        for (auto &prop: _properties)
            _members.emplace_back(&prop);
        for (auto &func: _functions)
            _members.emplace_back(&func);
        for (auto &member: _members)
            _members_lut[member->Name()] = member;
    }
    u32 Type::Size() const
    {
        return _size;
    }
    PropertyInfo *Type::FindPropertyByName(const String &name) const
    {
        if (_members_lut.contains(name))
            return dynamic_cast<PropertyInfo *>(_members_lut.at(name));
        return nullptr;
    }
    FunctionInfo *Type::FindFunctionByName(const String &name) const
    {
        if (_members_lut.contains(name))
            return dynamic_cast<FunctionInfo *>(_members_lut.at(name));
        return nullptr;
    }
    const std::set<Type *> &Type::DerivedTypes() const
    {
        return _derived_types;
    }

    //---------------------------------------------------------------------------------------Enum------------------------------------------------------------------------------
    Enum::Enum(const EnumInitializer &initializer) : Type(TypeInitializer{})
    {
        _name = initializer._name;
        _is_enum = true;
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
            auto new_enum = s_registers.front()();
            s_registers.pop();
            LOG_INFO("Enum::InitTypeInfo: Register enum type: {}", new_enum->Name());
        }
    }
    void Enum::RegisterEnum(Enum *enum_ptr)
    {
        s_global_enums[enum_ptr->Name()] = enum_ptr;
        Type::RegisterType(enum_ptr);
    }

    i32 Enum::GetIndexByName(const std::string &name) const
    {
        if (_str_to_enum_lut.contains(name))
        {
            return (i32) _str_to_enum_lut.at(name);
        }
        return -1;
    }

    //---------------------------------------------

    //template<T>
    static void RegisterHelper(const String &name, u32 size)
    {
        TypeInitializer initializer;
        initializer._base_name = "";
        initializer._name = name;
        initializer._full_name = name;
        initializer._namespace = "";
        initializer._is_abstract = false;
        initializer._is_class = false;
        initializer._size = size;
        Type::RegisterType(new Type(initializer));
    }
    void Type::InitBaseTypeInfo()
    {
        static auto make_base_type = [](const String &name, u32 size)->Type*
        {
            TypeInitializer initializer;
            initializer._base_name = "";
            initializer._name = name;
            initializer._full_name = name;
            initializer._namespace = "";
            initializer._is_abstract = false;
            initializer._is_class = false;
            initializer._size = size;
            return new Type(initializer);
        };
        Vector<Type *> all_base_types;
#define MAKE_BASE_TYPE(name) all_base_types.push_back(make_base_type(#name, sizeof(name)))
        MAKE_BASE_TYPE(i8);
        MAKE_BASE_TYPE(u8);
        MAKE_BASE_TYPE(i16);
        MAKE_BASE_TYPE(u16);
        MAKE_BASE_TYPE(i32);
        MAKE_BASE_TYPE(u32);
        MAKE_BASE_TYPE(i64);
        MAKE_BASE_TYPE(u64);
        MAKE_BASE_TYPE(f32);
        MAKE_BASE_TYPE(f64);
        MAKE_BASE_TYPE(bool);
        MAKE_BASE_TYPE(String);
        MAKE_BASE_TYPE(Vector2f);
        MAKE_BASE_TYPE(Vector3f);
        MAKE_BASE_TYPE(Vector4f);
        MAKE_BASE_TYPE(Vector2Int);
        MAKE_BASE_TYPE(Vector3Int);
        MAKE_BASE_TYPE(Vector4Int);
        MAKE_BASE_TYPE(Vector2UInt);
        MAKE_BASE_TYPE(Vector3UInt);
        MAKE_BASE_TYPE(Vector4UInt);
        for (auto *type: all_base_types)
            s_global_types[type->FullName()] = type;
    }

    IMPL_STATIC_TYPE(i8)
    IMPL_STATIC_TYPE(u8)
    IMPL_STATIC_TYPE(i16)
    IMPL_STATIC_TYPE(u16)
    IMPL_STATIC_TYPE(i32)
    IMPL_STATIC_TYPE(u32)
    IMPL_STATIC_TYPE(i64)
    IMPL_STATIC_TYPE(u64)
    IMPL_STATIC_TYPE(f32)
    IMPL_STATIC_TYPE(f64)
    IMPL_STATIC_TYPE(bool)
    IMPL_STATIC_TYPE(String)
}// namespace Ailu
