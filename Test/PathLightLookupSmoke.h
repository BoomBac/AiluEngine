#pragma once

#include "Render/RayTracing/SceneRayTracingProxy.h"

// Exercise real light extraction with shared meshes, sparse emissive triangles and non-ECS primitive ordering.
static bool RunPathLightLookupSmoke(const WString &output)
{
    auto scene = MakeRef<Scene>("PathLightLookupTemporary");
    auto mesh = MakeRef<Mesh>("SparseEmissiveTriangles");
    mesh->SetVertices({{-1.f, -1.f, 0.f}, {-1.f, 1.f, 0.f}, {0.f, 1.f, 0.f},
                       {0.f, -1.f, 0.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 0.f}});
    mesh->SetNormals(Vector<Vector3f>(6u, Vector3f(0.f, 0.f, -1.f)));
    mesh->SetTangents(Vector<Vector4f>(6u, Vector4f(1.f, 0.f, 0.f, 1.f)));
    mesh->SetUVs({{.0f, .5f}, {.0f, .5f}, {.0f, .5f}, {1.f, .5f}, {1.f, .5f}, {1.f, .5f}});
    mesh->SetVerticesCount(6u);
    mesh->AddSubmesh({0u, 1u, 2u, 3u, 4u, 5u});
    mesh->SetBounds(Vector<AABB>(2u, AABB(Vector3f(-1.f, -1.f, -.01f), Vector3f(1.f, 1.f, .01f))));
    mesh->Apply();
    auto material = Material::CreateStandard("SparseEmission");
    material->SetVector("_EmissionValue", Vector4f(.8f, .4f, .2f, 1.f));
    auto texture = Texture2D::Create(2u, 1u, ETextureFormat::kRGBA32);
    Vector<u8> pixels{0u, 0u, 0u, 255u, 255u, 255u, 255u, 255u};
    texture->SetPixelData(pixels.data(), 0u);
    material->SetTexture(ETextureUsage::kEmission, texture.get());
    const auto first = scene->AddObject(mesh, material);
    const auto second = scene->AddObject(mesh, material);
    scene->AddObject(mesh, material); // Absent from the renderer primitive list, as for a disabled renderable.
    const auto sun_entity = scene->AddObject("LookupSun");
    auto &sun = scene->GetRegister().AddComponent<ECS::LightComponent>(sun_entity);
    sun._type = ECS::ELightType::kDirectional;
    sun._light._light_color = Color(1.f, 1.f, 1.f, 1.f);
    const auto point_entity = scene->AddObject("LookupPoint");
    auto &point = scene->GetRegister().AddComponent<ECS::LightComponent>(point_entity);
    point._type = ECS::ELightType::kPoint;
    point._light._light_color = Color(1.f, 1.f, 1.f, 1.f);
    Vector<PrimitiveData> primitive_data(6u);
    primitive_data[5]._submesh_triangle_count = primitive_data[2]._submesh_triangle_count = 2u;
    Vector<ScenePrimitive> primitives(2u);
    primitives[0]._primitive_index = 5u;
    primitives[0]._entity_id = static_cast<u32>(first);
    primitives[0]._mesh = mesh.get();
    primitives[1]._primitive_index = 2u;
    primitives[1]._entity_id = static_cast<u32>(second);
    primitives[1]._mesh = mesh.get();
    SceneRayTracingProxy proxy;
    proxy.SyncLightCache(scene.get(), &primitive_data, &primitives);
    // Ensure the next upload has a current CPU-visible version even if creation consumed initial data.
    proxy.SyncLightCache(scene.get(), &primitive_data, &primitives);
    bool passed = true;
    u32 checks = 0u;
    const auto check = [&](bool value)
    {
        passed &= value;
        ++checks;
        if (!value) std::cerr << "PathLightLookup failed check " << checks << '\n';
    };
    const auto &lights = proxy.GetUnifiedLights();
    check(lights.size() == 4u);
    const auto version = proxy.GetPathLightLookup()->CurrentDataVersion();
    check(version._upload._cpu_ptr != nullptr && version._size >= 10u * sizeof(u32));
    if (version._upload._cpu_ptr != nullptr)
    {
        const auto *lookup = static_cast<const u32 *>(version._upload._cpu_ptr);
        const u32 word_count = version._size / sizeof(u32);
        check(lookup[0] == 6u && lookup[1] == 1u && lookup[2] == 1u && lookup[3] == 6u);
        check(lookup[4] < lights.size() && lights[lookup[4]]._type == AL_UNIFIED_LIGHT_TYPE_DIRECTIONAL);
        check(lookup[5] < lights.size() && lights[lookup[5]]._type == AL_UNIFIED_LIGHT_TYPE_POINT);
        for (u32 primitive_index : {2u, 5u})
        {
            const u32 start = lookup[lookup[3] + primitive_index];
            check(start < word_count && word_count - start >= 2u);
            if (start < word_count && word_count - start >= 2u)
            {
                check(lookup[start] == RenderConstants::kInvalidBindlessHandle);
                const u32 light_index = lookup[start + 1u];
                check(light_index < lights.size());
                if (light_index < lights.size())
                    check(lights[light_index]._shadow_index == static_cast<i32>(primitive_index) &&
                          lights[light_index]._tri_index == 1u);
            }
        }
        check(lookup[lookup[3]] == RenderConstants::kInvalidBindlessHandle);
    }
    // Material change and scene clear must invalidate the old mapping without changing the header contract.
    material->SetVector("_EmissionValue", Vector4f(0.f, 0.f, 0.f, 1.f));
    proxy.SyncLightCache(scene.get(), &primitive_data, &primitives);
    check(proxy.GetUnifiedLightCount() == 2u);
    const auto *updated = static_cast<const u32 *>(proxy.GetPathLightLookup()->CurrentDataVersion()._upload._cpu_ptr);
    check(updated && updated[updated[3] + 2u] == RenderConstants::kInvalidBindlessHandle &&
          updated[updated[3] + 5u] == RenderConstants::kInvalidBindlessHandle);
    proxy.SyncLightCache(nullptr);
    check(proxy.GetUnifiedLightCount() == 0u);
    const auto *empty = static_cast<const u32 *>(proxy.GetPathLightLookup()->CurrentDataVersion()._upload._cpu_ptr);
    check(empty && empty[0] == 0u && empty[1] == 0u && empty[2] == 0u);
    std::ofstream report(std::filesystem::path(output) / "path_light_lookup.txt");
    report << "checks=" << checks << "\npassed=" << passed << '\n';
    std::cout << "PathLightLookup checks=" << checks << " passed=" << passed << '\n';
    return passed;
}
