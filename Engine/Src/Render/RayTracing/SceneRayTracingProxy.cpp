#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Math/MathHash.hpp"
#include "Animation/SkinningSystem.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/Texture.h"
#include "pch.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace Ailu::Render
{
    namespace
    {
        template<typename T>
        Ref<GPUBuffer> CreateStructuredBuffer(const Vector<T> &data, const String &name)
        {
            BufferDesc desc = {};
            desc._target = EGPUBufferTarget::kStructured;
            desc._element_size = sizeof(T);
            desc._element_num = static_cast<u32>(data.size());
            desc._is_random_write = true;
            auto buffer = GPUBuffer::Create(desc, name);
            buffer->SetData(reinterpret_cast<const u8 *>(data.data()), static_cast<u32>(data.size() * sizeof(T)));
            buffer->ApplySync();
            return buffer;
        }

        template<typename T>
        Ref<GPUBuffer> CreateDynamicStructuredBuffer(const Vector<T> &data, const String &name)
        {
            BufferDesc desc = {};
            desc._target = EGPUBufferTarget::kStructured;
            desc._element_size = sizeof(T);
            desc._element_num = std::max(1u, static_cast<u32>(data.capacity()));
            desc._is_random_write = true;
            auto buffer = GPUBuffer::Create(desc, name);
            if (!data.empty())
                buffer->SetData(reinterpret_cast<const u8 *>(data.data()), static_cast<u32>(data.size() * sizeof(T)));
            buffer->ApplySync();
            return buffer;
        }

        constexpr float kDefaultSpotSourceRadius = 0.05f;
        constexpr float kEmissiveTriangleSampleThreshold = 1.0e-4f;

        struct EmissiveTriangleCacheKey
        {
            const Mesh *_mesh = nullptr;
            u16 _submesh = 0u;
            u16 _texture_width = 0u;
            u16 _texture_height = 0u;
            u64 _texture_hash = 0u;
            EALGFormat _texture_format = EALGFormat::kALGFormatUNKOWN;
            Vector3f _emission = Vector3f::kZero;

            bool operator==(const EmissiveTriangleCacheKey &other) const
            {
                return _mesh == other._mesh &&
                       _submesh == other._submesh &&
                       _texture_width == other._texture_width &&
                       _texture_height == other._texture_height &&
                       _texture_hash == other._texture_hash &&
                       _texture_format == other._texture_format &&
                       _emission == other._emission;
            }
        };

        struct EmissiveTriangleCacheKeyHasher
        {
            size_t operator()(const EmissiveTriangleCacheKey &key) const
            {
                size_t hash = std::hash<const void *>{}(key._mesh);
                hash = ALHash::HashCombineStable(hash, std::hash<u16>{}(key._submesh));
                hash = ALHash::HashCombineStable(hash, std::hash<u16>{}(key._texture_width));
                hash = ALHash::HashCombineStable(hash, std::hash<u16>{}(key._texture_height));
                hash = ALHash::HashCombineStable(hash, std::hash<u64>{}(key._texture_hash));
                hash = ALHash::HashCombineStable(hash, std::hash<int>{}(static_cast<int>(key._texture_format)));
                return ALHash::HashCombineStable(hash, ALHash::Vector3fHash{}(key._emission));
            }
        };

        HashMap<EmissiveTriangleCacheKey, Vector<u32>, EmissiveTriangleCacheKeyHasher> s_emissive_triangle_cache;
        const Vector<u32> kEmptyEmissiveTriangleIndices;

        bool HasEmissiveContribution(const Vector3f &emission_radiance, const Vector3f &texture_sample)
        {
            const Vector3f emitted_radiance(
                emission_radiance.x * texture_sample.x,
                emission_radiance.y * texture_sample.y,
                emission_radiance.z * texture_sample.z);
            return std::max(emitted_radiance.x, std::max(emitted_radiance.y, emitted_radiance.z)) > kEmissiveTriangleSampleThreshold;
        }

        Vector<u32> BuildAllTriangleIndices(u32 triangle_count)
        {
            Vector<u32> triangle_indices;
            triangle_indices.reserve(triangle_count);
            for (u32 tri_index = 0u; tri_index < triangle_count; ++tri_index)
                triangle_indices.push_back(tri_index);
            return triangle_indices;
        }

        bool TriangleHasEmissiveContribution(const Texture2D &texture, const Vector3f &emission_radiance, const Vector2f &uv0, const Vector2f &uv1, const Vector2f &uv2)
        {
            const Vector2f sample_uvs[] = {
                uv0,
                uv1,
                uv2,
                (uv0 + uv1) * 0.5f,
                (uv1 + uv2) * 0.5f,
                (uv2 + uv0) * 0.5f,
                (uv0 + uv1 + uv2) * (1.0f / 3.0f),
            };

            for (const auto &sample_uv : sample_uvs)
            {
                Color texture_sample = Colors::kBlack;
                if (!texture.TryGetPixelBilinear(sample_uv.x, sample_uv.y, texture_sample))
                    return true;
                if (HasEmissiveContribution(emission_radiance, texture_sample.xyz))
                    return true;
            }

            return false;
        }

        const Vector<u32> &GetEmissiveTriangleIndices(const Mesh &mesh, u16 submesh, Material &material)
        {
            if (!material.IsStandardLit())
                return kEmptyEmissiveTriangleIndices;
            const auto *emission_prop = material.MainProperty(ETextureUsage::kEmission);
            if (emission_prop == nullptr)
                return kEmptyEmissiveTriangleIndices;
            const auto emission_color = emission_prop->GetValue<Color>();
            const Vector3f emission_radiance = emission_color.xyz;
            const auto *emission_texture = dynamic_cast<const Texture2D *>(material.MainTex(ETextureUsage::kEmission));
            EmissiveTriangleCacheKey cache_key{};
            cache_key._mesh = &mesh;
            cache_key._submesh = submesh;
            cache_key._texture_width = emission_texture ? emission_texture->Width() : 0u;
            cache_key._texture_height = emission_texture ? emission_texture->Height() : 0u;
            cache_key._texture_hash = emission_texture ? emission_texture->HashCode() : 0u;
            cache_key._texture_format = emission_texture ? emission_texture->PixelFormat() : EALGFormat::kALGFormatUNKOWN;
            cache_key._emission = emission_radiance;

            if (auto cache_it = s_emissive_triangle_cache.find(cache_key); cache_it != s_emissive_triangle_cache.end())
                return cache_it->second;

            const auto &submesh_data = mesh.GetIndices(submesh);
            const u32 triangle_count = static_cast<u32>(submesh_data.size() / 3u);
            Vector<u32> triangle_indices;
            if (triangle_count == 0u)
                return s_emissive_triangle_cache.emplace(cache_key, std::move(triangle_indices)).first->second;

            if (emission_texture == nullptr)
            {
                triangle_indices = BuildAllTriangleIndices(triangle_count);
                return s_emissive_triangle_cache.emplace(cache_key, std::move(triangle_indices)).first->second;
            }

            const auto uvs = mesh.GetUVs();
            if (uvs.empty())
            {
                triangle_indices = BuildAllTriangleIndices(triangle_count);
                return s_emissive_triangle_cache.emplace(cache_key, std::move(triangle_indices)).first->second;
            }

            triangle_indices.reserve(triangle_count);
            for (u32 tri_index = 0u; tri_index < triangle_count; ++tri_index)
            {
                const u32 base_index = tri_index * 3u;
                const u32 i0 = submesh_data[base_index + 0u];
                const u32 i1 = submesh_data[base_index + 1u];
                const u32 i2 = submesh_data[base_index + 2u];
                if (i0 >= uvs.size() || i1 >= uvs.size() || i2 >= uvs.size())
                {
                    triangle_indices = BuildAllTriangleIndices(triangle_count);
                    break;
                }

                if (TriangleHasEmissiveContribution(*emission_texture, emission_radiance, uvs[i0], uvs[i1], uvs[i2]))
                    triangle_indices.push_back(tri_index);
            }

            return s_emissive_triangle_cache.emplace(cache_key, std::move(triangle_indices)).first->second;
        }

        Vector3f GetPremultipliedLightColor(const ECS::LightData &light_data)
        {
            Color color = light_data._light_color;
            color.r *= color.a;
            color.g *= color.a;
            color.b *= color.a;
            return color.xyz * kPi;
        }

        float GetSpotAngleScale(const Vector4f &light_param)
        {
            const float inner_cos = cos(light_param.y * 0.5f * k2Radius);
            const float outer_cos = cos(light_param.z * 0.5f * k2Radius);
            return 1.0f / std::max(0.001f, inner_cos - outer_cos);
        }

        float GetSpotAngleOffset(const Vector4f &light_param)
        {
            const float outer_cos = cos(light_param.z * 0.5f * k2Radius);
            return -outer_cos * GetSpotAngleScale(light_param);
        }

        UnifiedLightData BuildUnifiedLight(const ECS::LightComponent &comp)
        {
            UnifiedLightData light = {};
            const auto &light_data = comp._light;
            light._flags = light_data._is_two_side ? AL_UNIFIED_LIGHT_FLAG_TWO_SIDED : 0u;
            light._shadow_index = comp._shadow._is_cast_shadow ? 0 : -1;
            light._radiance = GetPremultipliedLightColor(light_data);
            light._shadow_params = float4(0.0f, comp._shadow._constant_bias, comp._shadow._slope_bias, 0.0f);

            switch (comp._type)
            {
            case ECS::ELightType::kDirectional:
                light._type = AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL;
                light._position = Vector3f::kZero;
                light._direction = -light_data._light_dir.xyz;
                light._source_radius = light_data._light_param.w;
                break;
            case ECS::ELightType::kPoint:
                light._type = AL_UNIFIED_LIGHT_TYPE_POINT;
                light._position = light_data._light_pos.xyz;
                light._range = light_data._light_param.x;
                light._source_radius = light_data._light_param.y;
                light._shadow_index = comp._shadow._is_cast_shadow ? static_cast<int>(light._shadow_index) : -1;
                light._shadow_params.x = light_data._light_param.x * 1.5f;
                break;
            case ECS::ELightType::kSpot:
                light._type = AL_UNIFIED_LIGHT_TYPE_SPOT;
                light._position = light_data._light_pos.xyz;
                light._direction = light_data._light_dir.xyz;
                light._range = light_data._light_param.x;
                light._source_radius = kDefaultSpotSourceRadius;
                light._spot_angle_scale = GetSpotAngleScale(light_data._light_param);
                light._spot_angle_offset = GetSpotAngleOffset(light_data._light_param);
                light._shadow_params.x = light_data._light_param.x * 1.5f;
                break;
            case ECS::ELightType::kArea:
            default:
                light._type = AL_UNIFIED_LIGHT_TYPE_RECT_AREA;
                light._shape_u = light_data._area_points[1] - light_data._area_points[0];
                light._shape_v = light_data._area_points[3] - light_data._area_points[0];
                light._position = light_data._area_points[0] + 0.5f * (light._shape_u + light._shape_v);
                light._range = light_data._light_param.x;
                light._direction = Vector3f::kZero;
                light._shadow_params.x = light_data._light_param.x * 1.5f;
                break;
            }

            return light;
        }

        template<typename T>
        Vector<UnifiedLightData> BuildUnifiedLight(const T &comp, u32 scene_triangle_offset, u32 base_instance_index)
        {
            (void)scene_triangle_offset;
            if (!comp._p_mesh)
                return {};
            Vector<UnifiedLightData> lights{};
            const auto &mesh = comp._p_mesh;

            for (u16 submesh = 0u; submesh < comp._p_mesh->SubmeshCount(); submesh++)
            {
                if (submesh >= comp._p_mats.size())
                    break;

                auto *mat = comp._p_mats[submesh].get();
                if (mat != nullptr && mat->IsStandardLit())
                {
                    const auto *emission_prop = mat->MainProperty(ETextureUsage::kEmission);
                    if (emission_prop == nullptr)
                        continue;
                    auto emission_color = emission_prop->GetValue<Color>();
                    if (emission_color.x <= 0.0f && emission_color.y <= 0.0f && emission_color.z <= 0.0f)
                        continue;
                    const Vector3f emission_radiance = emission_color.xyz;
                    UnifiedLightData light = {};
                    light._type  = AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA;
                    light._flags = AL_UNIFIED_LIGHT_FLAG_TWO_SIDED;
                    light._shadow_index = static_cast<int>(base_instance_index + submesh);
                    light._radiance = emission_radiance;
                    light._emissive_map = mat->MainTex(ETextureUsage::kEmission) ? mat->MainTex(ETextureUsage::kEmission)->GetBindlessSRVIndex() : RenderConstants::kInvalidBindlessHandle;
                    const auto &triangle_indices = GetEmissiveTriangleIndices(*mesh, submesh, *mat);
                    lights.reserve(lights.size() + triangle_indices.size());
                    for (u32 tri_index : triangle_indices)
                    {
                        lights.push_back(light);
                        auto &cur_light = lights.back();
                        cur_light._tri_index = tri_index;
                    }
                }
            }
            return lights;
        }
    }

    SceneRayTracingProxy::SceneRayTracingProxy()
    {
        _unified_light_data_cache.reserve(32u);
        _unified_light_sampling_cache.reserve(32u);
        _unified_light_buffer = CreateDynamicStructuredBuffer(_unified_light_data_cache, "SceneRayTracingProxy_UnifiedLightBuffer");
        _unified_light_sampling_buffer = CreateDynamicStructuredBuffer(_unified_light_sampling_cache, "SceneRayTracingProxy_UnifiedLightSamplingBuffer");
        _path_light_lookup_cache.resize(4u, 0u);
        _path_light_lookup_buffer =
            CreateDynamicStructuredBuffer(_path_light_lookup_cache, "SceneRayTracingProxy_PathLightLookup");
        _unified_light_config = ConstantBuffer::Create(sizeof(UnifiedLightBufferConfig), "SceneRayTracingProxy_UnifiedLightConfig");
        _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
    }


    void SceneRayTracingProxy::Sync(const SceneManagement::Scene *scene, Vector<PrimitiveData> *primitive_data,
                                    GPUBuffer *primitive_buffer, const Vector<ScenePrimitive> *scene_primitives)
    {
        PROFILE_BLOCK_CPU("SceneRayTracingProxy::Sync")
        _scene_primitive_data = primitive_data;
        _scene_primitive_buffer = primitive_buffer;
        _scene_primitives = scene_primitives;
        SyncLightCache(scene, primitive_data, scene_primitives);
        if (scene == nullptr)
            return;

        if (_scene_primitive_data == nullptr || _scene_primitive_buffer == nullptr || _scene_primitives == nullptr)
        {
            LOG_WARNING("SceneRayTracingProxy: scene primitive data is unavailable, skipping hardware ray tracing scene sync.");
            ClearSlot(FrontSlot());
            ClearSlot(BackSlot());
            _has_pending_swap = false;
            return;
        }

        TrySwapBuffers();

        _transforms.clear();
        auto instance_keys = CollectInstanceKeys(*scene, _transforms);
        if (instance_keys.empty())
        {
            ClearSlot(FrontSlot());
            ClearSlot(BackSlot());
            _has_pending_swap = false;
            return;
        }

        if (_has_pending_swap)
        {
            if (NeedsStructureRebuild(BackSlot(), instance_keys))
                StartRebuild(instance_keys, _transforms);
            else
                UpdateTransforms(BackSlot(), _transforms);

            TrySwapBuffers();

            if (_has_pending_swap || FrontSlot()._scene == nullptr || NeedsStructureRebuild(FrontSlot(), instance_keys))
                return;

            UpdateTransforms(FrontSlot(), _transforms);
            return;
        }

        if (FrontSlot()._scene == nullptr || NeedsStructureRebuild(FrontSlot(), instance_keys))
        {
            StartRebuild(instance_keys, _transforms);
            TrySwapBuffers();
            return;
        }

        UpdateTransforms(FrontSlot(), _transforms);
    }

    void SceneRayTracingProxy::SyncLightCache(const SceneManagement::Scene *scene, Vector<PrimitiveData> *primitive_data,
                                             const Vector<ScenePrimitive> *scene_primitives)
    {
        _scene_primitive_data = primitive_data;
        _scene_primitives = scene_primitives;
        if (scene == nullptr)
        {
            _unified_light_data_cache.clear();
            _unified_light_sampling_cache.clear();
            _unified_light_config_cpu = {};
            _path_light_lookup_cache.assign(4u, 0u);
            _path_light_lookup_buffer->SetData(_path_light_lookup_cache);
            _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
            return;
        }

        UpdateUnifiedLights(*scene);
    }

    Vector<SceneRayTracingProxy::SceneInstanceKey> SceneRayTracingProxy::CollectInstanceKeys(
        const SceneManagement::Scene &scene, Vector<Matrix4x4f> &transforms) const
    {
        Vector<SceneInstanceKey> instance_keys;
        const auto &reg = scene.GetRegister();
        auto append_instance_keys = [&](auto &renderables, bool is_skinned)
        {
            using ComponentType = std::remove_cvref_t<decltype(*renderables.begin())>;
            u64 entity_index = 0u;
            for (const auto &renderable : renderables)
            {
                const auto entity = reg.GetEntity<ComponentType>(entity_index);
                const auto *transform_comp = reg.GetComponent<ComponentType, ECS::TransformComponent>(entity_index);
                ++entity_index;
                if (!scene.IsEntityEnabled(entity) || !reg.IsComponentEnabled<ComponentType>(entity) ||
                    renderable._p_mesh == nullptr || transform_comp == nullptr)
                    continue;

                auto *mesh = renderable._p_mesh.get();
                if (mesh->SubmeshCount() == 0u || mesh->GetVertexBuffer() == nullptr || mesh->GetVertices().empty())
                    continue;
                auto *vertex_buffer = is_skinned ? ECS::SkinningSystem::ResolveVertexBuffer(mesh, static_cast<u32>(entity)) : nullptr;
                if (vertex_buffer == nullptr)
                    vertex_buffer = mesh->GetVertexBuffer().get();

                SceneInstanceKey instance_key;
                instance_key._entity = entity;
                instance_key._mesh = mesh;
                instance_key._vertex_buffer = vertex_buffer;
                instance_key._dynamic = is_skinned && vertex_buffer != mesh->GetVertexBuffer().get();
                auto mat = renderable._p_mats.empty() ? nullptr : renderable._p_mats[0].get();
                if (mat != nullptr && mat->IsStandardLit() && mat->MaterialID() == EMaterialID::kChecker)
                    instance_key._material_class = 1u;
                instance_keys.push_back(instance_key);
                for (u32 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
                {
                    transforms.push_back(transform_comp->GetRenderWorldMatrix());
                }
            }
        };
        append_instance_keys(reg.View<ECS::StaticMeshComponent>(), false);
        append_instance_keys(reg.View<ECS::CSkeletonMesh>(), true);
        return instance_keys;
    }

    bool SceneRayTracingProxy::NeedsStructureRebuild(const ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys) const
    {
        if (slot._instance_keys.size() != instance_keys.size())
            return true;

        for (u64 index = 0u; index < instance_keys.size(); ++index)
        {
            if (slot._instance_keys[index]._entity != instance_keys[index]._entity ||
                slot._instance_keys[index]._mesh != instance_keys[index]._mesh ||
                slot._instance_keys[index]._vertex_buffer != instance_keys[index]._vertex_buffer ||
                slot._instance_keys[index]._dynamic != instance_keys[index]._dynamic)
            {
                return true;
            }
        }
        return false;
    }

    Vector<Ref<RayTracingGeometry>> SceneRayTracingProxy::AcquireGeometry(Mesh *mesh, VertexBuffer *vertex_buffer,
                                                                            bool dynamic)
    {
        if (mesh == nullptr || vertex_buffer == nullptr || !vertex_buffer->IsReady())
            return {};

        const GeometryCacheKey cache_key{mesh, vertex_buffer};
        if (auto it = _geometry_cache.find(cache_key); it != _geometry_cache.end())
            return it->second._geometries;
        CachedGeometry cached_geometry;
        cached_geometry._vertex_buffer = vertex_buffer;
        cached_geometry._dynamic = dynamic;
        cached_geometry._geometries.reserve(mesh->SubmeshCount());
        for (u32 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
        {
            const auto &submesh_ib = mesh->GetIndexBuffer(static_cast<u16>(submesh_index));
            if (submesh_ib == nullptr || mesh->GetIndices(static_cast<u16>(submesh_index)).empty())
            {
                LOG_WARNING(std::format("SceneRayTracingProxy: mesh {} submesh {} has no valid index data, skipping mesh.", mesh->Name(), submesh_index));
                return {};
            }

            RayTracingGeometryDesc desc{};
            desc._vertex_buffer = vertex_buffer;
            desc._index_buffer.push_back(submesh_ib.get());
            desc._vertex_stride = desc._vertex_buffer->GetLayout().GetStride(0);
            desc._vertex_count = desc._vertex_buffer->GetVertexCount();
            desc._opaque = true;
            desc._build_mode = dynamic ? ERayTracingGeometryBuildMode::kDynamic :
                                         ERayTracingGeometryBuildMode::kStatic;
            const String vertex_buffer_suffix = dynamic ?
                std::format("_vb_{}_{}", vertex_buffer->Handle()._index, vertex_buffer->Handle()._generation) : "";
            cached_geometry._geometries.push_back(RayTracingGeometry::Create(
                desc, std::format("{}_rt_blas{}_{}", mesh->Name(), vertex_buffer_suffix, submesh_index)));
        }
        auto [it, inserted] = _geometry_cache.insert_or_assign(cache_key, std::move(cached_geometry));
        return it->second._geometries;
    }

    SceneRayTracingProxy::CachedGeometry *SceneRayTracingProxy::FindCachedGeometry(Mesh *mesh,
                                                                                     VertexBuffer *vertex_buffer)
    {
        const auto it = _geometry_cache.find({mesh, vertex_buffer});
        return it == _geometry_cache.end() ? nullptr : &it->second;
    }

    const SceneRayTracingProxy::CachedGeometry *SceneRayTracingProxy::FindCachedGeometry(
        Mesh *mesh, VertexBuffer *vertex_buffer) const
    {
        const auto it = _geometry_cache.find({mesh, vertex_buffer});
        return it == _geometry_cache.end() ? nullptr : &it->second;
    }

    void SceneRayTracingProxy::StartRebuild(const Vector<SceneInstanceKey> &instance_keys, const Vector<Matrix4x4f> &transforms)
    {
        auto &back_slot = BackSlot();
        ClearSlot(back_slot);
        _has_pending_swap = Rebuild(back_slot, instance_keys, transforms);
        if (!_has_pending_swap)
            ClearSlot(FrontSlot());
    }

    bool SceneRayTracingProxy::Rebuild(ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys, const Vector<Matrix4x4f> &transforms)
    {
        slot._scene = RayTracingScene::Create();
        slot._scene->Name("SceneRayTracingProxyScene_RebuildBack");

        Vector<SceneInstanceKey> valid_instance_keys;
        valid_instance_keys.reserve(instance_keys.size());
        slot._instances.clear();
        slot._instances.reserve(transforms.size());
        u32 transform_offset = 0u;

        for (u64 index = 0u; index < instance_keys.size(); ++index)
        {
            auto *mesh = instance_keys[index]._mesh;
            const u32 submesh_count = mesh != nullptr ? mesh->SubmeshCount() : 0u;
            if (transform_offset + submesh_count > transforms.size())
            {
                LOG_WARNING("SceneRayTracingProxy: transform count does not match mesh submesh layout, aborting rebuild.");
                ClearSlot(slot);
                return false;
            }

            auto geometries = AcquireGeometry(instance_keys[index]._mesh, instance_keys[index]._vertex_buffer,
                                               instance_keys[index]._dynamic);
            if (geometries.empty())
            {
                LOG_WARNING(std::format("SceneRayTracingProxy: geometry acquisition failed for mesh {}, postponing swap.", mesh->Name()));
                ClearSlot(slot);
                return false;
            }
            bool is_all_geometries_ready = true;
            for (const auto &geometry : geometries)
            {
                if (geometry->IsReady())
                    continue;
                is_all_geometries_ready = false;
                break;
            }
            if (!is_all_geometries_ready)
            {
                LOG_WARNING(std::format("SceneRayTracingProxy: geometry for mesh {} is not ready, postponing swap.", instance_keys[index]._mesh->Name()));
                ClearSlot(slot);
                return false;
            }

            if (geometries.size() != submesh_count)
            {
                LOG_WARNING(std::format("SceneRayTracingProxy: geometry count does not match submesh count for mesh {}, postponing swap.", mesh->Name()));
                ClearSlot(slot);
                return false;
            }

            for (u32 submesh_index = 0u; submesh_index < geometries.size(); ++submesh_index)
            {
                const auto &transform = transforms[transform_offset + submesh_index];
                RayTracingInstance instance = {};
                instance._geometry = geometries[submesh_index].get();
                instance._transform = transform;
                u32 primitive_index = RenderConstants::kInvalidBindlessHandle;
                for (const auto &primitive : *_scene_primitives)
                {
                    if (primitive._entity_id == static_cast<u32>(instance_keys[index]._entity) && primitive._mesh == mesh &&
                        primitive._submesh_index == submesh_index)
                    {
                        primitive_index = primitive._primitive_index;
                        break;
                    }
                }
                if (primitive_index == RenderConstants::kInvalidBindlessHandle || primitive_index >= _scene_primitive_data->size())
                {
                    LOG_WARNING("SceneRayTracingProxy: failed to resolve a ray tracing instance to a scene primitive.");
                    ClearSlot(slot);
                    return false;
                }
                instance._instance_id = primitive_index;
                instance._hit_group_offset = instance_keys[index]._material_class;
                slot._scene->AddInstance(instance);
                slot._instances.push_back({primitive_index, transform});
            }

            transform_offset += submesh_count;
            valid_instance_keys.push_back(instance_keys[index]);
        }

        slot._instance_keys = std::move(valid_instance_keys);
        if (slot._instance_keys.empty())
        {
            ClearSlot(slot);
            return false;
        }

        if (!RebuildPackedBuffers(slot, slot._instance_keys))
        {
            ClearSlot(slot);
            return false;
        }

        slot._scene->ApplySync();
        return true;
    }

    void SceneRayTracingProxy::UpdateTransforms(ProxyBufferSlot &slot, const Vector<Matrix4x4f> &transforms)
    {
        if (slot._scene == nullptr || transforms.size() != slot._instances.size() || _scene_primitive_data == nullptr)
            return;

        bool any_transform_changed = false;
        for (u64 index = 0u; index < transforms.size(); ++index)
        {
            auto &instance = slot._instances[index];
            if (instance._transform == transforms[index])
                continue;

            if (instance._primitive_index >= _scene_primitive_data->size())
            {
                LOG_WARNING("SceneRayTracingProxy: scene primitive index is out of range while updating transforms.");
                continue;
            }
            auto &primitive_data = (*_scene_primitive_data)[instance._primitive_index];
            primitive_data._prev_local_to_world = primitive_data._local_to_world;
            primitive_data._local_to_world = transforms[index];
            primitive_data._world_to_local = MatrixInverse(transforms[index]);
            const Vector3f inv_scale = Vector3f::kOne / transforms[index].LossyScale();
            primitive_data._max_inv_scale = std::max(inv_scale.x, std::max(inv_scale.y, inv_scale.z));
            instance._transform = transforms[index];
            slot._scene->UpdateInstance(index, transforms[index]);
            any_transform_changed = true;
        }

        if (any_transform_changed)
        {
            _scene_primitive_buffer->SetData(*_scene_primitive_data);
        }
    }

    void SceneRayTracingProxy::AddBLASUpdatePass(RDG::RenderGraph &graph)
    {
        struct PendingUpdate
        {
            Ref<RayTracingGeometry> _geometry;
            GPUBuffer *_position_buffer = nullptr;
            IndexBuffer *_index_buffer = nullptr;
            bool _is_build = false;
        };

        Vector<PendingUpdate> pending_updates;
        std::unordered_set<RayTracingGeometry *> scheduled_geometries;
        for (const auto &instance_key : FrontSlot()._instance_keys)
        {
            if (!instance_key._dynamic)
                continue;

            auto *cached_geometry = FindCachedGeometry(instance_key._mesh, instance_key._vertex_buffer);
            if (cached_geometry == nullptr || !cached_geometry->_dynamic)
                continue;

            const u64 skin_version = ECS::SkinningSystem::ResolveSkinVersion(
                instance_key._mesh, static_cast<u32>(instance_key._entity));
            const bool needs_update = skin_version != cached_geometry->_last_blas_version;
            if (!needs_update && std::none_of(cached_geometry->_geometries.begin(), cached_geometry->_geometries.end(),
                                              [](const auto &geometry) { return geometry != nullptr && geometry->NeedsBuild(); }))
                continue;

            bool all_geometries_scheduled = true;
            for (const auto &geometry : cached_geometry->_geometries)
            {
                if (geometry == nullptr || !scheduled_geometries.emplace(geometry.get()).second)
                {
                    all_geometries_scheduled = false;
                    continue;
                }

                auto *mesh = instance_key._mesh;
                auto *vertex_buffer = geometry->GetDesc()._vertex_buffer;
                GPUBuffer *position_buffer = nullptr;
                if (mesh != nullptr && mesh->GetVertexBuffer() != nullptr && vertex_buffer != nullptr)
                {
                    for (const auto &element : mesh->GetVertexBuffer()->GetLayout().GetBufferDesc())
                    {
                        if (element._semantic == EVertexSemantic::kPosition)
                        {
                            position_buffer = vertex_buffer->GetGpuStream(element.Stream);
                            break;
                        }
                    }
                }
                if (position_buffer == nullptr)
                {
                    all_geometries_scheduled = false;
                    continue;
                }

                pending_updates.push_back({geometry, position_buffer, geometry->GetIndexBuffer(),
                                           geometry->NeedsBuild()});
            }
            if (all_geometries_scheduled)
                cached_geometry->_last_blas_version = skin_version;
        }

        if (pending_updates.empty())
            return;

        graph.AddPass("SkinnedBLASUpdate", RDG::PassDesc(RDG::EPassType::kRayTracing),
                      [pending_updates](RDG::RenderGraphBuilder &builder)
                      {
                          for (const auto &pending : pending_updates)
                          {
                              (void)builder.Read(builder.Import(pending._position_buffer), EResourceUsage::kReadSRV);
                              if (pending._index_buffer != nullptr)
                                  (void)builder.Read(builder.Import(pending._index_buffer), EResourceUsage::kReadSRV);
                              (void)builder.Write(builder.Import(pending._geometry.get()), EResourceUsage::kAccelStructWrite);
                          }
                      },
                      [pending_updates](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                      {
                          for (const auto &pending : pending_updates)
                          {
                              if (pending._is_build)
                                  pending._geometry->Build(cmd);
                              else
                                  pending._geometry->Update(cmd);
                          }
                      });
    }

    void SceneRayTracingProxy::AddTLASUpdatePass(RDG::RenderGraph &graph)
    {
        auto &slot = FrontSlot();
        if (slot._scene == nullptr || !slot._scene->NeedsUpdate())
            return;

        const auto scene = slot._scene;
        graph.AddPass("TLASUpdate", RDG::PassDesc(RDG::EPassType::kRayTracing),
                      [scene](RDG::RenderGraphBuilder &builder)
                      {
                          for (u64 index = 0u; index < scene->InstanceCount(); ++index)
                          {
                              const auto *instance = scene->GetInstance(index);
                              if (instance != nullptr && instance->_geometry != nullptr)
                                  (void)builder.Read(builder.Import(instance->_geometry), EResourceUsage::kAccelStructRead);
                          }
                          (void)builder.Write(builder.Import(scene.get()), EResourceUsage::kAccelStructWrite);
                      },
                      [scene](RDG::RenderGraph &, CommandBuffer *cmd, const RenderingData &)
                      {
                          scene->RecordBuild(cmd);
                      });
    }

    void SceneRayTracingProxy::AddTraceResourceReads(RDG::RenderGraphBuilder &builder) const
    {
        std::unordered_set<GPUBuffer *> read_buffers;
        std::unordered_set<IndexBuffer *> read_indices;
        std::unordered_set<RayTracingGeometry *> read_geometries;
        for (const auto &instance_key : FrontSlot()._instance_keys)
        {
            if (instance_key._mesh == nullptr || instance_key._vertex_buffer == nullptr)
                continue;

            if (instance_key._dynamic)
            {
                if (const auto *cached_geometry = FindCachedGeometry(instance_key._mesh, instance_key._vertex_buffer);
                    cached_geometry != nullptr)
                {
                    for (const auto &geometry : cached_geometry->_geometries)
                    {
                        if (geometry != nullptr && read_geometries.emplace(geometry.get()).second)
                            builder.Read(builder.Import(geometry.get()), EResourceUsage::kAccelStructRead);
                    }
                }
            }

            const auto &layout = instance_key._mesh->GetVertexBuffer()->GetLayout();
            for (const auto &element : layout.GetBufferDesc())
            {
                if (element._semantic != EVertexSemantic::kPosition &&
                    element._semantic != EVertexSemantic::kNormal &&
                    element._semantic != EVertexSemantic::kTangent &&
                    element._semantic != EVertexSemantic::kTexcoord0)
                    continue;
                auto *stream = instance_key._vertex_buffer->GetGpuStream(element.Stream);
                if (stream != nullptr && read_buffers.emplace(stream).second)
                    builder.Read(builder.Import(stream), EResourceUsage::kReadSRV);
            }

            for (u32 submesh_index = 0u; submesh_index < instance_key._mesh->SubmeshCount(); ++submesh_index)
            {
                auto *index_buffer = instance_key._mesh->GetIndexBuffer(static_cast<u16>(submesh_index)).get();
                if (index_buffer != nullptr && read_indices.emplace(index_buffer).second)
                    builder.Read(builder.Import(index_buffer), EResourceUsage::kReadSRV);
            }
        }
    }

    bool SceneRayTracingProxy::RebuildPackedBuffers(ProxyBufferSlot &slot, const Vector<SceneInstanceKey> &instance_keys)
    {
        if (instance_keys.empty())
            return false;

        Vector<Vector3f> merged_vertices;
        Vector<Vector3f> merged_normals;
        Vector<Vector2f> merged_uvs;
        Vector<Vector3UInt> merged_indices;
        Vector<Vector4f> merged_tangents;
        slot._mesh_ranges.clear();
        for (u64 instance_index = 0u; instance_index < instance_keys.size(); ++instance_index)
        {
            auto *mesh = instance_keys[instance_index]._mesh;
            auto range_it = slot._mesh_ranges.find(mesh);
            if (range_it == slot._mesh_ranges.end())
            {
                Vector<GeometryRange> ranges;
                ranges.reserve(mesh->SubmeshCount());
                const auto vertices = mesh->GetVertices();
                const auto normals = mesh->GetNormals();
                const u32 vertex_offset = static_cast<u32>(merged_vertices.size());
                merged_vertices.insert(merged_vertices.end(), vertices.begin(), vertices.end());
                if (normals.size() == vertices.size())
                {
                    merged_normals.insert(merged_normals.end(), normals.begin(), normals.end());
                }
                else
                {
                    merged_normals.insert(merged_normals.end(), vertices.size(), Vector3f(0.0f, 1.0f, 0.0f));
                }

                const auto uvs = mesh->GetUVs();
                if (uvs.size() == vertices.size())
                {
                    merged_uvs.insert(merged_uvs.end(), uvs.begin(), uvs.end());
                }
                else
                {
                    merged_uvs.insert(merged_uvs.end(), vertices.size(), Vector2f(0.0f, 0.0f));
                }

                const auto tangents = mesh->GetTangents();
                if (tangents.size() == vertices.size())
                {
                    merged_tangents.insert(merged_tangents.end(), tangents.begin(), tangents.end());
                }
                else
                {
                    merged_tangents.insert(merged_tangents.end(), vertices.size(), Vector4f(0.0f, 0.0f, 1.0f, 1.0f));
                }

                for (u32 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
                {
                    GeometryRange range{};
                    range._vertex_offset = vertex_offset;
                    range._triangle_offset = static_cast<u32>(merged_indices.size());
                    const auto indices = mesh->GetIndices(static_cast<u16>(submesh_index));
                    const u32 triangle_count = static_cast<u32>(indices.size() / 3u);
                    for (u32 triangle_index = 0u; triangle_index < triangle_count; ++triangle_index)
                    {
                        const u32 base_index = triangle_index * 3u;
                        merged_indices.emplace_back(indices[base_index] + vertex_offset, indices[base_index + 1u] + vertex_offset, indices[base_index + 2u] + vertex_offset);
                    }
                    ranges.push_back(range);
                }
                range_it = slot._mesh_ranges.insert_or_assign(mesh, ranges).first;
            }
        }

        u32 rendering_instance_index = 0u;
        for (u64 instance_index = 0u; instance_index < instance_keys.size(); ++instance_index)
        {
            for (u32 submesh_index = 0u; submesh_index < instance_keys[instance_index]._mesh->SubmeshCount(); ++submesh_index)
            {
                if (rendering_instance_index >= slot._instances.size())
                {
                    LOG_WARNING("SceneRayTracingProxy: instance count does not match packed geometry count, aborting buffer rebuild.");
                    return false;
                }
                const u32 primitive_index = slot._instances[rendering_instance_index]._primitive_index;
                if (primitive_index >= _scene_primitive_data->size())
                {
                    LOG_WARNING("SceneRayTracingProxy: scene primitive index is out of range while rebuilding packed buffers.");
                    return false;
                }
                const auto &range = slot._mesh_ranges[instance_keys[instance_index]._mesh][submesh_index];
                auto *mesh = instance_keys[instance_index]._mesh;
                const i32 position_bindless_idx = mesh ? mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kPosition,
                                                                                          instance_keys[instance_index]._vertex_buffer) : -1;
                const i32 normal_bindless_idx = mesh ? mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kNormal,
                                                                                         instance_keys[instance_index]._vertex_buffer) : -1;
                const i32 uv_bindless_idx = mesh ? mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kTexcoord0,
                                                                                      instance_keys[instance_index]._vertex_buffer) : -1;
                const i32 tangent_bindless_idx = mesh ? mesh->GetBindlessVertexStreamIndex(EVertexSemantic::kTangent,
                                                                                             instance_keys[instance_index]._vertex_buffer) : -1;
                auto index_buffer = mesh ? mesh->GetIndexBuffer(static_cast<u16>(submesh_index)).get() : nullptr;
                auto &primitive_data = (*_scene_primitive_data)[primitive_index];
                primitive_data._global_triangle_offset = range._triangle_offset;
                primitive_data._position_bindless_idx = position_bindless_idx >= 0 ? static_cast<u32>(position_bindless_idx) : RenderConstants::kInvalidBindlessHandle;
                primitive_data._normal_bindless_idx = normal_bindless_idx >= 0 ? static_cast<u32>(normal_bindless_idx) : RenderConstants::kInvalidBindlessHandle;
                primitive_data._uv_bindless_idx = uv_bindless_idx >= 0 ? static_cast<u32>(uv_bindless_idx) : RenderConstants::kInvalidBindlessHandle;
                primitive_data._tangent_bindless_idx = tangent_bindless_idx >= 0 ? static_cast<u32>(tangent_bindless_idx) : RenderConstants::kInvalidBindlessHandle;
                primitive_data._index_bindless_idx = index_buffer ? static_cast<u32>(index_buffer->GetBindlessSRVIndex()) : RenderConstants::kInvalidBindlessHandle;
                primitive_data._submesh_triangle_offset = 0u;
                primitive_data._submesh_triangle_count = mesh ? mesh->GetTriangleCount(static_cast<u16>(submesh_index)) : 0u;
                ++rendering_instance_index;
            }
        }

        if (rendering_instance_index != slot._instances.size())
        {
            LOG_WARNING("SceneRayTracingProxy: packed geometry count does not match instance count, aborting buffer rebuild.");
            return false;
        }

        slot._vertex_data = CreateStructuredBuffer(merged_vertices, "SceneRayTracingProxy_VertexData");
        slot._normal_data = CreateStructuredBuffer(merged_normals, "SceneRayTracingProxy_NormalData");
        slot._uv_data = CreateStructuredBuffer(merged_uvs, "SceneRayTracingProxy_UVData");
        slot._tangent_data = CreateStructuredBuffer(merged_tangents, "SceneRayTracingProxy_TangentData");
        slot._indices_data = CreateStructuredBuffer(merged_indices, "SceneRayTracingProxy_IndexData");
        _scene_primitive_buffer->SetData(*_scene_primitive_data);
        return slot._vertex_data != nullptr &&
               slot._normal_data != nullptr &&
               slot._uv_data != nullptr &&
               slot._indices_data != nullptr;
    }

    bool SceneRayTracingProxy::IsSlotReady(ProxyBufferSlot &slot)
    {
        if (slot._scene == nullptr || slot._scene->InstanceCount() == 0u)
            return false;

        if (!slot._scene->IsReady())
            return false;

        return slot._vertex_data != nullptr && slot._vertex_data->IsReady() &&
               slot._normal_data != nullptr && slot._normal_data->IsReady() &&
               slot._indices_data != nullptr && slot._indices_data->IsReady();
    }

    void SceneRayTracingProxy::TrySwapBuffers()
    {
        if (!_has_pending_swap)
            return;

        auto &back_slot = BackSlot();
        if (!IsSlotReady(back_slot))
            return;

        const u32 previous_front_slot = _front_slot;
        _front_slot = 1u - _front_slot;
        _has_pending_swap = false;
        ClearSlot(_slots[previous_front_slot]);
    }

    void SceneRayTracingProxy::ClearSlot(ProxyBufferSlot &slot)
    {
        slot._mesh_ranges.clear();
        slot._instance_keys.clear();
        slot._instances.clear();
        slot._scene = nullptr;
        slot._vertex_data = nullptr;
        slot._normal_data = nullptr;
        slot._uv_data = nullptr;
        slot._tangent_data = nullptr;
        slot._indices_data = nullptr;
    }

    void SceneRayTracingProxy::UpdateUnifiedLights(const SceneManagement::Scene &scene)
    {
        _unified_light_data_cache.clear();
        _unified_light_config_cpu = {};

        for (const auto &light_comp : scene.GetRegister().View<ECS::LightComponent>())
        {
            if (light_comp._light._light_color.a <= 0.0f || light_comp._light._light_color.xyz == Vector3f::kZero)
                continue;
            const UnifiedLightData light = BuildUnifiedLight(light_comp);
            _unified_light_data_cache.push_back(light);
            ++_unified_light_config_cpu._light_count;
            if (light._type != AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL)
                ++_unified_light_config_cpu._finite_light_count;
            if (light._type == AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA)
                ++_unified_light_config_cpu._triangle_light_count;
        }

        u32 scene_triangle_offset = 0u;
        u32 instance_index_offset = 0u;
        HashMap<u64, u32> primitive_indices;
        if (_scene_primitives != nullptr)
        {
            for (const auto &primitive : *_scene_primitives)
                primitive_indices[(static_cast<u64>(primitive._entity_id) << 32u) | primitive._submesh_index] =
                    primitive._primitive_index;
        }
        auto append_mesh_lights = [this, &scene, &primitive_indices, &scene_triangle_offset,
                                   &instance_index_offset](auto &mesh_components)
        {
            using ComponentType = std::remove_cvref_t<decltype(*mesh_components.begin())>;
            u64 entity_index = 0u;
            for (const auto &mesh_comp : mesh_components)
            {
                const auto entity = scene.GetRegister().GetEntity<ComponentType>(entity_index++);
                auto lights = BuildUnifiedLight(mesh_comp, scene_triangle_offset, instance_index_offset);
                for (auto &light : lights)
                {
                    if (_scene_primitives != nullptr)
                    {
                        const u32 submesh_index = static_cast<u32>(light._shadow_index) - instance_index_offset;
                        const auto key = (static_cast<u64>(static_cast<u32>(entity)) << 32u) | submesh_index;
                        const auto it = primitive_indices.find(key);
                        if (it == primitive_indices.end())
                            continue;
                        light._shadow_index = static_cast<i32>(it->second);
                    }
                    _unified_light_data_cache.push_back(light);
                    ++_unified_light_config_cpu._light_count;
                    ++_unified_light_config_cpu._finite_light_count;
                    ++_unified_light_config_cpu._triangle_light_count;
                }
                if (mesh_comp._p_mesh)
                {
                    scene_triangle_offset += mesh_comp._p_mesh->GetTriangleCount();
                    instance_index_offset += mesh_comp._p_mesh->SubmeshCount();
                }
            }
        };
        append_mesh_lights(scene.GetRegister().View<ECS::StaticMeshComponent>());
        append_mesh_lights(scene.GetRegister().View<ECS::CSkeletonMesh>());

        // Header: primitive count, direction count, analytic count, primitive-offset table start.
        // Lists contain original light indices, preserving alias sampling and all external light references.
        const u32 primitive_count = _scene_primitive_data != nullptr ? static_cast<u32>(_scene_primitive_data->size()) : 0u;
        _path_light_lookup_cache.assign(4u, 0u);
        _path_light_lookup_cache[0] = primitive_count;
        for (u32 light_index = 0u; light_index < _unified_light_data_cache.size(); ++light_index)
        {
            if (_unified_light_data_cache[light_index]._type == AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL)
            {
                _path_light_lookup_cache.push_back(light_index);
                ++_path_light_lookup_cache[1];
            }
        }
        for (u32 light_index = 0u; light_index < _unified_light_data_cache.size(); ++light_index)
        {
            const auto type = _unified_light_data_cache[light_index]._type;
            if (type != AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL && type != AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA)
            {
                _path_light_lookup_cache.push_back(light_index);
                ++_path_light_lookup_cache[2];
            }
        }
        const u32 offset_table_start = static_cast<u32>(_path_light_lookup_cache.size());
        _path_light_lookup_cache[3] = offset_table_start;
        _path_light_lookup_cache.resize(offset_table_start + primitive_count, RenderConstants::kInvalidBindlessHandle);
        for (u32 light_index = 0u; light_index < _unified_light_data_cache.size(); ++light_index)
        {
            const auto &light = _unified_light_data_cache[light_index];
            const u32 primitive_index = static_cast<u32>(light._shadow_index);
            if (light._type != AL_UNIFIED_LIGHT_TYPE_TRIANGLE_AREA || primitive_index >= primitive_count)
                continue;
            const u32 triangle_count = (*_scene_primitive_data)[primitive_index]._submesh_triangle_count;
            if (light._tri_index >= triangle_count)
                continue;
            u32 triangle_start = _path_light_lookup_cache[offset_table_start + primitive_index];
            if (triangle_start == RenderConstants::kInvalidBindlessHandle)
            {
                triangle_start = static_cast<u32>(_path_light_lookup_cache.size());
                _path_light_lookup_cache[offset_table_start + primitive_index] = triangle_start;
                _path_light_lookup_cache.resize(triangle_start + triangle_count,
                                               RenderConstants::kInvalidBindlessHandle);
            }
            _path_light_lookup_cache[triangle_start + light._tri_index] = light_index;
        }
        const u64 lookup_size = _path_light_lookup_cache.capacity() * sizeof(u32);
        if (_path_light_lookup_buffer == nullptr || _path_light_lookup_buffer->GetSize() < lookup_size)
            _path_light_lookup_buffer =
                CreateDynamicStructuredBuffer(_path_light_lookup_cache, "SceneRayTracingProxy_PathLightLookup");
        else
            _path_light_lookup_buffer->SetData(_path_light_lookup_cache);

        UpdateUnifiedLightSamplingTable();

        const u64 required_size = std::max<u64>(1u, _unified_light_data_cache.capacity()) * sizeof(UnifiedLightData);
        if (_unified_light_buffer == nullptr || _unified_light_buffer->GetSize() < required_size)
            _unified_light_buffer = CreateDynamicStructuredBuffer(_unified_light_data_cache, "SceneRayTracingProxy_UnifiedLightBuffer");
        else if (!_unified_light_data_cache.empty())
            _unified_light_buffer->SetData(reinterpret_cast<const u8 *>(_unified_light_data_cache.data()), static_cast<u32>(_unified_light_data_cache.size() * sizeof(UnifiedLightData)));

        _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
    }

    float SceneRayTracingProxy::EstimateUnifiedLightSamplingWeight(const UnifiedLightData &light) const
    {
        constexpr double kMinWeight = 1.0e-6;
        const bool radiance_is_finite = std::isfinite(light._radiance.x) && std::isfinite(light._radiance.y) && std::isfinite(light._radiance.z);
        const double luminance = radiance_is_finite ? std::max(0.0, static_cast<double>(light._radiance.x) * 0.2126 +
                                                                    static_cast<double>(light._radiance.y) * 0.7152 +
                                                                    static_cast<double>(light._radiance.z) * 0.0722) : 0.0;
        if (!std::isfinite(luminance) || luminance <= 0.0)
            return 0.0f;

        double shape_factor = 1.0;
        if (light._type == AL_UNIFIED_LIGHT_TYPE_RECT_AREA)
        {
            const double cx = static_cast<double>(light._shape_u.y) * light._shape_v.z - static_cast<double>(light._shape_u.z) * light._shape_v.y;
            const double cy = static_cast<double>(light._shape_u.z) * light._shape_v.x - static_cast<double>(light._shape_u.x) * light._shape_v.z;
            const double cz = static_cast<double>(light._shape_u.x) * light._shape_v.y - static_cast<double>(light._shape_u.y) * light._shape_v.x;
            shape_factor = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (!std::isfinite(shape_factor))
                shape_factor = 0.0;
            shape_factor = std::max(kMinWeight, shape_factor);
        }

        const double weight = std::max(kMinWeight, luminance * shape_factor);
        return static_cast<float>(std::min(weight, static_cast<double>(std::numeric_limits<float>::max())));
    }

    void SceneRayTracingProxy::BuildUnifiedLightAliasTable(const Vector<float> &weights,
                                                           Vector<UnifiedLightSamplingData> &sampling_table) const
    {
        const u32 light_count = static_cast<u32>(weights.size());
        sampling_table.clear();
        sampling_table.resize(light_count);
        if (light_count == 0u)
            return;

        double total_weight = 0.0;
        for (float weight : weights)
        {
            if (std::isfinite(weight) && weight > 0.0f)
                total_weight += static_cast<double>(weight);
        }

        if (!std::isfinite(total_weight) || total_weight <= 1.0e-12)
        {
            const float pmf = 1.0f / static_cast<float>(light_count);
            for (u32 index = 0u; index < light_count; ++index)
                sampling_table[index] = {pmf, 1.0f, index, 0u};
            return;
        }

        Vector<double> scaled(light_count);
        Vector<u32> small_indices;
        Vector<u32> large_indices;
        small_indices.reserve(light_count);
        large_indices.reserve(light_count);

        for (u32 index = 0u; index < light_count; ++index)
        {
            const float input_weight = weights[index];
            const double weight = std::isfinite(input_weight) && input_weight > 0.0f ? static_cast<double>(input_weight) : 0.0;
            const double pmf = weight / total_weight;
            sampling_table[index] = {static_cast<float>(pmf), 1.0f, index, 0u};
            scaled[index] = pmf * static_cast<double>(light_count);
            if (scaled[index] < 1.0)
                small_indices.push_back(index);
            else
                large_indices.push_back(index);
        }

        while (!small_indices.empty() && !large_indices.empty())
        {
            const u32 small_index = small_indices.back();
            small_indices.pop_back();
            const u32 large_index = large_indices.back();
            large_indices.pop_back();

            sampling_table[small_index]._alias_probability = static_cast<float>(scaled[small_index]);
            sampling_table[small_index]._alias_index = large_index;
            scaled[large_index] += scaled[small_index] - 1.0;
            if (scaled[large_index] < 1.0)
                small_indices.push_back(large_index);
            else
                large_indices.push_back(large_index);
        }

        for (u32 index : large_indices)
        {
            sampling_table[index]._alias_probability = 1.0f;
            sampling_table[index]._alias_index = index;
        }
        for (u32 index : small_indices)
        {
            sampling_table[index]._alias_probability = 1.0f;
            sampling_table[index]._alias_index = index;
        }
    }

    void SceneRayTracingProxy::UpdateUnifiedLightSamplingTable()
    {
        Vector<float> weights;
        weights.reserve(_unified_light_data_cache.size());
        for (const UnifiedLightData &light : _unified_light_data_cache)
            weights.push_back(EstimateUnifiedLightSamplingWeight(light));

        BuildUnifiedLightAliasTable(weights, _unified_light_sampling_cache);
        _unified_light_config_cpu._sampling_table_count = static_cast<u32>(_unified_light_sampling_cache.size());

        const u64 required_size = std::max<u64>(1u, _unified_light_sampling_cache.capacity()) * sizeof(UnifiedLightSamplingData);
        if (_unified_light_sampling_buffer == nullptr || _unified_light_sampling_buffer->GetSize() < required_size)
            _unified_light_sampling_buffer = CreateDynamicStructuredBuffer(_unified_light_sampling_cache, "SceneRayTracingProxy_UnifiedLightSamplingBuffer");
        else if (!_unified_light_sampling_cache.empty())
            _unified_light_sampling_buffer->SetData(reinterpret_cast<const u8 *>(_unified_light_sampling_cache.data()),
                                                     static_cast<u32>(_unified_light_sampling_cache.size() * sizeof(UnifiedLightSamplingData)));

        AL_ASSERT(_unified_light_sampling_cache.size() == _unified_light_data_cache.size());
        double pmf_sum = 0.0;
        const u32 light_count = static_cast<u32>(_unified_light_sampling_cache.size());
        for (u32 index = 0u; index < light_count; ++index)
        {
            const UnifiedLightSamplingData &entry = _unified_light_sampling_cache[index];
            AL_ASSERT(std::isfinite(entry._pmf));
            AL_ASSERT(entry._pmf >= 0.0f);
            AL_ASSERT(std::isfinite(entry._alias_probability));
            AL_ASSERT(entry._alias_probability >= 0.0f && entry._alias_probability <= 1.0f);
            AL_ASSERT(entry._alias_index < light_count);
            pmf_sum += entry._pmf;
        }
        if (light_count > 0u)
            AL_ASSERT(std::abs(pmf_sum - 1.0) < 1.0e-4);
    }

}
