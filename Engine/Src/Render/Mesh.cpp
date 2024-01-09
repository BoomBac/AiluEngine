#include "pch.h"
#include "Render/Mesh.h"

namespace Ailu
{
	Mesh::Mesh()
	{
		_vertices = nullptr;
		_normals = nullptr;
		_colors = nullptr;
		_tangents = nullptr;
		_uv = new Vector2f*[8];
		for (size_t i = 0; i < 8; i++)
		{
			_uv[i] = nullptr;
		}
		_vertex_count = 0u;
		_index_count = 0u;
	}

	Mesh::Mesh(const std::string& name) : Mesh()
	{
		_name = name;
	}
	Mesh::~Mesh()
	{
		Clear();
		DESTORY_PTRARR(_uv)
	}
	void Mesh::SetVertices(Vector3f* vertices)
	{
		_vertices = vertices;
	}
	void Ailu::Mesh::SetNormals(Vector3f* normals)
	{
		_normals = normals;
	}
	void Mesh::SetTangents(Vector4f* tangents)
	{
		_tangents = tangents;
	}
	void Mesh::SetUVs(Vector2f* uv, uint8_t index)
	{
		if (index >= 8)
		{
			AL_ASSERT(true, "Uv index in mesh must be less than 8!");
			return;
		}
		_uv[index] = uv;
	}
	void Mesh::Clear()
	{
		DESTORY_PTRARR(_vertices);
		DESTORY_PTRARR(_normals);
		DESTORY_PTRARR(_colors);
		DESTORY_PTRARR(_tangents);
		if (_uv != nullptr)
		{
			for (size_t i = 0; i < 8; i++)
			{
				DESTORY_PTRARR(_uv[i]);
			}
		}
		_vertex_count = 0;
		_index_count = 0;
	}
	void Mesh::SetIndices(uint32_t* indices)
	{
		_p_indices = indices;
	}
	const Ref<VertexBuffer>& Mesh::GetVertexBuffer() const
	{
		return _p_vbuf;
	}
	const Ref<IndexBuffer>& Mesh::GetIndexBuffer() const
	{
		return _p_ibuf;
	}
	void Mesh::Build()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index;
		if (_vertices)
		{
			desc_list.push_back({ "POSITION",EShaderDateType::kFloat3,count });
			vert_index = count++;
		}
		if (_normals)
		{
			desc_list.push_back({ "NORMAL",EShaderDateType::kFloat3,count });
			normal_index = count++;
		}
		if (_uv[0])
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat2,count });
			uv_index = count++;
		}
		if (_tangents)
		{
			desc_list.push_back({ "TANGENT",EShaderDateType::kFloat4,count });
			tangent_index = count++;
		}
		_p_vbuf.reset(VertexBuffer::Create(desc_list));
		if(_vertices) _p_vbuf->SetStream(reinterpret_cast<float*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index);
		if(_normals) _p_vbuf->SetStream(reinterpret_cast<float*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index);
		if(_uv[0]) _p_vbuf->SetStream(reinterpret_cast<float*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index);
		if(_tangents) _p_vbuf->SetStream(reinterpret_cast<float*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index);
		//_p_vbuf.reset(VertexBuffer::Create({
		//		{"POSITION",EShaderDateType::kFloat3,0},
		//		{"NORMAL",EShaderDateType::kFloat3,1},
		//		{"TEXCOORD",EShaderDateType::kFloat2,2},
		//		{"TANGENT",EShaderDateType::kFloat4,3}
		//	}));
		//_p_vbuf->SetStream(reinterpret_cast<float*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), 0);
		//_p_vbuf->SetStream(reinterpret_cast<float*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), 1);
		//_p_vbuf->SetStream(reinterpret_cast<float*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), 2);
		//_p_vbuf->SetStream(reinterpret_cast<float*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4),3);
		_p_ibuf.reset(IndexBuffer::Create(_p_indices, _index_count));
	}
	//Ref<Mesh> MeshPool::FindMesh(const std::string& name)
	//{
	//	auto it = s_mesh_pool.find(name);
	//	if (it != s_mesh_pool.end()) return it->second;
	//	else
	//	{
	//		LOG_WARNING("Can't find mesh: {}!,will return a empty mesh!", name);
	//		return MakeRef<Mesh>("empty");
	//	}
	//}
	//void MeshPool::AddMesh(Ref<Mesh> mesh)
	//{
	//	s_mesh_pool.insert(std::make_pair(mesh->_name,mesh));
	//}
}
