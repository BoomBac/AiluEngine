#ifndef __UI_INTERACTION_ZONE_H__
#define __UI_INTERACTION_ZONE_H__
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"

namespace Ailu
{
    namespace UI
    {
        struct ZoneHandle
        {
            u32 _index;
            u32 _generation;
        };

        struct InteractionZone
        {
            Vector4f _rect;
            u32 _generation;
        };
    } // namespace UI
} // namespace Ailu

#endif  // __UI_INTERACTION_ZONE_H__
