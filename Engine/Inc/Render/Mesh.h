#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include "Animation/SkeletonAsset.h"
#include "Assets/Asset.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"
#include "Framework/Core/CoreMinimal.h"
#include "Assets/AssetRef.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Array.h"
#include "Objects/Object.h"
#include "Render/Buffer.h"
#include <string>
#include <span>
#include <unordered_map>
#include "Framework/Math/BVHBuilder.h"
#include "generated/Mesh.gen.h"


namespace Ailu
{
    class FbxParser;
    class GltfParser;

    namespace Render
    {
        ACLASS()
        class AILU_API Mesh : public Object
        {
            GENERATED_BODY()
            friend class FbxParser;
            friend class GltfParser;

        public:
            inline static constexpr u8 kMaxUVChannels = 4u;
            //-----------------------------------------
            // Nested Types
            //-----------------------------------------
            struct ImportedMaterialInfo
            {
                std::string _name;
                u64 _source_id = 0u;
                u16 _slot = 0;
                std::array<std::string, 5> _textures;
                Color _diffuse = Color(1.0f);
                Color _specular = Color(1.0f);
                Color _emissive = Color(0.0f);
                f32 _roughness = 1.0f;

                ImportedMaterialInfo(u16 s = 0, std::string n = "")
                    : _name(std::move(n)), _slot(s) {}
            };

            struct Submesh
            {
                Vector<u32> _indices;
                AABB _bounds{};
            };

        public:
            //-----------------------------------------
            // Static Primitive Meshes
            //-----------------------------------------
            inline static std::weak_ptr<Mesh> s_cube;
            inline static std::weak_ptr<Mesh> s_sphere;
            inline static std::weak_ptr<Mesh> s_plane;
            inline static std::weak_ptr<Mesh> s_capsule;
            inline static std::weak_ptr<Mesh> s_cylinder;
            inline static std::weak_ptr<Mesh> s_cone;
            inline static std::weak_ptr<Mesh> s_torus;
            inline static std::weak_ptr<Mesh> s_monkey;
            inline static std::weak_ptr<Mesh> s_quad;
            inline static std::weak_ptr<Mesh> s_fullscreen_triangle;

        public:
            //-----------------------------------------
            // Lifecycle
            //-----------------------------------------
            Mesh();
            explicit Mesh(String name);
            ~Mesh();
            virtual void Apply();
            virtual void BuildDerivedData();
            virtual void UploadGpuResources();
            virtual void Clear();

            Mesh(const Mesh &) = delete;
            Mesh &operator=(const Mesh &) = delete;
            Mesh(Mesh &&) noexcept = default;
            Mesh &operator=(Mesh &&) noexcept = default;

        public:
            void SetVertices(std::span<const Vector3f> vertices);
            void SetVertices(Vector<Vector3f>&& vertices);
            void SetNormals(std::span<const Vector3f> normals);
            void SetNormals(Vector<Vector3f> &&normals);
            void SetTangents(std::span<const Vector4f> tangents);
            void SetTangents(Vector<Vector4f> &&tangents);
            void SetColors(std::span<const Color> colors);
            void SetColors(Vector<Color> &&colors);
            void SetBounds(std::span<const AABB> bounds);
            void SetDerivedData(Vector<AABB> &&triangle_bounds, Vector<TriangleData> &&triangle_data,
                                Vector<BVHNode> &&bvh_nodes, Vector<Vector2UInt> &&bvh_node_ranges,
                                u32 triangle_count);
            void SetUVs(std::span<const Vector2f> uv, u8 channel = 0u);
            void SetUVs(Vector<Vector2f> &&uv, u8 channel = 0u);

            void AddSubmesh(std::span<const u32> indices);
            void SetVerticesCount(u32 count) { _vertex_count = count; }
            //-----------------------------------------
            // Accessors
            //-----------------------------------------
            [[nodiscard]] std::span<const Vector3f> GetVertices() const noexcept { return _vertices; }
            [[nodiscard]] std::span<const Vector3f> GetNormals() const noexcept { return _normals; }
            [[nodiscard]] std::span<const Vector4f> GetTangents() const noexcept { return _tangents; }
            [[nodiscard]] std::span<const Color> GetColors() const noexcept { return _colors; }
            [[nodiscard]] std::span<const Vector2f> GetUVs(uint8_t channel = 0) const noexcept { return _uvs.at(channel); }

            [[nodiscard]] std::span<const u32> GetIndices(u16 submesh_index = 0) const noexcept;
            [[nodiscard]] i32 GetIndicesCount(u16 submesh_index = 0) const noexcept;

            [[nodiscard]] u16 SubmeshCount() const noexcept { return static_cast<u16>(_submeshes.size()); }
            [[nodiscard]] const Vector<AABB> &BoundBox() const noexcept { return _bounds; }
            [[nodiscard]] const AABB &GetBoundBox(u16 index = 0) const noexcept { return _bounds.at(index); }

            [[nodiscard]] const Ref<VertexBuffer> &GetVertexBuffer() const noexcept { return _vertex_buffer; }
            [[nodiscard]] const Ref<IndexBuffer> &GetIndexBuffer(u16 submesh_index = 0) const noexcept;
            [[nodiscard]] i32 GetNormalStream() const noexcept { return _normal_stream; }
            [[nodiscard]] i32 GetTangentStream() const noexcept { return _tangent_stream; }
            [[nodiscard]] i32 GetBindlessVertexStreamIndex(const std::string &semantic_name, u8 semantic_index = 0u) const noexcept;
            [[nodiscard]] u32 GetTriangleStart(u16 submesh_index) const noexcept;
            [[nodiscard]] u32 GetTriangleCount(u16 submesh_index) const noexcept;
            [[nodiscard]] u32 GetBVHNodeStart(u16 submesh_index) const noexcept;
            [[nodiscard]] u32 GetBVHNodeCount(u16 submesh_index) const noexcept;

