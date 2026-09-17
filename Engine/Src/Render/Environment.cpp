#include "pch.h"

#include "Render/Environment.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace Ailu::Render
{
    namespace
    {
        bool SameColor(const Color &lhs, const Color &rhs)
        {
            return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
        }

        // 主光转过约 5 度以内不重烘环境 cubemap。绕场景旋转主光时若每帧重烘，
        // 会逐帧重建 512 的 cubemap + radiance + prefilter + SH 投影，形成 GPU 尖峰。
        constexpr f32 kMainlightRebakeCosThreshold = 0.996f;

        bool IsMainlightRebakeRequired(const Vector3f &current, const Vector3f &baked)
        {
            const f32 current_length_sq = SqrMagnitude(current);
            const f32 baked_length_sq = SqrMagnitude(baked);
            // 只有"当前有主光但从未按该方向烘过"才强制重烘。当前没有主光（零向量）由调用方
            // 处理：无方向光的场景不应每帧重烘环境。
            if (baked_length_sq < kFloatEpsilon)
                return true;
            if (current_length_sq < kFloatEpsilon)
                return false;
            const f32 cos_angle = DotProduct(current, baked) / sqrt(current_length_sq * baked_length_sq);
            return cos_angle < kMainlightRebakeCosThreshold;
        }
    }

    EnvironmentSystem::EnvironmentSystem()
    {
        _fallback_cubemap = RenderTexture::Create(1u, "EnvironmentFallback", ERenderTargetFormat::kDefaultHDR, true, true, true);
        _procedural_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/skybox.alasset"),
                                                  "ProceduralSkyIBL");
        _debug_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/cubemap_debug.alasset"),
                                            "EnvironmentDebug");
        _runtime._debug_material = _debug_material.get();
        _lut_generator = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/atmosphere_lut_gen.alasset");
        _transmittance_lut_kernel = _lut_generator->FindKernel("TransmittanceGen");
        _mult_scatter_lut_kernel = _lut_generator->FindKernel("MultiScattGen");
        _sky_lut_kernel = _lut_generator->FindKernel("SkyLightGen");
        _transmittance_lut = RenderTexture::Create(_transmittance_lut_size.x, _transmittance_lut_size.y,
                                                   "_EnvironmentTransmittanceLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);
        _mult_scatter_lut = RenderTexture::Create(_mult_scatter_lut_size.x, _mult_scatter_lut_size.y,
                                                  "_EnvironmentMultScatterLUT", ERenderTargetFormat::kRGBAHalf, false, false, true);
        _sky_view_lut = RenderTexture::Create(_sky_lut_size.x, _sky_lut_size.y, "_EnvironmentSkyViewLUT",
                                              ERenderTargetFormat::kRGBAHalf, false, false, true);
        auto cmd = CommandBufferPool::Get("EnvironmentLutGen");
        _lut_generator->SetTexture(_transmittance_lut_kernel, "_TransmittanceLUT", _transmittance_lut.get());
        cmd->Dispatch(_lut_generator.get(), _transmittance_lut_kernel, _transmittance_lut_size.x / 16u,
                      _transmittance_lut_size.y / 16u, 1u);
        _lut_generator->SetTexture(_mult_scatter_lut_kernel, "_TexTransmittanceLUT", _transmittance_lut.get());
        _lut_generator->SetTexture(_mult_scatter_lut_kernel, "_MultScatterLUT", _mult_scatter_lut.get());
        cmd->Dispatch(_lut_generator.get(), _mult_scatter_lut_kernel, _mult_scatter_lut_size.x / 16u,
                      _mult_scatter_lut_size.y / 16u, 1u);
        cmd->RequireState(_transmittance_lut.get(), EResourceState::kCommon);
        cmd->RequireState(_mult_scatter_lut.get(), EResourceState::kCommon);
        g_pGfxContext->ExecuteCommandBufferSync(cmd);
        CommandBufferPool::Release(cmd);
        _procedural_sky = ECS::ProceduralSkySettings{};
        SetFallbackRuntime();
    }

    Ref<Texture> EnvironmentSystem::ResolveTexture(const Guid &guid, u64 &revision)
    {
        revision = 0u;
        if (guid.IsEmpty())
            return nullptr;

        ResourceMgr::Get().Load<Texture2D>(guid);
        if (Ref<Texture2D> texture = ResourceMgr::Get().GetRef<Texture2D>(guid))
        {
            revision = ResourceMgr::Get().GetOrCreateAssetHandle<Texture2D>(guid).GetRevision();
            return texture;
        }

        ResourceMgr::Get().Load<CubeMap>(guid);
        if (Ref<CubeMap> texture = ResourceMgr::Get().GetRef<CubeMap>(guid))
        {
            revision = ResourceMgr::Get().GetOrCreateAssetHandle<CubeMap>(guid).GetRevision();
            return texture;
        }
        return nullptr;
    }

    void EnvironmentSystem::SetFallbackRuntime()
    {
        _runtime._source = nullptr;
        _runtime._sky_cubemap = _fallback_cubemap;
        _runtime._diffuse_irradiance = _fallback_cubemap;
        _runtime._specular_prefilter = _fallback_cubemap;
        _runtime._diffuse_sh = _diffuse_projector.Output();
        _runtime._transmittance_lut = _transmittance_lut;
        _runtime._mult_scatter_lut = _mult_scatter_lut;
        _runtime._sky_view_lut = _sky_view_lut;
        _runtime._source_revision = 0u;
    }

    void EnvironmentSystem::BindDiffuseSH()
    {
        Shader::SetGlobalBuffer("g_environment_sh", _diffuse_projector.Output().get());
        ComputeShader::SetGlobalBuffer("g_environment_sh", _diffuse_projector.Output().get());
    }

    bool EnvironmentSystem::HasProceduralSettingsChanged(const ECS::ProceduralSkySettings &settings) const
    {
        return !SameColor(settings._sky_color, _procedural_sky._sky_color) ||
               !SameColor(settings._ground_color, _procedural_sky._ground_color);
    }

    static bool SameVector(const Vector3f &lhs, const Vector3f &rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }

    void EnvironmentSystem::Update(const SceneManagement::Scene &scene)
    {
        const ECS::EnvironmentComponent *active_component = nullptr;
        ECS::Entity active_entity = ECS::kInvalidEntity;
        u32 environment_count = 0u;
        for (ECS::Entity entity : scene.GetRegister().EntityView<ECS::EnvironmentComponent>())
        {
            if (!scene.GetRegister().IsComponentEnabled<ECS::EnvironmentComponent>(entity))
                continue;
            ++environment_count;
            if (active_component == nullptr || entity < active_entity)
            {
                active_component = scene.GetRegister().GetComponent<ECS::EnvironmentComponent>(entity);
                active_entity = entity;
            }
        }
        if (environment_count > 1u)
            LOG_WARNING("EnvironmentSystem: multiple active EnvironmentComponent instances; using entity {}", active_entity);

        ECS::EnvironmentComponent default_component;
        const ECS::EnvironmentComponent &component = active_component != nullptr ? *active_component : default_component;
        bool has_mainlight = false;
        Vector3f mainlight_direction = Vector3f::kZero;
        for (const auto &light_component : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light_component._type != ECS::ELightType::kDirectional)
                continue;
            has_mainlight = true;
            mainlight_direction = light_component._light._light_dir.xyz;
            break;
        }
        u64 source_revision = 0u;
        Ref<Texture> source_ref = component._source == ECS::EEnvironmentSource::kTexture
                                      ? ResolveTexture(component._texture, source_revision)
                                      : nullptr;
        const bool source_changed = !_initialized || component._source != _source || component._texture != _texture_guid ||
                                    HasProceduralSettingsChanged(component._procedural_sky) || source_revision != _source_revision;
        const bool mainlight_changed = !_initialized || has_mainlight != _has_mainlight ||
                                       !SameVector(mainlight_direction, _mainlight_direction);
        if (source_changed)
            _resources_dirty = true;
        // cubemap 里烘了太阳，主光大幅转向后必须重建；但每帧重建代价过高，
        // 这里用角度阈值做迟滞，避免旋转主光时逐帧重烘整个环境。
        // 没有主光的场景（纯 GI / 纯自发光测试场景）cubemap 里不含太阳，
        // 不能走 IsMainlightRebakeRequired 的退化分支，否则会逐帧重建 512 cubemap
        // + radiance + prefilter + SH 投影；只有"原本有主光、现在没了"时才需要重烘一次
        // 把烘进去的太阳去掉。
        const bool mainlight_rebake_required = has_mainlight
            ? IsMainlightRebakeRequired(mainlight_direction, _baked_mainlight_direction)
            : _has_mainlight;
        const bool rebake_env = _source == ECS::EEnvironmentSource::kProceduralSky &&
                                (source_changed || (!_resources_dirty && mainlight_rebake_required));
        if (rebake_env)
        {
            _resources_dirty = true;
            _baked_mainlight_direction = mainlight_direction;
        }
        if (source_changed || mainlight_changed)
            _sky_lut_dirty = true;
        if (source_changed)
        {
            _cube_map_pass.reset();
            _source_ref = std::move(source_ref);
            _source = component._source;
            _texture_guid = component._texture;
            _procedural_sky = component._procedural_sky;
            _source_revision = source_revision;
            if (_source == ECS::EEnvironmentSource::kTexture && _source_ref != nullptr)
                _cube_map_pass = MakeRef<CubeMapGenPass>(_source_ref.get());
            else if (_source == ECS::EEnvironmentSource::kProceduralSky && _procedural_material != nullptr)
                _cube_map_pass = MakeRef<CubeMapGenPass>(_procedural_material.get());
            else
                SetFallbackRuntime();
        }
        _mainlight_direction = mainlight_direction;
        _has_mainlight = has_mainlight;

        _has_environment = active_component != nullptr;
        _runtime._intensity = component._intensity;
        _runtime._rotation = component._rotation;
        _runtime._render_sky = component._render_sky;
        _runtime._enable_ibl = component._enable_ibl;
        _runtime._is_texture_source = _source == ECS::EEnvironmentSource::kTexture && _source_ref != nullptr;
        _initialized = true;
        _runtime._source_revision = _source_revision;
        _runtime._sky_color = component._procedural_sky._sky_color;
        _runtime._ground_color = component._procedural_sky._ground_color;
        if (_runtime._is_texture_source)
        {
            _runtime._source = _source_ref.get();
            if (_cube_map_pass != nullptr)
            {
                _runtime._sky_cubemap = _cube_map_pass->IsSourceCubemap() ? _source_ref : _cube_map_pass->_src_cubemap;
                _runtime._diffuse_irradiance = _cube_map_pass->_radiance_map;
                _runtime._specular_prefilter = _cube_map_pass->_prefilter_cubemap;
            }
        }
        else if (_source == ECS::EEnvironmentSource::kProceduralSky && _cube_map_pass != nullptr)
        {
            _runtime._source = nullptr;
            _runtime._sky_cubemap = _cube_map_pass->_src_cubemap;
            _runtime._diffuse_irradiance = _cube_map_pass->_radiance_map;
            _runtime._specular_prefilter = _cube_map_pass->_prefilter_cubemap;
            _runtime._transmittance_lut = _transmittance_lut;
            _runtime._mult_scatter_lut = _mult_scatter_lut;
            _runtime._sky_view_lut = _sky_view_lut;
        }
        else
            SetFallbackRuntime();
    }

    void EnvironmentSystem::RecordRenderGraph(RDG::RenderGraph &graph, RenderingData &rendering_data)
    {
        rendering_data._rg_handles._environment_sky = graph.Import(_runtime._sky_cubemap.get());
        rendering_data._rg_handles._environment_sh = graph.Import(_diffuse_projector.Output().get());
        rendering_data._rg_handles._environment_diffuse = graph.Import(_runtime._diffuse_irradiance.get());
        rendering_data._rg_handles._environment_specular = graph.Import(_runtime._specular_prefilter.get());
        rendering_data._rg_handles._environment_transmittance_lut = graph.Import(_transmittance_lut.get());
        rendering_data._rg_handles._environment_mult_scatter_lut = graph.Import(_mult_scatter_lut.get());
        rendering_data._rg_handles._sky_view_lut = graph.Import(_sky_view_lut.get());
        if (!_runtime._is_texture_source && _sky_lut_dirty && _lut_generator != nullptr)
        {
            graph.AddPass("EnvironmentSkyLUT", RDG::PassDesc{RDG::EPassType::kCompute}, [&](RDG::RenderGraphBuilder &builder)
            {
                builder.Read(rendering_data._rg_handles._environment_transmittance_lut);
                builder.Read(rendering_data._rg_handles._environment_mult_scatter_lut);
                rendering_data._rg_handles._sky_view_lut = builder.Write(rendering_data._rg_handles._sky_view_lut,
                                                                          EResourceUsage::kWriteUAV);
            }, [this](RDG::RenderGraph &graph, CommandBuffer *cmd, const RenderingData &data)
            {
                auto *sky_lut = graph.Resolve<RenderTexture>(data._rg_handles._sky_view_lut);
                _lut_generator->SetTexture(_sky_lut_kernel, "_TexTransmittanceLUT",
                                           graph.Resolve<Texture>(data._rg_handles._environment_transmittance_lut));
                _lut_generator->SetTexture(_sky_lut_kernel, "_TexMultScatterLUT",
                                           graph.Resolve<Texture>(data._rg_handles._environment_mult_scatter_lut));
                _lut_generator->SetTexture(_sky_lut_kernel, "_SkyLightLUT", sky_lut);
                _lut_generator->SetVector("_MainLightPosition", data._mainlight_world_position);
                cmd->Dispatch(_lut_generator.get(), _sky_lut_kernel, _sky_lut_size.x / 16u, _sky_lut_size.y / 16u, 1u);
            });
            _sky_lut_dirty = false;
        }
        if (_resources_dirty && _cube_map_pass != nullptr)
        {
            _cube_map_pass->OnRecordRenderGraph(graph, rendering_data);
            _runtime._sky_cubemap = _cube_map_pass->IsSourceCubemap() ? _source_ref : _cube_map_pass->_src_cubemap;
            _runtime._diffuse_irradiance = _cube_map_pass->_radiance_map;
            _runtime._specular_prefilter = _cube_map_pass->_prefilter_cubemap;
            _runtime._diffuse_sh = _diffuse_projector.Output();
            rendering_data._rg_handles._environment_sh =
                _diffuse_projector.RecordRenderGraph(graph, rendering_data._rg_handles._environment_sky);
        }
        // 环境 SH 每帧重新绑定，不能被其它系统（探针烘焙）长期占用该全局槽位
        BindDiffuseSH();
        _resources_dirty = false;
        if (_runtime._is_texture_source || _lut_generator == nullptr)
            _sky_lut_dirty = false;
    }

    void EnvironmentSystem::Execute(GraphicsContext *context, RenderingData &rendering_data)
    {
        if (!_resources_dirty && !_sky_lut_dirty)
            return;
        if (_runtime._is_texture_source || _lut_generator == nullptr || _sky_view_lut == nullptr)
        {
            if (_resources_dirty && _cube_map_pass != nullptr)
            {
                _cube_map_pass->Execute(context, rendering_data);
                _runtime._sky_cubemap = _cube_map_pass->IsSourceCubemap() ? _source_ref : _cube_map_pass->_src_cubemap;
                _runtime._diffuse_irradiance = _cube_map_pass->_radiance_map;
                _runtime._specular_prefilter = _cube_map_pass->_prefilter_cubemap;
                _runtime._diffuse_sh = _diffuse_projector.Output();
                auto cmd = CommandBufferPool::Get("EnvironmentSHProject");
                _diffuse_projector.Execute(cmd.get(), _runtime._sky_cubemap.get(), _diffuse_projector.Output().get());
                context->ExecuteCommandBuffer(cmd);
                CommandBufferPool::Release(cmd);
                BindDiffuseSH();
            }
            _resources_dirty = false;
            _sky_lut_dirty = false;
            return;
        }
        if (_sky_lut_dirty)
        {
            auto cmd = CommandBufferPool::Get("EnvironmentSkyLUT");
            _lut_generator->SetTexture(_sky_lut_kernel, "_TexTransmittanceLUT", _transmittance_lut.get());
            _lut_generator->SetTexture(_sky_lut_kernel, "_TexMultScatterLUT", _mult_scatter_lut.get());
            _lut_generator->SetTexture(_sky_lut_kernel, "_SkyLightLUT", _sky_view_lut.get());
            _lut_generator->SetVector("_MainLightPosition", rendering_data._mainlight_world_position);
            cmd->RequireState(_sky_view_lut.get(), EResourceState::kUnorderedAccess);
            cmd->Dispatch(_lut_generator.get(), _sky_lut_kernel, _sky_lut_size.x / 16u, _sky_lut_size.y / 16u, 1u);
            cmd->RequireState(_sky_view_lut.get(), EResourceState::kCommon);
            context->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
            _runtime._sky_view_lut = _sky_view_lut;
            _sky_lut_dirty = false;
        }
        if (_resources_dirty && _cube_map_pass != nullptr)
        {
            _cube_map_pass->Execute(context, rendering_data);
            _runtime._sky_cubemap = _cube_map_pass->IsSourceCubemap() ? _source_ref : _cube_map_pass->_src_cubemap;
            _runtime._diffuse_irradiance = _cube_map_pass->_radiance_map;
            _runtime._specular_prefilter = _cube_map_pass->_prefilter_cubemap;
            _runtime._diffuse_sh = _diffuse_projector.Output();
            auto cmd = CommandBufferPool::Get("EnvironmentSHProject");
            _diffuse_projector.Execute(cmd.get(), _runtime._sky_cubemap.get(), _diffuse_projector.Output().get());
            context->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
            BindDiffuseSH();
        }
        _resources_dirty = false;
    }
}
