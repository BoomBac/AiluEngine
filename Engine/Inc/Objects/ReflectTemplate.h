#ifndef __ENUM_REFLECT_H
#define __ENUM_REFLECT_H

namespace Ailu
{
    class Enum;
    template<typename E>
    const Enum *StaticEnum()
    {
        return nullptr;
    }
    class Type;
    template<typename E>
    Type *StaticClass()
    {
        return nullptr;
    }

#define DECLARE_STATIC_TYPE(x) template<>\
    AILU_API Type *StaticClass<x>();

#define IMPL_STATIC_TYPE(x)                         \
    template<>                                      \
    Type *StaticClass<x>()                    \
    {                                               \
        static Type *s_type = Type::Find(#x); \
        return s_type;                              \
    };
}
#endif// !__ENUM_REFLECT_H
