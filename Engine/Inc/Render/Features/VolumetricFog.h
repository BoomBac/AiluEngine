#ifndef VOLUMETRIC_FOG_H
#define VOLUMETRIC_FOG_H
#pragma once
#include "Assets/AssetRef.h"
#include "RenderFeature.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/CoreType.h"
#include "Render/Buffer.h"
#include "FroxelBounds.h"
#include <array>
#include "generated/VolumetricFog.gen.h"
namespace Ailu
{
    namespace Render
    {
        class VolumetricFogPass;
        class VolumetricFogCompositePass;

        constexpr u32 kMaxLocalFogVolumes = 8u;

        struct LocalFogVolumeData
        {
            Matrix4x4f _world_to_local;
            Vector3f _scattering_albedo;
            f32 _density;
            f32 _edge_fade;
            u32 _shape;
            u32 _padding0 = 0u;
            u32 _padding1 = 0u;
        };
        static_assert(sizeof(LocalFogVolumeData) == 96u);
        static_assert(offsetof(LocalFogVolumeData, _density) == 76u);
        static_assert(offsetof(LocalFogVolumeData, _shape) == 84u);

        struct VolumetricFogHistory
        {
            Ref<Texture3D> _light_a;
            Ref<Texture3D> _light_b;
            Matrix4x4f _prev_view = Matrix4x4f::Identity();
            Matrix4x4f _prev_proj = Matrix4x4f::Identity();
            Vector3f _prev_position = Vector3f::kZero;
            Vector3f _prev_forward = Vector3f::kForward;
            f32 _prev_near = 0.0f;
            f32 _prev_far = 0.0f;
            u64 _local_lighting_signature = 0u;
            bool _is_cur_a = true;
            bool _valid = false;
            bool _temporal_was_enabled = false;
        };

        ACLASS()
        class AILU_API VolumetricFog : public RenderFeature
        {
            GENERATED_BODY()
        public:
            VolumetricFog();
            ~VolumetricFog();
            void AddRenderPasses(Renderer& renderer, const RenderingData& rendering_data) override;
            Texture3D* GetVolumetricLightTexture() const { return _debug_light_texture; }
            u32 GetLocalFogVolumeCount() const { return _local_fog_volume_count; }
            u32 GetLocalLightCount() const;
            u64 GetLocalLightFroxelCount() const;
            Texture3D* GetAccTexture() const { return _accum_texture.get(); }
            void OnPropertyChanged(const PropertyInfo& prop) override;
        public:
            APROPERTY()
            bool _debug_voxel_pos = false;
            APROPERTY()
            bool _debug_extinction = false;
            // 0 total medium, 1 global only, 2 local only, 3 total extinction debug.
            APROPERTY(Range(0,3))
            u32 _medium_debug_mode = 0u;
            APROPERTY()
            bool _debug_local_fog_bounds = false;
            APROPERTY()
            bool _debug_local_light_bounds = false;
            APROPERTY()
            bool _local_lights_enabled = true;
            // Punctual fog gain, independent of surface lighting and directional fog.
            APROPERTY(Range(0.0f,10.0f))
            f32 _local_light_intensity = 0.25f;
            APROPERTY()
            bool _local_light_supersampling = true;
            APROPERTY()
            bool _local_light_shadows = true;
            APROPERTY()
            bool _bounded_local_lights = true;
            // Disable only for deterministic reference comparisons.
            APROPERTY()
            bool _jitter_enabled = true;
            APROPERTY(Range(0,8))
            u32 _max_local_lights = 8u;
            APROPERTY(Range(0.0f,10.0f))
            f32 _fog_density = 0.1f;
            APROPERTY(Range(-1000.0f,1000.0f))
            f32 _fog_height = 0.0f;
            APROPERTY(Range(0.0f,1.0f))
            f32 _height_falloff = 0.02f;
            APROPERTY(Range(1.0f,1000.0f))
            f32 _fog_distance = 50.0f;
            APROPERTY()
            bool _temporal_reprojection = false;
            // Weight of the current injection; lower values retain more history.
            APROPERTY(Range(0.0f,1.0f))
            f32 _blend_factor = 0.5f;
            APROPERTY(Range(-1.0f,1.0f))
            f32 _g = 0.0f;
            APROPERTY(Range(0.0f,10.0f))
            f32 _intensity = 1.0f;
            APROPERTY(Range(0.0f,10.0f))
            f32 _ambient_intensity = 1.0f;
            APROPERTY(Range(1,32))
            u32 _xy_tile_size = 8u;
            APROPERTY(Range(16,256))
            u32 _z_slice_count = 192u;
        private:
            void UpdateLocalFogVolumes(const RenderingData &rendering_data);
            Ref<GPUBuffer> _local_fog_buffer;
            bool _local_fog_overflow = false;
            Vector<LocalFogVolumeData> _previous_local_fog_volumes;
            std::array<f32, 13> _previous_fog_settings{};
            u32 _local_fog_volume_count = 0u;
            void UpdateFroxelResolution(u32 viewport_width, u32 viewport_height);
            Scope<VolumetricFogPass> _volumetric_fog_pass;
            Scope<VolumetricFogCompositePass> _composite_pass;
            Ref<Texture3D> _accum_texture;
            Ref<Texture3D> _medium_texture;
            u32 _resource_viewport_width = 0u;
            u32 _resource_viewport_height = 0u;
            u32 _resource_xy_tile_size = 0u;
            u32 _resource_z_slice_count = 0u;
            Ref<Texture3D> _scratch_light_a;
            Ref<Texture3D> _scratch_light_b;
            Map<u64, VolumetricFogHistory> _histories;
            u64 _last_camera_hash = 0u;
            Texture3D *_debug_light_texture = nullptr;
            bool _scratch_is_cur_a = true;
            AssetRef<ComputeShader> _volumetric_fog_cs;
            AssetRef<ComputeShader> _max_z_cs;
            Vector3UInt _voxel_num = Vector3UInt(170, 90, 96);
        };

