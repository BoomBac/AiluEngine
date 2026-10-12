#include "Assets/AssetDocument.h"
#include "Assets/MeshArtifact.h"
#include "Objects/JsonArchive.h"
#include "Framework/Common/Log.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace Ailu;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    LogMgr::Init();
    try
    {
        SceneTagComponentDocument original;
        original._name = "Linked object";
        original._source_scene_guid = "11111111-1111-4111-8111-111111111111";
        original._source_object_guid = "22222222-2222-4222-8222-222222222222";
        JsonArchive output;
        SerializerWrapper<SceneTagComponentDocument>::Serialize(&original, output);
        JsonArchive input;
        Require(input.LoadFromString(output.SaveToString()), "Linked tag JSON must load");
        SceneTagComponentDocument restored;
        SerializerWrapper<SceneTagComponentDocument>::Deserialize(&restored, input);
        Require(restored._source_scene_guid == original._source_scene_guid && restored._source_object_guid == original._source_object_guid,
                "Source association must survive serialization");
        JsonArchive legacy;
        Require(legacy.LoadFromString(R"({"_name":"Legacy","_tag":"Untagged","_layer_mask":1})"), "Legacy JSON must load");
        SceneTagComponentDocument old_tag;
        SerializerWrapper<SceneTagComponentDocument>::Deserialize(&old_tag, legacy);
        Require(old_tag._source_scene_guid.empty() && old_tag._source_object_guid.empty(), "Legacy tags must remain unlinked");

        Render::Mesh mesh("Native persistent triangle");
        mesh.SetVertices(Vector<Vector3f>{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}});
        mesh.SetNormals(Vector<Vector3f>(3u, {0.f, 0.f, 1.f}));
        mesh.SetTangents(Vector<Vector4f>(3u, {1.f, 0.f, 0.f, 1.f}));
        mesh.SetUVs(Vector<Vector2f>{{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}});
        mesh.AddSubmesh(Vector<u32>{0u, 1u, 2u});
        mesh.SetVerticesCount(3u);
        mesh.SetBounds(Vector<AABB>(2u, AABB(Vector3f::kZero, Vector3f(1.f, 1.f, 0.f))));
        MeshArtifact artifact;
        AssetArtifactKey key;
        key._artifact_version = kMeshArtifactVersion;
        Vector<u8> bytes;
        Require(BuildMeshArtifact(mesh, artifact) && SerializeMeshArtifact(artifact, key, bytes), "Native source must serialize");
        const auto source_path = std::filesystem::temp_directory_path() / (Guid::Generate().ToString() + ".almesh");
        {
            std::ofstream source(source_path, std::ios::binary);
            source.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            source.close();
            Require(static_cast<bool>(source), "Native binary source must save");
        }
        std::ifstream source(source_path, std::ios::binary);
        Vector<u8> disk_bytes((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        source.close();
        std::filesystem::remove(source_path);
        MeshArtifact from_disk;
        Require(DeserializeMeshArtifact(disk_bytes, key, from_disk), "Native source must load without DDC");
        auto copy = CreateMeshFromArtifact(from_disk);
        Require(copy && copy->GetVertices().size() == 3u && copy->GetIndices()[2] == 2u && copy->GetUVs()[1].x == 1.f,
                "Native geometry must survive disk roundtrip");
        disk_bytes.resize(16u);
        Require(!DeserializeMeshArtifact(disk_bytes, key, from_disk), "Truncated native source must fail");
        std::cout << "BlenderPersistenceSmoke PASS: source identity, legacy defaults, binary mesh disk roundtrip, truncation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BlenderPersistenceSmoke FAIL: " << error.what() << '\n';
        return 1;
    }
}
