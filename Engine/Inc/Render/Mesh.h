#pragma once
#ifndef __MESH_H__
#define __MESH_H__
#include <string>
#include <unordered_map>
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Framework/Math/AABB.h"


namespace Ailu
{
	class Mesh
	{
		DECLARE_PRIVATE_PROPERTY(origin_path, OriginPath, String)
		DECLARE_PRIVATE_PROPERTY(name, Name, std::string)
	public:
		Mesh();
		Mesh(const std::string& name);
		~Mesh();
		void SetVertices(Vector3f* vertices);
		void SetNormals(Vector3f* normals);
		void SetTangents(Vector4f* tangents);
		void SetUVs(Vector2f* uv, uint8_t index);
		inline  Vector3f* GetVertices() { return _vertices; };
		inline Vector3f* GetNormals() { return _normals; };
		inline Vector4f* GetTangents() { return _tangents; };
		inline Vector2f* GetUVs(uint8_t index) { return _uv[index]; };
		inline uint32_t* GetIndices() { return _p_indices; };
		void Clear();
		void SetIndices(uint32_t* indices);
		const Ref<VertexBuffer>& GetVertexBuffer() const;
		const Ref<IndexBuffer>& GetIndexBuffer() const;
		void Build();
	public:
		uint32_t _vertex_count;
		uint32_t _index_count;
		AABB _bound_box;
	private:
		Ref<VertexBuffer> _p_vbuf;
		Ref<IndexBuffer> _p_ibuf;
		Vector3f* _vertices;
		Vector3f* _normals;
		Color* _colors;
		Vector4f* _tangents;
		Vector2f** _uv;
		uint32_t* _p_indices;
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
		inline static uint32_t s_next_mesh_id = 0u;
		inline static std::unordered_map<std::string, Ref<Mesh>> s_meshes;
		inline static Vector<Ref<Mesh>> s_it_meshes;
		inline static Vector<const char*> s_meshe_names;
	};
}


#endif // !__MESH_H__
