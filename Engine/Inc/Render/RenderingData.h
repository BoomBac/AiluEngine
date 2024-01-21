#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include "CBuffer.h"
#include "GlobalMarco.h"

namespace Ailu
{
    enum class EShaderingMode : uint8_t
    {
        kShader,kWireFrame,kShaderedWireFrame
    };
	DECLARE_ENUM(EColorRange,kLDR,kHDR)

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
        inline static EShaderingMode s_shadering_mode = EShaderingMode::kShader;
    };

    struct RenderingShadowData
    {
        Matrix4x4f _shadow_view;
        Matrix4x4f _shadow_proj;
        float _shadow_bias = 0.001f;
    };
    struct RenderingData
    {
    public:
        RenderingShadowData _shadow_data[8];
        u8 _actived_shadow_count = 0;
        void Reset()
        {
            _actived_shadow_count = 0;
        }
    };
}


#endif // !RENDERING_DATA_H__

