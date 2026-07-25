#pragma once
#include "Framework/Core/CoreMinimal.h"

namespace Ailu::Render
{
    struct AILU_API RenderingStates
    {
        #define DECLARE_STATE_COUNTER(state_name,value_name) \
            static void Increment##state_name(u32 count = 1u) \
            { \
                s_temp_##value_name += count; \
            } \
            static u32 Get##state_name() \
            { \
                return s_##value_name; \
            }
        DECLARE_STATE_COUNTER(DrawCallCount, draw_call)
        DECLARE_STATE_COUNTER(DispatchCallCount, dispatch_call)
        DECLARE_STATE_COUNTER(VertexCount, vertex_num)
        DECLARE_STATE_COUNTER(TriangleCount, triangle_num)
        DECLARE_STATE_COUNTER(GfxPsoBindCount, gfx_pso_bind_count)
        DECLARE_STATE_COUNTER(GfxResBindCount, gfx_res_bind_count)
        
        #define DECLARE_STATE_PROP(state_name, value_name) \
            static void Set##state_name(f32 value) \
            { \
                s_##value_name = value; \
            } \
            static f32 Get##state_name() \
            { \
                return s_##value_name; \
            }
        DECLARE_STATE_PROP(GpuLatency, gpu_latency)
        DECLARE_STATE_PROP(FrameTime, frame_time)
        DECLARE_STATE_PROP(FrameRate, frame_rate)

        static void Reset()
        {
            s_vertex_num = s_temp_vertex_num;
            s_triangle_num = s_temp_triangle_num;
            s_draw_call = s_temp_draw_call;
            s_dispatch_call = s_temp_dispatch_call;
            s_gfx_pso_bind_count = s_temp_gfx_pso_bind_count;
            s_gfx_res_bind_count = s_temp_gfx_res_bind_count;
            s_temp_vertex_num = 0u;
            s_temp_triangle_num = 0u;
            s_temp_draw_call = 0u;
            s_temp_dispatch_call = 0u;
            s_temp_gfx_pso_bind_count = 0u;
            s_temp_gfx_res_bind_count = 0u;
        }
    private:
        inline static u32 s_vertex_num = 0u;
        inline static u32 s_triangle_num = 0u;
        inline static u32 s_draw_call = 0u;
        inline static u32 s_dispatch_call = 0u;
        inline static u32 s_gfx_pso_bind_count = 0u;
        inline static u32 s_gfx_res_bind_count = 0u;
        
        inline static u32 s_temp_vertex_num = 0u;
        inline static u32 s_temp_triangle_num = 0u;
        inline static u32 s_temp_draw_call = 0u;
        inline static u32 s_temp_dispatch_call = 0u;
        inline static u32 s_temp_gfx_pso_bind_count = 0u;
        inline static u32 s_temp_gfx_res_bind_count = 0u;
        
        inline static f32 s_gpu_latency = 0.0f;
        inline static f32 s_frame_time = 0.0f;
        inline static f32 s_frame_rate = 0.0f;
    };
}