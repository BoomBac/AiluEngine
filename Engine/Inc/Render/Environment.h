#ifndef __RENDER_ENVIRONMENT_H__
#define __RENDER_ENVIRONMENT_H__

#include "Assets/AssetRef.h"
#include "Render/Features/CommonPasses.h"
#include "Render/SphericalHarmonicsProjector.h"
#include "Scene/Scene.h"

namespace Ailu::Render
{
    class AILU_API EnvironmentSystem : public NonCopyable
    {
    public:
        EnvironmentSystem();
        ~EnvironmentSystem() = default;

        void Update(const SceneManagement::Scene &scene);
        void RecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data);
        void Execute(GraphicsContext *context, RenderingData &rendering_data);

        const EnvironmentRuntimeData &Runtime() const { return _runtime; }

    private:
        Ref<Texture> ResolveTexture(const Guid &guid, u64 &revision);
        void SetFallbackRuntime();
        void BindDiffuseSH();
        bool HasProceduralSettingsChanged(const ECS::ProceduralSkySettings &settings) const;

        EnvironmentRuntimeData _runtime;
        Ref<Texture> _source_ref;
        Ref<RenderTexture> _fallback_cubemap;
        Ref<CubeMapGenPass> _cube_map_pass;
        SphericalHarmonicsProjector _diffuse_projector;
        Ref<Material> _procedural_material;
        Ref<Material> _debug_material;
        AssetRef<ComputeShader> _lut_generator;
        ComputeShaderKernelId _transmittance_lut_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _mult_scatter_lut_kernel = kInvalidComputeShaderKernelId;
        ComputeShaderKernelId _sky_lut_kernel = kInvalidComputeShaderKernelId;
        Ref<RenderTexture> _transmittance_lut;
        Ref<RenderTexture> _mult_scatter_lut;
        Ref<RenderTexture> _sky_view_lut;
        Vector2Int _sky_lut_size = Vector2Int(192, 192);
        Vector2Int _transmittance_lut_size = Vector2Int(256, 64);
        Vector2Int _mult_scatter_lut_size = Vector2Int(32, 32);
        ECS::EEnvironmentSource _source = ECS::EEnvironmentSource::kProceduralSky;
        ECS::ProceduralSkySettings _procedural_sky;
        Vector3f _mainlight_direction = Vector3f::kZero;
        // 上一次真正重烘环境 cubemap 时使用的主光方向。ProceduralSky 的 cubemap 会把太阳烘进去，
        // 但每帧重烘一遍 cubemap + radiance + prefilter + SH 会造成巨大的逐帧 GPU 尖峰，
        // 所以只在主光转过一定角度后才标记重烘；SkyViewLUT 仍然每次变化都更新，太阳位置保持精确。
        Vector3f _baked_mainlight_direction = Vector3f::kZero;
        Guid _texture_guid = Guid::EmptyGuid();
        u64 _source_revision = 0u;
        bool _resources_dirty = true;
        bool _sky_lut_dirty = true;
        bool _initialized = false;
        bool _has_environment = false;
        bool _has_mainlight = false;
    };
}

#endif // __RENDER_ENVIRONMENT_H__
