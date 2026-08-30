#include "Assets/MeshArtifact.h"

#include "Framework/Common/Log.h"
#include "pch.h"

#include <cstring>

namespace Ailu
{
    namespace
    {
        using namespace Render;

        template<typename T>
        void AppendValue(Vector<u8> &data, const T &value)
        {
            const auto *begin = reinterpret_cast<const u8 *>(&value);
            data.insert(data.end(), begin, begin + sizeof(T));
        }

        template<typename T>
        bool ReadValue(std::span<const u8> data, size_t &offset, T &value)
        {
            if (offset > data.size() || sizeof(T) > data.size() - offset)
                return false;
            memcpy(&value, data.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void AppendVector(Vector<u8> &data, const Vector2f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
        }

        void AppendVector(Vector<u8> &data, const Vector3f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
        }

        void AppendVector(Vector<u8> &data, const Vector4f &value)
        {
            AppendValue(data, value.x);
            AppendValue(data, value.y);
            AppendValue(data, value.z);
            AppendValue(data, value.w);
        }

        bool ReadVector(std::span<const u8> data, size_t &offset, Vector2f &value)
        {
            return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y);
        }

        bool ReadVector(std::span<const u8> data, size_t &offset, Vector3f &value)
        {
            return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y) &&
                   ReadValue(data, offset, value.z);
        }

        bool ReadVector(std::span<const u8> data, size_t &offset, Vector4f &value)
        {
            return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y) &&
                   ReadValue(data, offset, value.z) && ReadValue(data, offset, value.w);
        }

        void AppendAabb(Vector<u8> &data, const AABB &aabb)
        {
            AppendVector(data, aabb._min);
            AppendVector(data, aabb._max);
        }

        bool ReadAabb(std::span<const u8> data, size_t &offset, AABB &aabb)
        {
            return ReadVector(data, offset, aabb._min) && ReadVector(data, offset, aabb._max);
        }

        void AppendMatrix(Vector<u8> &data, const Matrix4x4f &matrix)
        {
            for (u32 row = 0u; row < 4u; ++row)
                for (u32 column = 0u; column < 4u; ++column)
                    AppendValue(data, matrix[row][column]);
        }

        bool ReadMatrix(std::span<const u8> data, size_t &offset, Matrix4x4f &matrix)
        {
            for (u32 row = 0u; row < 4u; ++row)
                for (u32 column = 0u; column < 4u; ++column)
                    if (!ReadValue(data, offset, matrix[row][column]))
                        return false;
            return true;
        }

        void AppendString(Vector<u8> &data, const String &value)
        {
            const u32 length = static_cast<u32>(value.size());
            AppendValue(data, length);
            data.insert(data.end(), value.begin(), value.end());
        }

        bool ReadString(std::span<const u8> data, size_t &offset, String &value)
        {
            u32 length = 0u;
            if (!ReadValue(data, offset, length) || length > data.size() - offset)
                return false;
            value.assign(reinterpret_cast<const char *>(data.data() + offset), length);
            offset += length;
            return true;
        }

        template<typename T, typename WriteElement>
        void AppendArray(Vector<u8> &data, const Vector<T> &values, WriteElement write_element)
        {
            const u32 count = static_cast<u32>(values.size());
            AppendValue(data, count);
            for (const T &value : values)
                write_element(data, value);
        }

        template<typename T, typename ReadElement>
        bool ReadArray(std::span<const u8> data, Vector<T> &values, ReadElement read_element)
        {
            size_t offset = 0u;
            u32 count = 0u;
            if (!ReadValue(data, offset, count))
                return false;
            if (count > data.size() - offset)
                return false;
            values.resize(count);
            for (T &value : values)
                if (!read_element(data, offset, value))
                    return false;
            return offset == data.size();
        }

        void AppendTriangle(Vector<u8> &data, const TriangleData &triangle)
        {
            AppendVector(data, triangle.v0);
            AppendVector(data, triangle.v1);
            AppendVector(data, triangle.v2);
            AppendVector(data, triangle.n0);
            AppendVector(data, triangle.n1);
            AppendVector(data, triangle.n2);
            AppendVector(data, triangle.uv0);
            AppendVector(data, triangle.uv1);
            AppendVector(data, triangle.uv2);
        }

