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
    const Type *StaticClass()
    {
        return nullptr;
    }

#define DECLARE_STATIC_TYPE(x) template<>\
    const AILU_API Type *StaticClass<x>();

#define IMPL_STATIC_TYPE(x)                         \
    template<>                                      \
    const Type *StaticClass<x>()                    \
    {                                               \
        static const Type *s_type = Type::Find(#x); \
        return s_type;                              \
    };
}
#endif// !__ENUM_REFLECT_H
