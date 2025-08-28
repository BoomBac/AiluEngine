#include "Framework/Math/ALMath.hpp"
#include "Objects/Type.h"


namespace Ailu
{
    using namespace Math;
#define IMPL(x) template<>\
    Type *StaticClass<x>() { \
        static Type * s_type = Type::Find(#x); \
        return s_type;                              \
    };
    IMPL(Vector2f);
    IMPL(Vector3f);
    IMPL(Vector4f);
    IMPL(Vector2Int);
    IMPL(Vector3Int);
    IMPL(Vector4Int);
    IMPL(Vector2UInt);
    IMPL(Vector3UInt);
    IMPL(Vector4UInt);
#undef IMPL
}