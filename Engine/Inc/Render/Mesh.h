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
            struct ImportedMaterialInfo
            {
                String _name;
                u16 _slot;
                Array<String, 2> _textures;
                ImportedMaterialInfo(u16 slot = 0, String name = "") : _slot(slot), _name(name) {};
            };
            inline static std::weak_ptr<Mesh> s_p_cube;
            inline static std::weak_ptr<Mesh> s_p_shpere;
            inline static std::weak_ptr<Mesh> s_p_plane;
            inline static std::weak_ptr<Mesh> s_p_capsule;
            inline static std::weak_ptr<Mesh> s_p_cylinder;
            inline static std::weak_ptr<Mesh> s_p_cone;
            inline static std::weak_ptr<Mesh> s_p_torus;
            inline static std::weak_ptr<Mesh> s_p_monkey;

            inline static std::weak_ptr<Mesh> s_p_quad;
            inline static std::weak_ptr<Mesh> s_p_fullscreen_triangle;

        public:
            Mesh();
            Mesh(const std::string &name);
            ~Mesh();
            virtual void Clear();
            virtual void Apply();
            void SetVertices(Vector3f *vertices);
            void SetNormals(Vector3f *normals);
            void SetTangents(Vector4f *tangents);
            void SetUVs(Vector2f *uv, u8 index);
            inline Vector3f *GetVertices() { return _vertices; };
            inline Vector3f *GetNormals() { return _normals; };
            inline Vector4f *GetTangents() { return _tangents; };
            inline Vector2f *GetUVs(u8 index) { return _uv[index]; };
            inline u32 *GetIndices(u16 submesh_index = 0) { return std::get<0>(_p_indices[submesh_index]); };
            inline u32 GetIndicesCount(u16 submesh_index = 0) { return std::get<1>(_p_indices[submesh_index]); };
            const Ref<VertexBuffer> &GetVertexBuffer() const;
            const Ref<IndexBuffer> &GetIndexBuffer(u16 submesh_index = 0) const;
            void AddSubmesh(u32 *indices, u32 indices_count);
            const u16 SubmeshCount() const { return static_cast<u16>(_p_indices.size()); };
            void AddCacheMaterial(ImportedMaterialInfo material) { _imported_materials.emplace_back(material); };
            const List<ImportedMaterialInfo> &GetCacheMaterials() const { return _imported_materials; };
            /// @brief 返回边界盒子，0为所有子网格的并集，子网格从1开始
            /// @return 对象空间包围盒
            const Vector<AABB> &BoundBox() const { return _bound_boxs; };

        public:
            u32 _vertex_count;

        protected:
            Vector<AABB> _bound_boxs;
            Ref<VertexBuffer> _p_vbuf;
            Vector<Ref<IndexBuffer>> _p_ibufs;
            Vector3f *_vertices;
            Vector3f *_normals;
            Color *_colors;
            Vector4f *_tangents;
            Vector2f **_uv;
            Vector<std::tuple<u32 *, u32>> _p_indices;
            List<ImportedMaterialInfo> _imported_materials;
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
            void SetBoneWeights(Vector4f *bone_weights);
            void SetBoneIndices(Vector4D<u32> *bone_indices);
            inline Vector4D<u32> *GetBoneIndices() { return _bone_indices; };
            inline Vector4f *GetBoneWeights() { return _bone_weights; };
            void SetSkeleton(const Skeleton &skeleton);
            [[nodiscard]] Skeleton &GetSkeleton();

        private:
        private:
            Skeleton _skeleton;
            Vector4f *_bone_weights;
            Vector4D<u32> *_bone_indices;
            Vector3f *_previous_vertices;
        };
    }// namespace Render
}// namespace Ailu::Render


#endif// !__MESH_H__
