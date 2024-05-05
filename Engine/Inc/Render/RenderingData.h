#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include "GlobalMarco.h"
#include "Buffer.h"
#include "Texture.h"
#include "RendererAPI.h"

namespace Ailu
{
    enum class EShaderingMode : u8
    {
        kShader,kWireFrame,kShaderedWireFrame
    };

    struct RenderingStates
    {      
        inline static u32 s_vertex_num = 0u;
        inline static u32 s_triangle_num = 0u;
        inline static u32 s_draw_call = 0u;
        inline static f32 s_gpu_latency = 0.0f;
        static void Reset()
        {
            s_vertex_num = 0u;
            s_triangle_num = 0u;
            s_draw_call = 0u;
            s_gpu_latency = 0.0f;
        }
        inline static EShaderingMode s_shadering_mode = EShaderingMode::kShader;
    };

    struct RenderingShadowData
    {
        Matrix4x4f _shadow_view;
        Matrix4x4f _shadow_proj;
        float _shadow_bias = 0.001f;
    };
    class CommandBuffer;
    struct RenderingData
    {
    public:
        RenderingShadowData _shadow_data[8];
        u8 _actived_shadow_count = 0;
        ConstantBuffer* _p_per_frame_cbuf;
        ConstantBuffer** _p_per_object_cbuf;
        RTHandle _camera_color_target_handle;
        RTHandle _camera_depth_target_handle;
        RTHandle _final_rt_handle;
        Rect _viewport,_scissor_rect;
        u32 _width, _height;
        CommandBuffer* cmd;
        void Reset()
        {
            _actived_shadow_count = 0;
        }
    };
}


#endif // !RENDERING_DATA_H__