        bool ReadTriangle(std::span<const u8> data, size_t &offset, TriangleData &triangle)
        {
            return ReadVector(data, offset, triangle.v0) && ReadVector(data, offset, triangle.v1) &&
                   ReadVector(data, offset, triangle.v2) && ReadVector(data, offset, triangle.n0) &&
                   ReadVector(data, offset, triangle.n1) && ReadVector(data, offset, triangle.n2) &&
                   ReadVector(data, offset, triangle.uv0) && ReadVector(data, offset, triangle.uv1) &&
                   ReadVector(data, offset, triangle.uv2);
        }

        void AppendBvhNode(Vector<u8> &data, const BVHNode &node)
        {
            AppendAabb(data, node._aabb);
            AppendValue(data, node._child_index_or_first);
            AppendValue(data, node._count_or_flag);
        }

        bool ReadBvhNode(std::span<const u8> data, size_t &offset, BVHNode &node)
        {
            return ReadAabb(data, offset, node._aabb) && ReadValue(data, offset, node._child_index_or_first) &&
                   ReadValue(data, offset, node._count_or_flag);
        }

        void AddSection(AssetArtifactWriter &writer, u32 type, const Vector<u8> &data, u32 count, u32 stride)
        {
            writer.AddSection(type, data, count, stride);
        }
    }

    bool BuildMeshArtifact(const Render::Mesh &mesh, MeshArtifact &out_artifact)
    {
        out_artifact = {};
        out_artifact._name = mesh.Name();
        out_artifact._positions.assign(mesh.GetVertices().begin(), mesh.GetVertices().end());
        out_artifact._normals.assign(mesh.GetNormals().begin(), mesh.GetNormals().end());
        out_artifact._tangents.assign(mesh.GetTangents().begin(), mesh.GetTangents().end());
        out_artifact._colors.assign(mesh.GetColors().begin(), mesh.GetColors().end());
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
        {
            const auto uvs = mesh.GetUVs(channel);
            out_artifact._uvs[channel].assign(uvs.begin(), uvs.end());
        }
        out_artifact._submeshes.resize(mesh.SubmeshCount());
        for (u16 index = 0u; index < mesh.SubmeshCount(); ++index)
        {
            out_artifact._submeshes[index]._indices.assign(mesh.GetIndices(index).begin(), mesh.GetIndices(index).end());
            if (index + 1u < mesh.BoundBox().size())
                out_artifact._submeshes[index]._bounds = mesh.GetBoundBox(index + 1u);
        }
        out_artifact._bounds.assign(mesh.BoundBox().begin(), mesh.BoundBox().end());
        out_artifact._triangle_bounds.assign(mesh.GetTriangleBounds().begin(), mesh.GetTriangleBounds().end());
        out_artifact._triangle_data.assign(mesh.GetTriangleData().begin(), mesh.GetTriangleData().end());
        out_artifact._bvh_nodes.assign(mesh.GetBVHNodes().begin(), mesh.GetBVHNodes().end());
        out_artifact._submesh_bvh_ranges.resize(mesh.SubmeshCount());
        for (u16 index = 0u; index < mesh.SubmeshCount(); ++index)
            out_artifact._submesh_bvh_ranges[index] = Vector2UInt{mesh.GetBVHNodeStart(index), mesh.GetBVHNodeCount(index)};
        out_artifact._vertex_count = mesh.GetVertexCount();
        out_artifact._triangle_count = mesh.GetTriangleCount();

        if (const auto *skeleton_mesh = dynamic_cast<const SkeletonMesh *>(&mesh))
        {
            out_artifact._is_skeleton = true;
            out_artifact._bone_indices.assign(skeleton_mesh->GetBoneIndices().begin(), skeleton_mesh->GetBoneIndices().end());
            out_artifact._bone_weights.assign(skeleton_mesh->GetBoneWeights().begin(), skeleton_mesh->GetBoneWeights().end());
            out_artifact._mesh_bind_global = skeleton_mesh->GetMeshBindGlobalTransform();
        }
        return !out_artifact._positions.empty() && !out_artifact._submeshes.empty();
    }

    bool SerializeMeshArtifact(const MeshArtifact &artifact, const AssetArtifactKey &key, Vector<u8> &out_data)
    {
        Vector<u8> metadata;
        AppendString(metadata, artifact._name);
        AppendValue(metadata, artifact._vertex_count);
        AppendValue(metadata, artifact._triangle_count);
        AppendValue(metadata, static_cast<u8>(artifact._is_skeleton));
        Vector<u8> positions, normals, tangents, colors, bounds, triangle_bounds, triangle_data, bvh, ranges;
        AppendArray(positions, artifact._positions, [](Vector<u8> &data, const Vector3f &value) { AppendVector(data, value); });
        AppendArray(normals, artifact._normals, [](Vector<u8> &data, const Vector3f &value) { AppendVector(data, value); });
        AppendArray(tangents, artifact._tangents, [](Vector<u8> &data, const Vector4f &value) { AppendVector(data, value); });
        AppendArray(colors, artifact._colors, [](Vector<u8> &data, const Color &value) { AppendVector(data, value); });
        AppendArray(bounds, artifact._bounds, [](Vector<u8> &data, const AABB &value) { AppendAabb(data, value); });
        AppendArray(triangle_bounds, artifact._triangle_bounds,
                    [](Vector<u8> &data, const AABB &value) { AppendAabb(data, value); });
        AppendArray(triangle_data, artifact._triangle_data,
                    [](Vector<u8> &data, const TriangleData &value) { AppendTriangle(data, value); });
        AppendArray(bvh, artifact._bvh_nodes, [](Vector<u8> &data, const BVHNode &value) { AppendBvhNode(data, value); });
        AppendArray(ranges, artifact._submesh_bvh_ranges,
                    [](Vector<u8> &data, const Vector2UInt &value) { AppendValue(data, value.x); AppendValue(data, value.y); });

        AssetArtifactWriter writer;
        AddSection(writer, kMeshArtifactMetadataSection, metadata, 1u, static_cast<u32>(metadata.size()));
        AddSection(writer, kMeshArtifactPositionsSection, positions, static_cast<u32>(artifact._positions.size()), sizeof(Vector3f));
        AddSection(writer, kMeshArtifactNormalsSection, normals, static_cast<u32>(artifact._normals.size()), sizeof(Vector3f));
        AddSection(writer, kMeshArtifactTangentsSection, tangents, static_cast<u32>(artifact._tangents.size()), sizeof(Vector4f));
        AddSection(writer, kMeshArtifactColorsSection, colors, static_cast<u32>(artifact._colors.size()), sizeof(Color));
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
        {
            Vector<u8> uv_data;
            AppendArray(uv_data, artifact._uvs[channel], [](Vector<u8> &data, const Vector2f &value) { AppendVector(data, value); });
            AddSection(writer, kMeshArtifactUv0Section + channel, uv_data,
                       static_cast<u32>(artifact._uvs[channel].size()), sizeof(Vector2f));
        }
        Vector<u8> submesh_data;
        AppendValue(submesh_data, static_cast<u32>(artifact._submeshes.size()));
        for (const auto &submesh : artifact._submeshes)
        {
            AppendValue(submesh_data, static_cast<u32>(submesh._indices.size()));
            for (u32 index : submesh._indices)
                AppendValue(submesh_data, index);
            AppendAabb(submesh_data, submesh._bounds);
        }
        AddSection(writer, kMeshArtifactSubmeshesSection, submesh_data, static_cast<u32>(artifact._submeshes.size()), 0u);
        AddSection(writer, kMeshArtifactBoundsSection, bounds, static_cast<u32>(artifact._bounds.size()), sizeof(AABB));
        AddSection(writer, kMeshArtifactTriangleBoundsSection, triangle_bounds,
                   static_cast<u32>(artifact._triangle_bounds.size()), sizeof(AABB));
        AddSection(writer, kMeshArtifactTriangleDataSection, triangle_data,
                   static_cast<u32>(artifact._triangle_data.size()), sizeof(TriangleData));
        AddSection(writer, kMeshArtifactBvhSection, bvh, static_cast<u32>(artifact._bvh_nodes.size()), sizeof(BVHNode));
        AddSection(writer, kMeshArtifactBvhRangesSection, ranges,
                   static_cast<u32>(artifact._submesh_bvh_ranges.size()), sizeof(Vector2UInt));

        if (artifact._is_skeleton)
        {
            Vector<u8> bone_indices, bone_weights;
            AppendArray(bone_indices, artifact._bone_indices,
                        [](Vector<u8> &data, const Vector4D<u32> &value)
                        {
                            AppendValue(data, value.x); AppendValue(data, value.y); AppendValue(data, value.z); AppendValue(data, value.w);
                        });
            AppendArray(bone_weights, artifact._bone_weights,
                        [](Vector<u8> &data, const Vector4f &value) { AppendVector(data, value); });
            AddSection(writer, kMeshArtifactBoneIndicesSection, bone_indices,
                       static_cast<u32>(artifact._bone_indices.size()), sizeof(Vector4D<u32>));
            AddSection(writer, kMeshArtifactBoneWeightsSection, bone_weights,
                       static_cast<u32>(artifact._bone_weights.size()), sizeof(Vector4f));
            Vector<u8> mesh_bind_global;
            AppendMatrix(mesh_bind_global, artifact._mesh_bind_global);
            AddSection(writer, kMeshArtifactMeshBindGlobalSection, mesh_bind_global, 1u,
                       static_cast<u32>(mesh_bind_global.size()));
        }
        return writer.Serialize(EAssetArtifactType::kMesh, kMeshArtifactVersion, key, out_data);
    }

    bool DeserializeMeshArtifact(std::span<const u8> data, const AssetArtifactKey &key, MeshArtifact &out_artifact)
    {
        AssetArtifactReader reader;
        if (!reader.Load(data, key, EAssetArtifactType::kMesh))
            return false;
        out_artifact = {};
        const auto metadata = reader.GetSectionData(kMeshArtifactMetadataSection);
        size_t offset = 0u;
        u8 is_skeleton = 0u;
        if (!ReadString(metadata, offset, out_artifact._name) || !ReadValue(metadata, offset, out_artifact._vertex_count) ||
            !ReadValue(metadata, offset, out_artifact._triangle_count) || !ReadValue(metadata, offset, is_skeleton) ||
            offset != metadata.size())
            return false;
        out_artifact._is_skeleton = is_skeleton != 0u;
        if (!ReadArray(reader.GetSectionData(kMeshArtifactPositionsSection), out_artifact._positions,
                       [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactNormalsSection), out_artifact._normals,
                       [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactTangentsSection), out_artifact._tangents,
                       [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactColorsSection), out_artifact._colors,
                       [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }))
            return false;
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
            if (!ReadArray(reader.GetSectionData(kMeshArtifactUv0Section + channel), out_artifact._uvs[channel],
                           [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }))
                return false;
        if (!ReadArray(reader.GetSectionData(kMeshArtifactBoundsSection), out_artifact._bounds,
                       [](auto data, auto &offset, auto &value) { return ReadAabb(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactTriangleBoundsSection), out_artifact._triangle_bounds,
                       [](auto data, auto &offset, auto &value) { return ReadAabb(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactTriangleDataSection), out_artifact._triangle_data,
                       [](auto data, auto &offset, auto &value) { return ReadTriangle(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactBvhSection), out_artifact._bvh_nodes,
                       [](auto data, auto &offset, auto &value) { return ReadBvhNode(data, offset, value); }) ||
            !ReadArray(reader.GetSectionData(kMeshArtifactBvhRangesSection), out_artifact._submesh_bvh_ranges,
                       [](auto data, auto &offset, auto &value)
                       { return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y); }))
            return false;

        const auto submesh_data = reader.GetSectionData(kMeshArtifactSubmeshesSection);
        offset = 0u;
        u32 submesh_count = 0u;
        if (!ReadValue(submesh_data, offset, submesh_count))
            return false;
        if (submesh_count > submesh_data.size() - offset)
            return false;
        out_artifact._submeshes.resize(submesh_count);
        for (auto &submesh : out_artifact._submeshes)
        {
            u32 index_count = 0u;
            if (!ReadValue(submesh_data, offset, index_count))
                return false;
            submesh._indices.resize(index_count);
            for (u32 &index : submesh._indices)
                if (!ReadValue(submesh_data, offset, index))
                    return false;
            if (!ReadAabb(submesh_data, offset, submesh._bounds))
                return false;
        }
        if (offset != submesh_data.size())
            return false;

        if (out_artifact._is_skeleton)
        {
            if (!ReadArray(reader.GetSectionData(kMeshArtifactBoneIndicesSection), out_artifact._bone_indices,
                           [](auto data, auto &offset, auto &value)
                           {
                               return ReadValue(data, offset, value.x) && ReadValue(data, offset, value.y) &&
                                      ReadValue(data, offset, value.z) && ReadValue(data, offset, value.w);
                           }) ||
                !ReadArray(reader.GetSectionData(kMeshArtifactBoneWeightsSection), out_artifact._bone_weights,
                           [](auto data, auto &offset, auto &value) { return ReadVector(data, offset, value); }))
                return false;
            const auto mesh_bind_global = reader.GetSectionData(kMeshArtifactMeshBindGlobalSection);
            offset = 0u;
            if (!ReadMatrix(mesh_bind_global, offset, out_artifact._mesh_bind_global) || offset != mesh_bind_global.size())
                return false;
        }
        return true;
    }

    Ref<Render::Mesh> CreateMeshFromArtifact(const MeshArtifact &artifact)
    {
        Ref<Render::Mesh> mesh = artifact._is_skeleton ? std::static_pointer_cast<Render::Mesh>(MakeRef<SkeletonMesh>()) :
                                                          MakeRef<Render::Mesh>();
        mesh->Name(artifact._name);
        mesh->SetVertices(Vector<Vector3f>(artifact._positions.begin(), artifact._positions.end()));
        mesh->SetNormals(Vector<Vector3f>(artifact._normals.begin(), artifact._normals.end()));
        mesh->SetTangents(Vector<Vector4f>(artifact._tangents.begin(), artifact._tangents.end()));
        mesh->SetColors(Vector<Color>(artifact._colors.begin(), artifact._colors.end()));
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
            mesh->SetUVs(Vector<Vector2f>(artifact._uvs[channel].begin(), artifact._uvs[channel].end()), channel);
        for (const auto &submesh : artifact._submeshes)
            mesh->AddSubmesh(submesh._indices);
        mesh->SetBounds(artifact._bounds);
        mesh->SetVerticesCount(artifact._vertex_count);
        mesh->SetDerivedData(Vector<AABB>(artifact._triangle_bounds.begin(), artifact._triangle_bounds.end()),
                             Vector<TriangleData>(artifact._triangle_data.begin(), artifact._triangle_data.end()),
                             Vector<BVHNode>(artifact._bvh_nodes.begin(), artifact._bvh_nodes.end()),
                             Vector<Vector2UInt>(artifact._submesh_bvh_ranges.begin(), artifact._submesh_bvh_ranges.end()),
                             artifact._triangle_count);
        if (auto *skeleton_mesh = dynamic_cast<SkeletonMesh *>(mesh.get()))
        {
            skeleton_mesh->SetBoneIndices(artifact._bone_indices);
            skeleton_mesh->SetBoneWeights(artifact._bone_weights);
            skeleton_mesh->SetMeshBindGlobalTransform(artifact._mesh_bind_global);
        }
        return mesh;
    }
}
