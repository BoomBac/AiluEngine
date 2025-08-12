#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include "Buffer.h"
#include "GlobalMarco.h"
#include "RendererAPI.h"
#include "Texture.h"
#include "RenderGraph/RenderGraph.h"

namespace Ailu::RHI::DX12
{
    class D3DContext;
}

namespace Ailu::Render
{
    enum class EShaderingMode : u8
    {
        kShader,
        kWireFrame,
        kShaderedWireFrame
    };

    struct AILU_API RenderingStates
    {
        friend class Ailu::RHI::DX12::D3DContext;

        inline static u32 s_vertex_num = 0u;
        inline static u32 s_triangle_num = 0u;
        inline static u32 s_draw_call = 0u;
        inline static u32 s_dispatch_call = 0u;
        inline static f32 s_gpu_latency = 0.0f;
        inline static f32 s_frame_time = 0.0f;
        inline static f32 s_frame_rate = 0.0f;
        static void Reset()
        {
            s_vertex_num = s_temp_vertex_num;
            s_triangle_num = s_temp_triangle_num;
            s_draw_call = s_temp_draw_call;
            s_dispatch_call = s_temp_dispatch_call;
            s_temp_vertex_num = 0u;
            s_temp_triangle_num = 0u;
            s_temp_draw_call = 0u;
            s_temp_dispatch_call = 0u;
        }
    private:
        inline static u32 s_temp_vertex_num = 0u;
        inline static u32 s_temp_triangle_num = 0u;
        inline static u32 s_temp_draw_call = 0u;
        inline static u32 s_temp_dispatch_call = 0u;
    };

    DECLARE_ENUM(ERenderLayer, kDefault = 0x01, kSkyBox = 0x08)

    struct AILU_API QuailtySetting
    {
        //m
        inline static f32 s_main_light_shaodw_distance = 80.0f;
        inline static u16 s_cascade_shadow_map_count = 2;
        inline static f32 s_cascade_shadow_map_split[4] = {0.25f, 0.9f, 1.0f, 1.0f};
        inline static f32 s_shadow_fade_out_factor = 0.1f;
        inline static u32 s_cascade_shaodw_map_resolution = 2048u;
    };

    class Mesh;
    class Material;
    struct RenderableObjectData
    {
        u16 _scene_id;//index per object buffer
        f32 _distance_to_cam;
        u16 _submesh_index;
        u32 _instance_count;
        Mesh *_mesh;
        Material *_material;
        const Matrix4x4f *_world_matrix;
    };

    using CullResult = Map<u32, Vector<RenderableObjectData>>;

    struct RenderingShadowData
    {
        i16 _shadowmap_index = -1;
        Matrix4x4f _shadow_matrix;
        float _shadow_bias = 0.001f;
        const CullResult *_cull_results;
    };
    struct RenderingPointShadowData
    {
        i16 _shadowmap_index = -1;
        Matrix4x4f _shadow_matrices[6];
        float _shadow_bias = 0.001f;
        Vector3f _light_world_pos;
        float _camera_near, _camera_far;
        Array<const CullResult *, 6> _cull_results;
    };


    struct CameraData
    {
        TextureDesc _camera_color_target_desc;
        TextureDesc _camera_depth_target_desc;
    };
    struct VoxelGIData
    {
        Vector3f _center;
        Vector3f _size;
        Vector3Int _grid_num;
        Vector3f _grid_size;
    };

    class CommandBuffer;
    class Camera;

    struct RenderingData
    {
    public:
        RenderingData() {};
        //index 0 for directional shadow
        RenderingShadowData _cascade_shadow_data[RenderConstants::kMaxCascadeShadowMapSplitNum];
        RenderingShadowData _spot_shadow_data[RenderConstants::kMaxSpotLightNum];
        RenderingShadowData _area_shadow_data[RenderConstants::kMaxAreaLightNum];
        RenderingPointShadowData _point_shadow_data[RenderConstants::kMaxPointLightNum];
        u8 _addi_shadow_num = 0, _addi_point_shadow_num = 0;
        ConstantBuffer *_p_per_scene_cbuf;
        ConstantBuffer *_p_per_camera_cbuf;
        Vector<ConstantBuffer *> *_p_per_object_cbuf;
        RTHandle _camera_color_target_handle;
        RTHandle _camera_depth_target_handle;
        RTHandle _final_rt_handle;
        RTHandle _camera_opaque_tex_handle;
        RTHandle _camera_depth_tex_handle;
        RenderTexture* _postprocess_input;
        Rect _viewport, _scissor_rect;
        u32 _width, _height;
        u32 _pre_width, _pre_height;
        bool _is_res_changed = false;
        /*
          GBuffer0 (RG, 2通道) - 存储法线 (Packed)
          GBuffer1 (RGBA, 4通道) - 存储颜色 (RGB) 和 粗糙度 (A)
          GBuffer2 (RGBA, 4通道) - 存储高光颜色 (RGB) 和 金属度 (A)
          GBuffer3 (RGBA, 4通道) - 存储自发光颜色 (RGB) 和 可能的附加信息 (A)*/
        Vector<RTHandle> _gbuffers;
        struct
        {
            Vector<RDG::RGHandle> _gbuffers;
            RDG::RGHandle         _color_target;
            RDG::RGHandle         _color_tex;
            RDG::RGHandle         _depth_target;
            RDG::RGHandle         _depth_tex;
            RDG::RGHandle         _motion_vector_tex;
            RDG::RGHandle         _motion_vector_depth;
            RDG::RGHandle         _hzb;
            RDG::RGHandle _main_light_shadow_map;
            RDG::RGHandle _addi_shadow_maps;
            RDG::RGHandle _point_light_shadow_maps;
        } _rg_handles;
        CommandBuffer *cmd;
        const CullResult *_cull_results;
        const Camera *_camera;
        CameraData _camera_data;
        VoxelGIData _vxgi_data;
        bool _is_debug_voxel = false;
        u16 _vxgi_debug_mipmap = 0u;
        //temp
        Vector4f _mainlight_world_position;
        void Reset()
        {
            _addi_shadow_num = 0;
            _addi_point_shadow_num = 0;
            _postprocess_input = nullptr;
        }
    };
}// namespace Ailu


#endif// !RENDERING_DATA_H__
