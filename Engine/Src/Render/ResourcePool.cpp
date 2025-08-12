#include "Render/ResourcePool.h"
#include "Render/GraphicsContext.h"

namespace Ailu::Render
{
    namespace FrameHelper
    {
        u64 GetFrameCout()
        {
            return GraphicsContext::Get().GetFrameCount();
        }
    }
}