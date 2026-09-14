#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Geometry.h"
#include "Texture.h"
#include "PostprocessSettings.h"
#include "RenderGraph/RenderGraphFwd.h"
#include "generated/RenderingData.gen.h"

namespace Ailu::RHI::DX12
{
    class D3DContext;
}

namespace Ailu::SceneManagement
{
    class Scene;
}

namespace Ailu::Render
{
    class ConstantBuffer;
    class GPUBuffer;
    class Material;

    struct EnvironmentRuntimeData
    {
        Texture *_source = nullptr;
        Ref<Texture> _sky_cubemap;
        Ref<Texture> _diffuse_irradiance;
        Ref<Texture> _specular_prefilter;
        Ref<Texture> _transmittance_lut;
        Ref<Texture> _mult_scatter_lut;
        Ref<Texture> _sky_view_lut;
        Material *_debug_material = nullptr;
        f32 _intensity = 1.0f;
        f32 _rotation = 0.0f;
        bool _render_sky = true;
        bool _enable_ibl = true;
        bool _is_texture_source = false;
        u64 _source_revision = 0u;
        Color _sky_color = Color(0.18f, 0.32f, 0.58f, 1.0f);
        Color _ground_color = Color(0.08f, 0.06f, 0.04f, 1.0f);
    };

    enum class EShaderingMode : u8
    {
        kShader,
        kWireFrame,
        kShaderedWireFrame
    };

    AENUM()
    enum class ERenderLayer : u8
    {
        kDefault = 0x01,
        kSkyBox = 0x08
    };

    struct AILU_API QuailtySetting
    {
        //m
        inline static f32 s_main_light_shaodw_distance = 80.0f;
        inline static u16 s_cascade_shadow_map_count = 4;
        inline static f32 s_cascade_shadow_map_split[4] = {0.10f, 0.25f, 0.50f, 1.0f};
        inline static f32 s_shadow_fade_out_factor = 0.1f;
        inline static u32 s_cascade_shaodw_map_resolution = 2048u;
    };

    class Mesh;
    class Material;
    class VertexBuffer;
    class IndexBuffer;

    struct ScenePrimitive
    {
        u32 _primitive_index = 0u;
        u32 _entity_id = 0u;
        Mesh *_mesh = nullptr;
        Material *_material = nullptr;
        VertexBuffer *_vertex_buffer = nullptr;
        IndexBuffer *_index_buffer = nullptr;
        AABB _world_bounds{};
        u16 _submesh_index = 0u;
        u16 _flags = 0u;
    };

    struct RenderableObjectData
    {
        u32 _primitive_index = 0u;
        f32 _distance_to_cam;
        u16 _submesh_index;
        Mesh *_mesh;
        Material *_material;
        VertexBuffer *_vertex_buffer = nullptr;
        IndexBuffer *_index_buffer = nullptr;
        u16 _flags = 0u;
        u32 _entity = 0u;
    };

    using CullResult = Map<u32, Vector<RenderableObjectData>>;

    class Camera;

    enum class ERenderViewType : u8
    {
        kCamera,
        kPreview,
        kCubemap,
        kDirectionalShadow,
        kPointShadow,
        kSpotShadow,
        kAreaShadow
    };

    struct ViewCullInput
    {
        ViewFrustum _frustum;
        Vector3f _camera_position = Vector3f::kZero;
        u32 _layer_mask = 0u;
        bool _is_custom_vp = false;
    };

    struct ShadowViewRequest
    {
        const Camera *_camera = nullptr;
        ERenderViewType _view_type = ERenderViewType::kDirectionalShadow;
        u16 _light_index = 0u;
        u16 _cascade_or_face = 0u;
        u32 _view_index = 0u;
    };

    struct SceneRenderData
    {
        const SceneManagement::Scene *_scene = nullptr;
        const Vector<ScenePrimitive> *_primitives = nullptr;
        const Vector<PrimitiveData> *_primitive_data = nullptr;
        const Vector<MaterialData> *_material_data = nullptr;
        Vector<ShadowViewRequest> _shadow_views;
    };

    struct ViewRenderData
    {
        const Camera *_camera = nullptr;
        ERenderViewType _view_type = ERenderViewType::kCamera;
        u64 _frame_index = 0u;
        ViewCullInput _input;
        CullResult _cull_result;
        Vector<u32> _visible_skinned_primitives;
    };

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
        GPUBuffer *_scene_primitive_buffer = nullptr;
        Vector<PrimitiveData> *_primitive_data = nullptr;
        const Vector<ScenePrimitive> *_scene_primitives = nullptr;
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
            RDG::RGHandle         _main_light_shadow_map;
            RDG::RGHandle         _addi_shadow_maps;
            RDG::RGHandle         _point_light_shadow_maps;
            RDG::RGHandle         _ao_tex;
            RDG::RGHandle         _volumetric_fog_accum;
            RDG::RGHandle         _sky_view_lut;
            RDG::RGHandle         _environment_sky;
            RDG::RGHandle         _environment_diffuse;
            RDG::RGHandle         _environment_specular;
            RDG::RGHandle         _environment_transmittance_lut;
            RDG::RGHandle         _environment_mult_scatter_lut;
        } _rg_handles;
        CommandBuffer *cmd;
        const CullResult *_cull_results;
        const SceneManagement::Scene *_scene = nullptr;
        const Camera *_camera;
        CameraData _camera_data;
        VoxelGIData _vxgi_data;
        PostprocessSettings _postprocess_settings;
        const EnvironmentRuntimeData *_environment = nullptr;
        EDisplayOutput _display_output = EDisplayOutput::kHdrScRgb;
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