        class VolumetricFogPass : public RenderPass
        {
            friend class VolumetricFog;
        public:
            VolumetricFogPass();
            ~VolumetricFogPass();
            void OnRecordRenderGraph(RDG::RenderGraph& graph, RenderingData& rendering_data) final;
            void BeginPass(GraphicsContext* context) final;
            void EndPass(GraphicsContext* context) final;
        public:
            bool _debug_voxel_pos = false;
            bool _debug_extinction = false;
            Vector3UInt _voxel_num;
            f32 _fog_distance = 50.0f;
            bool _temporal_reprojection = false;
            bool _history_valid = false;
            VolumetricFogHistory *_history_state = nullptr;
            f32 _environment_intensity = 0.0f;
            Ref<GPUBuffer> _local_fog_buffer;
            u32 _local_fog_count = 0u;
            bool _local_lights_enabled = true;
            bool _bounded_local_lights = true;
            u32 _max_local_lights = 8u;
            u32 _local_light_count = 0u;
            u64 _local_light_froxel_count = 0u;
            Texture3D *_medium_texture = nullptr;
        private:
            AssetRef<ComputeShader> _volumetric_fog;
            AssetRef<ComputeShader> _max_z_cs;
            ComputeShaderKernelId _light_injection_kernel = kInvalidComputeShaderKernelId;
            bool _local_light_overflow = false;
            ComputeShaderKernelId _local_light_kernel = kInvalidComputeShaderKernelId;
            ComputeShaderKernelId _temporal_kernel = kInvalidComputeShaderKernelId;
            RDG::RGHandle _medium_handle;
            ComputeShaderKernelId _light_integration_kernel = kInvalidComputeShaderKernelId;
            Texture3D* _cur_light_texture;
            Texture3D* _history_light_texture;
            Texture3D* _accum_texture;
            RDG::RGHandle _inject_handle;
            RDG::RGHandle _history_light_handle;
            RDG::RGHandle _accum_handle;
            Ref<Material> _debug_material;
        };

        class VolumetricFogCompositePass : public RenderPass
        {
        public:
            VolumetricFogCompositePass();
            void OnRecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data) override;
            f32 _fog_distance = 50.0f;

        private:
            Ref<Material> _composite_material;
        };

    } // namespace Render
}
#endif // VOLUMETRIC_FOG_H
