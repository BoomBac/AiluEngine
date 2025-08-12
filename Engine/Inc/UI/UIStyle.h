#include "Objects/Type.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    namespace UI
    {
        ASTRUCT()
        struct UIStyleSheet
        {
            APROPERTY()
            static inline f32 _vertical_spacing = 4.0f;
            APROPERTY()
            static inline f32 _horizontal_spacing = 4.0f;
            APROPERTY()
            static inline Vector4f _layout_padding = {4.0f, 4.0f, 4.0f, 4.0f};//ltrb
        };
    }
}// namespace Ailu