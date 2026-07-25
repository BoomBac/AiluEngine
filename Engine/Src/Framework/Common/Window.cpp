#include "Framework/Common/Window.h"
#include "Platform/WinWindow.h"

namespace Ailu
{
    Scope<Window> WindowFactory::Create(const WString &title, u32 width, u32 height, u32 flags)
    {
        WindowProps prop(title, width, height);
        prop._flag = flags;
#ifdef AL_PLATFORM_WINDOWS
        return MakeScope<WinWindow>(prop);
#else
        return nullptr;
#endif// AL_PLATFORM_WINDOWS

    }
}
