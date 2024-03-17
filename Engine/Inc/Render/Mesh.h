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


namespace Ailu
{
	struct ImportedMaterialInfo
	{
		String _name;
		u16 _slot;
		Array<String, 2> _textures;
		ImportedMaterialInfo(u16 slot = 0, String name = "") : _slot(slot), _name(name) {};
	};

	class Mesh
	{
		DECLARE_PROTECTED_PROPERTY(origin_path, OriginPath, String)
		DECLARE_PROTECTED_PROPERTY(name, Name, std::string)
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
	public:
		u32 _vertex_count;
		AABB _bound_box;
	protected:
		Ref<VertexBuffer> _p_vbuf;
		Vector<Ref<IndexBuffer>> _p_ibufs;
		Vector3f* _vertices;
		Vector3f* _normals;
		Color* _colors;
		Vector4f* _tangents;
		Vector2f** _uv;
		Vector<std::tuple<u32*, u32>> _p_indices;
		List<ImportedMaterialInfo> _imported_materials;
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

	class MeshPool
	{
	public:
		static Ref<Mesh> CreateMesh(const std::string& name)
		{
			auto mesh = MakeRef<Mesh>(name);
			s_meshes[name] = mesh;
			s_meshe_names.emplace_back(s_meshes.find(name)->first.c_str());
			s_it_meshes.emplace_back(mesh);
			return mesh;
		}

		static void AddMesh(const std::string& name, Ref<Mesh> mesh)
		{
			s_meshes[name] = mesh;
			s_meshe_names.emplace_back(s_meshes.find(name)->first.c_str());
			s_it_meshes.emplace_back(mesh);
			mesh->OriginPath(name);
			mesh->Name(name);
		}

		static void AddMesh(Ref<Mesh> mesh)
		{
			s_meshes[mesh->Name()] = mesh;
			s_meshe_names.emplace_back(s_meshes.find(mesh->Name())->first.c_str());
			s_it_meshes.emplace_back(mesh);
		}

		static Ref<Mesh> GetMesh(const std::string& name)
		{
			auto it = s_meshes.find(name);
			if (it != s_meshes.end())
			{
				return it->second;
			}
			return nullptr;
		}

		static void ReleaseMesh(const std::string& name)
		{
			auto it = s_meshes.find(name);
			if (it != s_meshes.end())
			{
				s_it_meshes.erase(std::find(s_it_meshes.begin(), s_it_meshes.end(), it->second));
				s_meshes.erase(it);
			}
		}

		static std::tuple<const char**, u32> GetMeshForGUI()
		{
			return std::make_tuple(s_meshe_names.data(), static_cast<u32>(s_meshe_names.size()));
		}

		static auto Begin()
		{
			return s_it_meshes.begin();
		}

		static auto End()
		{
			return s_it_meshes.end();
		}
	private:
		inline static u32 s_next_mesh_id = 0u;
		inline static std::unordered_map<std::string, Ref<Mesh>> s_meshes;
		inline static Vector<Ref<Mesh>> s_it_meshes;
		inline static Vector<const char*> s_meshe_names;
	};
}


#endif // !__MESH_H__

