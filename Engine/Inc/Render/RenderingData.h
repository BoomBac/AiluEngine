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

    struct AILU_API RenderingStates
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

    struct AILU_API QuailtySetting
    {
        //cm
        inline static u32 s_main_light_shaodw_distance = 8000u;
        inline static u16 s_cascade_shadow_map_count = 2;
        inline static f32 s_cascade_shadow_map_split[4] = {0.25f,0.5f,1.0f,1.0f};
        inline static f32 s_shadow_fade_out_factor = 0.1f;
    };

    class Mesh;
    class Material;
    struct RenderableObjectData
    {
        u16         _scene_id;//index per object buffer
        f32         _distance_to_cam;
        u16         _submesh_index;
        u32         _instance_count;
        Mesh* _mesh;
        Material* _material;
        const Matrix4x4f* _world_matrix;
    };

    using CullResult = Map<u32, Vector<RenderableObjectData>>;

    struct RenderingShadowData
    {
        Matrix4x4f _shadow_matrix;
        i16        _shadow_index = -1;
        float _shadow_bias = 0.001f;
        const CullResult* _cull_results;
    };
    struct RenderingPointShadowData
    {
        i16   _shadow_indices[6];
        float _shadow_bias = 0.001f;
        Vector3f _light_world_pos;
        float _camera_near, _camera_far;
        Array<const CullResult*, 6> _cull_results;
    };

    class CommandBuffer;
    struct RenderingData
    {
    public:
        //index 0 for directional shadow
        RenderingShadowData _shadow_data[RenderConstants::kMaxCascadeShadowMapSplitNum + RenderConstants::kMaxSpotLightNum];
        RenderingPointShadowData _point_shadow_data[RenderConstants::kMaxPointLightNum];
        u8 _addi_shadow_num = 0,_addi_point_shadow_num = 0;
        IConstantBuffer* _p_per_frame_cbuf;
        IConstantBuffer** _p_per_object_cbuf;
        RTHandle _camera_color_target_handle;
        RTHandle _camera_depth_target_handle;
        RTHandle _final_rt_handle;
        RTHandle _camera_opaque_tex_handle;
        RTHandle _camera_depth_tex_handle;
        Rect _viewport,_scissor_rect;
        u32 _width, _height;
        Vector<RTHandle> _gbuffers;
        CommandBuffer* cmd;
        const CullResult* _cull_results;
        void Reset()
        {
            _addi_shadow_num = 0;
            _addi_point_shadow_num = 0;
        }
    };
}


#endif // !RENDERING_DATA_H__

