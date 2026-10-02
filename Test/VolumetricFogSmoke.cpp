#include "Render/Features/FroxelBounds.h"
#include "Framework/Math/Projection.h"
#include "Assets/AssetDocument.h"
#include "Objects/JsonArchive.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <DirectXTex.h>
#include "Render/ImageCapture/DdsCaptureEncoder.h"
#include "Scene/Scene.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/EntitySerializer.h"
#include "Scene/PrefabSystem.h"
#include "Assets/PrefabAsset.h"
#include "Framework/Common/Log.h"

using namespace Ailu;
using namespace Ailu::Math;
using namespace Ailu::Render;

static bool CheckBounds()
{
    const Vector3UInt dimensions(96u, 54u, 64u);
    constexpr f32 kNear = .1f, kFar = 50.0f;
    Matrix4x4f projection;
    BuildPerspectiveFovLHMatrix(projection, 1.04719755f, 16.0f / 9.0f, kNear, kFar);
    const auto inverse_projection = MatrixInverse(projection);
    const Vector4f lights[] = {{0, 0, 6, 2}, {6, 0, 6, 2}, {0, 0, -5, 1}, {0, 0, .15f, .2f},
        {0, 0, 0, 6}, {0, 0, 20, 100}, {0, 0, 6, .05f}, {0, 0, 51, 2}, {100, 0, 5, 1}};
    for (const auto &light : lights)
    {
        const auto bounds = CalculateFroxelBounds(light.xyz, light.w, Matrix4x4f::Identity(), projection,
            kNear, kFar, dimensions);
        for (u32 z = 0u; z < dimensions.z; ++z)
        {
            const f32 depth = kNear * std::pow(kFar / kNear, (f32(z) + .5f) / dimensions.z);
            const f32 thickness = kNear * (std::pow(kFar / kNear, f32(z + 1u) / dimensions.z) -
                std::pow(kFar / kNear, f32(z) / dimensions.z));
            for (u32 y = 0u; y < dimensions.y; ++y)
                for (u32 x = 0u; x < dimensions.x; ++x)
                {
                    auto ray = TransformVector(inverse_projection, Vector4f(
                        (f32(x) + .5f) / dimensions.x * 2 - 1, (f32(y) + .5f) / dimensions.y * 2 - 1, 1, 1));
                    ray /= ray.w;
                    const Vector3f direction = Normalize(Vector3f(ray.xyz));
                    for (f32 jitter : {-.4f, 0.0f, .4f})
                    {
                        const auto sample = direction * (depth / direction.z + jitter * thickness);
                        const auto delta = sample - light.xyz;
                        if (DotProduct(delta, delta) >= light.w * light.w)
                            continue;
                        if (x < bounds._min.x || x >= bounds._max.x || y < bounds._min.y || y >= bounds._max.y ||
                            z < bounds._min.z || z >= bounds._max.z)
                        {
                            std::cerr << "Bounds missed an illuminated jittered froxel: " << x << ',' << y << ',' << z;
                            return false;
                        }
                    }
                }
        }
    }
    return true;
}

static bool CheckNativeVolumeCapture()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("VolumetricFogSmoke_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dds");
    ImageCaptureCpuImage image;
    image._format = EALGFormat::kALGFormatR32_FLOAT;
    image._dimension = ETextureDimension::kTex3D;
    image._width = image._height = 2u;
    image._depth = 3u;
    const float pixels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    ImageCaptureCpuSubresource subresource;
    subresource._data = reinterpret_cast<const u8 *>(pixels);
    subresource._data_size = sizeof(pixels);
    subresource._row_pitch = 8u;
    subresource._slice_pitch = 16u;
    subresource._width = subresource._height = 2u;
    subresource._depth = 3u;
    image._sub_resources.push_back(subresource);
    DdsCaptureEncoder encoder;
    String error;
    const bool encoded = encoder.Encode(image, path.wstring(), error);
    DirectX::TexMetadata metadata;
    DirectX::ScratchImage decoded;
    const auto result = encoded ? DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, decoded)
                               : E_FAIL;
    std::filesystem::remove(path);
    if (FAILED(result) || metadata.depth != 3u || decoded.GetImageCount() != 3u)
        return false;
    for (u32 z = 0u; z < 3u; ++z)
    {
        const auto *slice = decoded.GetImage(0u, 0u, z);
        if (slice == nullptr || std::memcmp(slice->pixels, pixels + z * 4u, 16u) != 0)
            return false;
    }
    image._sub_resources[0]._data_size = 16u;
    return !encoder.Encode(image, path.wstring(), error);
}

