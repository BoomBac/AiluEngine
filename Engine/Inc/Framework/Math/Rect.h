#pragma once
#include "Framework/Math/MathCommon.hpp"

namespace Ailu
{
#pragma warning(push)
#pragma warning(disable : 4244)
    namespace Math
    {
        struct Rect
        {
            u16 left;
            u16 top;
            u16 width;
            u16 height;
            Rect(u16 l, u16 t, u16 w, u16 h)
                : left(l), top(t), width(w), height(h)
            {
            }
            Rect() : Rect(0, 0, 0, 0) {};
            bool operator==(const Rect &other) const
            {
                return left == other.left && top == other.top && width == other.width && height == other.height;
            }
        };
    
        using ScissorRect = Rect;
    }
#pragma warning(pop)
}
