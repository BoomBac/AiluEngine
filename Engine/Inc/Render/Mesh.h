#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include <string>
#include <unordered_map>
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Framework/Math/Geometry.h"
#include "Animation/Skeleton.h"
#include "Framework/Interface/IAssetable.h"
#include "Objects/Object.h"


namespace Ailu
{
	struct ImportedMaterialInfo
	{
		String _name;
		u16 _slot;
		Array<String, 2> _textures;
		ImportedMaterialInfo(u16 slot = 0, String name = "") : _slot(slot), _name(name) {};
	};

	class Mesh : public Object,public IAssetable
	{
	public:
		inline static Mesh* s_p_cube = nullptr;
		inline static Mesh* s_p_shpere = nullptr;
		inline static Mesh* s_p_quad = nullptr;
	public:
		Mesh();
		Mesh(const std::string& name);
		~Mesh();
		virtual void Clear();
		virtual void BuildRHIResource();
		void SetVertices(Vector3f* vertices);
		void SetNormals(Vector3f* normals);
		void SetTangents(Vector4f* tangents);
		void SetUVs(Vector2f* uv, u8 index);
		inline  Vector3f* GetVertices() { return _vertices; };
		inline Vector3f* GetNormals() { return _normals; };
		inline Vector4f* GetTangents() { return _tangents; };
		inline Vector2f* GetUVs(u8 index) { return _uv[index]; };
		inline u32* GetIndices(u16 submesh_index = 0) { return std::get<0>(_p_indices[submesh_index]); };
		inline u32 GetIndicesCount(u16 submesh_index = 0) { return std::get<1>(_p_indices[submesh_index]); };
		const Ref<VertexBuffer>& GetVertexBuffer() const;
		const Ref<IndexBuffer>& GetIndexBuffer(u16 submesh_index = 0) const;
		void AddSubmesh(u32* indices, u32 indices_count);
		bool _is_rhi_res_ready = false;
		const u16 SubmeshCount() const { return static_cast<u16>(_p_indices.size()); };
		void AddCacheMaterial(ImportedMaterialInfo material) { _imported_materials.emplace_back(material);};
		const List<ImportedMaterialInfo>& GetCacheMaterials() const { return _imported_materials; };
		const Guid& GetGuid() const final;
		void AttachToAsset(Asset* owner) final;
	public:
		u32 _vertex_count;
		AABB _bound_box;
	protected:
		Ref<VertexBuffer> _p_vbuf;
		Vector<Ref<IndexBuffer>> _p_ibufs;
		Vector3f* _vertices;
		Vector3f* _normals;
		Color32* _colors;
		Vector4f* _tangents;
		Vector2f** _uv;
		Vector<std::tuple<u32*, u32>> _p_indices;
		List<ImportedMaterialInfo> _imported_materials;
		Asset* _p_asset_owned_this;
	};

	class SkinedMesh : public Mesh
	{
		DECLARE_PRIVATE_PROPERTY(skeleton, CurSkeleton, Skeleton)
	public:
		inline static bool s_use_local_transf = false;
		SkinedMesh();
		SkinedMesh(const String& name);
		~SkinedMesh();
		void BuildRHIResource() final;
		void Clear() final;
		void SetBoneWeights(Vector4f* bone_weights);
		void SetBoneIndices(Vector4D<u32>* bone_indices);
		inline Vector4D<u32>* GetBoneIndices() { return _bone_indices; };
		inline Vector4f* GetBoneWeights() { return _bone_weights; };
	private:

	private:
		Vector4f* _bone_weights;
		Vector4D<u32>* _bone_indices;
	};
}


#endif // !__MESH_H__

