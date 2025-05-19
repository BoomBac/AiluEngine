#pragma once
#ifndef __STACK_TRACE__
#define __STACK_TRACE__
#include "GlobalMarco.h"

namespace Ailu
{
    class StackTrace
    {
    public:
        static String Capture(u16 max_frames = 62);
    };
}// namespace Ailu
#endif//__STACK_TRACE__