            [[nodiscard]] std::span<const AABB> GetTriangleBounds() const noexcept { return _triangle_bounds; }
            [[nodiscard]] std::span<const TriangleData> GetTriangleData() const noexcept { return _triangle_data; }
            [[nodiscard]] std::span<const BVHNode> GetBVHNodes() const noexcept { return _bvh_nodes; }

            u32 GetVertexCount() const noexcept { return _vertex_count; }
            u32 GetTriangleCount() const noexcept { return _triangle_count; }
            [[nodiscard]] bool HasDerivedData() const noexcept { return _derived_data_ready; }
            //-----------------------------------------
            // Imported Material Cache (Import Stage Only)
            //-----------------------------------------
            void AddCacheMaterial(ImportedMaterialInfo material) { _imported_taterials.emplace_back(std::move(material)); }
            [[nodiscard]] const Vector<ImportedMaterialInfo> &GetCacheMaterials() const noexcept { return _imported_taterials; }

        protected:
            ////-----------------------------------------
            //// Internal Helpers
            ////-----------------------------------------
            //void UpdateBounds();
            //void UploadToGPU();
            void GenerateTriangleBounds();
        protected:
            //-----------------------------------------
            // CPU Data
            //-----------------------------------------
            Vector<Vector3f> _vertices;
            Vector<Vector3f> _normals;
            Vector<Vector4f> _tangents;
            Vector<Color> _colors;
            Array<Vector<Vector2f>, kMaxUVChannels> _uvs;// up to 4 UV sets

            Vector<Submesh> _submeshes;
            Vector<AABB> _bounds;// 0 = full mesh, [1..] = per-submesh
            Vector<AABB> _triangle_bounds;// per-triangle bounds
            Vector<TriangleData> _triangle_data;
            Vector<BVHNode> _bvh_nodes;
            Vector<Vector2UInt> _submesh_bvh_node_ranges;
            Vector<ImportedMaterialInfo> _imported_taterials;

            //-----------------------------------------
            // GPU Data
            //-----------------------------------------
            Ref<VertexBuffer> _vertex_buffer;
            Vector<Ref<IndexBuffer>> _index_buffers;
            i32 _normal_stream = -1;
            i32 _tangent_stream = -1;

            //-----------------------------------------
            // Metadata
            //-----------------------------------------
            u32 _vertex_count = 0u;
            u32 _triangle_count = 0u;
            bool _derived_data_ready = false;
        };

        ACLASS()
        class AILU_API SkeletonMesh : public Mesh
        {
            GENERATED_BODY()
        public:
            inline static bool s_use_local_transf = false;
            SkeletonMesh();
            explicit SkeletonMesh(const String &name);
            ~SkeletonMesh();
            void Apply() final;
            void Clear() final;
            void SetBoneWeights(std::span<const Vector4f> bone_weights);
            void SetBoneIndices(std::span<const Vector4D<u32>> bone_indices);
            bool RemapBoneIndices(std::span<const u16> bone_remap);
            void SetMeshBindGlobalTransform(const Matrix4x4f &transform);
            void RestoreBindPoseVertices();
            void BuildSkinMatrixPalette(std::span<const Matrix4x4f> global_pose_palette,
                                        Vector<Matrix4x4f> &out_palette) const;
            void BuildMeshSpaceJointPositions(std::span<const Matrix4x4f> global_pose_palette,
                                              Vector<Vector3f> &out_positions) const;
            [[nodiscard]] std::span<const Vector4D<u32>> GetBoneIndices() const noexcept { return _bone_indices; };
            [[nodiscard]] std::span<const Vector4f> GetBoneWeights() const noexcept { return _bone_weights; };
            [[nodiscard]] std::span<const Vector3f> GetPreviousVertices() const noexcept { return _previous_vertices; };
            [[nodiscard]] const Matrix4x4f &GetMeshBindGlobalTransform() const noexcept { return _mesh_bind_global; };
            [[nodiscard]] const Matrix4x4f &GetMeshCurrentGlobalInverseTransform() const noexcept
            {
                return _mesh_current_global_inv;
            };
            void SetSkeletonAsset(Ref<SkeletonAsset> skeleton_asset) { _skeleton_asset = std::move(skeleton_asset); }
            void SetSkeletonAsset(const Guid &guid, Ref<SkeletonAsset> skeleton_asset)
            {
                _skeleton_asset.Set(guid, std::move(skeleton_asset));
            }
            [[nodiscard]] const AssetRef<SkeletonAsset> &GetSkeletonAsset() const noexcept { return _skeleton_asset; }

        private:
            AssetRef<SkeletonAsset> _skeleton_asset;
            Vector<Vector4f> _bone_weights;
            Vector<Vector4D<u32>> _bone_indices;
            Vector<Vector3f> _previous_vertices;
            Matrix4x4f _mesh_bind_global = Matrix4x4f::Identity();
            Matrix4x4f _mesh_current_global_inv = Matrix4x4f::Identity();
        };
    }// namespace Render
}// namespace Ailu::Render


#endif// !__MESH_H__
