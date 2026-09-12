#include "Render/RayTracing/SceneRayTracingProxy.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Math/MathHash.hpp"
#include "Animation/SkinningSystem.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Render/Texture.h"
#include "pch.h"

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
            desc._target = EGPUBufferTarget::kStructured | EGPUBufferTarget::kConstant;
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
            return color.xyz;
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
        _material_data_cache.reserve(200u);
        _material_buffer = CreateDynamicStructuredBuffer(_material_data_cache, "SceneRayTracingProxy_MaterialBuffer");
        _unified_light_data_cache.reserve(32u);
        _unified_light_buffer = CreateDynamicStructuredBuffer(_unified_light_data_cache, "SceneRayTracingProxy_UnifiedLightBuffer");
        _unified_light_config = ConstantBuffer::Create(sizeof(UnifiedLightBufferConfig), "SceneRayTracingProxy_UnifiedLightConfig");
        _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
        _material_data_lut[0] = 0; //default material
        MaterialData miss_mat{};
        miss_mat._base_color = float3(1.0f, 0.0f, 1.0f);//洋红色
        miss_mat._base_color_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._normal_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._emission = float3(0.0f, 0.0f, 0.0f);
        miss_mat._emission_tex = RenderConstants::kInvalidBindlessHandle;
        miss_mat._metallic = 0.0f;
        miss_mat._roughness = 1.0f;
        miss_mat._metallic_roughness_tex = RenderConstants::kInvalidBindlessHandle;
        _material_data_cache.push_back(miss_mat);
    }


    void SceneRayTracingProxy::Sync(const SceneManagement::Scene *scene, Vector<PrimitiveData> *primitive_data,
                                    GPUBuffer *primitive_buffer, const Vector<ScenePrimitive> *scene_primitives)
    {
        PROFILE_BLOCK_CPU("SceneRayTracingProxy::Sync")
        _scene_primitive_data = primitive_data;
        _scene_primitive_buffer = primitive_buffer;
        _scene_primitives = scene_primitives;
        SyncLightCache(scene);
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

        UpdateMaterials(*scene);
        _transforms.clear();
        _materials.clear();
        auto instance_keys = CollectInstanceKeys(*scene, _transforms, _materials);
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

    void SceneRayTracingProxy::SyncLightCache(const SceneManagement::Scene *scene)
    {
        if (scene == nullptr)
        {
            _unified_light_config_cpu = {};
            _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
            return;
        }

        UpdateUnifiedLights(*scene);
    }

    Vector<SceneRayTracingProxy::SceneInstanceKey> SceneRayTracingProxy::CollectInstanceKeys(const SceneManagement::Scene &scene,
                                                                                             Vector<Matrix4x4f> &transforms,
                                                                                             Vector<Material*> &materials) const
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
                auto mat = renderable._p_mats.empty() ? nullptr : renderable._p_mats[0].get();
                if (mat != nullptr && mat->IsStandardLit() && mat->MaterialID() == EMaterialID::kChecker)
                    instance_key._material_class = 1u;
                instance_keys.push_back(instance_key);
                for (u32 submesh_index = 0u; submesh_index < mesh->SubmeshCount(); ++submesh_index)
                {
                    transforms.push_back(transform_comp->GetRenderWorldMatrix());
                    auto submesh_material = renderable._p_mats.size() > submesh_index
                        ? renderable._p_mats[submesh_index].get() : nullptr;
                    materials.push_back(submesh_material);
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
                slot._instance_keys[index]._vertex_buffer != instance_keys[index]._vertex_buffer)
            {
                return true;
            }
        }
        return false;
    }

    Vector<Ref<RayTracingGeometry>> SceneRayTracingProxy::AcquireGeometry(Mesh *mesh, VertexBuffer *vertex_buffer)
    {
        if (mesh == nullptr || vertex_buffer == nullptr)
            return {};

        if (auto it = _mesh_geometry_cache.find(mesh); it != _mesh_geometry_cache.end() &&
            it->second._vertex_buffer == vertex_buffer)
            return it->second._geometries;
        CachedGeometry cached_geometry;
        cached_geometry._vertex_buffer = vertex_buffer;
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
            cached_geometry._geometries.push_back(RayTracingGeometry::Create(desc, std::format("{}_rt_blas_{}", mesh->Name(), submesh_index)));
        }
        auto [it, inserted] = _mesh_geometry_cache.insert_or_assign(mesh, std::move(cached_geometry));
        return it->second._geometries;
    }

    void SceneRayTracingProxy::StartRebuild(const Vector<SceneInstanceKey> &instance_keys, const Vector<Matrix4x4f> &transforms)
    {
        auto &back_slot = BackSlot();
        ClearSlot(back_slot);
        _has_pending_swap = Rebuild(back_slot, instance_keys, transforms);
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

            auto geometries = AcquireGeometry(instance_keys[index]._mesh, instance_keys[index]._vertex_buffer);
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
        slot._scene->Build();
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
            slot._scene->Update();
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
                primitive_data._material_id = rendering_instance_index < _materials.size() && _materials[rendering_instance_index] != nullptr ?
                    _material_data_lut[_materials[rendering_instance_index]->HashCode()] : 0u;
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

    static void FillMaterialData(Material* src, MaterialData& dst)
    {
        dst._base_color_tex         = RenderConstants::kInvalidBindlessHandle;
        dst._normal_tex             = RenderConstants::kInvalidBindlessHandle;
        dst._metallic_roughness_tex = RenderConstants::kInvalidBindlessHandle;
        dst._emission_tex           = RenderConstants::kInvalidBindlessHandle;
        if (src != nullptr && src->IsStandardLit())
        {
            const auto *albedo_prop = src->MainProperty(ETextureUsage::kAlbedo);
            if (albedo_prop != nullptr)
                dst._base_color = albedo_prop->GetValue<Vector4f>().xyz;
            if (auto base_color_tex = src->MainTex(ETextureUsage::kAlbedo); base_color_tex)
            {
                dst._base_color_tex = base_color_tex->GetBindlessSRVIndex();
            }
            const auto *metallic_prop = src->MainProperty(ETextureUsage::kMetallic);
            if (metallic_prop != nullptr)
                dst._metallic = metallic_prop->GetValue<f32>();
            const auto *roughness_prop = src->MainProperty(ETextureUsage::kRoughness);
            if (roughness_prop != nullptr)
                dst._roughness = std::max(0.03f, roughness_prop->GetValue<f32>());
            if (auto normal_tex = src->MainTex(ETextureUsage::kNormal); normal_tex)
            {
                dst._normal_tex = normal_tex->GetBindlessSRVIndex();
            }
            if (auto mr_tex = src->MainTex(ETextureUsage::kRoughness); mr_tex)
            {
                dst._metallic_roughness_tex = mr_tex->GetBindlessSRVIndex();
            }
            const auto *emission_prop = src->MainProperty(ETextureUsage::kEmission);
            if (emission_prop != nullptr)
                dst._emission = emission_prop->GetValue<Color>().xyz;
            dst._ior = src->GetFloat("_IOR");
            dst._transmission = src->GetFloat("_Transmission");
        }
        //dst._base_color        = Vector3f::kOne;
        //dst._metallic          = src.metallic;
        //dst._roughness         = src.roughness;
        //dst._specular          = src.specular;
        //dst._ior               = src.ior;
        //dst._opacity           = src.opacity;

        // dst._emission          = src.emission_color;
        // dst._emission_strength = src.emission_intensity;

        // dst._base_color_tex    = src.base_color_tex ? src.base_color_tex->BindlessIndex() : kInvalidTex;
        // dst._normal_tex        = src.normal_tex     ? src.normal_tex->BindlessIndex()     : kInvalidTex;
        // dst._metallic_roughness_tex = src.mr_tex ? src.mr_tex->BindlessIndex() : kInvalidTex;
        // dst._emission_tex      = src.emission_tex ? src.emission_tex->BindlessIndex() : kInvalidTex;

        // dst._flags = 0;
        // if (src.is_metallic)   dst._flags |= kMaterial_Metallic;
        // if (src.is_emissive)   dst._flags |= kMaterial_Emissive;
        // if (src.is_thin)       dst._flags |= kMaterial_Thin;
    }

    void SceneRayTracingProxy::UpdateMaterials(const SceneManagement::Scene &s)
    {
        PROFILE_BLOCK_CPU("Renderer::PrepareMaterial")

        auto collect_materials = [this](auto &renderables)
        {
            for (auto &renderable : renderables)
            {
                for (auto &material : renderable._p_mats)
                {
                    if (material == nullptr)
                        continue;
                    if (!_material_data_lut.contains(material->HashCode()))
                    {
                        const u32 material_index = static_cast<u32>(_material_data_cache.size());
                        _material_data_lut[material->HashCode()] = material_index;
                        _material_data_cache.push_back(MaterialData());
                    }
                    FillMaterialData(material.get(), _material_data_cache[_material_data_lut[material->HashCode()]]);
                }
            }
        };
        collect_materials(s.GetRegister().View<ECS::StaticMeshComponent>());
        collect_materials(s.GetRegister().View<ECS::CSkeletonMesh>());
        _material_buffer->SetData(
            (u8*)_material_data_cache.data(),
            (u32)(_material_data_cache.size() * sizeof(MaterialData)));
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
        auto append_mesh_lights = [this, &scene_triangle_offset, &instance_index_offset](auto &mesh_components)
        {
            for (const auto &mesh_comp : mesh_components)
            {
                const auto lights = BuildUnifiedLight(mesh_comp, scene_triangle_offset, instance_index_offset);
                const auto light_num = static_cast<u32>(lights.size());
                _unified_light_data_cache.insert(_unified_light_data_cache.end(), lights.begin(), lights.end());
                _unified_light_config_cpu._light_count += light_num;
                _unified_light_config_cpu._finite_light_count += light_num;
                _unified_light_config_cpu._triangle_light_count += light_num;
                if (mesh_comp._p_mesh)
                {
                    scene_triangle_offset += mesh_comp._p_mesh->GetTriangleCount();
                    instance_index_offset += mesh_comp._p_mesh->SubmeshCount();
                }
            }
        };
        append_mesh_lights(scene.GetRegister().View<ECS::StaticMeshComponent>());
        append_mesh_lights(scene.GetRegister().View<ECS::CSkeletonMesh>());

        const u64 required_size = std::max<u64>(1u, _unified_light_data_cache.capacity()) * sizeof(UnifiedLightData);
        if (_unified_light_buffer == nullptr || _unified_light_buffer->GetSize() < required_size)
            _unified_light_buffer = CreateDynamicStructuredBuffer(_unified_light_data_cache, "SceneRayTracingProxy_UnifiedLightBuffer");
        else if (!_unified_light_data_cache.empty())
            _unified_light_buffer->SetData(reinterpret_cast<const u8 *>(_unified_light_data_cache.data()), static_cast<u32>(_unified_light_data_cache.size() * sizeof(UnifiedLightData)));

        _unified_light_config->SetData(reinterpret_cast<const u8 *>(&_unified_light_config_cpu), sizeof(_unified_light_config_cpu));
    }

}
