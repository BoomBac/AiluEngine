#pragma once
#ifndef __RENDERING_DATA_H__
#define __RENDERING_DATA_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Array.h"
#include "Framework/Math/Geometry.h"
#include "Render/DrawSubmission.h"
#include "Texture.h"
#include "PostprocessSettings.h"
#include "SphericalHarmonics.h"
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
        Ref<Texture> _background_latlong;
        Ref<Texture> _diffuse_irradiance;
        Ref<GPUBuffer> _diffuse_sh;
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
        inline static f32 s_probe_shadow_distance = 40.0f;
        inline static u32 s_probe_directional_shadow_map_resolution = 1024u;
        // 静态漫反射探针烘焙遍数。第 2 遍起会把上一遍烘出的 GI 作为间接光参与 capture，
        // 每多一遍多一次光能弹射（单遍烘焙等于零弹射，室内间接光会明显偏弱）。
        // 每遍耗时约等于探针数量帧，探针多时不要把遍数设大。
        inline static u32 s_static_probe_bake_pass_count = 3u;
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
        DrawGeometryId _geometry_id = 0u;
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
        DrawGeometryId _geometry_id = 0u;
        u16 _flags = 0u;
        u32 _entity = 0u;
    };

    using CullResult = Map<u32, Vector<RenderableObjectData>>;

    enum class ERenderPurpose : u8
    {
        kCamera,
        kReflectionProbe,
        kStaticDiffuseProbe
    };

    enum ERenderFeature : u32
    {
        kRenderFeatureStaticDiffuseGi = 1u << 0u,
        kRenderFeatureReflectionProbe = 1u << 1u,
        kRenderFeatureEnvironmentIbl = 1u << 2u
    };

    inline constexpr u32 kDefaultRenderFeatures = kRenderFeatureStaticDiffuseGi |
                                                   kRenderFeatureReflectionProbe |
                                                   kRenderFeatureEnvironmentIbl;

    struct RenderRequest
    {
        ERenderPurpose _purpose = ERenderPurpose::kCamera;
        u32 _feature_flags = kDefaultRenderFeatures;
        bool _enable_shadows = true;
        f32 _probe_shadow_distance = 40.0f;
    };

    inline RenderRequest MakeCameraRenderRequest()
    {
        return RenderRequest{};
    }

    inline RenderRequest MakeReflectionProbeRenderRequest(f32 shadow_distance)
    {
        RenderRequest request;
        request._purpose = ERenderPurpose::kReflectionProbe;
        request._feature_flags = kRenderFeatureEnvironmentIbl;
        request._enable_shadows = true;
        request._probe_shadow_distance = shadow_distance;
        return request;
    }

    inline RenderRequest MakeStaticDiffuseProbeRenderRequest(f32 shadow_distance)
    {
        RenderRequest request;
        request._purpose = ERenderPurpose::kStaticDiffuseProbe;
        request._feature_flags = kRenderFeatureEnvironmentIbl;
        request._enable_shadows = true;
        request._probe_shadow_distance = shadow_distance;
        return request;
    }

    enum class EDirectionalShadowMode : u8
    {
        kNone,
        kCsm,
        kSingleOrthographic
    };

    struct ShadowSet;

    class Camera;

    enum class ERenderViewType : u8
    {
        kCamera,
        kPreview
    };

    struct ViewCullInput
    {
        ViewFrustum _frustum;
        Vector3f _camera_position = Vector3f::kZero;
        u32 _layer_mask = 0u;
        bool _is_custom_vp = false;
    };

    struct SceneRenderData
    {
        const SceneManagement::Scene *_scene = nullptr;
        const Vector<ScenePrimitive> *_primitives = nullptr;
        const Vector<PrimitiveData> *_primitive_data = nullptr;
        const Vector<MaterialData> *_material_data = nullptr;
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

    struct DDGILightingData
    {
        RDG::RGHandle _irradiance_atlas_handle{};
        Ref<Texture2D> _irradiance_atlas_texture;
        RDG::RGHandle _distance_atlas_handle{};
        Ref<Texture2D> _distance_atlas_texture;
        RDG::RGHandle _probe_data_handle{};
        Ref<GPUBuffer> _probe_data;
        Matrix4x4f _local_to_world = Matrix4x4f::Identity();
        Matrix4x4f _world_to_local = Matrix4x4f::Identity();
        Vector3f _probe_spacing = Vector3f::kOne;
        u32 _probe_count_x = 0u;
        u32 _probe_count_y = 0u;
        u32 _probe_count_z = 0u;
        u32 _atlas_tiles_x = 0u;
        u32 _atlas_tiles_y = 0u;
        u32 _atlas_width = 0u;
        u32 _atlas_height = 0u;
        u32 _distance_atlas_tiles_x = 0u;
        u32 _distance_atlas_tiles_y = 0u;
        u32 _distance_atlas_width = 0u;
        u32 _distance_atlas_height = 0u;
        bool _use_visibility = true;
        f32 _probe_normal_bias = 0.1f;
        f32 _probe_view_bias = 0.1f;
        bool _use_probe_relocation = false;
        u32 _debug_mode = 4u;
        bool _enabled = false;
    };

    class CommandBuffer;
    class Camera;

    struct RenderingData
    {
    public:
        RenderingData() {};
        ConstantBuffer *_p_per_scene_cbuf;
        ConstantBuffer *_p_per_camera_cbuf;
        GPUBuffer *_scene_primitive_buffer = nullptr;
        GPUBuffer *_material_buffer = nullptr;
        Vector<PrimitiveData> *_primitive_data = nullptr;
        const Vector<ScenePrimitive> *_scene_primitives = nullptr;
        RTHandle _camera_color_target_handle;
        RTHandle _camera_depth_target_handle;
        RTHandle _final_rt_handle;
        RTHandle _camera_opaque_tex_handle;
        RTHandle _camera_depth_tex_handle;
        RTHandle _motion_vector_tex_handle;
        RenderTexture* _postprocess_input;
        Rect _viewport, _scissor_rect;
        GPUBuffer *_raster_area_light_buffer = nullptr;
        u32 _width, _height;
        u32 _pre_width, _pre_height;
        bool _is_res_changed = false;
        /*
          GBuffer0 (RG, 2通道) - 存储法线 (Packed)
          GBuffer1 (RGBA, 4通道) - 存储颜色 (RGB) 和 粗糙度 (A)
          GBuffer2 (RGBA, 4通道) - 存储自发光颜色 (RGB) 和 金属度 (A)
          GBuffer3 (RGBA, 4通道) - 存储选中的 diffuse irradiance (RGB)，A 保留*/
        Vector<RTHandle> _gbuffers;
        struct
        {
            RDG::RGHandle _raster_area_lights;
            Vector<RDG::RGHandle> _gbuffers;
            // _color_target 始终是 _CameraColorAttachmentA：场景 Pass 在注册阶段对它 Write，在录制阶段直接绑定它。
            // 后处理链（Bloom/ToneMapping）的输出写入 _final_color，FinalBlit 使用 _final_color，
            // 这样 PostprocessStack 不会把 _color_target 改写成另一个资源，导致注册在它之前的 Pass 绑错目标。
            RDG::RGHandle         _color_target;
            RDG::RGHandle         _color_tex;
            RDG::RGHandle         _upscaled_color;
            RDG::RGHandle         _taa_color;
            RDG::RGHandle         _taa_history_color; // Jittered accumulation; _taa_color is stable presentation.
            RDG::RGHandle         _taa_opaque_color;
            RDG::RGHandle         _taa_reactive;
            RDG::RGHandle         _pre_exposure_ev;
            RDG::RGHandle         _previous_pre_exposure_ev;
            RDG::RGHandle         _final_color;
            RDG::RGHandle         _final_depth;
            RDG::RGHandle         _depth_target;
            RDG::RGHandle         _depth_tex;
            RDG::RGHandle         _motion_vector_tex;
            RDG::RGHandle         _hzb;
            RDG::RGHandle         _main_light_shadow_map;
            RDG::RGHandle         _addi_shadow_maps;
            RDG::RGHandle         _point_light_shadow_maps;
            RDG::RGHandle         _ao_tex;
            RDG::RGHandle         _surfel_coverage;
            RDG::RGHandle         _volumetric_fog_accum;
            RDG::RGHandle         _sky_view_lut;
            RDG::RGHandle         _environment_sky;
            RDG::RGHandle         _environment_background;
            RDG::RGHandle         _environment_sh;
            RDG::RGHandle         _environment_diffuse;
            RDG::RGHandle         _environment_specular;
            RDG::RGHandle         _environment_transmittance_lut;
            RDG::RGHandle         _environment_mult_scatter_lut;
        } _rg_handles{};
        CommandBuffer *cmd;
        const CullResult *_cull_results;
        const ShadowSet *_shadow_set = nullptr;
        bool _render_shadow_maps = false;
        const SceneManagement::Scene *_scene = nullptr;
        const Camera *_camera;
        CameraData _camera_data;
        VoxelGIData _vxgi_data;
        PostprocessSettings _postprocess_settings;
        const EnvironmentRuntimeData *_environment = nullptr;
        EDisplayOutput _display_output = EDisplayOutput::kHdrScRgb;
        f32 _pre_exposure = 1.0f;
        bool _is_debug_voxel = false;
        u16 _vxgi_debug_mipmap = 0u;
        DDGILightingData _ddgi_lighting;
        //temp
        Vector4f _mainlight_world_position;
        // 后处理之后的最终颜色：PostprocessStack 写入 _final_color，未启用后处理时回退到场景颜色 _color_target。
        // 注册顺序在后处理之后的叠加类 Pass（Gizmo / 编辑器 Debug / DDGI Debug 等）必须使用它；
        // 若写入 _color_target(_CameraColorAttachmentA)，该写入发生在 CopyColor 之后，不会进入最终画面。
        RDG::RGHandle &FinalColorHandle()
        {
            return _rg_handles._final_color.IsValid() ? _rg_handles._final_color : _rg_handles._color_target;
        }
        const RDG::RGHandle &FinalColorHandle() const
        {
            return _rg_handles._final_color.IsValid() ? _rg_handles._final_color : _rg_handles._color_target;
        }
        RDG::RGHandle &FinalDepthHandle()
        {
            return _rg_handles._final_depth.IsValid() ? _rg_handles._final_depth : _rg_handles._depth_target;
        }
        const RDG::RGHandle &FinalDepthHandle() const
        {
            return _rg_handles._final_depth.IsValid() ? _rg_handles._final_depth : _rg_handles._depth_target;
        }
        void Reset()
        {
            _postprocess_input = nullptr;
            _motion_vector_tex_handle = RTHandle{};
            _rg_handles._taa_color = {};
            _rg_handles._upscaled_color = {};
            _rg_handles._final_depth = {};
            _rg_handles._taa_history_color = {};
            _rg_handles._taa_opaque_color = {};
            _rg_handles._taa_reactive = {};
            _rg_handles._pre_exposure_ev = {};
            _rg_handles._previous_pre_exposure_ev = {};
            _ddgi_lighting = {};
            _rg_handles._surfel_coverage = {};
        }
    };
}// namespace Ailu


#endif// !RENDERING_DATA_H__