static bool CheckEntityClone()
{
    ResourceMgr resource_manager;
    SceneManagement::Scene scene("LocalFogSmoke", false);
    const auto source = scene.AddObject("Fog");
    auto &registry = scene.GetRegister();
    auto &volume = registry.AddComponent<ECS::LocalFogVolumeComponent>(source);
    volume._shape = ECS::ELocalFogVolumeShape::kSphere;
    volume._density = .375f;
    registry.SetComponentEnabled<ECS::LocalFogVolumeComponent>(source, false);
    const auto copy = SceneManagement::EntitySerializer::CloneEntity(scene, source);
    const auto *copied_volume = registry.GetComponent<ECS::LocalFogVolumeComponent>(copy);
    const auto document = SceneManagement::EntitySerializer::BuildEntityDocument(scene, copy);
    const bool cloned = copied_volume != nullptr && copied_volume->_shape == ECS::ELocalFogVolumeShape::kSphere &&
        copied_volume->_density == .375f && !registry.IsComponentEnabled<ECS::LocalFogVolumeComponent>(copy) &&
        document._has_local_fog_volume_component && document._local_fog_volume_component._density == .375f &&
        std::find(document._disabled_components.begin(), document._disabled_components.end(),
            "LocalFogVolumeComponent") != document._disabled_components.end();
    if (!cloned)
        return false;
    using SceneManagement::PrefabSystem;
    const auto prefab = PrefabSystem::CreatePrefabDocument(scene, source);
    if (!prefab || prefab->_entities.empty())
        return false;
    prefab->_header._guid = Guid::Generate().ToString();
    const auto instance = PrefabSystem::Instantiate(scene, *prefab);
    auto *instance_volume = registry.GetComponent<ECS::LocalFogVolumeComponent>(instance._root);
    if (instance_volume == nullptr || instance_volume->_density != .375f ||
        registry.IsComponentEnabled<ECS::LocalFogVolumeComponent>(instance._root))
        return false;
    instance_volume->_density = .875f;
    const auto component_name = ECS::LocalFogVolumeComponent::StaticTypeName();
    if (!PrefabSystem::RecordPropertyOverride(scene, instance._root, component_name, "_density"))
        return false;
    prefab->_entities.front()._entity._local_fog_volume_component._density = .5f;
    prefab->_entities.front()._entity._local_fog_volume_component._shape = ECS::ELocalFogVolumeShape::kBox;
    if (!PrefabSystem::Refresh(scene, instance._root, *prefab))
        return false;
    instance_volume = registry.GetComponent<ECS::LocalFogVolumeComponent>(instance._root);
    if (instance_volume->_density != .875f || instance_volume->_shape != ECS::ELocalFogVolumeShape::kBox)
        return false;
    return PrefabSystem::RevertProperty(scene, instance._root, *prefab, component_name, "_density") &&
        instance_volume->_density == .5f;
}

static bool CheckSerialization()
{
    SceneEntityDocument original;
    original._has_local_fog_volume_component = true;
    auto &volume = original._local_fog_volume_component;
    volume._enabled = false;
    volume._shape = ECS::ELocalFogVolumeShape::kSphere;
    volume._density = .375f;
    volume._scattering_albedo = Vector3f(.2f, .4f, .8f);
    volume._edge_fade = .15f;
    original._disabled_components.push_back("Ailu.ECS.LocalFogVolumeComponent");
    JsonArchive writer;
    const String name = "entity";
    SerializerWrapper<SceneEntityDocument>::Serialize(&original, writer, &name);
    const auto text = writer.SaveToString();
    JsonArchive reader;
    if (!reader.LoadFromString(text))
        return false;
    SceneEntityDocument loaded;
    SerializerWrapper<SceneEntityDocument>::Deserialize(&loaded, reader, &name);
    const auto &copy = loaded._local_fog_volume_component;
    if (!loaded._has_local_fog_volume_component || copy._enabled || copy._shape != volume._shape ||
        copy._density != volume._density || copy._edge_fade != volume._edge_fade ||
        copy._scattering_albedo.x != .2f || copy._scattering_albedo.z != .8f ||
        loaded._disabled_components != original._disabled_components)
        return false;
    SceneEntityDocument old_document;
    JsonArchive old_writer;
    SerializerWrapper<SceneEntityDocument>::Serialize(&old_document, old_writer, &name);
    JsonArchive old_reader;
    if (!old_reader.LoadFromString(old_writer.SaveToString()))
        return false;
    SceneEntityDocument old_loaded;
    SerializerWrapper<SceneEntityDocument>::Deserialize(&old_loaded, old_reader, &name);
    return !old_loaded._has_local_fog_volume_component;
}

int main()
{
    LogMgr::Init();
    Enum::InitTypeInfo();
    const bool bounds = CheckBounds();
    const bool serialization = CheckSerialization();
    const bool clone = CheckEntityClone();
    const bool dds = CheckNativeVolumeCapture();
    if (!bounds || !serialization || !clone || !dds)
    {
        std::cerr << "FAIL bounds=" << bounds << " serialization=" << serialization << " clone=" << clone
                  << " dds=" << dds << std::endl;
        LogMgr::Shutdown();
        return 1;
    }
    LogMgr::Shutdown();
    std::cout << "PASS: bounds, optional serialization, entity clone, prefab overrides and native volume DDS\n";
    return 0;
}
