#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include <cstdint>

namespace Ailu
{
    struct RenderingStates
    {
        inline static uint32_t s_vertex_num = 0u;
        inline static uint32_t s_triangle_num = 0u;
        inline static uint32_t s_draw_call = 0u;
        static void Reset()
        {
            s_vertex_num = 0u;
            s_triangle_num = 0u;
            s_draw_call = 0u;
        }
    };
}


#endif // !RENDERING_DATA_H__

