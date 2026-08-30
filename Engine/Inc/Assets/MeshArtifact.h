#pragma once

#include "Assets/AssetArtifact.h"
#include "Render/Mesh.h"

namespace Ailu
{
    inline constexpr u32 kMeshArtifactMetadataSection = 1u;
    inline constexpr u32 kMeshArtifactPositionsSection = 2u;
    inline constexpr u32 kMeshArtifactNormalsSection = 3u;
    inline constexpr u32 kMeshArtifactTangentsSection = 4u;
    inline constexpr u32 kMeshArtifactColorsSection = 5u;
    inline constexpr u32 kMeshArtifactUv0Section = 6u;
    inline constexpr u32 kMeshArtifactUv1Section = 7u;
    inline constexpr u32 kMeshArtifactUv2Section = 8u;
    inline constexpr u32 kMeshArtifactUv3Section = 9u;
    inline constexpr u32 kMeshArtifactSubmeshesSection = 10u;
    inline constexpr u32 kMeshArtifactBoundsSection = 11u;
    inline constexpr u32 kMeshArtifactTriangleBoundsSection = 12u;
    inline constexpr u32 kMeshArtifactTriangleDataSection = 13u;
    inline constexpr u32 kMeshArtifactBvhSection = 14u;
    inline constexpr u32 kMeshArtifactBvhRangesSection = 15u;
    inline constexpr u32 kMeshArtifactBoneIndicesSection = 16u;
    inline constexpr u32 kMeshArtifactBoneWeightsSection = 17u;
    inline constexpr u32 kMeshArtifactMeshBindGlobalSection = 18u;

    struct AILU_API MeshArtifact
    {
        String _name;
        Vector<Vector3f> _positions;
        Vector<Vector3f> _normals;
        Vector<Vector4f> _tangents;
        Vector<Color> _colors;
        Array<Vector<Vector2f>, Render::Mesh::kMaxUVChannels> _uvs;
        Vector<Render::Mesh::Submesh> _submeshes;
        Vector<AABB> _bounds;
        Vector<AABB> _triangle_bounds;
        Vector<Render::TriangleData> _triangle_data;
        Vector<BVHNode> _bvh_nodes;
        Vector<Vector2UInt> _submesh_bvh_ranges;
        u32 _vertex_count = 0u;
        u32 _triangle_count = 0u;
        bool _is_skeleton = false;
        Vector<Vector4D<u32>> _bone_indices;
        Vector<Vector4f> _bone_weights;
        Matrix4x4f _mesh_bind_global = Matrix4x4f::Identity();
    };

    AILU_API bool BuildMeshArtifact(const Render::Mesh &mesh, MeshArtifact &out_artifact);
    AILU_API bool SerializeMeshArtifact(const MeshArtifact &artifact, const AssetArtifactKey &key, Vector<u8> &out_data);
    AILU_API bool DeserializeMeshArtifact(std::span<const u8> data, const AssetArtifactKey &key,
                                          MeshArtifact &out_artifact);
    AILU_API Ref<Render::Mesh> CreateMeshFromArtifact(const MeshArtifact &artifact);
}
