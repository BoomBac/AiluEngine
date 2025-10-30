#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include "Animation/Skeleton.h"
#include "Framework/Common/Asset.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Geometry.h"
#include "GlobalMarco.h"
#include "Objects/Object.h"
#include "Render/Buffer.h"
#include <string>
#include <span>
#include <unordered_map>


namespace Ailu
{
    class FbxParser;

    namespace Render
    {
        class AILU_API Mesh : public Object
        {
            friend class FbxParser;

        public:
            inline static constexpr u8 kMaxUVChannels = 4u;
            //-----------------------------------------
            // Nested Types
            //-----------------------------------------
            struct ImportedMaterialInfo
            {
                std::string _name;
                u16 _slot = 0;
                std::array<std::string, 2> _textures;
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

            u32 GetVertexCount() const noexcept { return _vertex_count; }
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

            Vector<ImportedMaterialInfo> _imported_taterials;

            //-----------------------------------------
            // GPU Data
            //-----------------------------------------
            Ref<VertexBuffer> _vertex_buffer;
            Vector<Ref<IndexBuffer>> _index_buffers;

            //-----------------------------------------
            // Metadata
            //-----------------------------------------
            u32 _vertex_count = 0;
        };

        class AILU_API SkeletonMesh : public Mesh
        {
        public:
            inline static bool s_use_local_transf = false;
            SkeletonMesh();
            explicit SkeletonMesh(const String &name);
            ~SkeletonMesh();
            void Apply() final;
            void Clear() final;
            void SetBoneWeights(std::span<const Vector4f> bone_weights);
            void SetBoneIndices(std::span<const Vector4D<u32>> bone_indices);
            [[nodiscard]] std::span<const Vector4D<u32>> GetBoneIndices() const noexcept { return _bone_indices; };
            [[nodiscard]] std::span<const Vector4f> GetBoneWeights() const noexcept { return _bone_weights; };
            void SetSkeleton(const Skeleton &skeleton);
            [[nodiscard]] Skeleton &GetSkeleton();

        private:
        private:
            Skeleton _skeleton;
            Vector<Vector4f> _bone_weights;
            Vector<Vector4D<u32>> _bone_indices;
            Vector<Vector3f> _previous_vertices;
        };
    }// namespace Render
}// namespace Ailu::Render


#endif// !__MESH_H__
