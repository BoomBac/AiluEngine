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
            f32 _vertical_spacing = 4.0f;
            APROPERTY()
            f32 _horizontal_spacing = 4.0f;
            APROPERTY()
            Vector4f _layout_padding = {4.0f, 4.0f, 4.0f, 4.0f};//ltrb
        };
        inline static UIStyleSheet g_style_sheet;
    }
}// namespace Ailu